**RVX — LZX module feasibility review**

RVX is the project and plugin-suite name; `rvx-vcv-rack` is the repository name. This review assesses LZX hardware functions for RVX, an analog video synthesis system for VCV Rack on Mac. See the [RVX implementation plan](VCV-Video-System-Plan.md) for the proposed architecture and roadmap.

Prepared September 7, 2026. This is a researched engineering assessment, not a completed port or a performance benchmark.

**Verdict: most LZX synthesis and processing functions are good software candidates.** The hard part is a shared video engine that preserves timing, signal ranges, and feedback across the modules. Memory Palace is especially worthwhile; dirty NTSC mixing is feasible but is a distinct, harder subsystem.

This review covers **all 80 entries in LZX's current module index**, adds **11 historical modules/utilities** absent from that index, and includes **five standalone instruments/media products**: **96 assessed entries**. Known prototypes and ambiguous historical names are recorded separately. Panel colors, kit/assembled editions, and revisions with the same function are not separate modules. This is an audit of the discoverable catalog, not a claim to have located every unpublished prototype. Inventory sources: [LZX module index](https://lzxindustries.net/modules), [historical Visionary archive](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352), and [instrument index](https://lzxindustries.net/instruments).

The grades evaluate useful software equivalents of the documented creative behavior. They do not promise circuit-exact emulation or compatibility with electrical video signals. Every grade and proposed implementation is my inference; linked sources establish the original functions.

| Grade | Meaning |
|---|---|
| A | Strong candidate: relatively straightforward once the shared video engine exists. |
| B | Feasible, with substantial timing, state, integration, or fidelity work. |
| C | A major part of the original purpose requires external hardware or a changed workflow. |
| N | No meaningful signal-processing function to port. |
| U | Insufficient evidence for a dependable module-specific grade. |
| * | Provisional: incomplete behavior documentation or changing hardware revisions. |

A high grade does not mean every unusual feedback patch is easy. An A-grade multiplier in a near-instantaneous analog feedback loop can become a B/C-grade system simulation problem. Also, an A-grade large module can take more UI work than a small B-grade filter.

Mac is a supported development target: Rack publishes both [Apple Silicon and Intel SDKs](https://vcvrack.com/downloads/). I would target standalone Rack on Apple Silicon first, with Intel builds as a separate validation target. Your exact Mac, memory and Rack version are not known, so there is no justified frame-rate promise. Start with SD/NTSC-class imagery, then measure HD performance.

Rack offers [OpenGL rendering widgets](https://vcvrack.com/docs-v2/structrack_1_1widget_1_1OpenGlWidget). Apple has [deprecated OpenGL in favor of Metal](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/opengl_intro/opengl_intro.html). My proposed path is a small Rack-compatible rendering prototype first, with the video engine isolated from its renderer so a Metal backend can be added if needed. Metal-to-Rack display integration is engineering work, not automatic. [Syphon](https://syphon.info/) input and output are required first-release features, exposed through a Video I/O module with a workflow modeled on Rack's Core Audio module. Syphon supports OpenGL/Metal interoperability; its external frame sharing connects to our internal video graph through this module.

The **Video I/O module** is a confirmed product requirement. It follows the familiar driver-and-source selection pattern of [Rack audio I/O](https://vcvrack.com/manual/Core), with Syphon as a selectable Mac backend. This is a module in our plugin suite, not a proposed change to Rack's built-in Audio module.

| Control or connection | Required behavior |
|---|---|
| Driver | Select Syphon; leave room for later camera, file and hardware backends. |
| FROM APP / source | Discover available Syphon servers, showing application and server names. Select one source per module instance. |
| FROM APP / patch outputs | Feed the received image into the video graph, with an image connection and access to R, G, B, alpha and derived luma signal fields. These are dedicated video ports. |
| TO APP / patch input | Accept the graph's image output for publication to other applications. |
| TO APP / stream name | Choose the published Syphon server name and enable or disable output independently of input. Receiving applications select this stream; the module does not need to pick one destination application. |
| Format and timing | Show incoming dimensions and measured update rate; select output resolution and target publication rate. Display actual output rate separately. Define fit, crop or stretch explicitly when dimensions differ. |
| Monitoring | Show source availability, receiving/publishing status, and a small optional preview. |
| Patch recall | Save backend, source application/server identity, publishing name, format and enabled state. Rediscover on reload; avoid relying only on an ephemeral server identifier. |
| Reconnection | Recover when a source application restarts. Show an unavailable or ambiguous source clearly; never silently switch to an unrelated source. |
| Multiple instances | Support multiple sources and separately named output streams, including input and output at the same time. Resolve duplicate publisher names visibly. |

Syphon transfers image frames; source arrival, our video processing clock and Rack audio run on separate schedules. The default design consumes the latest completed source frame, repeats it when necessary and avoids accumulating a latency queue. Missing-source behavior should be selectable as black or hold-last-frame, with a visible status indication. Preserve alpha and define color/alpha conventions at the boundary. GPU synchronization, source resizing and disconnects must never block Rack's audio callback.

A deliberate external feedback patch is supported: another application's output can return through FROM APP. It will carry buffering and application latency; it is not equivalent to an internal sample-delay loop. NTSC damage remains simulated within the graph, since Syphon shares decoded image frames rather than an electrical composite waveform. Feasibility: **B**, mainly for GPU resource lifetime, timing and robust integration. This new I/O module is additional to the LZX catalog grades.

All grades assume three capabilities in the shared engine:

1. **Image processing:** floating-point signal fields, per-pixel modulation, explicit clipping and optional bandwidth limits. “Red” remains a general signal until sent to a display; signed and above-white values must survive intermediate stages.
2. **Raster-time processing:** a sample/edge time base independent of Rack's audio rate for oscillators, filters and clocked logic. Stateful operations may need CPU or compute processing rather than a simple fragment shader. Line boundaries and blanking matter.
3. **Frame memory:** explicit delayed feedback, bounded history buffers, capture, freeze and deterministic ordering.

Normal Rack cables still carry audio/CV, not full video waveforms or arbitrary image data. Our video connections would need a custom protocol. Stock Rack modules could modulate controls, but could not automatically process full-bandwidth video. The [Rack processing API](https://vcvrack.com/manual/PluginDevelopmentTutorial) is called at the engine's audio rate.

The following entries cover **P series**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [LNK](https://lzxindustries.net/modules/lnk) | **A** | Signal distribution | Routing references is simple; electrical loading would need a separate optional model. |
| [MLT](https://lzxindustries.net/modules/mlt) | **A** | Cascaded multiples | Easy fan-out and normalled connections; little need for this many duplicate ports in software. |
| [P](https://lzxindustries.net/modules/p) | **A** | Attenuation and crossfade | Simple arithmetic. Preserve its intended low-frequency control behavior. |
| [PAB](https://lzxindustries.net/modules/pab) | **B** | Buffers and tiny delays | Fractional raster-sample delays can reproduce registration shifts; timing and interpolation matter. |
| [PGO](https://lzxindustries.net/modules/pgo) | **A** | Gain and offset | Straightforward signed arithmetic with configurable routing and clipping. |
| [PRM](https://lzxindustries.net/modules/prm) | **A** | Rectification and multiplication | Direct mathematical operators; feedback-patched division needs the shared loop solver. |

The following entries cover **Gen3**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [Angles](https://lzxindustries.net/modules/angles) | **A** | Rotated ramps | Compute horizontal/vertical coordinates and fixed-angle mixtures directly. |
| [Contour](https://lzxindustries.net/modules/contour) | **B** | Video high-pass filters | Needs filters running in raster order, including history across lines, for convincing streaks. |
| [DC Distro 3A](https://lzxindustries.net/modules/dc-distro-3a) | **N** | Power distribution | No signal-processing function to recreate in Rack. |
| [DSG3](https://lzxindustries.net/modules/dsg3) | **A** | Shapes and analog logic | Folds, curves, min/max and blends map well to per-pixel processing. |
| [DWO3](https://lzxindustries.net/modules/dwo3) | **B** | Wideband oscillators | Free-running phase, reset, sync, and video-rate FM require time-aware synthesis. |
| [ESG3](https://lzxindustries.net/modules/esg3) | **B** | Encoding and sync | Virtual timing and composite encoding are feasible. Electrical output and genlock require hardware. |
| [Factors](https://lzxindustries.net/modules/factors) | **A** | Four-quadrant modulation | Multiplication around a defined midpoint maps directly to software. |
| [FKG3](https://lzxindustries.net/modules/fkg3) | **A** | Keying and RGB fades | Per-pixel comparisons, soft transitions and RGB interpolation are straightforward. |
| [Keychain](https://lzxindustries.net/modules/keychain) | **A** | Hard keys | Threshold comparisons are easy; exact edge speed and tiny delays require oversampling. |
| [Matte](https://lzxindustries.net/modules/matte) | **A** | Static color levels | Constant fields and averages are elementary; retain its averaging behavior. |
| [Proc](https://lzxindustries.net/modules/proc) | **A** | Triple gain/offset mixer | Signed values and operation order must be preserved before final display clipping. |
| [Ribbons](https://lzxindustries.net/modules/ribbons) | **A** | Three-bit amplitude slicing | Straightforward thresholds and bit outputs. Avoid adding an artificial pixelation clock. |
| [Scrolls](https://lzxindustries.net/modules/scrolls/manual) | **B** | Animated ramp generation | Feasible stateful motion engine; reproduce modes, slew and timing rather than only translating a texture. |
| [SMX3](https://lzxindustries.net/modules/smx3) | **A** | Matrix mixing | Small per-pixel matrix operations; preserve negative and above-white intermediate values. |
| [Stacker](https://lzxindustries.net/modules/stacker) | **A** | Quadrilateral keys and priority | Comparisons and priority masks are inexpensive GPU operations. |
| [Stairs](https://lzxindustries.net/modules/stairs) | **A** | Multistage wavefolding | Piecewise transfer curves are easy; fine patterns need controlled antialiasing. |
| [Sum/Dist](https://lzxindustries.net/modules/sumdist/manual) | **A** | Summing and distribution | Simple adds and routing. Optional propagation-delay modeling would raise fidelity effort. |
| [Swatch](https://lzxindustries.net/modules/swatch) | **A** | RGB/YIQ conversion | Matrix transforms are straightforward; preserve unclamped outputs where appropriate. |
| [Switcher](https://lzxindustries.net/modules/switcher) | **A** | RGB source routing | Per-pixel selection supports image-controlled switching as well as ordinary CV selection. |
| [TBC2](https://lzxindustries.net/modules/tbc2) | **B** | Dual video input and timing | File/camera ingestion and frame synchronization are feasible; raw analog decoding depends on capture hardware. |
| [TBC2 Expander](https://lzxindustries.net/modules/tbc2-expander) | **N** | VGA connector expansion | Physical connector breakout; represent supported inputs in the software input module. |

The following entries cover **Castle**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [Castle 000 ADC](https://lzxindustries.net/modules/castle-000-adc) | **A** | Three-bit conversion | Thresholds generate bit planes; analog edge characteristics are a separate fidelity layer. |
| [Castle 001 DAC](https://lzxindustries.net/modules/castle-001-dac) | **A** | Bit-to-level conversion | Weighted sums of bit planes are easy. |
| [Castle 010 Clock VCO](https://lzxindustries.net/modules/castle-010-clock-vco) | **B** | Video clock oscillator | Must resolve clock edges at video time scales independently of Rack's audio rate. |
| [Castle 011 Shift Register](https://lzxindustries.net/modules/castle-011-shift-register) | **B** | Clocked bit memory | State must advance in raster-time order, not once per display frame. |
| [Castle 100 Multi Gate](https://lzxindustries.net/modules/castle-100-multi-gate) | **A** | Boolean logic | Direct boolean operations after the input thresholds. |
| [Castle 101 Quad Gate](https://lzxindustries.net/modules/castle-101-quad-gate) | **A** | Configurable logic combinations | Small truth tables and routing; chip variants can become selectable modes. |
| [Castle 110 Counter](https://lzxindustries.net/modules/castle-110-counter) | **B** | Clock counting and division | Simple state logic, but arbitrary video clocks require edge-accurate evaluation. |
| [Castle 111 D Flip Flops](https://lzxindustries.net/modules/castle-111-d-flip-flops) | **B** | Clocked bit storage | Sampling, reset and state history need a defined timing model. |

The following entries cover **Orion**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [Diver](https://community.lzxindustries.net/t/1455/) | **B** | Audio-to-video waveform sampling | Capture an audio buffer and read it along raster axes; freeze and phase behavior need careful timing. |
| [Escher Sketch](https://community.lzxindustries.net/t/1345/) | **B** | XY/pressure gesture controller | XY and gesture recording are easy; pressure and the physical feel depend on the input device. |
| [Fortress](https://community.lzxindustries.net/t/1392/) | **B** | Clocked low-resolution graphics | Counters, bit operations, shift registers and cellular automata are implementable; slip-sync is the harder part. |
| [Memory Palace](https://community.lzxindustries.net/t/memory-palace-user-guide/884) | **B** | Frame memory and feedback | Strong candidate. Delay buffers, transforms, keying and painting are standard techniques, but integration is substantial. |

The following entries cover **Expedition**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [Arch](https://community.lzxindustries.net/t/1305/) | **A** | Nonlinear signal functions | Min/max, rectification and transfer curves map directly to GPU arithmetic. |
| [Bridge](https://community.lzxindustries.net/t/1295/) | **A** | Scaling, mixing and fading | Simple operations; video ports and ordinary Rack CV still need explicit conversion. |
| [Color Chords](https://community.lzxindustries.net/t/1297/) | **A** | Color mixing with layer priority | Layer opacity and priority are straightforward to implement. |
| [Curtain](https://community.lzxindustries.net/t/1306/) | **B** | Edges, blur and enhancement | Model raster-time filter response, rectification and wet/dry mixing for its characteristic trails. |
| [Cyclops](https://lzxindustries.net/modules/cyclops) | **C** | Laser display interface | An onscreen vector preview is easy; actual ILDA output needs a compatible DAC and separate integration. |
| [Doorway](https://community.lzxindustries.net/t/1307/) | **A** | Soft keys and outlines | Gain, clipping, rectification and foreground/background blending are direct operations. |
| [Liquid TV](https://lzxindustries.net/modules/liquid-tv) | **A** | Video monitoring | An embedded monitor is straightforward; physical display electronics add no necessary software behavior. |
| [Mapper](https://lzxindustries.net/modules/mapper) | **A** | Hue-based color conversion | Implement its color mapping and transfer curves; do not substitute a generic HSV picker without checking behavior. |
| [Marble Index](https://lzxindustries.net/modules/marble-index) | **B** | Three-layer compositing | All operations are feasible; numerous routing, inversion and solarization combinations require careful reconstruction. |
| [Navigator](https://lzxindustries.net/modules/navigator) | **A** | Coordinate rotation and position | Arithmetic on two signal fields; it does not require a general 3D renderer. |
| [Passage](https://lzxindustries.net/modules/passage) | **A** | Triple signal processing | Basic gain, inversion, offset and mixing; preserve the documented operation order. |
| [Pendulum](https://lzxindustries.net/modules/pendulum) | **A** | Animation and modulation | LFOs, crossfading and routing fit Rack naturally; response curves need calibration. |
| [Polar Fringe](https://lzxindustries.net/modules/polar-fringe) | **B** | Chroma key generation | Feasible color-distance/transfer model; matching the hardware's soft boundary needs references. |
| [Prismatic Ray](https://lzxindustries.net/modules/prismatic-ray) | **B** | Video oscillator | Same timing challenges as DWO3: free phase, modulation, reset and alias control. |
| [Sensory Translator](https://lzxindustries.net/modules/sensory-translator) | **A** | Audio-band envelope extraction | Audio filters and envelope followers are native DSP tasks; route Mac microphone through an audio input. |
| [Shapechanger](https://lzxindustries.net/modules/shapechanger) | **B** | Coordinate waveshaping | Feasible nonlinear math, with work needed to recover exact curves and control interactions. |
| [Staircase](https://lzxindustries.net/modules/staircase) | **A** | Continuous solarization | Piecewise waveshaping is inexpensive; preserve continuous folds rather than imposing stepped posterization. |
| [Topogram](https://lzxindustries.net/modules/topogram) | **A** | Band keys and colorization masks | Threshold bands, edge softness and grouped outputs are direct per-pixel operations. |
| [Visual Cortex](https://community.lzxindustries.net/t/1314/) | **B** | Core video synthesis and I/O | Its generator, colorizer and compositor translate well; encoder/decoder and timing add integration work. |
| [War Of The Ants](https://community.lzxindustries.net/t/1291/) | **B** | Controlled noise textures | Model correlated noise across raster time and motion, not independent random pixels alone. |

The following entries cover **Cadet**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [Cadet I Sync Generator](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-i-sync-generator) | **B** | Raster timing and reference | Virtual timing is straightforward; reproducing lock behavior and actual electrical sync is harder. |
| [Cadet II RGB Encoder](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-ii-rgb-encoder) | **B** | Composite encoding | Shares the NTSC encoder engine; real composite output needs hardware. |
| [Cadet III Video Input](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-iii-video-input) | **B** | External video conditioning | Virtual clamping/conditioning is feasible; physical signal recovery depends on the input interface. |
| [Cadet IV Dual Ramp Generator](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-iv-dual-ramp-generator) | **A** | Horizontal and vertical ramps | Deterministic coordinate fields with defined reset and blanking. |
| [Cadet IX VCO](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-ix-voltage-controlled-oscillator) | **B** | Wideband triangle oscillator | Requires video-time phase integration and sync modeling. |
| [Cadet V Scaler](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-v-scaler) | **A** | Voltage scaling | Small fixed gain/offset operators; build variants can be modes. |
| [Cadet VI Fader](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-vi-fader) | **A** | Crossfading | Direct interpolation with the appropriate control range. |
| [Cadet VII Processor](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-vii-processor) | **A** | Signal processing | Simple gain/offset/summing primitives; verify exact circuit routing before implementation. |
| [Cadet VIII Hard Key Generator](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-viii-hard-key-generator) | **A** | Comparators | Direct thresholds, with edge modeling available later. |
| [Cadet X Multiplier](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-x-multiplier) | **A** | Two-/four-quadrant multiplication | Direct arithmetic with the appropriate sign and clipping conventions. |

The following entries cover **Visionary**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [Colorspace Mapper](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **A** | Color conversion | Recover documented color mapping rather than assuming standard HSV behavior. |
| [Differentiator](https://www.analoguehaven.com/lzx-industries/differentiator/manual.pdf) | **B*** | Legacy filter circuit | A raster-time filter is feasible; exact response needs a more complete circuit audit. |
| [Function Generator](https://www.analoguehaven.com/lzx-industries/function-generator/manual.pdf) | **A*** | Legacy nonlinear shaper | Piecewise transfer functions fit software well; confirm the production revision's response. |
| [Triple Video Fader & Key Generator](https://lzxindustries.net/modules/triple-video-fader-key-generator) | **A** | Faders and keys | Comparisons and blends are easy; retain normalled connections and mode logic. |
| [Triple Video Interface](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **B** | Analog input conditioning | Virtual processing is feasible; external voltages and genlock require hardware. |
| [Triple Video Processor](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **A** | Gain and bias processing | Order of operations is significant and must match the original. |
| [Video Blending Matrix](https://lzxindustries.net/modules/video-blending-matrix) | **A** | Signal mixing | Simple sums and absolute-value processing. |
| [Video Flip Flops](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **B** | Bit memory | Needs raster-clock state, not frame-only evaluation. |
| [Video Logic](https://lzxindustries.net/modules/video-logic) | **A** | Boolean processing | Truth tables are easy; hardware edge artifacts need extra modeling. |
| [Video Waveform Generator](https://lzxindustries.net/modules/video-waveform-generator) | **B** | Video oscillator | Stable sync and modulation require the shared time-domain oscillator engine. |
| [Voltage Interface I](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **A** | Voltage conditioning | Gain, offset and clipping are simple; physical voltage output remains external. |

The following entries cover **Additional historical modules**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [Audio Frequency Decoder](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **A** | Audio envelopes | Ordinary audio DSP; filter constants need matching. |
| [Color Time Base Corrector](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **B** | External input synchronization | Virtual frame alignment is practical; physical decoding needs capture hardware. |
| [Color Video Encoder](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **B** | Composite output encoding | Shares the NTSC engine; electrical output is separate. |
| [Octal Video Quantizer & Sequencer](https://lzxindustries.net/modules/octal-video-quantizer-sequencer) | **B** | Quantization and sequencing | The arithmetic is easy; clock ordering needs care. |
| [Triple Video Multimode Filter](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **B** | Filtering | Preserve raster-time state and response curves. |
| [Video Divisions](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **B** | Clock division | Needs the shared edge-timing engine. |
| [Video Ramps](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **A** | Ramp generation | Straightforward coordinate arithmetic. |
| [Video Sync Generator](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **B** | Sync generation | Virtual timing is feasible; physical locking is separate. |
| [Voltage Bridge](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **A** | Voltage scaling | Straightforward gain operators. |
| [XY Display Driver](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | **C*** | Display interface | Virtual preview is feasible; hardware behavior is sparsely documented. |
| [DC Distro 5A](https://lzxindustries.net/modules/dc-distro-5a) | **N** | Power distribution | Hardware power utility; no software processing role. |

The following entries cover **Standalone instruments and media**.

| Module / source | Grade | Main function | Feasibility assessment |
|---|---|---|---|
| [Chromagnon](https://lzxindustries.net/instruments/chromagnon) | **B*** | Integrated video instrument | Shape/color processing is feasible; evolving revisions complicate exact matching. Physical ILDA and analog I/O need hardware. |
| [Videomancer](https://github.com/lzxindustries/videomancer-sdk) | **B** | Programmable video effects | Feasible program by program. Public VHDL examples help, but FPGA programs must be translated; they are not Rack binaries. |
| [Vidiot](https://lzxindustries.net/instruments/vidiot) | **B** | Integrated analog video synth | Build from oscillator, keyer, color and input engines; interaction and feedback require tuning. |
| [BitVision](https://community.lzxindustries.net/t/all-about-bitvision-legacy/1353) | **B** | Low-resolution audiovisualizer | Pixel graphics and audio modulation are easy; analog color-phase behavior needs the composite model. |
| [Andor 1](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/andor-1) | **A*** | Media playback | Ordinary media playback is feasible; exact format support and legacy quirks have not been audited. |

The following **historical names need qualification**, so they are excluded from the 96 assessed products.

| Name / source | Grade | Finding |
|---|---|---|
| [Scroll & Position Controller](https://lzxindustries.net/modules/scrolls/manual) | B* concept | Unreleased prototype; LZX says its ideas continued in Diver and Scrolls. Grade concerns the motion-control concept only. |
| [Video Key Generator](https://community.lzxindustries.net/t/visionary-series-overview/5213) | U | Appears as a historical tag. I could not verify a distinct production design and complete behavior. Generic keying is A. |
| [Video Multiplier](https://community.lzxindustries.net/t/visionary-series-overview/5213) | U | Appears as a historical tag. Distinct production identity is unresolved; generic multiplication is A. |
| [Video Waveform Distributor](https://community.lzxindustries.net/t/visionary-series-overview/5213) | U | Appears as a historical tag. Do not assume it is just a mult without a manual or schematic. |

**Bundles and support hardware:** [Double Vision](https://docs.lzxindustries.net/docs/instruments/double-vision) and its expansion bundles package other modules, so their feasibility is inherited from the included modules. [Vessel enclosures, Bus 168, Rack 84HP, power-entry panels and adapters](https://lzxindustries.net/cases-and-power) are N as software ports; any sync-routing concept belongs in the engine. Cables, blank panels, front-panel variants and mounting accessories add no independent processing behavior.

**Memory Palace deserves an early place in the build.** Its B grade reflects the size of the implementation, not serious doubt about feasibility. The documented frame memory, transform, keying and paint workflows translate well to software. I would reproduce feedback mode and paint mode as different signal routings, expose delay/freeze/clear to Rack triggers, and keep color processing inside the loop when required. The [original guide](https://community.lzxindustries.net/t/memory-palace-user-guide/884) specifies frame-rate sampling of the main CV parameters; matching that is different from making every control respond per pixel. Exact firmware behavior, interpolation, numerical precision and palette response remain reference-validation work.

Memory scales rapidly with resolution and precision. As a planning calculation, 60 frames at 720 × 480 in RGBA16F require about 158 MiB just for history; 60 frames at 1920 × 1080 need about 949 MiB. These are estimates from dimensions × four channels × two bytes × frame count, not measured application usage. NTSC field storage/deinterlacing and working surfaces add further design choices. Use bounded memory and make saving recorded history an explicit option.

**NTSC and dirty mixing are additional capabilities**, rather than one-for-one LZX module ports. LZX normally distributes clean sync and strips/reinserts sync at its interfaces; its RGB patching environment is different from mixing damaged composite waveforms. See [LZX's sync explanation](https://docs.lzxindustries.net/docs/guides/installing-modules).

| Requested capability | Grade | What the grade means |
|---|---|---|
| NTSC encode/decode inside Rack | B | Model luma/chroma filtering, colorburst, blanking and field timing; process samples separately from audio. |
| Dirty mixing with stable source timing | B | Combine simulated composite signals before decoding; tune gain, offsets and nonlinear distortion. |
| Dirty mixing with independent source clocks and sync loss | B, highest risk | Requires a receiver model that can lose and reacquire sync/color lock; exact CRT behavior is C until matched to a specific reference setup. |
| Composite degradation inside Memory Palace feedback | B | Feasible with explicit frame delay and bounded state; composite artifacts accumulate each pass. |
| Displaying the rendered result on a real NTSC CRT | C | Requires video-output hardware. A conventional converter generates fresh electrical timing. |
| Sending deliberately malformed composite/sync to real hardware | C | Requires a suitable waveform output/DAC path; ordinary audio interfaces and normal HDMI conversion do not supply this capability. |
| Bringing an already dirty hardware signal into Rack | C | Capture hardware may stabilize, discard, or lose lock on damaged timing before software receives frames. |

The basis for the NTSC design is [Analog Devices' encoder description](https://www.analog.com/en/products/ad724.html) and [decoder architecture](https://www.analog.com/en/products/adv7282a.html). Dirty mixer behavior and sync replacement are documented in the [MisMatcher manual](https://freedomenterprise.pt/files/manuals/Owners_Manual_MisMatcher01RevD_RevA.pdf). Performance and similarity of our proposed simulation remain untested.

I would build in this order:

1. **Engine proof:** two video modules, monitor, bidirectional Syphon Video I/O, shared time base, custom video routing, signed values, CV bridge, and a defined one-frame feedback connection. Validate Syphon discovery, publishing, reconnection and source format changes. Verify rendering continues correctly through module movement, visibility changes and patch reloads without interrupting audio.
2. **Useful first instrument:** Angles, DSG3, Proc, SMX3, FKG3, Stairs and Swatch equivalents, followed by Memory Palace. This gives shapes, color and expressive feedback early.
3. **Your distinctive sound-and-video workflow:** Diver and Sensory Translator equivalents, then the NTSC dirty-mixing and receiver engine. Test with slowly drifting, independently timed sources.
4. **Analog and clock detail:** DWO3/Prismatic Ray, Contour/Curtain, PAB, Castle and Fortress. Validate scanline response, clock/reset ordering, and aliasing against references.
5. **Broader catalog and external I/O:** reuse the tested engines for historical panel variations, additional media/capture backends and any hardware-output integration; Syphon is already included in the first milestone.

Before expanding to dozens of panels, the important proof is one patch combining **shapes → color → Memory Palace feedback → NTSC dirty mixing → monitor**. That tests the visual character you actually want and exposes the shared architectural risks.

Evidence limitations: This is a functional catalog review, not a schematic-by-schematic audit or a firmware port. Some current LZX pages contain legacy or contradictory release text, so release promises and stock status were not used as technical evidence. Broken historical Cadet links were reconciled with the [current documentation repository](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf) and [Cadet source repository](https://github.com/lzxindustries/lzxcadet). The older Function Generator and Differentiator assembly documents are manufacturer-authored copies hosted by a dealer; their exact transfer responses still need verification. Chromagnon is graded provisionally because a fixed revision must be selected. Videomancer is assessed as a programmable instrument, not a claim that every shipping program has been audited.
