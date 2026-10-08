# Domain Docs

How the engineering skills should consume domain documentation when working in this fork.

This fork adds one screen (the KRX prediction screen) to upstream Fincept Terminal. The engine behind it lives in the private repo `codeyoma/krx-quant`. Expect two domains: Fincept's own architecture, and the krx-quant engine whose API the screen consumes.

## Before exploring, read these

- **`custom/README.md`**: fork rules: branch model, upstream merge procedure, the list of upstream files we are allowed to touch.
- **`../docs/design.md`**: the agreed system design. This fork is checked out as the `fincept/` submodule of the private repo `codeyoma/krx-quant`, so the parent directory is the krx-quant root. Section 10 covers the screen; the Engine API contract is in the spec issue `codeyoma/krx-quant#1`.
- **`../CONTEXT.md`** and **`../docs/adr/`** if they exist: the engine's glossary and decisions.
- **`docs/ARCHITECTURE.md`**: upstream Fincept architecture.
- **`docs/adr/`**: upstream Fincept ADRs. Read the ones that touch the area you work in. The index in `docs/adr/README.md` may list ADRs whose files are not in the tree; treat the index titles as the decisions (e.g. "Screens do not own caches") and do not recreate the files.

If any of these files don't exist, **proceed silently**. Don't flag their absence; don't suggest creating them upfront.

## Upstream docs known to be stale

`docs/CPP_CONTRIBUTOR_GUIDE.md` describes an older screen-registration flow. As of upstream v4.5.0 a new screen needs: sources added to the explicit lists in `fincept-qt/CMakeLists.txt` (no globbing), a factory registration in `src/app/WindowFrame_Setup.cpp`, a title entry in `src/app/DockScreenRouter.cpp`, a menu entry in `src/ui/navigation/ToolBar.cpp`, and a palette entry in `src/ui/navigation/CommandBar.cpp`. A good template is the `asia_markets` screen + service pair. Re-verify after each upstream merge.

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined in the krx-quant `CONTEXT.md` (or `design.md` until a glossary exists). Don't drift to synonyms.

## Flag ADR conflicts

If your output contradicts an existing ADR (upstream or krx-quant), surface it explicitly rather than silently overriding:

> _Contradicts ADR-0007 (screens do not own caches), but worth reopening because…_
