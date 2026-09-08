**RVX**

**Installation**

RVX is an experimental Mac prototype for Apple Silicon and VCV Rack 2.6.6. Build requirements: Apple Command Line Tools, Python 3.9 or later, and an internet connection for the pinned Rack SDK and Syphon sources.

From this checkout:

```sh
make deps
make -j4
make test
make test-syphon
make dist
make install
```

Restart Rack after installation. The installer retains any previous RVX folder under the Rack user folder's `rvx-backups` directory. `make dist` also creates a zip containing the RVX folder; it is not a `.vcvplugin` archive.

To use an existing SDK, set `RACK_DIR` when invoking make, for example `make -j4 RACK_DIR=/path/to/Rack-SDK`. Build Syphon separately with `bash scripts/fetch-syphon.sh` if skipping `make deps`.

**Quick start**

1. Open an included patch from `examples/` in Rack, or add **RVX Test Image**, **Signal Processor**, and **Video Monitor** from the module browser.
2. Connect Test Image's image output to Signal Processor's image input, then its image output to Video Monitor. Change gain and offset to process the image.
3. Add **CV Bridge** to convert ordinary Rack CV/audio into a video field, then connect its field output to Signal Processor's field input.
4. Add **Frame Delay** inside a feedback connection to give the loop one video tick of delay.
5. Use **Video I/O** for Syphon. Right-click it to select an application/server, name the publisher, and enable output. Input and output can run together; audio uses a separate Rack Audio/Core Audio route.

Test Image's **Phase Speed** animates every pattern. Turn it clockwise or counterclockwise for opposite directions; set it to zero to stop. Changing pattern or speed preserves phase; reset the module to return to its initial phase.

`examples/RVX-Audio-to-Video.vcv` uses the VCV Fundamental VCO (tested with Fundamental 2.6.4) to demonstrate buffered audio-to-raster conversion. Change the VCO frequency to change the image. Other examples use RVX modules only.

Video ports connect RVX modules. Ordinary audio/CV ports accept standard Rack signals. The monitor clips its preview to the display range; processing retains signed and above-range values.

**Modules**

The six prototype modules are experimental utilities. Other rows list proposed modules and LZX reference designs; reference entries are not included emulations.

| Module / reference design | Function | Availability |
|---|---|---|
| Video Engine / Settings | Video format, timing and resource settings | Unreleased |
| Video Monitor | Image preview and signal monitoring | Prototype |
| Video I/O | Simultaneous Syphon input and output | Prototype |
| Test Image | Reference images and test patterns | Prototype |
| Signal Processor | Signed image mixing, gain/offset and field conversion | Prototype |
| Component Split / Combine | Image and individual signal-field conversion | Unreleased |
| CV Bridge | Rack audio/CV to video control conversion | Prototype |
| Frame Delay | Explicit frame delay and feedback storage | Prototype |
| Still Image Input | Still-image loading | Unreleased |
| NTSC Encoder | Image-to-composite signal encoding | Unreleased |
| Dirty Mixer | Composite signal mixing and distortion | Unreleased |
| NTSC Receiver | Composite decoding and sync/color lock | Unreleased |
| [LNK](https://lzxindustries.net/modules/lnk) | Signal distribution | Reference only |
| [MLT](https://lzxindustries.net/modules/mlt) | Cascaded multiples | Reference only |
| [P](https://lzxindustries.net/modules/p) | Attenuation and crossfade | Reference only |
| [PAB](https://lzxindustries.net/modules/pab) | Buffers and tiny delays | Reference only |
| [PGO](https://lzxindustries.net/modules/pgo) | Gain and offset | Reference only |
| [PRM](https://lzxindustries.net/modules/prm) | Rectification and multiplication | Reference only |
| [Angles](https://lzxindustries.net/modules/angles) | Rotated ramps | Reference only |
| [Contour](https://lzxindustries.net/modules/contour) | Video high-pass filters | Reference only |
| [DC Distro 3A](https://lzxindustries.net/modules/dc-distro-3a) | Power distribution | Hardware-only reference |
| [DSG3](https://lzxindustries.net/modules/dsg3) | Shapes and analog logic | Reference only |
| [DWO3](https://lzxindustries.net/modules/dwo3) | Wideband oscillators | Reference only |
| [ESG3](https://lzxindustries.net/modules/esg3) | Encoding and sync | Reference only |
| [Factors](https://lzxindustries.net/modules/factors) | Four-quadrant modulation | Reference only |
| [FKG3](https://lzxindustries.net/modules/fkg3) | Keying and RGB fades | Reference only |
| [Keychain](https://lzxindustries.net/modules/keychain) | Hard keys | Reference only |
| [Matte](https://lzxindustries.net/modules/matte) | Static color levels | Reference only |
| [Proc](https://lzxindustries.net/modules/proc) | Triple gain/offset mixer | Reference only |
| [Ribbons](https://lzxindustries.net/modules/ribbons) | Three-bit amplitude slicing | Reference only |
| [Scrolls](https://lzxindustries.net/modules/scrolls/manual) | Animated ramp generation | Reference only |
| [SMX3](https://lzxindustries.net/modules/smx3) | Matrix mixing | Reference only |
| [Stacker](https://lzxindustries.net/modules/stacker) | Quadrilateral keys and priority | Reference only |
| [Stairs](https://lzxindustries.net/modules/stairs) | Multistage wavefolding | Reference only |
| [Sum/Dist](https://lzxindustries.net/modules/sumdist/manual) | Summing and distribution | Reference only |
| [Swatch](https://lzxindustries.net/modules/swatch) | RGB/YIQ conversion | Reference only |
| [Switcher](https://lzxindustries.net/modules/switcher) | RGB source routing | Reference only |
| [TBC2](https://lzxindustries.net/modules/tbc2) | Dual video input and timing | Reference only |
| [TBC2 Expander](https://lzxindustries.net/modules/tbc2-expander) | VGA connector expansion | Hardware-only reference |
| [Castle 000 ADC](https://lzxindustries.net/modules/castle-000-adc) | Three-bit conversion | Reference only |
| [Castle 001 DAC](https://lzxindustries.net/modules/castle-001-dac) | Bit-to-level conversion | Reference only |
| [Castle 010 Clock VCO](https://lzxindustries.net/modules/castle-010-clock-vco) | Video clock oscillator | Reference only |
| [Castle 011 Shift Register](https://lzxindustries.net/modules/castle-011-shift-register) | Clocked bit memory | Reference only |
| [Castle 100 Multi Gate](https://lzxindustries.net/modules/castle-100-multi-gate) | Boolean logic | Reference only |
| [Castle 101 Quad Gate](https://lzxindustries.net/modules/castle-101-quad-gate) | Configurable logic combinations | Reference only |
| [Castle 110 Counter](https://lzxindustries.net/modules/castle-110-counter) | Clock counting and division | Reference only |
| [Castle 111 D Flip Flops](https://lzxindustries.net/modules/castle-111-d-flip-flops) | Clocked bit storage | Reference only |
| [Diver](https://community.lzxindustries.net/t/1455/) | Audio-to-video waveform sampling | Reference only |
| [Escher Sketch](https://community.lzxindustries.net/t/1345/) | XY/pressure gesture controller | Reference only |
| [Fortress](https://community.lzxindustries.net/t/1392/) | Clocked low-resolution graphics | Reference only |
| [Memory Palace](https://community.lzxindustries.net/t/memory-palace-user-guide/884) | Frame memory and feedback | Reference only |
| [Arch](https://community.lzxindustries.net/t/1305/) | Nonlinear signal functions | Reference only |
| [Bridge](https://community.lzxindustries.net/t/1295/) | Scaling, mixing and fading | Reference only |
| [Color Chords](https://community.lzxindustries.net/t/1297/) | Color mixing with layer priority | Reference only |
| [Curtain](https://community.lzxindustries.net/t/1306/) | Edges, blur and enhancement | Reference only |
| [Cyclops](https://lzxindustries.net/modules/cyclops) | Laser display interface | Hardware interface reference |
| [Doorway](https://community.lzxindustries.net/t/1307/) | Soft keys and outlines | Reference only |
| [Liquid TV](https://lzxindustries.net/modules/liquid-tv) | Video monitoring | Reference only |
| [Mapper](https://lzxindustries.net/modules/mapper) | Hue-based color conversion | Reference only |
| [Marble Index](https://lzxindustries.net/modules/marble-index) | Three-layer compositing | Reference only |
| [Navigator](https://lzxindustries.net/modules/navigator) | Coordinate rotation and position | Reference only |
| [Passage](https://lzxindustries.net/modules/passage) | Triple signal processing | Reference only |
| [Pendulum](https://lzxindustries.net/modules/pendulum) | Animation and modulation | Reference only |
| [Polar Fringe](https://lzxindustries.net/modules/polar-fringe) | Chroma key generation | Reference only |
| [Prismatic Ray](https://lzxindustries.net/modules/prismatic-ray) | Video oscillator | Reference only |
| [Sensory Translator](https://lzxindustries.net/modules/sensory-translator) | Audio-band envelope extraction | Reference only |
| [Shapechanger](https://lzxindustries.net/modules/shapechanger) | Coordinate waveshaping | Reference only |
| [Staircase](https://lzxindustries.net/modules/staircase) | Continuous solarization | Reference only |
| [Topogram](https://lzxindustries.net/modules/topogram) | Band keys and colorization masks | Reference only |
| [Visual Cortex](https://community.lzxindustries.net/t/1314/) | Core video synthesis and I/O | Reference only |
| [War Of The Ants](https://community.lzxindustries.net/t/1291/) | Controlled noise textures | Reference only |
| [Cadet I Sync Generator](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-i-sync-generator) | Raster timing and reference | Reference only |
| [Cadet II RGB Encoder](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-ii-rgb-encoder) | Composite encoding | Reference only |
| [Cadet III Video Input](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-iii-video-input) | External video conditioning | Reference only |
| [Cadet IV Dual Ramp Generator](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-iv-dual-ramp-generator) | Horizontal and vertical ramps | Reference only |
| [Cadet IX VCO](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-ix-voltage-controlled-oscillator) | Wideband triangle oscillator | Reference only |
| [Cadet V Scaler](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-v-scaler) | Voltage scaling | Reference only |
| [Cadet VI Fader](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-vi-fader) | Crossfading | Reference only |
| [Cadet VII Processor](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-vii-processor) | Signal processing | Reference only |
| [Cadet VIII Hard Key Generator](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-viii-hard-key-generator) | Comparators | Reference only |
| [Cadet X Multiplier](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/cadet-x-multiplier) | Two-/four-quadrant multiplication | Reference only |
| [Colorspace Mapper](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Color conversion | Reference only |
| [Differentiator](https://www.analoguehaven.com/lzx-industries/differentiator/manual.pdf) | Legacy filter circuit | Reference only |
| [Function Generator](https://www.analoguehaven.com/lzx-industries/function-generator/manual.pdf) | Legacy nonlinear shaper | Reference only |
| [Triple Video Fader & Key Generator](https://lzxindustries.net/modules/triple-video-fader-key-generator) | Faders and keys | Reference only |
| [Triple Video Interface](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Analog input conditioning | Reference only |
| [Triple Video Processor](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Gain and bias processing | Reference only |
| [Video Blending Matrix](https://lzxindustries.net/modules/video-blending-matrix) | Signal mixing | Reference only |
| [Video Flip Flops](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Bit memory | Reference only |
| [Video Logic](https://lzxindustries.net/modules/video-logic) | Boolean processing | Reference only |
| [Video Waveform Generator](https://lzxindustries.net/modules/video-waveform-generator) | Video oscillator | Reference only |
| [Voltage Interface I](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Voltage conditioning | Reference only |
| [Audio Frequency Decoder](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Audio envelopes | Reference only |
| [Color Time Base Corrector](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | External input synchronization | Reference only |
| [Color Video Encoder](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Composite output encoding | Reference only |
| [Octal Video Quantizer & Sequencer](https://lzxindustries.net/modules/octal-video-quantizer-sequencer) | Quantization and sequencing | Reference only |
| [Triple Video Multimode Filter](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Filtering | Reference only |
| [Video Divisions](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Clock division | Reference only |
| [Video Ramps](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Ramp generation | Reference only |
| [Video Sync Generator](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Sync generation | Reference only |
| [Voltage Bridge](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Voltage scaling | Reference only |
| [XY Display Driver](https://community.lzxindustries.net/t/all-about-visionary-series-legacy/1352) | Display interface | Hardware interface reference |
| [DC Distro 5A](https://lzxindustries.net/modules/dc-distro-5a) | Power distribution | Hardware-only reference |
| [Chromagnon](https://lzxindustries.net/instruments/chromagnon) | Integrated video instrument | Reference only |
| [Videomancer](https://github.com/lzxindustries/videomancer-sdk) | Programmable video effects | Reference only |
| [Vidiot](https://lzxindustries.net/instruments/vidiot) | Integrated analog video synth | Reference only |
| [BitVision](https://community.lzxindustries.net/t/all-about-bitvision-legacy/1353) | Low-resolution audiovisualizer | Reference only |
| [Andor 1](https://github.com/lzxindustries/lzxdocs/tree/master/static/pdf/andor-1) | Media playback | Reference only |
