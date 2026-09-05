# Agent instructions

This repo is tool-agnostic. **Read and follow `docs/README.md`.** That file is the working agreement for every agent. Then `docs/ROADMAP.md`, `docs/STYLE.md`, and the RFC or architecture doc for the area you touch.

Do not put a second plan in this file.

## Tool-agnostic by construction

Rules live in `docs/`. Tool-specific files — this one, `CLAUDE.md`,
`.cursor/rules/`, and any future equivalent — are **loader stubs only**: they
point at `docs/README.md` and carry no rules of their own.

Supporting a new tool means adding a stub, never copying or moving rules into
it. If a stub and `docs/` disagree, `docs/` wins and the stub is the bug.

Do not adopt a vendor-specific format for anything that belongs in `docs/` —
workflow, style, roadmap, or RFCs.
