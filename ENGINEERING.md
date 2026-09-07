**RVX — engineering and consistency review**

RVX's source code is a deliverable alongside the working product. Completion requires understandable, maintainable code with useful reuse boundaries, supported behavior and appropriate validation. A working visual result alone does not establish that a change is ready.

This policy applies to research, specifications and eventual implementation. Plugin coding still requires the user's explicit approval of the complete plan. Repository visibility remains private unless the user requests a change.

**Audit the open issues**

Audit every open issue before starting a work item, before its PR becomes ready, immediately before an authorized merge, and after every merge. Repeat the audit during work when evidence changes a shared assumption. If the user merges outside the current session, reconcile those merges before starting the next work item. This is a required step in the work process; no background monitor is implied.

1. Read the current default branch and list all open issues and open PRs, including every page. Record the base commit and the PR revision being assessed. Distinguish merged behavior from proposed changes in open PRs.
2. Review each issue's assumptions, scope, acceptance criteria, evidence and dependencies against current requirements and the actual code/specifications. Check all open issues; inspect affected contracts and dependent work in depth. A title-only scan is insufficient. An unresolved assumption remains unresolved until evidence settles it.
3. Identify contradictions, duplicated work, invalidated assumptions, newly satisfied prerequisites, obsolete tasks and opportunities to share existing code. Trace a changed signal, timing, memory or API contract through every affected module and issue.
4. Correct affected issue bodies, criteria and dependencies, and link the evidence or decision that changed them. Update the authoritative specification in the same PR, or create a focused follow-up issue when additional repository work is needed. Mark proposed updates as contingent on the PR until it merges. Do not silently lower acceptance criteria to match incomplete implementation.
5. Stop work that would treat an invalid or unverified prerequisite as established. State the missing evidence or decision in the issue. Authorized research or validation specifically intended to resolve that uncertainty can proceed; an empirical proof gate must not be mistaken for an already-satisfied prerequisite. Keep independent work moving. Split, supersede or close obsolete issues only when the evidence supports that action; preserve links to their replacements.
6. After merge, audit against the resulting default-branch commit, verify issue closure, reconcile affected open PRs, and update dependency/parent progress. Read the actual merged result, including any integration changes. Finish this reconciliation before starting the next item.

Maintain one concise audit record in the working issue or PR. Include date, base and change revisions, the issues reviewed, findings, issue/specification updates, unresolved questions and follow-ups. Record an explicit no-change result when appropriate. After merge, append the new baseline and results to the merged PR or its issue. Avoid repetitive comments on unaffected issues.

Issues should identify assumptions and their source/status. PRs should include an **Issue consistency audit** section with the audit record or a link to it. An audit is evidence from a specific revision, not a permanent guarantee. Recheck if the base or substantive proposal changes before merge.

**Worktrees, parallel work and orchestration**

Give every active issue its own Git branch and worktree. Keep the main checkout stable and use `issue-<number>-<slug>` for the issue branch. Place worktrees alongside the repository, for example under `../rvx-vcv-rack-worktrees/`. Record the issue, branch, worktree and owned files in the worker assignment. Never switch another worker's branch, share its mutable checkout, or overwrite its uncommitted changes. Remove a worktree only when its work is preserved and no agent is using it.

Run issues in parallel when their prerequisites and interfaces permit independent changes. The orchestrator checks dependencies and file/contract overlap first, assigns bounded work, and coordinates integration. Agree shared contracts before consumers rely on them; proposed interfaces stay provisional. If concurrent work changes a common contract, reconcile the affected issues and workers before continuing dependent work. Each issue retains its own reviewable PR. An explicitly dependent PR remains draft until its prerequisite lands and it is checked against the resulting base.

Use Ultra reasoning for orchestration: scope, dependencies, architecture, integration, consistency audits and final readiness decisions. Routine implementation workers may use a cheaper available model and lower reasoning effort for well-specified tasks. Increase capability/effort when ambiguity, numerical fidelity, concurrency or resource ownership requires it. Model cost never changes acceptance or review criteria. Record assignments in the issue/PR when useful; do not imply that model selection guarantees correctness.

**Fresh independent review**

Before a PR is ready, always spawn a new reviewer agent with no inherited conversation history (`fork_turns: "none"`). The writer cannot serve as that reviewer. Supply the issue and acceptance criteria, applicable project instructions, exact base/head revisions, changed files, relevant implementation/specifications, and commands/results or artifacts from validation. Give access to surrounding code and dependencies as needed. Exclude the author's chat history, self-assessment and persuasive rationale; the reviewer should establish findings from the requirements and repository evidence.

Ask the reviewer to look for correctness and regression risks, inconsistent assumptions, unnecessary duplication or coupling, unclear interfaces, lifecycle/resource problems, fidelity deviations and missing meaningful validation. Use an appropriately capable reviewer; any savings on writing workers do not reduce review depth. For documentation changes, review requirements, source support, consistency and links instead of claiming runtime validation.

Record the review agent, reviewed revisions, findings and their resolution in the PR, including an explicit result when no actionable findings remain. Fix findings in the issue worktree. Substantive fixes or integration changes require another newly spawned reviewer without inherited history. Supply current requirements and changed evidence to that fresh reviewer; verify prior findings separately so their resolution is not lost. Purely mechanical edits can use a targeted recheck, but any doubt about behavior, contracts or acceptance criteria requires fresh review. Re-run appropriate checks after fixes.

Independent review informs readiness; it does not authorize merging or plugin coding. The orchestrator reconciles the review with the current issue audit and the actual diff before presenting the PR. Re-audit after merge as described above.

**Review source code as a deliverable**

| Review area | Completion evidence |
|---|---|
| Clarity | Cohesive functions and modules, consistent naming/units, and comments explaining non-obvious behavior or numerical choices. The code can be understood without reconstructing the chat history. |
| Reuse | Identify existing operators and utilities before adding another implementation. Name actual consumers of shared code or the concrete extension requirement it serves. Explain a necessary duplicate or specialized path. |
| Boundaries | Keep signal algorithms, graph scheduling, rendering, I/O and Rack panel code separate at their documented interfaces. Core operations should be testable without constructing Rack UI or requiring a live Syphon source. |
| Contracts | Document signal ranges, precision, timebase, ownership/lifetime, threading, errors, state/reset and compatibility behavior where they form an interface. Update affected callers and specifications together. |
| Behavior and fidelity | Validate the intended signal behavior, significant hardware differences and known approximations. Shared implementation must preserve module-specific control laws, normalizations and timing. |
| Tests | Use focused behavioral tests and independent reference calculations where valuable. Tests should catch regressions across consumers; avoid tests that merely repeat the implementation. Documentation-only changes need relevant document checks. |
| Performance and resources | Account for audio callback constraints, GPU synchronization, memory limits and latency. Measure when the change affects these budgets and report the target/configuration and limits of the measurement. |
| Maintainability | Remove superseded paths when safe, keep dependencies explicit, document how to build/use/extend the affected component, and preserve patch/schema compatibility or provide a reviewed migration. |

Prefer small, established shared operators over separate copies in each panel. Extract an abstraction when its common behavior and consumers are clear. Keep differing state or transfer functions explicit; a general interface must not hide behavior differences. Avoid building a speculative framework merely to claim reuse. Broader cleanup gets its own issue when it is not necessary for the current deliverable.

Each issue should describe its reuse implications. Each implementation PR should include a short **Code quality and reuse** assessment naming what it reused or extracted, affected consumers, contract changes and validation. For a research/documentation PR, identify the shared contracts it specifies and state that no implementation has been validated.

**Ready to merge and complete**

A PR is ready when its issue criteria, behavior, relevant validation, fresh independent review, code-quality assessment and pre-merge issue audit are satisfied. Remaining defects or invalid assumptions must be fixed or explicitly reflected in scope and follow-up issues; recording a follow-up is not permission to omit required behavior. After an authorized merge, the task remains in reconciliation until the post-merge audit and affected issue updates are complete.

Keep user-facing installation, quick start and module availability in README.md. Store engineering decisions in specifications and project documents, and keep audit history with the corresponding GitHub issue/PR.
