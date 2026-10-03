"""Generate the deliberately temporary DEV icon using only Python stdlib."""

from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "frame-dev.ico"
GLYPHS = {
    "D": ["1110", "1001", "1001", "1001", "1001", "1001", "1110"],
    "E": ["1111", "1000", "1000", "1110", "1000", "1000", "1111"],
    "V": ["1001", "1001", "1001", "1001", "1001", "0110", "0110"],
}


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png(size: int) -> bytes:
    rows = []
    scale = max(1, size // 32)
    total = 3 * 4 + 2 * 2
    left = (size - total * scale) // 2
    top = (size - 7 * scale) // 2
    for y in range(size):
        row = bytearray(b"\0")
        for x in range(size):
            # The flat square and literal DEV mark this as a development icon.
            color = (26, 30, 60, 255)
            if 1 <= x < size - 1 and 1 <= y < size - 1:
                color = (61, 48, 114, 255)
            gx = (x - left) // scale
            gy = (y - top) // scale
            if left <= x < left + total * scale and top <= y < top + 7 * scale:
                word = "DEV"
                index = gx // 6
                column = gx % 6
                if index < 3 and column < 4 and GLYPHS[word[index]][gy][column] == "1":
                    color = (245, 241, 255, 255)
            row.extend(color)
        rows.append(bytes(row))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")


def main() -> None:
    sizes = (16, 32, 48, 256)
    data = [png(size) for size in sizes]
    offset = 6 + 16 * len(sizes)
    entries = []
    for size, image in zip(sizes, data):
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset))
        offset += len(image)
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_bytes(struct.pack("<HHH", 0, 1, len(sizes)) + b"".join(entries) + b"".join(data))
    print(OUT)


if __name__ == "__main__":
    main()
