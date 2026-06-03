# AGENTS.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:

- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

**Scope — applies to code architecture, NOT rendering quality.** For visual/rendering work, default to the proper high-quality technique within the GTX 1660 Super budget (high-resolution / CSM shadows, mipmaps + anisotropic filtering, authored textures, soft shadows, material-lite). Do NOT cheapen visuals or remove/fake features to be "simple" or "low-spec" — the minimal-looking code (64² textures, flat shading, 2048 shadow map) is placeholder, and the target is high-quality stylized (README 목표 / DESIGN: 덕코프류 north star). If a cheaper visual path is genuinely better, say so explicitly and let the user decide — never pick it silently.

**Conventional over clever — and "simple" means standard, not hacky.** Use the mainstream, Vulkan-idiomatic, well-documented implementation that most engines / references use (SaschaWillems, Khronos Vulkan-Samples, LearnOpenGL — see VULKAN_REFERENCES.md), and that future features can mesh into cleanly. Prioritize stability + extensibility + compatibility. Do NOT skip, ignore, or cut off parts of a standard technique to make it "easier" if that yields a non-standard, fragile, or hard-to-extend result — implement it properly. "Simplest" = the standard well-trodden path, not a novel shortcut.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:

- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:

- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:

- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

## 5. Code Style

- All comments must be written in **English**.

## 5.1 Language Rules

- Git commit messages must be written in **Korean**.
- Markdown documentation files (e.g. `README`, `DEVLOG`, `DESIGN`, `ARCHITECTURE`, and other `.md` files) must be written in **Korean** unless explicitly requested otherwise.
- Code comments and documentation language are separate rules: comments remain **English**, documentation remains **Korean**.

## 6. Before You Start

**Summarize, confirm, then act.**

- Before starting any task, summarize the plan and **ask for explicit approval** before proceeding.
- If you haven't seen the relevant source files, **ask before implementing**. Don't guess at existing code structure.
- Before proposing structural changes, check `ARCHITECTURE.md`. Decisions already marked as resolved should not be revisited without discussion.
- Do not create new files or add dependencies without explicit approval.

## 7. Build & Workflow

**The user owns the build. You own the handoff.**

- **Never run the build yourself.** The user handles all builds.
- After implementation, list what the user should verify in the build result (behavior, visuals, errors, edge cases).
- After implementation, list what **existing functionality might be affected or broken**.
- After build verification is confirmed, update relevant docs as appropriate: `README`, `DEVLOG`, `DESIGN`, `ARCHITECTURE`.

## 8. Refactoring

- Do not mix refactoring with feature implementation.
- If refactoring is needed, **propose it as a separate task** after the current task is complete.

## 9. Multi-LLM Handoff

**LLM handoff is a continuity tool, not a trust chain.**

When multiple LLMs are used sequentially (e.g. Claude → Codex → Claude), treat previous LLM output as context, not authority.

### Verify Before Continuing

Before continuing work started by another LLM:

1. Review the handoff summary
2. Inspect the relevant source files directly
3. Confirm the implementation matches the user's request
4. Check for conflicts with `AGENTS.md`, `ARCHITECTURE.md`, `README.md` and `DESIGN.md`
5. Only then continue implementation

Do not continue blindly from previous conclusions or summaries.

### Previous LLM Output Is Not Source of Truth

A previous LLM may be:

- incomplete
- mistaken
- overengineered
- inconsistent with project architecture
- missing important assumptions or risks

Verify reasoning against:

- source code
- project documents
- current user request

The codebase and project documents are the source of truth.

### Surface Problems Before Proceeding

If previous work appears problematic:

- state the concern explicitly
- explain the tradeoff or risk
- ask for clarification when needed
- do not silently inherit flawed assumptions

Examples:

- unnecessary abstraction
- scope creep
- risky Vulkan lifetime or synchronization logic
- architecture mismatch
- excessive diff size

### Session Handoff Before Context Limit

When approaching context or token limits, prepare a concise handoff summary for the next LLM.

The handoff should include:

- original task
- completed work
- key implementation decisions
- assumptions and tradeoffs
- open issues or risks
- suggested next steps
- user verification items

A handoff summary supports continuity but does not replace direct code verification.

### Human Ownership

The user remains:

- build owner
- test owner
- final reviewer

LLMs assist with continuity and implementation, but do not delegate responsibility to previous LLM decisions.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.
