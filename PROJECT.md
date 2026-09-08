**RVX — project context**

RVX is the project and plugin-suite name; `rvx-vcv-rack` is the repository name and directory. VCV Rack is the host. LZX names identify the reference hardware; individual RVX module names and artwork have not been selected.

The broader first release remains in research and specification. On September 7, 2026, the user explicitly approved the six-module architecture prototype tracked by issue #13. That approval authorizes implementation, dependency setup and validation within the recorded prototype scope; it does not establish broader release scope or verify unresolved fidelity and acceptance gates. Repository and planning-document maintenance are also authorized.

Confirmed requirements are Mac support; close reproduction of LZX controls, ports and analog signal behavior; Memory Palace processing; NTSC and dirty mixing; and bidirectional Syphon with selection modeled on Rack audio I/O.

Source code is a first-class deliverable. The [engineering review policy](ENGINEERING.md) requires maintainable, reusable implementations and an audit of every open issue before work, before PR readiness/merge, and after each merge. Assumptions, shared contracts, evidence and dependencies must stay consistent as the project evolves. GitHub issue/PR history holds each audit record; README remains user documentation. The repository remains private until the user requests otherwise.

Use separate issue worktrees and parallel workers for independent work. Orchestration uses Ultra; routine writing tasks may use cheaper models/lower effort. Every PR receives a newly spawned independent reviewer with no inherited author conversation before readiness. The engineering policy defines the context supplied to reviewers, escalation and integration rules.

The prototype development target is the observed Apple M4 with 16 GiB RAM, macOS 26.2 and Rack Pro 2.6.6 standalone arm64. The [prototype contract](docs/PROTOTYPE.md) defines this experiment; the [validation record](docs/VALIDATION.md) identifies measured evidence and remaining gates. Outstanding planning work includes confirmation of the full-release platform/DAW scope, first-release control/port sheets, reference revisions, fidelity criteria and user review of the completed release specification. Implementation outside an explicitly approved scope remains proposed. Work already covered by the recorded #13 approval can continue without requesting approval again.

- [RVX implementation plan](VCV-Video-System-Plan.md): architecture, existing work, signal/timing contracts, module roadmap, acceptance gates, effort estimates and outstanding decisions.
- [RVX LZX feasibility review](LZX-Mac-Feasibility.md): 96 assessed catalog entries, grades and source references.
- [Structured grades](research/grades.json): machine-readable assessment data.
- [Project working instructions](AGENTS.md): planning boundary and documentation practices.
- [Contribution workflow](CONTRIBUTING.md): one issue, branch and PR per deliverable; review, validation and merge handling.

The GitHub repository is [kevindjacobson/rvx-vcv-rack](https://github.com/kevindjacobson/rvx-vcv-rack). It was created with private visibility. Use the contribution workflow for each focused deliverable. The [planning milestone](https://github.com/kevindjacobson/rvx-vcv-rack/milestone/1) tracks the remaining pre-code specifications and their integration for user review.

README.md is user documentation: installation, quick start and a module table only. Keep project history, design decisions, research procedures and approval state in this document or the implementation plan. Until a runnable release exists, its installation and quick-start sections state availability honestly rather than describe unverified commands or controls.

The README module table includes the six implemented experimental utilities, other shared RVX modules named in the plan and all 96 entries in the reference inventory. Other RVX module proposals are unreleased. Inventory entries are reference designs, not release commitments; physical interfaces and hardware-only products are labeled separately. Feasibility grades belong in the research documents, not the README. Future releases must update the table from actual shipped module coverage.

The research Python files are documentation utilities, not plugin implementation. Downloaded source-text snapshots and the raw inventory cache stay local and are ignored by Git. The report generator requires the local `research/inventory.json` input; `research/inventory.py` can fetch it, but live source changes can affect the inventory and must be reviewed before regenerating the report. Run research utilities from the repository root.
