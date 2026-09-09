<!-- Replace the issue placeholder with the one deliverable this PR completes. Normally target main. A dependent draft may target its declared prerequisite branch to keep its diff focused; after that prerequisite merges, retarget the PR to the resulting actual main and repeat the issue audit and fresh actual-base review before readiness or merge. Use a draft while work or validation is incomplete. -->

Closes #<issue-number>

**Result**

<!-- Explain the problem and resulting behavior or document change. Keep this focused on the final change a reviewer needs to assess. -->

**Validation**

<!-- Map the issue's acceptance criteria to checks and results. Link source evidence or artifacts where useful. State checks not run and unresolved limitations explicitly. -->

**Screenshot evidence**

<!-- Every PR must embed evidence here; do not remove this section or mark it N/A. Use a durable GitHub attachment or a repository image embed pinned to the commit containing the image. Confirm that each embed renders in this PR, is readable, is scoped to RVX, and contains no credentials, personal information, or unrelated private content. Local paths and expiring artifact links are insufficient. -->

<!-- For UI-visible work, use representative native UI screenshots and include before/after states when that comparison is relevant. For non-UI work, capture an authentic relevant executed test, output, diagnostic, or rendered-document state. A screenshot supplements logs and review; it does not prove an acceptance gate that was not run. Refresh captures after relevant behavior changes. -->

<!-- Caption every image using this form:
Revision: `<exact captured commit SHA>`
Scenario: <what was exercised or rendered>
Evidence: <what the image shows>
Limits: <what this capture does not establish>
Source: <GitHub attachment or commit-pinned repository image>
If this PR is still a draft and capture is unavailable, write `Capture pending` with the intended scenario and reason. A PR cannot become ready or merge until embedded evidence replaces that status. -->

**Issue consistency audit**

<!-- Record date, reviewed base/change revisions, all open issues/PRs reviewed, changed assumptions, issue/specification updates and unresolved follow-ups, or link the audit record. Record a no-change result when appropriate. Recheck before merge; append the actual post-merge baseline and reconciliation results after merging. -->

**Code quality and reuse**

<!-- Name reused/extracted components and their consumers, necessary specializations, contract changes, and maintainability/test evidence. For documentation-only work, identify the contracts reviewed and state that implementation was not validated. -->

**Review notes**

<!-- Include relevant fidelity limits, dependencies or decisions needing review. Remove this section if none apply. -->

**Independent review**

<!-- Before readiness, provide the reviewer with the issue and acceptance criteria, base/head revisions, relevant files and test evidence. Use a separate reviewer agent with fork_turns none; do not provide the author's narrative as a substitute. State the reviewer identity/context, findings, resolutions and whether a second fresh review was required. -->

- [ ] This PR contains one issue's deliverable, targets the actual `main`, and has reconciled any merged prerequisite through a repeated issue audit and fresh actual-base review.
- [ ] The issue's acceptance criteria are satisfied and supported by the validation above.
- [ ] Screenshot evidence is embedded, readable and current for this revision, with a caption stating its revision, scenario, evidence and limits. A draft may record `Capture pending`, but it cannot become ready or merge until the evidence is present.
- [ ] The open-issue audit is current and affected assumptions, specifications and dependencies are reconciled.
- [ ] Code quality and reuse have been reviewed alongside the delivered behavior, as applicable to this change.
- [ ] Any plugin implementation stays within its recorded explicit user approval; unresolved broader release and fidelity work remains proposed.
- [ ] A fresh independent reviewer assessed the issue, acceptance criteria, base/head, relevant files and test evidence before readiness; substantive fixes received another fresh review.

<!-- Do not merge or enable auto-merge unless the user requests it. -->
