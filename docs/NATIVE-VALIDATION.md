# Native audio and Syphon validation

These opt-in command-line probes support repeatable validation on a Mac running the RVX prototype. They query live CoreAudio properties or publish and receive temporary Syphon servers, so they are intentionally excluded from `make test`. Neither probe records audio, opens an audio stream, changes a device or Rack setting, controls Rack's UI, or writes a results file. JSON is written to standard output for the operator to retain when appropriate.

## Build

The CoreAudio probe needs only Apple Command Line Tools:

```sh
make build/native-coreaudio-probe
```

The Syphon path probe compiles the same `src/io/SyphonBackend.mm` and links the same pinned Syphon archive as the plugin. Run `bash scripts/fetch-syphon.sh` first, or point the build at an existing compatible dependency:

```sh
make build/native-syphon-path-probe \
  SYPHON_DIR=/path/to/Syphon \
  SYPHON_LIB=/path/to/Syphon/libSyphon.a
```

`make native-validation-tools` builds both. The live smoke tests are separate explicit targets:

```sh
make test-native-coreaudio
make test-native-syphon-path
```

The CoreAudio target first runs pure selector fixtures covering numeric exact names, transient ID selection, duplicate names and malformed IDs without querying live devices. It then observes the current default output for two seconds. The Syphon smoke test publishes a process-ID-qualified private source, receives it through the RVX backend for two seconds, and retires the source on exit. Both targets also check rejected zero, over-limit and incomplete command lines. Durations must be greater than zero and no more than 3600 seconds.

## CoreAudio observation

Inventory the current devices without registering a listener:

```sh
./build/native-coreaudio-probe
```

Observe the default output device, or select an exact device name:

```sh
./build/native-coreaudio-probe --observe-default-output --duration 600

./build/native-coreaudio-probe \
  --observe-device "DEVICE NAME" \
  --duration 600
```

`--observe-device` always treats its argument as an exact name, including names made entirely of digits. If duplicate devices have the same name, inventory the devices immediately before the observation and select one using its current `deviceID`:

```sh
./build/native-coreaudio-probe \
  --observe-device-id CURRENT_ID \
  --duration 600
```

The JSON records the transient CoreAudio device ID, device name, current nominal sample rate, CoreAudio buffer frame size, channel counts, default roles, running state, overload-property presence and listener registration/removal status. A `deviceID` is valid only for the current CoreAudio device topology and may change after devices are added, removed or restarted. Use it only to disambiguate an immediately following observation; do not persist it in a patch, script or validation configuration. The probe deliberately omits persistent device UIDs and hardware serials.

`SIGINT` or `SIGTERM` ends an observation early, removes the listener and emits an interrupted JSON result.

`deviceWideOverloadNotificationCount` counts `kAudioDeviceProcessorOverload` notifications. Apple defines this notification as a device-detected I/O cycle that ran past its deadline. It is device-wide and can reflect any client using that device; it is not a Rack underrun count and cannot attribute an event to RVX. Conversely, zero notifications do not prove that Rack produced no short buffers, repeated data or internal buffer loss. The tested Rack source receives `RtAudioStreamStatus` but does not export or accumulate it. Compare baseline and stress observations on the same device, sample rate and Rack block size, and report this attribution limit with the result.

The CoreAudio buffer size is a live HAL device property. Confirm Rack's separately saved/requested block size from the generated patch and its startup log rather than treating the HAL value as proof of Rack state.

## External Syphon path

Choose a run-specific suffix and use distinct source and output names. Start the probe first:

```sh
./build/native-syphon-path-probe \
  --duration 600 \
  --progress 30 \
  --source-name "RVX validation source RUN-SUFFIX" \
  --output-name "RVX validation output RUN-SUFFIX"
```

The first flushed JSON line has `event: "ready"`. Copy `publishedSource.application` and `publishedSource.name` exactly; the application name comes from the built executable and can change if the binary is renamed. Do not hard-code an example application name.

After the integration commit containing [`scripts/make-validation-patch.py`](../scripts/make-validation-patch.py) is present, generate fresh baseline and stress patches. The script refuses to overwrite an existing output:

```sh
python3 scripts/make-validation-patch.py baseline /tmp/rvx-native-baseline.vcv \
  --audio-device "DEVICE NAME" \
  --sample-rate 48000 \
  --block-size 512

python3 scripts/make-validation-patch.py stress /tmp/rvx-native-stress.vcv \
  --audio-device "DEVICE NAME" \
  --sample-rate 48000 \
  --block-size 512 \
  --syphon-application "READY publishedSource.application" \
  --syphon-source "READY publishedSource.name" \
  --publisher "RVX validation output RUN-SUFFIX"
```

Load the generated stress patch into the isolated Rack validation profile. Its Video I/O receives the exact emitted application/source pair, routes that image through the patch's operators, and publishes the exact output name awaited by the probe. The input is a deterministic moving 720 × 480 RGBA pattern scheduled at 30000/1001 frames per second. The pattern contains color bars, moving horizontal and vertical marks, and frame-index bits.

The probe emits periodic JSON progress records and one summary. `completedFramePtrs` counts new completed RVX receiver frame identities rather than caller ticks. `pixelHashChanges` hashes every float in each new working frame. The summary also reports publisher cadence slots missed, exact-output absence or ambiguity, output identity changes, observed working formats, output interarrival gaps and measured FPS relative to 30000/1001.

`SIGINT` or `SIGTERM` retires the temporary publisher and emits an interrupted summary before the process exits with status 130.

These are observations rather than a transport guarantee. Syphon does not carry FPS metadata, so received FPS and interarrival overruns use the probe's steady clock. The RVX backend converts input to the requested 720 × 480 working frame before exposing it; `receivedFormats` therefore cannot prove the source's native Syphon texture dimensions. A changing hash proves changing completed pixels, not correct operator math or visual fidelity. Whole-window frame shortfall includes time spent waiting for Rack to publish the expected output. Process scheduling, GPU readback and the probe's full-frame hashing also contribute to measured gaps.

Use the CoreAudio listener in a separate process over the same baseline and stress intervals. Keep the Rack process, hardware device, sample rate, block size and measurement duration fixed between runs. Record Rack's opt-in diagnostics alongside the two probe summaries. A combined result can establish the observed behavior of that exact host run; it does not establish DAW, Intel Mac, other hardware, end-to-end latency, or LZX hardware fidelity.
