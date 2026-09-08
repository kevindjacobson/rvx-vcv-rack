#!/usr/bin/env python3
"""Create reproducible baseline/stress patches without changing Rack settings.

The generated patch selects a hardware device only when --audio-device is given.
No cable connects to a physical audio output. Loading the patch in Rack opens
the selected duplex device; Rack can briefly use its default settings before
restoring the requested sample rate and block size.
"""

import argparse
import copy
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def make_patch(args):
    audio_example = json.loads((ROOT / "examples/RVX-Audio-to-Video.vcv").read_text())
    oscillator = copy.deepcopy(audio_example["modules"][0])
    oscillator.update(id=400, pos=[10, 1 if args.mode == "stress" else 0])
    device = {"driver": 5, "inputOffset": 0, "outputOffset": 0}
    if args.audio_device:
        device.update(deviceName=args.audio_device, sampleRate=args.sample_rate,
                      blockSize=args.block_size)
    audio = {
        "id": 500, "plugin": "Core", "model": "AudioInterface2", "version": "2.6.6",
        "params": [{"id": 0, "value": 1.0}],
        "data": {"audio": device, "dcFilter": False},
        "pos": [0, 1 if args.mode == "stress" else 0],
    }
    if args.mode == "baseline":
        patch = audio_example
        patch.update(modules=[audio, oscillator], cables=[], zoom=1.0)
        return patch

    patch = json.loads((ROOT / "examples/RVX-Experimental-Feedback.vcv").read_text())
    modules = {module["id"]: module for module in patch["modules"]}
    modules[200]["params"] = [{"id": 0, "value": 0.0}, {"id": 1, "value": 0.25}]
    modules[201]["params"] = copy.deepcopy(audio_example["modules"][1]["params"])
    modules[202]["params"][0]["value"] = 0.75
    modules[202]["params"][1]["value"] = 0.25
    modules[206]["data"].update(
        sourceId="", sourceApplication=args.syphon_application or "",
        sourceName=args.syphon_source or "", publisherName=args.publisher,
        publish=True, holdLast=False,
    )
    hardware_bridge = copy.deepcopy(modules[201])
    hardware_bridge.update(id=207, pos=[22, 1])
    # A quiet, available hardware interval maps to unity. Missing intervals
    # remain zero under the shared capture contract and are visibly distinct.
    hardware_bridge["params"][2]["value"] = 1.0
    patch["modules"].extend([audio, oscillator, hardware_bridge])
    for cable_id, source, output, destination, input_, color in [
        (2010, 400, 0, 201, 1, "#f8ba33"),
        (2011, 500, 0, 207, 1, "#f8ba33"),
        (2012, 207, 0, 203, 2, "#ff5ac2"),
        (2013, 206, 0, 202, 1, "#4dc9ff"),
    ]:
        patch["cables"].append({
            "id": cable_id, "outputModuleId": source, "outputId": output,
            "inputModuleId": destination, "inputId": input_, "color": color,
        })
    return patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["baseline", "stress"])
    parser.add_argument("output", type=Path)
    parser.add_argument("--audio-device", help="Exact Rack Core Audio device name")
    parser.add_argument("--sample-rate", type=int, default=48000)
    parser.add_argument("--block-size", type=int, default=512)
    parser.add_argument("--syphon-application")
    parser.add_argument("--syphon-source")
    parser.add_argument("--publisher", default="RVX validation output")
    args = parser.parse_args()
    if args.sample_rate <= 0 or args.block_size <= 0:
        parser.error("Sample rate and block size must be positive")
    if bool(args.syphon_application) != bool(args.syphon_source):
        parser.error("Syphon application and source name must be supplied together")
    if args.output.exists():
        parser.error("Output already exists; choose a new path")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(make_patch(args), indent=2) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
