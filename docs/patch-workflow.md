# Patch workflow

**Status:** what we actually do. Extend it when practice demands it, not before.

Small, atomic, independently-buildable commits, developed on `dev` and
fast-forwarded into a published `main` that descends from `upstream/main`.

## Commit subjects

Prefix the subject with the area it touches — the directory for tooling, the
subsystem's own name for firmware source — so the log skims and related work
groups by eye.

## Two branches

- **`dev`** — where work happens. Rebased, squashed and reordered freely.
- **`main`** — published. Only ever fast-forwarded from `dev`, so it stays
  linear and nothing on it is ever rewritten.

Merge a series into `main` once it has stopped churning:

```
git checkout main && git merge --ff-only dev && git push
```

`--ff-only` is the point: it keeps `main` linear, and refuses if `dev` isn't a
descendant — which is the alarm you want if something was committed to `main`
by mistake.

When upstream ships a release, `main` takes it as a merge (one merge commit,
a few times a year) and `dev` rebases onto the result:

```
git checkout main && git merge upstream/main
git checkout dev && git rebase main
```

## The rules

1. **The commit is the unit of work.**
2. **Each commit builds on its own.** One behaviour per commit.
3. **Curate before sending anything out.** Exploratory commits are fine while
   working; `git rebase -i` to fold fixups, split and reorder into a clean,
   narrated series.
4. **Docs live in the patch.** The commit message carries the *why*; any
   user-facing doc is updated in the same commit, so the two never drift.

## Sending firmware changes upstream

Cherry-pick the relevant commits onto a branch off `upstream/main`, by hand.
An occasional side activity, not a gate: whether upstream takes a patch, takes
it slowly, or never answers has no bearing on this fork's history — the commits
stay on `main` either way. We haven't done it yet; write down what it actually
takes once we have.
