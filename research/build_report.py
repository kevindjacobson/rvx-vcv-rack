import json
from pathlib import Path
inv=json.loads(Path('research/inventory.json').read_text())
# Grades are engineering assessments, not manufacturer ratings.
data='''LNK|A|Signal distribution|Routing references is simple; electrical loading would need a separate optional model.
MLT|A|Cascaded multiples|Easy fan-out and normalled connections; little need for this many duplicate ports in software.
P|A|Attenuation and crossfade|Simple arithmetic. Preserve its intended low-frequency control behavior.
PAB|B|Buffers and tiny delays|Fractional raster-sample delays can reproduce registration shifts; timing and interpolation matter.
PGO|A|Gain and offset|Straightforward signed arithmetic with configurable routing and clipping.
PRM|A|Rectification and multiplication|Direct mathematical operators; feedback-patched division needs the shared loop solver.
Angles|A|Rotated ramps|Compute horizontal/vertical coordinates and fixed-angle mixtures directly.
Contour|B|Video high-pass filters|Needs filters running in raster order, including history across lines, for convincing streaks.
DC Distro 3A|N|Power distribution|No signal-processing function to recreate in Rack.
DSG3|A|Shapes and analog logic|Folds, curves, min/max and blends map well to per-pixel processing.
DWO3|B|Wideband oscillators|Free-running phase, reset, sync, and video-rate FM require time-aware synthesis.
ESG3|B|Encoding and sync|Virtual timing and composite encoding are feasible. Electrical output and genlock require hardware.
Factors|A|Four-quadrant modulation|Multiplication around a defined midpoint maps directly to software.
FKG3|A|Keying and RGB fades|Per-pixel comparisons, soft transitions and RGB interpolation are straightforward.
Keychain|A|Hard keys|Threshold comparisons are easy; exact edge speed and tiny delays require oversampling.
Matte|A|Static color levels|Constant fields and averages are elementary; retain its averaging behavior.
Proc|A|Triple gain/offset mixer|Signed values and operation order must be preserved before final display clipping.
Ribbons|A|Three-bit amplitude slicing|Straightforward thresholds and bit outputs. Avoid adding an artificial pixelation clock.
Scrolls|B|Animated ramp generation|Feasible stateful motion engine; reproduce modes, slew and timing rather than only translating a texture.
SMX3|A|Matrix mixing|Small per-pixel matrix operations; preserve negative and above-white intermediate values.
Stacker|A|Quadrilateral keys and priority|Comparisons and priority masks are inexpensive GPU operations.
Stairs|A|Multistage wavefolding|Piecewise transfer curves are easy; fine patterns need controlled antialiasing.
Sum/Dist|A|Summing and distribution|Simple adds and routing. Optional propagation-delay modeling would raise fidelity effort.
Swatch|A|RGB/YIQ conversion|Matrix transforms are straightforward; preserve unclamped outputs where appropriate.
Switcher|A|RGB source routing|Per-pixel selection supports image-controlled switching as well as ordinary CV selection.
TBC2|B|Dual video input and timing|File/camera ingestion and frame synchronization are feasible; raw analog decoding depends on capture hardware.
TBC2 Expander|N|VGA connector expansion|Physical connector breakout; represent supported inputs in the software input module.
Castle 000 ADC|A|Three-bit conversion|Thresholds generate bit planes; analog edge characteristics are a separate fidelity layer.
Castle 001 DAC|A|Bit-to-level conversion|Weighted sums of bit planes are easy.
Castle 010 Clock VCO|B|Video clock oscillator|Must resolve clock edges at video time scales independently of Rack's audio rate.
Castle 011 Shift Register|B|Clocked bit memory|State must advance in raster-time order, not once per display frame.
Castle 100 Multi Gate|A|Boolean logic|Direct boolean operations after the input thresholds.
Castle 101 Quad Gate|A|Configurable logic combinations|Small truth tables and routing; chip variants can become selectable modes.
Castle 110 Counter|B|Clock counting and division|Simple state logic, but arbitrary video clocks require edge-accurate evaluation.
Castle 111 D Flip Flops|B|Clocked bit storage|Sampling, reset and state history need a defined timing model.
Diver|B|Audio-to-video waveform sampling|Capture an audio buffer and read it along raster axes; freeze and phase behavior need careful timing.
Escher Sketch|B|XY/pressure gesture controller|XY and gesture recording are easy; pressure and the physical feel depend on the input device.
Fortress|B|Clocked low-resolution graphics|Counters, bit operations, shift registers and cellular automata are implementable; slip-sync is the harder part.
Memory Palace|B|Frame memory and feedback|Strong candidate. Delay buffers, transforms, keying and painting are standard techniques, but integration is substantial.
Arch|A|Nonlinear signal functions|Min/max, rectification and transfer curves map directly to GPU arithmetic.
Bridge|A|Scaling, mixing and fading|Simple operations; video ports and ordinary Rack CV still need explicit conversion.
Color Chords|A|Color mixing with layer priority|Layer opacity and priority are straightforward to implement.
Curtain|B|Edges, blur and enhancement|Model raster-time filter response, rectification and wet/dry mixing for its characteristic trails.
Cyclops|C|Laser display interface|An onscreen vector preview is easy; actual ILDA output needs a compatible DAC and separate integration.
Doorway|A|Soft keys and outlines|Gain, clipping, rectification and foreground/background blending are direct operations.
Liquid TV|A|Video monitoring|An embedded monitor is straightforward; physical display electronics add no necessary software behavior.
Mapper|A|Hue-based color conversion|Implement its color mapping and transfer curves; do not substitute a generic HSV picker without checking behavior.
Marble Index|B|Three-layer compositing|All operations are feasible; numerous routing, inversion and solarization combinations require careful reconstruction.
Navigator|A|Coordinate rotation and position|Arithmetic on two signal fields; it does not require a general 3D renderer.
Passage|A|Triple signal processing|Basic gain, inversion, offset and mixing; preserve the documented operation order.
Pendulum|A|Animation and modulation|LFOs, crossfading and routing fit Rack naturally; response curves need calibration.
Polar Fringe|B|Chroma key generation|Feasible color-distance/transfer model; matching the hardware's soft boundary needs references.
Prismatic Ray|B|Video oscillator|Same timing challenges as DWO3: free phase, modulation, reset and alias control.
Sensory Translator|A|Audio-band envelope extraction|Audio filters and envelope followers are native DSP tasks; route Mac microphone through an audio input.
Shapechanger|B|Coordinate waveshaping|Feasible nonlinear math, with work needed to recover exact curves and control interactions.
Staircase|A|Continuous solarization|Piecewise waveshaping is inexpensive; preserve continuous folds rather than imposing stepped posterization.
Topogram|A|Band keys and colorization masks|Threshold bands, edge softness and grouped outputs are direct per-pixel operations.
Visual Cortex|B|Core video synthesis and I/O|Its generator, colorizer and compositor translate well; encoder/decoder and timing add integration work.
War Of The Ants|B|Controlled noise textures|Model correlated noise across raster time and motion, not independent random pixels alone.
Cadet I Sync Generator|B|Raster timing and reference|Virtual timing is straightforward; reproducing lock behavior and actual electrical sync is harder.
Cadet II RGB Encoder|B|Composite encoding|Shares the NTSC encoder engine; real composite output needs hardware.
Cadet III Video Input|B|External video conditioning|Virtual clamping/conditioning is feasible; physical signal recovery depends on the input interface.
Cadet IV Dual Ramp Generator|A|Horizontal and vertical ramps|Deterministic coordinate fields with defined reset and blanking.
Cadet IX VCO|B|Wideband triangle oscillator|Requires video-time phase integration and sync modeling.
Cadet V Scaler|A|Voltage scaling|Small fixed gain/offset operators; build variants can be modes.
Cadet VI Fader|A|Crossfading|Direct interpolation with the appropriate control range.
Cadet VII Processor|A|Signal processing|Simple gain/offset/summing primitives; verify exact circuit routing before implementation.
Cadet VIII Hard Key Generator|A|Comparators|Direct thresholds, with edge modeling available later.
Cadet X Multiplier|A|Two-/four-quadrant multiplication|Direct arithmetic with the appropriate sign and clipping conventions.
Colorspace Mapper|A|Color conversion|Recover documented color mapping rather than assuming standard HSV behavior.
Differentiator|B*|Legacy filter circuit|A raster-time filter is feasible; exact response needs a more complete circuit audit.
Function Generator|A*|Legacy nonlinear shaper|Piecewise transfer functions fit software well; confirm the production revision's response.
Triple Video Fader & Key Generator|A|Faders and keys|Comparisons and blends are easy; retain normalled connections and mode logic.
Triple Video Interface|B|Analog input conditioning|Virtual processing is feasible; external voltages and genlock require hardware.
Triple Video Processor|A|Gain and bias processing|Order of operations is significant and must match the original.
Video Blending Matrix|A|Signal mixing|Simple sums and absolute-value processing.
Video Flip Flops|B|Bit memory|Needs raster-clock state, not frame-only evaluation.
Video Logic|A|Boolean processing|Truth tables are easy; hardware edge artifacts need extra modeling.
Video Waveform Generator|B|Video oscillator|Stable sync and modulation require the shared time-domain oscillator engine.
Voltage Interface I|A|Voltage conditioning|Gain, offset and clipping are simple; physical voltage output remains external.'''
rows=[]
for i,line in enumerate(data.splitlines()):
 n,g,f,reason=line.split('|')
 source=inv[i]['url']
 if i in range(59,69):
  paths=json.loads(Path('research/repo-paths.json').read_text());needle={'Cadet IX VCO':'cadet-ix-voltage-controlled-oscillator'}.get(n,n.lower().replace(' ','-'))
  source='https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/'+needle
 if i in [69,73,74,76,79]:source='https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352'
 if i==70:source='https://www.analoguehaven.com/lzx-industries/differentiator/manual.pdf'
 if i==71:source='https://www.analoguehaven.com/lzx-industries/function-generator/manual.pdf'
 if i==38:source='https://community.lzxindustries.net/t/memory-palace-user-guide/884'
 if i==22:source='https://lzxindustries.net/modules/sumdist/manual'
 if i==18:source='https://lzxindustries.net/modules/scrolls/manual'
 series= 'P series' if i<6 else 'Gen3' if i<27 else 'Castle' if i<35 else 'Orion' if i<39 else 'Expedition' if i<59 else 'Cadet' if i<69 else 'Visionary'
 rows.append(dict(name=n,grade=g,function=f,reason=reason,source=source,series=series))
assert len(rows)==80
archive='https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352'
extra='''Audio Frequency Decoder|A|Audio envelopes|Ordinary audio DSP; filter constants need matching.
Color Time Base Corrector|B|External input synchronization|Virtual frame alignment is practical; physical decoding needs capture hardware.
Color Video Encoder|B|Composite output encoding|Shares the NTSC engine; electrical output is separate.
Octal Video Quantizer & Sequencer|B|Quantization and sequencing|The arithmetic is easy; clock ordering needs care.
Triple Video Multimode Filter|B|Filtering|Preserve raster-time state and response curves.
Video Divisions|B|Clock division|Needs the shared edge-timing engine.
Video Ramps|A|Ramp generation|Straightforward coordinate arithmetic.
Video Sync Generator|B|Sync generation|Virtual timing is feasible; physical locking is separate.
Voltage Bridge|A|Voltage scaling|Straightforward gain operators.
XY Display Driver|C*|Display interface|Virtual preview is feasible; hardware behavior is sparsely documented.'''
for line in extra.splitlines():
 n,g,f,reason=line.split('|');source='https://lzxindustries.net/modules/octal-video-quantizer-sequencer' if n.startswith('Octal') else archive
 rows.append(dict(name=n,grade=g,function=f,reason=reason,source=source,series='Additional historical modules'))
rows.append(dict(name='DC Distro 5A',grade='N',function='Power distribution',reason='Hardware power utility; no software processing role.',source='https://lzxindustries.net/modules/dc-distro-5a',series='Additional historical modules'))
extras=[
('Chromagnon','B*','Integrated video instrument','Shape/color processing is feasible; evolving revisions complicate exact matching. Physical ILDA and analog I/O need hardware.','https://lzxindustries.net/instruments/chromagnon'),
('Videomancer','B','Programmable video effects','Feasible program by program. Public VHDL examples help, but FPGA programs must be translated; they are not Rack binaries.','https://github.com/lzxindustries/videomancer-sdk'),
('Vidiot','B','Integrated analog video synth','Build from oscillator, keyer, color and input engines; interaction and feedback require tuning.','https://lzxindustries.net/instruments/vidiot'),
('BitVision','B','Low-resolution audiovisualizer','Pixel graphics and audio modulation are easy; analog color-phase behavior needs the composite model.','https://community.lzxindustries.net/t/all-about-bitvision-legacy/1353'),
('Andor 1','A*','Media playback','Ordinary media playback is feasible; exact format support and legacy quirks have not been audited.','https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/andor-1')]
for n,g,f,reason,source in extras:rows.append(dict(name=n,grade=g,function=f,reason=reason,source=source,series='Standalone instruments and media'))
Path('research/grades.json').write_text(json.dumps(rows,indent=2))
intro='''LZX → VCV Rack on Mac: module feasibility review

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

'''
parts=[intro]
for series in dict.fromkeys(x['series'] for x in rows):
 parts.append(f'The following entries cover **{series}**.\n\n| Module / source | Grade | Main function | Feasibility assessment |\n|---|---|---|---|\n')
 for r in rows:
  if r['series']==series:parts.append(f"| [{r['name']}]({r['source']}) | **{r['grade']}** | {r['function']} | {r['reason']} |\n")
 parts.append('\n')
parts.append('''The following **historical names need qualification**, so they are excluded from the 96 assessed products.

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
''')
Path('LZX-Mac-Feasibility.md').write_text(''.join(parts))
print('Assessed entries',len(rows))
from collections import Counter
print('Grades',dict(Counter(r['grade'].rstrip('*') for r in rows)))
print('Series',dict(Counter(r['series'] for r in rows)))
print('Report words',len(''.join(parts).split()))
