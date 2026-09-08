#!/usr/bin/env python3
"""Create reproducible baseline, stress, or Syphon relay validation patches.

The generated patch selects a hardware device only when --audio-device is given.
No cable connects to a physical audio output. Loading the patch in Rack opens
the selected duplex device; Rack can briefly use its default settings before
restoring the requested sample rate and block size.
"""

import argparse
import copy
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def write_exclusive(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x") as output:
        output.write(payload)


def make_patch(args):
    if args.mode == "relay":
        video_io = {
            "id": 600, "plugin": "RVX", "model": "VideoIo", "version": "2.0.0",
            "params": [],
            "data": {
                "rvxSchema": 1, "sourceId": "",
                "sourceApplication": args.syphon_application,
                "sourceName": args.syphon_source,
                "publisherName": args.publisher,
                "defaultPublisherName": "RVX default",
                "publisherOwnerModuleId": 600,
                "publish": True, "holdLast": False,
            },
            "pos": [0, 0],
        }
        processor = {
            "id": 601, "plugin": "RVX", "model": "SignalProcessor", "version": "2.0.0",
            "params": [
                {"id": 0, "value": 1.0}, {"id": 1, "value": 0.0},
                {"id": 2, "value": 0.0}, {"id": 3, "value": 0.0},
            ],
            "data": {"rvxSchema": 1}, "pos": [14, 0],
        }
        monitor = {
            "id": 602, "plugin": "RVX", "model": "VideoMonitor", "version": "2.0.0",
            "params": [], "data": {"rvxSchema": 1}, "pos": [30, 0],
        }
        cables = []
        for cable_id, source, output, destination, input_ in [
            (6000, 600, 0, 601, 0),
            (6001, 601, 0, 602, 0),
            (6002, 601, 0, 600, 0),
        ]:
            cables.append({
                "id": cable_id, "outputModuleId": source, "outputId": output,
                "inputModuleId": destination, "inputId": input_, "color": "#4dc9ff",
            })
        return {
            "version": "2.6.6", "zoom": 1.0, "gridOffset": [0, 0],
            "modules": [video_io, processor, monitor], "cables": cables,
        }

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
    if sys.argv[1:] == ["--self-test"]:
        with tempfile.TemporaryDirectory(prefix="rvx-patch-test-") as directory:
            root = Path(directory)
            args = argparse.Namespace(
                mode="relay", syphon_application="Source App",
                syphon_source="Source Name", publisher="Relay Output",
            )
            patch = make_patch(args)
            modules = {module["id"]: module for module in patch["modules"]}
            if len(patch["modules"]) != 3 or {
                key: (module["plugin"], module["model"])
                for key, module in modules.items()
            } != {600: ("RVX", "VideoIo"), 601: ("RVX", "SignalProcessor"),
                  602: ("RVX", "VideoMonitor")}:
                raise AssertionError("relay must contain only I/O, processor and monitor")
            routes = [(c["outputModuleId"], c["outputId"],
                       c["inputModuleId"], c["inputId"]) for c in patch["cables"]]
            if sorted(routes) != [(600, 0, 601, 0), (601, 0, 600, 0), (601, 0, 602, 0)]:
                raise AssertionError("relay must route received pixels through the processor")
            params = modules[601]["params"]
            if len(params) != 4 or {p["id"]: p["value"] for p in params} != {
                0: 1.0, 1: 0.0, 2: 0.0, 3: 0.0
            }:
                raise AssertionError("relay processor must preserve input A at unity")
            expected_io = {
                "sourceId": "", "sourceApplication": "Source App",
                "sourceName": "Source Name", "publisherName": "Relay Output",
                "publish": True, "holdLast": False,
            }
            if any(modules[600]["data"].get(key) != value
                   for key, value in expected_io.items()):
                raise AssertionError("relay must select the requested source and publish fresh input")
            payload = json.dumps(patch, indent=2) + "\n"
            output = root / "relay.vcv"
            write_exclusive(output, payload)
            try:
                write_exclusive(output, payload)
            except FileExistsError:
                pass
            else:
                raise AssertionError("exclusive write replaced an existing patch")
            dangling = root / "dangling.vcv"
            dangling.symlink_to(root / "missing-target")
            try:
                write_exclusive(dangling, payload)
            except FileExistsError:
                pass
            else:
                raise AssertionError("exclusive write followed a dangling symlink")
        print("make-validation-patch self-test passed")
        return

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["baseline", "stress", "relay"])
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
    if args.mode in ("stress", "relay") and not args.syphon_application:
        parser.error(f"{args.mode} mode requires Syphon application and source name")
    if args.mode == "relay" and not args.publisher:
        parser.error("relay mode requires a non-empty publisher name")
    payload = json.dumps(make_patch(args), indent=2) + "\n"
    try:
        write_exclusive(args.output, payload)
    except FileExistsError:
        parser.error("Output already exists; choose a new path")
    print(args.output)


if __name__ == "__main__":
    main()
