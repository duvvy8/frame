"""Reproducible near-black content sampling for a real PrintWindow recording."""
import argparse
import json
from pathlib import Path

import av
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("recording", type=Path)
parser.add_argument("--output", type=Path)
args = parser.parse_args()
samples = []
with av.open(str(args.recording)) as container:
    for index, frame in enumerate(container.decode(video=0)):
        pixels = frame.to_ndarray(format="rgb24")
        content = pixels[160:min(830, pixels.shape[0]),
                         200:min(1200, pixels.shape[1])]
        if content.size == 0:
            raise ValueError("Recording is too small for the stated content region")
        fraction = float(np.all(content < 24, axis=2).mean())
        samples.append({"frame": index, "nominal_seconds": float(frame.time),
                        "near_black_fraction": fraction})
if not samples:
    raise ValueError("No video frames decoded")
report = {
    "recording": str(args.recording.resolve()),
    "sample_count": len(samples),
    "region": {"left": 200, "top": 160, "right": 1200, "bottom": 830},
    "threshold": "Every RGB component < 24; predominantly dark means > 75%",
    "maximum_fraction": max(item["near_black_fraction"] for item in samples),
    "predominantly_dark_samples": [item for item in samples
                                   if item["near_black_fraction"] > .75],
    "limitations": "PrintWindow sampling only. Coordinates clip to capture size. "
                   "Nominal video timestamps are not capture wall times; consult "
                   "the recording timing sidecar. Unsampled/GPU/theme/discard "
                   "transitions are not established by this measurement.",
}
output = args.output or args.recording.with_suffix(".analysis.json")
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({"samples": len(samples),
                  "maximum_fraction": report["maximum_fraction"],
                  "predominantly_dark_samples": len(report["predominantly_dark_samples"])}))
