# Issue tracker: GitHub (cross-repo, `codeyoma/krx-quant`)

Issues, specs and tickets for work in this fork live in the **private** repo `codeyoma/krx-quant`, not in this fork. This fork is public; keeping tickets in the private repo keeps the engine and algorithm design private.

This fork's own GitHub Issues are reserved for automated notices from the `custom-upstream-watch` workflow (new upstream release, failed `main` sync). Do not file work items here.

Use the `gh` CLI and **always pass `--repo codeyoma/krx-quant`**; `gh` would otherwise infer this fork from `git remote -v`.

## Conventions

- **Create an issue**: `gh issue create --repo codeyoma/krx-quant --title "..." --body "..."`. Use a heredoc or `--body-file` for multi-line bodies.
- **Read an issue**: `gh issue view <number> --repo codeyoma/krx-quant --comments`, filtering comments by `jq` and also fetching labels.
- **List issues**: `gh issue list --repo codeyoma/krx-quant --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with appropriate `--label` and `--state` filters.
- **Comment on an issue**: `gh issue comment <number> --repo codeyoma/krx-quant --body "..."`
- **Apply / remove labels**: `gh issue edit <number> --repo codeyoma/krx-quant --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> --repo codeyoma/krx-quant --comment "..."`
- **Reference from a commit or PR in this fork**: use the full form `codeyoma/krx-quant#<n>`; a bare `#<n>` would point at this fork.

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set to `yes` if this repo treats external PRs as feature requests; `/triage` reads this flag.)_

## When a skill says "publish to the issue tracker"

Create a GitHub issue in `codeyoma/krx-quant`.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --repo codeyoma/krx-quant --comments`.

## Wayfinding operations

Used by `/wayfinder`. Same as the GitHub defaults, applied to `codeyoma/krx-quant`.

- **Map**: a single issue labelled `wayfinder:map`, holding the Notes / Decisions-so-far / Fog body. `gh issue create --repo codeyoma/krx-quant --label wayfinder:map`.
- **Child ticket**: an issue linked to the map as a GitHub sub-issue (`gh api` on the sub-issues endpoint of `repos/codeyoma/krx-quant`). Where sub-issues aren't enabled, add the child to a task list in the map body and put `Part of #<map>` at the top of the child body. Labels: `wayfinder:<type>` (`research`/`prototype`/`grilling`/`task`). Once claimed, the ticket is assigned to the driving dev.
- **Blocking**: GitHub's native issue dependencies. Add an edge with `gh api --method POST repos/codeyoma/krx-quant/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`, where `<blocker-db-id>` is the blocker's numeric database id (`gh api repos/codeyoma/krx-quant/issues/<n> --jq .id`). Where dependencies aren't available, fall back to a `Blocked by: #<n>, #<n>` line at the top of the child body. A ticket is unblocked when every blocker is closed.
- **Frontier query**: list the map's open children, drop any with an open blocker or an assignee; first in map order wins.
- **Claim**: `gh issue edit <n> --repo codeyoma/krx-quant --add-assignee @me`, the session's first write.
- **Resolve**: `gh issue comment <n> --repo codeyoma/krx-quant --body "<answer>"`, then `gh issue close <n> --repo codeyoma/krx-quant`, then append a context pointer (gist + link) to the map's Decisions-so-far.
