**RVX — project context**

RVX is the project and plugin-suite name; `rvx-vcv-rack` is the repository name and directory. VCV Rack is the host. LZX names identify the reference hardware; individual RVX module names and artwork have not been selected.

The project is in research and specification. Plugin implementation has not started. The user requires a complete plan before coding and has authorized maintaining this repository and its planning documents.

Confirmed requirements are Mac support; close reproduction of LZX controls, ports and analog signal behavior; Memory Palace processing; NTSC and dirty mixing; and bidirectional Syphon with selection modeled on Rack audio I/O.

Source code is a first-class deliverable. The [engineering review policy](ENGINEERING.md) requires maintainable, reusable implementations and an audit of every open issue before work, before PR readiness/merge, and after each merge. Assumptions, shared contracts, evidence and dependencies must stay consistent as the project evolves. GitHub issue/PR history holds each audit record; README remains user documentation. The repository remains private until the user requests otherwise.

Use separate issue worktrees and parallel workers for independent work. Orchestration uses Ultra; routine writing tasks may use cheaper models/lower effort. Every PR receives a newly spawned independent reviewer with no inherited author conversation before readiness. The engineering policy defines the context supplied to reviewers, escalation and integration rules.

Outstanding planning work includes target Mac/Rack details, first-release control/port sheets, reference revisions and fidelity criteria, and user review of the completed specification. Repository and documentation work does not authorize plugin implementation.

- [RVX implementation plan](VCV-Video-System-Plan.md): architecture, existing work, signal/timing contracts, module roadmap, acceptance gates, effort estimates and outstanding decisions.
- [RVX LZX feasibility review](LZX-Mac-Feasibility.md): 96 assessed catalog entries, grades and source references.
- [Structured grades](research/grades.json): machine-readable assessment data.
- [Project working instructions](AGENTS.md): planning boundary and documentation practices.

README.md is user documentation: installation, quick start and a module table only. Keep project history, design decisions, research procedures and approval state in this document or the implementation plan. Until a runnable release exists, its installation and quick-start sections state availability honestly rather than describe unverified commands or controls.

The README module table includes the shared RVX modules named in the plan and all 96 entries in the reference inventory. RVX module proposals are unreleased. Inventory entries are reference designs, not release commitments; physical interfaces and hardware-only products are labeled separately. Feasibility grades belong in the research documents, not the README. Future releases must update the table from actual shipped module coverage.

The research Python files are documentation utilities, not plugin implementation. Downloaded source-text snapshots and the raw inventory cache stay local and are ignored by Git. The report generator requires the local `research/inventory.json` input; `research/inventory.py` can fetch it, but live source changes can affect the inventory and must be reviewed before regenerating the report. Run research utilities from the repository root.
