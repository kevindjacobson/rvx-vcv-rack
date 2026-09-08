**RVX — implementation plan for review**

RVX is the project and plugin-suite name. The repository is `rvx-vcv-rack`. RVX provides analog video synthesis for VCV Rack on Mac, using LZX hardware as the functional reference.

September 7, 2026. Status: proposed design, awaiting user review. Research and documentation only; implementation, prototype builds, and dependency installation have not begun. Coding starts only after the user approves the plan.

The companion [RVX LZX feasibility review](LZX-Mac-Feasibility.md) records 96 assessed catalog entries and their sources. This document turns that inventory into a product scope, architecture, development sequence, and acceptance criteria. Proposed engineering choices below are recommendations, not claims about software already built or benchmarked.

**1. Intended outcome and scope**

Build RVX as one Rack plugin package containing a family of interoperable video modules. It should support LZX-style signal patching, audio/CV modulation, frame feedback and painting, and simulated dirty NTSC mixing. Mac Syphon receiving and publishing are first-release requirements, with device-style selection familiar from Rack's audio modules.

The complete first release must demonstrate this workflow:

`Shapes and/or Syphon input → color/key processing → frame memory → NTSC encode/mix/decode → monitor and Syphon output`

Composite damage must also be patchable inside the memory feedback loop. Audio from ordinary Rack modules must animate the patch. Internal generation must work without an external video application.

The proposed first release includes the creative functions above. Broader LZX catalog coverage follows in stages. Hardware power utilities have no processing implementation; physical composite output, raw analog capture, genlock, and laser DAC integration remain separate hardware projects. These boundaries follow the C/N grades in the inventory.

Confirmed design preference: reproduce LZX controls, ports and behavior as closely as practical, with video represented as analog signals wherever that affects patch behavior. Preserve control order, ranges, center points, normalizations and modulation response. Shared internal operators must not flatten meaningful differences between modules. Additional software settings belong in secondary controls so the primary panel remains familiar.

Provisional platform defaults while machine details are pending: Apple Silicon, Rack 2 standalone and SD first. These are not inferred facts about the user's machine. Intel, Rack Pro in a DAW, and higher resolutions require separately named validation targets. Panel artwork and individual module naming remain design work; functional control fidelity is the confirmed priority.

**2. Existing work and reuse decision**

There is useful precedent, but the research did not identify a complete publicly available Mac Rack system satisfying the combined requirements. Repository documentation and selected source files were inspected; these projects have not been installed or benchmarked here.

| Project | Verified relevance | Planned role |
|---|---|---|
| [trowaSoft ISF-Synth-VCV](https://github.com/j4s0n-c/ISF-Synth-VCV) | Closest functional match: custom texture connections, generators, keyers, blending and displacement. Its README describes an invite-only beta, documents Windows/Spout sharing, and warns of VST lifecycle crashes. The public tree contains documentation and example shaders, not the plugin engine. | Study its patching model. Do not make access to its unpublished engine a dependency. Mac Syphon support is unverified. |
| [Pure Dreams](https://github.com/yodko/pure-dreams) | Public GPL-3.0 Rack code integrating projectM audio-reactive visuals; macOS support is documented. | Reference for visual-engine integration and lifecycle review, rather than the modular signal engine. |
| [Aura Audio / ModularForecast](https://github.com/emurray2/auraaudio-vcv-rack) | Small MIT-licensed Rack OpenGL visualizer, originally developed on macOS. | Compact rendering example. Modern Mac compatibility needs checking. |
| [0x502](https://github.com/teriyake/0x502) | Public GLSL experiments, shader sharing, a visualizer, and a documented Mac ARM64 build. Author describes it as a rough draft. | Reference for shader controls and Rack integration. Confirm reuse permissions before copying code. |
| [LZX Dummy Panels](https://github.com/j4s0n-c/LZX_Dummy_Panels) | Gen3 patch-documentation panels, including a Mac ARM64 bundle. The inspected DSG3 processing callback is empty. | Patch-documentation reference; these are not video emulators. |
| [VVGL / VVISF](https://github.com/mrRay/VVISF-GL) | BSD-3-Clause C++ texture/rendering and ISF libraries with Mac examples. | Candidate library for shader loading and texture management. Validate against the selected Rack SDK and Mac architecture before adoption. |
| [Syphon Framework](https://github.com/Syphon/Syphon-Framework) | Public Mac image-sharing SDK with client/server and OpenGL/Metal implementations. | Preferred external video I/O dependency. Build our Rack controls and lifecycle handling around it. |
| [ntsc-rs](https://github.com/ntsc-rs/ntsc-rs) | Public Rust implementation of NTSC/VHS artifacts, supplied as standalone and other-host effects. | Evaluate algorithms and possible isolated reuse. It does not establish that a drifting-source dirty mixer with receiver lock behavior is already solved. |

Recommendation: own the Rack adapter, video graph, timing, and signal model. Reuse a suitable rendering library and the Syphon SDK after compatibility checks. Keep NTSC algorithms behind a narrow interface so using Rust or translating selected algorithms does not dictate the whole engine. ISF's documented [persistent buffers and multiple passes](https://docs.isf.video/ref_multipass) are useful building blocks for effects with memory; they do not specify our complete patch graph or hardware matching.

Record exact dependency revisions and applicable licenses when selecting code. No contacting authors, joining private betas, or publishing a project is part of the current planning work.

**3. Signal model and patching**

Four signal domains need explicit treatment:

| Domain | Represents | Rules |
|---|---|---|
| Ordinary Rack audio/CV | Modulation, envelopes, triggers, audio samples | Keep normal Rack cables and units. Audio-rate signals enter video through defined bridges. |
| Video field | One scalar value at each raster position; usable as brightness, a color component, coordinate, key, or modulation | Nominal 0–1 signal range; preserve signed and above-range intermediate values. Single-channel floating-point storage where practical. |
| Image | RGB plus alpha, with dimensions and timing metadata | Split/combine explicitly with video fields. Floating-point working storage; display conversion occurs at output boundaries. |
| Composite/raster stream | Ordered signal samples including active image and timing intervals | Independent sample clock and state. Convert through encoder/decoder modules; never pretend it is an ordinary audio-rate cable. |

Proposed video cables use custom-looking Rack ports backed by a suite-owned graph. Prefer retaining Rack's native cable editing, undo and patch storage, while interpreting approved video endpoints as graph connections. Native audio values on these special ports should remain inert; textures and pointers should not be encoded as voltages.

The adapter must validate source model, port type, instance identity, and generation before admitting an edge. An incompatible connection displays a clear error and produces the defined empty input. It must never accidentally treat audio as an image reference. Whether every incompatible drag can be prevented through supported Rack UI hooks remains an implementation validation item; correctness cannot depend on that prevention.

Connection discovery must use supported APIs and a safe snapshot, not traverse mutable Rack objects from a rendering worker. Rack's [engine API](https://vcvrack.com/docs-v2/structrack_1_1engine_1_1Engine) is the starting point for this adapter audit. If native cable metadata cannot be read safely with adequate lifecycle guarantees, the fallback is explicit suite-managed video links with their own serialization and undo integration. That changes the interaction design and must be reviewed before expansion.

Every module specification must define unpatched inputs, normalled routes, bypass behavior, units, channel count, parameter scaling, and whether a modulation input is frame-sampled, audio-sampled, or a full video field. Stock audio utilities will control the system; they will not automatically become video-bandwidth processors.

Analog fidelity is a core architectural requirement. A scalar video field represents samples of a virtual voltage through raster time, not merely display brightness. Use voltage-referenced units in module specifications and a documented conversion between the nominal LZX video range and Rack control voltages. Account for bandwidth, phase response, saturation, propagation delay, reset thresholds and correlated noise when they matter to the reference module. Avoid gratuitous noise or distortion on every module: their amount must come from evidence or a clearly identified user setting.

Use inexpensive pointwise GPU processing where it preserves the specified behavior, and ordered raster processing for stateful analog behavior. Image sampling/interpolation and waveform resampling are different operations. Timing metadata must survive conversion between them. The earliest engine proof must include a small raster-state calibration path, so later analog modules do not require replacing an image-only foundation.

**4. Shared engine and concurrency**

One engine instance belongs to one Rack patch/context. Avoid an unrestricted process-wide singleton: multiple instances, especially in a future DAW target, must not exchange resources accidentally.

| Component | Responsibility |
|---|---|
| Rack module adapter | Parameters, port declarations, timestamped CV/audio capture, patch state, lifecycle events |
| Graph compiler | Validate types and cycles, order operators, allocate reusable surfaces, produce an immutable execution plan |
| Video scheduler | Advance rational video time, consume source snapshots, run operators and publish completed frames |
| Rendering backend | Own GPU context/device, texture lifetimes, processing passes and synchronization |
| Raster engine | Oscillator phase, sequential filters, clocked logic and composite receiver state |
| Memory service | Delay history, feedback surfaces, freeze/capture and bounded storage |
| I/O adapters | Syphon discovery, ingest, format conversion and publication |
| Monitor | Display the latest completed image and diagnostics without driving synthesis |

The audio callback must perform bounded work. It must not compile shaders, allocate image buffers, wait for the GPU, discover Syphon servers, read files, or acquire locks that can be held by those operations. Timestamped control changes and audio buffers pass through bounded queues or snapshots. Overflow is counted and follows a specified policy; it must not grow memory indefinitely.

The proposed renderer runs under its own scheduling/lifetime management. Rack UI drawing only presents completed frames. Hiding a monitor, scrolling it offscreen, minimizing a window, or changing Rack's display refresh must not redefine the video clock. GPU context sharing and presentation across those lifecycles are early proof requirements, not assumed capabilities.

Prefer C++ for Rack and the engine interfaces, Objective-C++ for Mac SDK boundaries, and GPU shaders for parallel image operations. Keep a small CPU reference implementation of important math and raster operators for validation. First evaluate an OpenGL path compatible with Rack and VVGL/VVISF. Preserve a backend boundary for Metal; select it early if OpenGL ownership, throughput, or current Mac compatibility fails. This is a planned fallback, not a promise of effortless backend substitution.

For save/restore, use versioned module state and stable graph identities. Large optional frame/media data belongs in patch storage rather than JSON; file work stays off the audio callback. These facilities and constraints are described in the [Rack plugin API guide](https://vcvrack.com/manual/PluginGuide).

**5. Timing, color and feedback contracts**

Use a rational timebase so long runs do not drift because 29.97 was rounded to an integer frame rate. Distinguish image frames, interlaced fields, horizontal timing and composite sample positions. A frame-sized image texture alone cannot represent every timing behavior.

Proposed SD working canvas: 720 × 480 active image samples with explicit display aspect metadata. The NTSC profile separately accounts for total raster timing and field parity. A square-pixel preview is not assumed to be the physical display geometry. Input cropping and active-area conventions must be documented, particularly when comparing hardware whose documentation uses different active-height conventions.

Define these behaviors before implementing individual effects:

- Frame-mode controls are latched at a documented boundary. Audio-modulated raster operators read a timestamped audio history with specified interpolation and latency. Triggers are captured as events so short pulses survive slower video updates.
- Source arrival and video output are independent. By default use the newest completed Syphon source image, repeat when needed, and count skipped frames. Audio-device removal does not stop internally clocked video; the clock bridge handles restart explicitly.
- Acyclic image chains run within a video tick. Merely adding another color operator does not add one frame of latency.
- Feedback edges require an explicit delay/state operator. Read all prior state before committing new state. A zero-delay cycle is reported, never resolved by arbitrary module order.
- Frame feedback and sub-frame analog feedback are distinct. The first release supports explicit frame feedback and validates the raster engine foundation. Later raster delays support a restricted causal subset; arbitrary instantaneous analog networks need separate modeling and are not promised by the A grades. For close LZX behavior, record each unsupported feedback topology as a fidelity limitation. Do not insert a whole frame into an analog feedback patch and call it equivalent.
- Proposed signal math uses encoded video component values for the analog-inspired path. Color-managed import/export must make transfer, range and primaries explicit; linear-light blending may be offered as a separate documented operation. Alpha uses a declared convention with explicit boundary conversion.
- Default working textures may use 16-bit floats; precision-sensitive feedback/raster state can use 32-bit. Quantization, bandwidth, clipping and noise are operator choices, not accidental consequences of a display format.
- NaN/infinite state is detected and reported. Finite out-of-range signals remain valid until an operator intentionally limits them.

Changing dimensions rebuilds dependent surfaces at a safe boundary. Initial policy: clear incompatible history with visible notice. Overload repeats the last completed output and shows lateness; it does not silently lower resolution. Limit simulation catch-up, report time discontinuities, and make temporal-state recovery deterministic rather than consuming an ever-growing queue.

**6. Syphon Video I/O contract**

One module supports input and output independently and simultaneously. Proposed panel arrangement:

| Area | Controls and connections |
|---|---|
| Driver | Syphon selected from a backend menu |
| From App | Application/server selector; source status; image output and component breakout |
| To App | Image input; publisher name; enable switch |
| Format | Output dimensions/rate, incoming measured dimensions/rate, fit/crop/stretch |
| Monitor | Optional small preview; receiving/publishing indicators; late/disconnected status |

Receiving apps choose the published stream; the output module does not choose a destination application. Multiple module instances support multiple sources and publishers. Defaults must produce distinct publisher names, with visible handling of collisions.

Save source application/name plus available identity information, then rediscover on reload. A stopped/restarted source reconnects when unambiguous; duplicate matches require an explicit selection. A missing source produces black or holds its last frame according to a saved option. Preserve orientation and alpha, and test source resizing while active.

Source textures must be retained/copied into resources with a known completion lifetime. Minimize GPU copies where SDK and renderer ownership allow; do not promise universal zero-copy sharing. An external feedback path includes application/buffering latency. Syphon carries image frames, so composite corruption must be simulated before publication or after ingestion.

The detailed I/O requirements in the feasibility report remain part of this plan.

**7. Memory Palace-inspired module**

Reference behavior includes four paths: Warp transforms within keyed feedback; Paint transforms incoming imagery before accumulation; Scene composites a transformed foreground; Ghost combines different moments. The guide also documents luma/chroma/alpha keys, media/live routing, a 0–60-frame delay, motion controls, tiling and mirroring. Freeze captures the input and subsequent Freeze events capture another single frame; Clear exits that frozen state. The primary controls are sampled at frame rate. These are requirements to compare against the [Memory Palace guide](https://community.lzxindustries.net/t/memory-palace-user-guide/884), with firmware revision selected before making a fidelity claim.

Implementation proposal: expose an integrated memory instrument backed by reusable delay, transform and key operators. Provide separately named actions for unfreezing and erasing canvas/history, so the two operations cannot be confused. Document zero-delay feedforward separately from the mandatory causal delay in a feedback loop. Default patch recall saves settings and starts with empty live history; embedding captured state is an explicit option.

Completion requires tests for all four routings, exact delay indexing, capture/freeze events, key inversion, border modes, source changes and repeated color processing. Pixel-identical firmware matching is unverified; interpolation, precision and edge handling need reference clips or hardware comparisons.

**8. NTSC and dirty mixing**

The user-requested video → ordinary Rack waveform → video path is specified in the [video/audio round-trip requirement](docs/VIDEO-AUDIO-ROUNDTRIP.md) (issue #16). Its proposed slowed composite and real-time coarse raster modes supplement this subsystem; ordinary audio cables cannot carry full-bandwidth NTSC at normal speed. The broader composite specification and fidelity gates remain open.

Treat this as a patchable subsystem with three modules: Encoder, Dirty Mixer and Receiver. A convenience combined panel can come later. Keep clean RGB mixing available separately.

| Module | Proposed controls and semantics |
|---|---|
| Encoder | RGB/image input; selectable timing reference; chroma/luma bandwidth; level and setup conventions; phase; encoded raster output |
| Dirty Mixer | Two composite inputs; independent gains; offset; polarity; clipping/nonlinearity; bandwidth; optional sync replacement; independent source drift/phase controls |
| Receiver | Composite input; horizontal/vertical lock behavior; colorburst recovery; holdover/reacquisition; decoder bandwidth; decoded image and lock diagnostics |

Development proceeds from a stable encode/decode round trip, to mixing synchronized sources, then independently timed sources. Different source clocks must be resampled into a common simulation timeline before mixing. Receiver extraction must use the damaged mixed signal; perfect source timing must not leak into the decoding path and accidentally prevent sync loss.

Separate a low-cost artifact approximation from the timing-aware receiver model. Label them clearly if both ship. The required dirty-mixing mode must produce useful clock drift, tearing/rolling and color-lock disturbances, followed by observable reacquisition. Matching a particular CRT or capture decoder requires a selected reference device; there is no universal receiver appearance.

Raster sample rate and oversampling are performance/fidelity choices to measure. A candidate is a multiple of color-subcarrier frequency, with convergence tests before choosing the default. Avoid baking an unbenchmarked rate into every video operator. Noise must have a seed for reproducible tests.

Use the [AD724 encoder documentation](https://www.analog.com/en/products/ad724.html), [ADV7282A decoder documentation](https://www.analog.com/en/products/adv7282a.html), and the [MisMatcher manual](https://freedomenterprise.pt/files/manuals/Owners_Manual_MisMatcher01RevD_RevA.pdf) as reference material alongside ntsc-rs. Exact analog behavior remains a calibration task. Hardware electrical output is outside this software milestone.

**9. Module roadmap and catalog coverage**

The catalog inventory remains the authoritative per-product list. The roadmap groups shared implementation work; an LZX product name here identifies a functional reference, not a final plugin name or a claim of affiliation.

| Stage | Modules/functions | Catalog reach |
|---|---|---|
| Foundation | Engine/settings, monitor, Syphon I/O, test image, component split/combine, CV bridge, explicit delay | Shared infrastructure; portions of Liquid TV, input and sync modules |
| Core synthesis | Ramps/Angles, shape functions/DSG3, Proc, SMX3, FKG3, Stairs, Swatch, constant colors, utilities | Broad Gen3 and P arithmetic; Cadet gain/fade/key/multiply functions; equivalent legacy primitives |
| Memory instrument | Four routing modes, transforms, keying, history and still-image input | Memory Palace-inspired workflow; transform building blocks for Navigator and related processors |
| Audio and composite | Audio image sampling, envelopes, Encoder, Dirty Mixer, Receiver | Diver/Sensory Translator concepts; virtual portions of ESG3 and legacy encoders |
| Raster and logic | Wideband oscillators, sequential filters/delays, noise, clocked gates/registers/counters | DWO3, Prismatic Ray, Contour/Curtain, PAB, Castle, Fortress, corresponding Cadet/Visionary functions |
| Catalog expansion | More complex color mapping, layer routing, motion controls, historical panel variants | Remaining A/B modules in the inventory, each with its own behavior specification |
| Integrated instruments and hardware | Compose established primitives; evaluate individual programmable effects and external adapters | Vidiot, Chromagnon, Videomancer, BitVision, additional media features, C-grade hardware integrations |

Do not implement 96 independent engines. Reuse tested operators while preserving significant differences between hardware modules. Provisional and U entries need adequate evidence before being promoted into an implementation commitment. N entries need documentation only.

For each module before its coding stage, produce a behavior sheet containing: source/revision; ports and normalizations; control ranges and laws; equations or routing; state/reset rules; timing; bypass; expected clipping; reference patch; known deviations; and acceptance tests. First-release sheets are part of the pre-code specification work. Later catalog sheets can be completed before their respective expansion stages.

**10. Milestones and acceptance gates**

All implementation milestones below occur after plan approval. A technical gate means testing a stated uncertainty; it does not authorize silently dropping a requested feature.

| Milestone | Deliverable | Exit criteria |
|---|---|---|
| P — Planning | Approved scope, target machine/host, architecture choices, initial module behavior sheets, reference patch and test specification | The user can review the intended controls, behavior, limits and sequence before coding |
| M1 — Engine proof | Two connected image operators, monitor, CV bridge, delay, a small raster-state calibration path and bidirectional Syphon | Typed routing survives edits/reload; simultaneous receive/publish; source restart/resize; hidden-monitor operation; bounded memory; raster sample order and phase preserved; no rendering waits in audio |
| M2 — Core instrument | Core synthesis group and example patches | Shapes, signed mixing, keying and color transforms pass numeric/reference-image checks; per-pixel modulation and ordinary CV are distinguishable and work |
| M3 — Memory | Memory instrument plus still-image input | Four paths work; exact history indexing; capture events retained; multiple loops deterministic; memory cap and recall behavior pass |
| M4 — Dirty NTSC | Encoder, mixer, receiver | Clean round trip; predictable synchronized mixing; independent-clock disturbance and recovery; damage accumulates in an internal feedback patch |
| M5 — First release | M1–M4, audio bridges, documentation and packaged build | Entire demonstration patch passes target-Mac performance and stability suite; installation and patch migration checked |
| M6 — Raster expansion | Oscillators, filters and clocked logic | Edge/reset ordering, filter response and aliasing validated against selected references |
| M7 — Catalog expansion | Remaining documented modules in batches | Every released module has a completed behavior sheet and justified fidelity designation |

At M1, inability to maintain video while the UI is hidden or to share Syphon textures safely triggers a renderer/adapter redesign before adding panels. At M4, inadequate receiver behavior keeps dirty mixing in development; it must not be relabeled as complete because a generic VHS effect looks attractive.

**11. Validation and performance budget**

Performance targets are provisional until the Mac and host are specified. Proposed baseline: the complete SD demonstration patch at NTSC-class cadence, with output rates measured separately from monitor redraw and no audio underruns attributable to the video workload. Report median, 95th/99th-percentile processing times, missed deadlines, end-to-end latency, CPU/GPU usage and peak/steady memory. A small average render time alone is not enough.

| Test area | Evidence required |
|---|---|
| Arithmetic | Known ramps, impulses, negative and above-white values; CPU/GPU comparison with declared tolerances |
| Color | RGB/component round trips, alpha edges, selected input/output transfer and range conventions |
| Timing | Long-run rational clock checks; frame/field parity; timestamped triggers; reset/sample-rate changes |
| Feedback | Expected delay in ticks; no module-position dependence; reproducible seeds; stable explicit-loop scheduling |
| Memory | History allocation cap; repeated resizing/clearing; many instances; deletion releases resources |
| NTSC | Color bars, multiburst/edge patterns, phase sweeps, independently drifting sources, lock loss and reacquisition |
| Syphon | Two simultaneous sources; two publishers; restart, duplicate names, format changes, alpha/orientation and disconnection |
| Rack lifecycle | Add/delete/duplicate, cable undo/redo, save/reload, bypass/reset, visibility and window lifecycle, patch replacement |
| Audio coexistence | A repeatable audio patch at the user's intended sample rate/block size, comparing baseline with video stress |
| Release | Fresh install, missing optional assets, older patch schemas, one-hour demonstration run and repeated reload cycles |

Use analytic expectations where available, reference renders with tolerances for GPU math, and human comparison for visual character. Hardware resemblance cannot be established from a self-generated golden image alone. Reference footage should identify the patch, module/firmware revision and capture chain where known.

As a capacity estimate, 60 RGBA16F history frames at 720 × 480 take about 158 MiB; at 1920 × 1080 they take about 949 MiB, before working surfaces and driver overhead. Memory scales per instance. Expose a budget, estimate allocations before committing them, and refuse unsupported sizes with a useful message instead of exhausting the machine.

Keep the first package standalone-capable. DAW support, if requested, adds multiple-instance isolation, editor close/reopen, transport/offline behavior, and host-session restoration tests before claiming compatibility.

**12. Effort, uncertainty and remaining decisions**

This is a substantial graphics/audio application inside Rack. For one experienced developer working full time, a provisional planning allowance is 6–12 engineer-weeks for the engine/Syphon foundation, 3–6 for core modules, 4–8 for the memory instrument, 6–12 for the composite subsystem, and 3–6 for integration and release work: roughly 22–44 engineer-weeks for the proposed first release. These are judgment-based allowances, not measured estimates or a delivery promise. Full catalog coverage is additional. Re-estimate after the engine and receiver gates; a renderer redesign or hardware-matching requirement can exceed these ranges.

| Decision | Proposed position | What resolves it |
|---|---|---|
| Mac and host | Apple Silicon / standalone as temporary planning baseline | User's chip, memory, Rack edition/version, and DAW requirement |
| Fidelity and interface | Confirmed: close LZX controls/ports and analog signal behavior | Per-module source/revision audit, control sheets and comparison evidence |
| First-release scope | Core shapes/color + memory + dirty NTSC + bidirectional Syphon | User reviews the staged scope; full catalog remains a roadmap |
| Renderer | Evaluate Rack-compatible OpenGL first; maintain a backend boundary | Architecture review now; build/lifecycle evidence at M1 after coding approval |
| Dependency reuse | Syphon SDK preferred; VVGL/VVISF evaluated; other repos primarily references | Selected-revision source/build and license checks during M1 |
| Hardware fidelity reference | Functional equivalence until a specific reference is chosen | Named modules/firmware/receiver and available evidence |
| Distribution | Private development builds initially | Public/commercial packaging and dependency choices can be decided before distribution |

The architecture plan is reviewable now. Before requesting final permission to code, incorporate the target-Mac answer and finish the first-release behavior sheets, including a control/port table and representative patch for the memory and composite modules. Keep unresolved empirical questions attached to the explicit implementation gates above. Approval of this plan should never be represented as proof that those experiments have already passed.
