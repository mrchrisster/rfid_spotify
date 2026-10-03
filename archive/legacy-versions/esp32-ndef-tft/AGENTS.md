# AI session continuity

## Before working
- Read `.ai/ARCHITECTURE.md`, `.ai/SCRATCHPAD.md`, and recent `.ai/CHANGELOG_AI.md` entries.
- Inspect the relevant current code; documentation is context, not a substitute for verification.
- Preserve the user's accepted requirements and working hardware configuration.

## During work
- Document intent, constraints, rejected approaches and material trade-offs—not just syntax.
- Use dense Markdown, exact repository-relative file paths, and explicit validation status.
- Never put tokens, Wi-Fi passwords, client secrets, private keys or generated credential-bearing firmware into context files, logs or commits.
- Do not claim a hardware issue fixed solely because host tests or compilation pass.

## Completion checklist (every significant response/change)
1. Update `.ai/ARCHITECTURE.md` for stack, ownership, component or architectural changes; add/update an ADR when warranted.
2. Update `.ai/SCRATCHPAD.md` with current task state, next steps, known bugs, gotchas and validation gaps.
3. Append notable changes, new modules/refactors and verification to `.ai/CHANGELOG_AI.md`; preserve historical entries.
4. State tests/builds/device verification accurately. Documentation-only work does not require a firmware rebuild.

These rules persist for future sessions. Keep the files current as part of the task, not as an optional follow-up.
