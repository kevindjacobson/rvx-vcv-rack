**Contributing to RVX**

Use one focused GitHub issue, one branch and one pull request for each deliverable in `rvx-vcv-rack`. This applies to research and documentation as well as future plugin work.

RVX planning and research remain active. Research, specifications and repository documentation are authorized. Plugin implementation, scaffolding, prototype builds and dependency installation require the user's explicit approval for the corresponding scope. Record that approval in the implementation issue; it persists for that scope and must not be requested again. Creating an implementation issue or merging a planning PR does not grant approval or expand an existing approval to broader release or fidelity work. Read [AGENTS.md](AGENTS.md), [PROJECT.md](PROJECT.md) and the [implementation plan](VCV-Video-System-Plan.md) before starting substantial work.

**Issue to pull request**

1. Create or select a GitHub issue before changing project files. Use the work-item template to define the outcome, scope, acceptance criteria, assumptions, reference evidence, reuse implications and dependencies. Audit every open issue against the current base, open PRs and requirements before starting; follow the [engineering review policy](ENGINEERING.md). Keep the scope small enough for one reviewable PR.
2. Start from the current `main` and create a branch named `issue-<number>-<slug>`, for example `issue-42-specify-syphon-reconnection`. If the issue explicitly depends on an unmerged PR, the dependent branch may instead start from that prerequisite's reviewed head so its own diff stays focused. Keep unrelated work on separate branches. Subsequent changes reach `main` through PRs; do not push work directly to `main`.
3. Complete the issue's deliverable on that branch. Record decisions and unresolved questions in the relevant specification, with the issue linking to those files. Keep README.md limited to installation, quick start and the module table.
4. Validate the actual change against the issue's acceptance criteria and review code clarity, contracts, reuse, tests and maintainability as deliverables alongside product behavior. For documentation, check consistency, source support and local links. For an approved implementation scope, use the applicable technical checks from the plan. Report what ran, the result and anything still unverified; a proposed test is not a passing test.
5. Open one PR for the issue, normally targeting `main`. A PR that depends on an unmerged prerequisite may target that prerequisite branch while it remains a draft, keeping the dependent diff focused. It cannot become ready or merge until the prerequisite has merged, the PR has been retargeted to the resulting actual `main`, and the issue audit and fresh independent review have been repeated against that actual base. Include `Closes #<number>` in the PR description, explain the resulting behavior or document change, and map the acceptance criteria to evidence. Include the issue consistency audit and code-quality/reuse assessment. Update this same PR during review.
6. Before readiness and again before an authorized merge, audit all open issues and reconcile affected assumptions, acceptance criteria, specifications and dependencies. Leave merging to the user unless they explicitly request it; do not enable auto-merge without a request. After merge, audit all remaining open issues against the actual resulting commit, verify issue closure and reconcile affected open PRs and parent progress before starting the next item. If the user merged elsewhere, perform that reconciliation at the next session before new work. Record the revision, issue set and results as defined in the engineering policy.

Use a separate Git worktree for every active issue. Independent issues may proceed in parallel after their dependencies and shared interfaces have been checked. Do not share mutable checkouts or overwrite another worker's changes. Keep each issue, branch and PR to one focused deliverable.

Before a PR is marked ready, run a fresh independent review with a separate reviewer agent created with `fork_turns none`; the reviewer must not inherit the author's chat. Give the reviewer the issue and acceptance criteria, base and head revisions, relevant files and test evidence rather than an author narrative. The author applies fixes. Any substantive fix receives another fresh independent review. Record the complete review findings and their resolution in the PR. Orchestration uses Ultra; routine, well-specified writing work may use a cheaper model or lower effort and should escalate when ambiguity or risk appears. The quality criteria remain the same.

`main` should be the repository's default branch. GitHub interprets closing keywords for PRs targeting the default branch and closes linked issues when those PRs merge. [GitHub: linking a pull request to an issue](https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/linking-a-pull-request-to-an-issue).

**Scope and dependencies**

A large feature can have a parent issue with child issues for independently reviewable deliverables. Each child gets its own branch and PR. A parent that only tracks children needs no separate PR; close it when its recorded outcomes are satisfied. Link dependencies by issue number, explain what they block and wait for required work before starting dependent changes.

Backlog issues remain issues until there is a real repository change to review. Do not create empty PRs to represent planned work. Close duplicate, cancelled or question-only issues with an explanation when appropriate. If work expands beyond one focused PR, split the remaining deliverables into child or follow-up issues before expanding the branch.

**Evidence and fidelity**

For LZX behavior, cite the module and documentation or firmware revision where available. Distinguish documented behavior, engineering proposals, measured results and known approximations. Preserve the requirements for close control/port behavior, analog signal handling, Memory Palace, dirty NTSC mixing and bidirectional Mac Syphon. An issue cannot silently narrow those requirements.

Keep persistent design details in project documents and progress/review discussion in the issue and PR. When changing the feasibility report, update both [research/build_report.py](research/build_report.py) and its generated report. Keep downloaded page caches out of Git.

**Templates**

The [work-item template](.github/ISSUE_TEMPLATE/work_item.md) and [PR template](.github/pull_request_template.md) are plain Markdown. They become available in GitHub's creation flow after reaching the default branch; these files document the workflow and do not themselves enforce branch protection. Detailed engineering policy is defined in [ENGINEERING.md](ENGINEERING.md). [GitHub: issue templates](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/configuring-issue-templates-for-your-repository), [GitHub: pull request templates](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/creating-a-pull-request-template-for-your-repository).
