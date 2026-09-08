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

## External Syphon observation

Choose a run-specific suffix and use distinct source and output names. Start the probe first:

```sh
./build/native-syphon-path-probe \
  --duration 600 \
  --progress 30 \
  --source-name "RVX validation source RUN-SUFFIX" \
  --output-application "EXACT OUTPUT APPLICATION" \
  --output-name "RVX validation output RUN-SUFFIX"
```

The first flushed JSON line has `event: "ready"`. Copy `publishedSource.application` and `publishedSource.name` exactly; the application name comes from the built executable and can change if the binary is renamed. The awaited output is also selected by its exact application and server name. Names alone are not Syphon identities and two applications can publish the same server name.

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

This mode is a measured observation of the combined stress workload. Its summary deliberately contains `"transportAssertion": null` instead of a `passed` flag, and a completed observation exits with status zero even when the selected output was absent. The stress patch contains internal Test Image and VCO paths that can animate the publication without proving that the external Syphon input contributed to it. Use the relay verification below for that claim.

The probe emits periodic JSON progress records and one summary. `completedFramePtrs` counts new completed RVX receiver frame identities rather than caller ticks. `pixelHashChanges` hashes every float in each new working frame. The summary also reports publisher cadence slots missed, exact-output absence or ambiguity, output identity changes, observed working formats, output interarrival gaps and measured FPS relative to 30000/1001.

## Source-bound relay verification

Use a new run suffix, the exact application name of the Rack validation host, and a short measured interval first:

```sh
./build/native-syphon-path-probe \
  --verify-relay \
  --duration 2 \
  --startup-timeout 120 \
  --progress 1 \
  --source-name "RVX relay source RUN-SUFFIX" \
  --output-application "EXACT RACK APPLICATION" \
  --output-name "RVX relay output RUN-SUFFIX"
```

After the `ready` record appears, generate a relay fixture from its exact `publishedSource` values:

```sh
python3 scripts/make-validation-patch.py relay /tmp/rvx-native-relay-RUN-SUFFIX.vcv \
  --syphon-application "READY publishedSource.application" \
  --syphon-source "READY publishedSource.name" \
  --publisher "RVX relay output RUN-SUFFIX"
```

Load that patch in the isolated Rack validation profile before the startup timeout expires. It contains exactly one Video I/O source routed through a unity Signal Processor to a Monitor and back to the same Video I/O publisher. It has no Test Image, CV Bridge, VCO, feedback or other internal image generator.

Every probe run creates a fresh 64-bit nonce and publishes it with a 32-bit source frame index in protected blocks of the known pattern. Relay verification first rejects a frame unless its dimensions, channel count and pixel-vector length are exactly 720 × 480 RGBA, then rejects any non-finite channel before decoding or comparing indexed pixels. It decodes the nonce and source index and compares all 345,600 expected pixels (1,382,400 RGBA channels), including alpha. The finite per-channel tolerance is the existing `2.5 / 255` (about 0.009804) allowance for the 8-bit Syphon boundary; this issue makes that tolerance explicit without relaxing it. Relay verification does not begin its requested `--duration` until an output with the exact application/name, current nonce and a complete matching frame arrives. The startup wait is bounded separately by `--startup-timeout`; this lets the operator load the fixture without shortening the measurement.

During the measured window the output must remain continuously discoverable under the same Syphon identity, remain 720 × 480 RGBA, match every expected channel on every new frame, and advance its source index without repeats or regression. A full-frame match cannot bypass the run nonce or identity rules. The active receive cadence must be at least 99% of 30000/1001, skipped source indices must remain below 1%, and the final valid frame must be no more than three nominal periods old. A server that emits three early frames and then stops therefore fails. These relay thresholds establish source-bound continuity for this fixture; they do not replace the separate 600-second renderer p99, missed-deadline, audio or storage budgets in `VALIDATION.md`.

The summary's `fullFrameVerification` object states the coverage, tolerance, finite-value requirement, attempted and matched frames, compared pixel/channel counts and verification-time mean/percentiles/maximum. `patternVerificationFailures` is the current aggregate failure count; `patternDecodeFailures` remains as a compatibility alias for existing result consumers. Separate malformed, non-finite, header-decode and full-frame mismatch counts show why verification failed.

The following short negative cases are opt-in because they depend on operator-controlled native fixtures:

- Generate the relay patch with a different `--syphon-source`; startup must time out without beginning measurement.
- Run the probe with a different `--output-application`; the same-named server from another application must not be selected.
- After `measurementStarted`, stop or disable the Rack publisher after three valid frames; the full measured window must fail freshness, availability, or cadence.

The local regression checks exercise exact application/name matching; fresh/wrong nonce handling; a complete quantized frame; finite corruption outside the old header/sentinel positions in red, green, blue and alpha; tolerance boundaries; body orientation and content changes; NaN and positive/negative infinity in every channel; malformed dimensions, channel counts and pixel-vector lengths; repeated/regressed sequences; early-frames-then-outage and interrupted-run rejection; minimal relay topology; exclusive patch creation; and dangling-symlink refusal:

```sh
make test-native-syphon-path \
  SYPHON_DIR=/path/to/Syphon \
  SYPHON_LIB=/path/to/Syphon/libSyphon.a
```

Patch generation constructs the complete JSON payload before opening the destination with exclusive creation. It refuses existing files, concurrent winners and dangling symlinks rather than following or replacing them.

`SIGINT` or `SIGTERM` retires the temporary publisher, forces `passed` false, and emits an interrupted summary before the process exits with status 130.

Syphon does not carry FPS metadata, so received FPS and interarrival overruns use the probe's steady clock. The RVX backend converts input to the requested 720 × 480 working frame before exposing it; `receivedFormats` therefore cannot prove the source's native Syphon texture dimensions. The full-frame comparison proves that this generated transport pattern traversed a unity processor and the tested I/O boundary. A self-generated pattern does not establish arbitrary operator math, native source dimensions, end-to-end latency, LZX behavior or visual fidelity. Process scheduling, GPU readback, full-frame hashing and the separately reported full-frame verification cost contribute to measured gaps.

Use the CoreAudio listener in a separate process over the same baseline and stress intervals. Keep the Rack process, hardware device, sample rate, block size and measurement duration fixed between runs. Record Rack's opt-in diagnostics alongside the two probe summaries. A combined result can establish the observed behavior of that exact host run; it does not establish DAW, Intel Mac, other hardware, end-to-end latency, or LZX hardware fidelity.
