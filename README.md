**Raster Voltage**

Analog video synthesis for VCV Rack on Mac. Repository name: `raster-voltage`.

Planning a family of video synthesis modules with close fidelity to LZX controls and analog signal behavior, Memory Palace-style processing, simulated NTSC dirty mixing, and bidirectional Syphon I/O.

Status: research and specification. Implementation has not started. The user requires a complete plan before coding and has authorized creating this local repository.

- [System plan](VCV-Video-System-Plan.md): proposed architecture, existing work, signal/timing contracts, module roadmap, acceptance gates, estimates and outstanding decisions.
- [LZX feasibility inventory](LZX-Mac-Feasibility.md): 96 assessed catalog entries, grades and source references.
- [Structured grades](research/grades.json): machine-readable assessment data.
- [Project working instructions](AGENTS.md): planning boundary and documentation practices.

Confirmed preferences: Mac; close reproduction of LZX controls and ports; analog signal behavior where practical; Memory Palace; NTSC and dirty mixing; Syphon input/output with device-style selection like Rack audio I/O.

Outstanding planning work: target Mac/Rack details, first-release control/port sheets, reference revisions and fidelity criteria, and user review of the completed specification. Repository creation does not authorize implementation.

The research Python files are existing documentation utilities, not plugin implementation. Downloaded source-text snapshots and the raw inventory cache stay local and are ignored by Git. The report generator currently requires the local `research/inventory.json` input; it can be fetched with `research/inventory.py`, but live source changes can affect the inventory and must be reviewed before regenerating the report. Run research utilities from the repository root.

This is an independent planning project. LZX names identify reference products; individual module names and artwork have not been selected.
