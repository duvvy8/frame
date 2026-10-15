"""Capture only the specified Frame window, including native chrome.

PrintWindow avoids capturing unrelated windows covering the test browser.
Optional PyAV turns these actual window captures into a recording.
"""
import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import time
import json
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('handle', type=int)
parser.add_argument('output', type=Path)
parser.add_argument('--seconds', type=float, default=0)
parser.add_argument('--fps', type=int, default=30)
args = parser.parse_args()
user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
user32.GetWindowDC.restype = wintypes.HDC
user32.GetWindowDC.argtypes = [wintypes.HWND]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.restype = wintypes.HANDLE
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
gdi32.DeleteObject.argtypes = [wintypes.HANDLE]
gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT,
                          wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]

class BitmapInfo(ctypes.Structure):
    _fields_ = [('size', wintypes.DWORD), ('width', wintypes.LONG),
                ('height', wintypes.LONG), ('planes', wintypes.WORD),
                ('bits', wintypes.WORD), ('compression', wintypes.DWORD),
                ('image_size', wintypes.DWORD), ('x', wintypes.LONG),
                ('y', wintypes.LONG), ('used', wintypes.DWORD), ('important', wintypes.DWORD)]

def capture():
    rectangle = wintypes.RECT()
    if not user32.GetWindowRect(args.handle, ctypes.byref(rectangle)):
        raise RuntimeError('Test window missing')
    width, height = rectangle.right - rectangle.left, rectangle.bottom - rectangle.top
    window_dc = user32.GetWindowDC(args.handle)
    memory_dc = gdi32.CreateCompatibleDC(window_dc)
    bitmap = gdi32.CreateCompatibleBitmap(window_dc, width, height)
    previous = gdi32.SelectObject(memory_dc, bitmap)
    try:
        if not user32.PrintWindow(args.handle, memory_dc, 2):
            raise RuntimeError('PrintWindow unavailable')
        info = BitmapInfo(40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
        pixels = ctypes.create_string_buffer(width * height * 4)
        if not gdi32.GetDIBits(memory_dc, bitmap, 0, height, pixels, ctypes.byref(info), 0):
            raise RuntimeError('Window pixels unavailable')
        return Image.frombuffer('RGB', (width, height), pixels, 'raw', 'BGRX', 0, 1)
    finally:
        gdi32.SelectObject(memory_dc, previous)
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(memory_dc)
        user32.ReleaseDC(args.handle, window_dc)

args.output.parent.mkdir(parents=True, exist_ok=True)
if not args.seconds:
    capture().save(args.output)
else:
    import av
    with av.open(str(args.output), 'w') as output:
        first = capture()
        stream = output.add_stream('libx264', rate=args.fps)
        stream.width = first.width // 2 * 2
        stream.height = first.height // 2 * 2
        stream.pix_fmt = 'yuv420p'
        start = time.monotonic()
        captures = []
        for index in range(round(args.seconds * args.fps)):
            image = capture().crop((0, 0, stream.width, stream.height))
            captures.append(time.monotonic() - start)
            frame = av.VideoFrame.from_image(image)
            frame.pts = index
            for packet in stream.encode(frame):
                output.mux(packet)
            time.sleep(max(0, start + (index + 1) / args.fps - time.monotonic()))
        for packet in stream.encode():
            output.mux(packet)
    args.output.with_suffix('.timing.json').write_text(json.dumps({
        'capture': 'Win32 PrintWindow of the specified browser window',
        'requested_fps': args.fps, 'frames': len(captures),
        'elapsed_seconds': time.monotonic() - start,
        'captured_seconds': captures,
        'limitations': 'Composited snapshots may miss flashes between samples; encoded timing uses requested frame rate.'
    }, indent=2))
