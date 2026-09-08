**RVX — project context**

RVX is the project and plugin-suite name; `rvx-vcv-rack` is the repository name and directory. VCV Rack is the host. LZX names identify the reference hardware; individual RVX module names and artwork have not been selected.

The project is in research and specification. Plugin implementation has not started. The user requires a complete plan before coding and has authorized maintaining this repository and its planning documents.

Confirmed requirements are Mac support; close reproduction of LZX controls, ports and analog signal behavior; Memory Palace processing; NTSC and dirty mixing; and bidirectional Syphon with selection modeled on Rack audio I/O.

The user also requests video → ordinary Rack audio waveform → video conversion with NTSC-like behavior. The [preliminary, deferred video/audio round-trip design notes](docs/VIDEO-AUDIO-ROUNDTRIP.md) separate proposed slowed composite and real-time coarse raster modes from the high-rate RVX composite path, with explicit bandwidth, timing and information-loss limits for issue #16; they do not authorize implementation. The sequence before coding is: finish prototype #13; settle the shared contracts in #3; establish the Syphon boundary in #5; complete #7's NTSC specification and place the waveform work in its implementation sequence; use #9 to confirm release placement and obtain coding approval; then implement #16. Memory Palace #6 can coordinate optional later feedback integration, but is not a prerequisite for the feedforward path.

Outstanding planning work includes target Mac/Rack details, first-release control/port sheets, reference revisions and fidelity criteria, and user review of the completed specification. Repository and documentation work does not authorize plugin implementation.

- [RVX implementation plan](VCV-Video-System-Plan.md): architecture, existing work, signal/timing contracts, module roadmap, acceptance gates, effort estimates and outstanding decisions.
- [RVX LZX feasibility review](LZX-Mac-Feasibility.md): 96 assessed catalog entries, grades and source references.
- [Structured grades](research/grades.json): machine-readable assessment data.
- [Project working instructions](AGENTS.md): planning boundary and documentation practices.

README.md is user documentation: installation, quick start and a module table only. Keep project history, design decisions, research procedures and approval state in this document or the implementation plan. Until a runnable release exists, its installation and quick-start sections state availability honestly rather than describe unverified commands or controls.

The README module table includes the shared RVX modules named in the plan and all 96 entries in the reference inventory. RVX module proposals are unreleased. Inventory entries are reference designs, not release commitments; physical interfaces and hardware-only products are labeled separately. Feasibility grades belong in the research documents, not the README. Future releases must update the table from actual shipped module coverage.

The research Python files are documentation utilities, not plugin implementation. Downloaded source-text snapshots and the raw inventory cache stay local and are ignored by Git. The report generator requires the local `research/inventory.json` input; `research/inventory.py` can fetch it, but live source changes can affect the inventory and must be reviewed before regenerating the report. Run research utilities from the repository root.
