"""Decode source-video evidence using optional PyAV (never modify the source)."""
import argparse
from pathlib import Path
import av

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
container = av.open(str(args.source))
stream = container.streams.video[0]
print(stream.width, stream.height, stream.average_rate, float(stream.duration * stream.time_base))
targets = iter([0, 4, 15.4, 17.3, 27.8, 32])
target = next(targets)
for frame in container.decode(video=0):
    if frame.time >= target:
        frame.to_image().save(args.output / f'source-{target}.png')
        try:
            target = next(targets)
        except StopIteration:
            break
