# RVX operator-panel design

This sheet defines the shared visual language for the six implemented RVX modules. It is a visual specification; it does not change signal behavior, patch data, or LZX fidelity claims.

## Reference boundary

The source collection is the private `kevindjacobson/idea-box` repository at commit `664f1dd6ff587312b2e90ab427825cd9140094ae`. The reviewed notes were *Evangelion Interface Language*, *Evangelion UI Aesthetic*, and the shared *Evangelion UI Sound Design Experiments* capture. Four LFS frames were inspected and verified against the supplied manifest:

| Frame | SHA-256 | Observed feature used by RVX |
|---|---|---|
| `frame-0001.jpg` | `8828a042b30ea490a0d66e139c333dc6f3a5f8539211d95c0a26520849b4ea46` | Amber trace, numbered baseline scale, green vertical calibration rails, boxed condensed title |
| `frame-0003.jpg` | `0aaf63f4404dd0fe6bf8143bc182cd29698ab7d1baa8efe7215fb79334388c22` | Repeated green signal columns, amber vertical identifiers, one red shared axis |
| `frame-0005.jpg` | `06b98f381fbd7724f8c4d0a26d21e027a48b2343999f1e5287f0020d12f27ebc` | Repeated hexagonal cells and a restrained amber-to-red range |
| `frame-0008.jpg` | `1d50bead70d7f73c09adddbdd2bcef941511f3132eb1794b12d77b3ed10f924a` | Sparse crosshair matrix, fine green top/bottom rulers, amber coordinate labels |

These observations come from the references. RVX's section IDs, semantic color roles, knob scales, port frames, status vocabulary, and physical layouts are new design choices. The source images are neither copied nor packaged.

## Composition and tokens

The panel ground is near-black `#080908`, with inset cells at `#11130F` and structural strokes at `#34382F`. Each module has a rigid top identity band: a narrow amber registration rail, the module title in condensed uppercase, the RVX suite mark, and a two-digit section identifier. Thin rules, corner cuts, calibration ticks, and grouped cells create the identity at normal Rack scale. Decorative markings remain static and never suggest live measurements.

| Token | Value | Meaning |
|---|---|---|
| Paper | `#E8E2CF` | Primary text and neutral port/control labels |
| Amber | `#F29A38` | Module identity, parameter arcs, selected/readout values |
| Signal green | `#70D99B` | Image/field video ports and healthy/ready state |
| Alert red | `#E05244` | Destructive action and actual warning/error state |
| Utility gray | `#8C9184` | Ordinary Rack audio/CV/trigger ports and secondary text |
| Violet | `#A884C7` | Bypass/held state only when the current API exposes it |

The old `#191C26` navy ground with independent cyan and pink accents treated every panel as a neon card. The replacement changes typography, framing, grouping, port geometry, control scales, and readout hierarchy as well as color. Green now means video/healthy signal, amber means instrument identity/value, red means a real warning or destructive action, and neutral gray identifies ordinary Rack voltage ports. Image versus field remains explicit in text and type codes (`IMG`, `FLD`) rather than depending on hue.

## Typography and controls

Barlow Condensed is packaged in Regular and SemiBold weights under the SIL Open Font License 1.1. It supplies compact technical labels without copying source glyph artwork. The source revision and hashes are recorded in `licenses/BarlowCondensed-PROVENANCE.md`; Rack's UI font is the predictable runtime fallback if a packaged face cannot load.

Module titles use SemiBold condensed uppercase at 13 px. Section IDs and control labels use SemiBold at 7–9 px with explicit alignment. Status and editable text use Regular at 8–10 px. Text is clipped or ellipsized inside its owning cell; full values remain available through existing menus, editing, and tooltips.

Knobs retain Rack's proven interaction target and parameter bindings, with a reusable static calibration ring drawn behind them. Major marks show endpoints and center or nominal position; minor marks are visual graduations and do not claim a transfer function beyond the real parameter range. Video jacks use a green square/diamond frame plus `IMG` or `FLD`; ordinary Rack ports use a gray circular frame plus `CV`, `AUD`, or `TRG`. The Clear button has a red guard frame and remains the existing momentary control.

Status cells show only `NodeDisplay::status` and existing adapter errors. Green indicates a non-problem state, amber indicates waiting/startup, red indicates detected errors, overflow, invalid wiring, or lateness. Decorative trace motifs never animate. The monitor preview draws the source frame without a tint, overlay, scanline, or color transform.

## Physical layout

All module widths and control/port order stay unchanged. Test Image, Signal Processor, CV Bridge, Video Monitor, and Video I/O retain every current control center. Frame Delay moves only its status cell slightly upward and gives the Frames value a larger dedicated readout while preserving the Frames knob, Clear button, image/clear inputs, and delayed output centers. The value remains snapped to the implemented integer range 1–60.

The two reference mockups resolve the system before source rollout:

- [`frame-delay.svg`](mockups/frame-delay.svg) shows the narrow panel, calibration ring, guarded Clear action, mixed video/ordinary ports, and large integer readout.
- [`video-io.svg`](mockups/video-io.svg) shows the dense panel, safe text cells, paired toggles, status area, and directional Syphon port grouping.

Module-browser construction with a null module is required: widgets show default or waiting text without dereferencing engine state. Rack context menus and parameter tooltips remain native so they retain familiar interaction and accessibility behavior.

