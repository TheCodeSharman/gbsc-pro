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

## When to squash, and when not to

**Squash a commit into its predecessor when it fixes a bug this fork's own work
introduced. Keep it separate when it fixes a bug in the original firmware.**

A fix to code this fork introduced is churn: keep the final state, drop the
attempts. A fix to upstream code has independent value and must stay
cherry-pickable.

Evidence is not lost by squashing — it lives in the `docs/` page, not the commit
sequence. `docs/investigations/hscale-tearing-characterisation.md` still carries every refuted
model after its commits are folded. Move detail to the doc *before* folding.

### Deciding which a commit is

**File provenance.** A commit touching only files absent from `main` cannot be an
upstream patch:

```sh
git ls-tree -r --name-only main > /tmp/mainfiles
git diff-tree --no-commit-id --name-only -r <commit> | grep -qxF -f /tmp/mainfiles \
  && echo "touches upstream — read it" || echo "internal — squash freely"
```

Measured on `main..dev` at 402 commits: **292 touch only new files** (136
tooling-only, 77 docs-only), 110 touch an upstream file. Step 4 ran 17 → 4.

Two traps, both hit:

- **`git blame` at the parent does not work.** It returns `PURE-ADD` for 12 of
  step 4's 16 commits — this work appends rather than edits, so a commit adding a
  missing register write to a new class is invisible to it. Do not rebuild it.
- **`GBSC-Pro-Source code/` contains a space.** Unquoted `for f in $(git
  diff-tree …)` splits the path and reports that nothing touches upstream.

## The review round

`main` is advanced one reviewed chunk at a time. Per round:

1. **Pull the next chunk** to the front of `dev` if it is not already there.
2. **Check nothing later retracts it**, below. A commit whose claim a later
   commit withdraws puts an invalid finding on `main`, and fixing it there means
   rewriting published history.
3. **Comment sweep**, before the review — process narration out of the source
   files, a short *why* and a `docs/` pointer left behind. Doing this first means
   the review is of the code, not of the commentary.
4. **Review.**
5. **Apply review changes** with `git rebase -i main` and the target marked
   `edit` — never `--fixup` at the tip, which folds a patch written against
   today's code hundreds of commits back. Mark every fix for the chunk in one
   rebase so the stack replays once.
6. **`git merge --ff-only`** into `main` once he is happy.
7. Repeat.

### What is still corrected later

List the files already on `main` that queued commits change again:

```sh
git ls-tree -r --name-only main > /tmp/mainfiles
git diff --name-only main dev | while IFS= read -r f; do
  grep -qxF "$f" /tmp/mainfiles && echo "$(git log --oneline main..dev -- "$f" | wc -l) $f"
done | sort -rn
```

Code evolving across commits is ordinary. A **document** doing it is the signal:
read each change and ask whether it adds or retracts. An addition is fine; a
retraction folds back into the commit that made the claim, so the claim is never
published.

Two reached `main` before this check existed. `riscpc-no-sync.md` called `HTOTAL`
1856 a "~118 MHz pixel clock" — 1856 is the scaler's sample count, and the
source's line is 1728 pixels — with the correction still queued behind it. This
file published a `git merge --squash <chunk-tip>` recipe that the commit two
later withdrew. Reading the diffs found neither.

**That check sees only files already on `main`, so a new file's prose is invisible
to it** — and prose in a new file is where a chunk's claims usually live. The
register panel's docstring said "only `/uc?4` writes to flash"; two commits later
came the finding that `/uc?f`, `?g`, `?h`, `?p` and `?s` all end in
`saveUserPrefs()`, which corrected the README and not the panel, so the wrong
claim survived to the tip. Read the chunk's own comments and docstrings against
what the rest of the queue says, not just its diff.

### Forward references to docs that have not landed

A comment citing a `docs/` page reads correctly in the diff whether or not the
page exists at that commit, and consolidating an investigation into one commit
placed after everything it cites leaves every earlier commit citing *it* pointing
at nothing:

```sh
for h in $(git log --format=%h --reverse main..dev); do
  git show --format= -U0 "$h" | grep '^+' |
    grep -o 'docs/[A-Za-z0-9._/-]*\.md' | sort -u |
  while read -r ref; do
    git cat-file -e "${h}:${ref}" 2>/dev/null || echo "$h -> $ref"
  done
done
```

The register panel cited `docs/investigations/riscpc-game-modes.md` 117 commits
before it lands; `docs/tv5725-chip.md` is cited by the fourth queued commit and
lands 270 later. Either drop the pointer from the citing commit or move the doc
ahead of its first citation — but a doc moved ahead of what *it* cites dangles
the other way, so the two constraints are read together.

Rewriting is `git rebase`, which drops applied commits by **patch-id** — a content
edit during review changes the id, so a cherry-picked original will replay on top
of the edited copy. Advance `main` along `dev`; do not cherry-pick to a staging
branch.

**Do not check a whole file out of a tip-state stash at an intermediate `edit`
stop.** It pulls later commits' changes into the one being edited. Caught once by
diffing `--stat`; a targeted edit is the safe move.

A chunk goes in front of the reviewer as a commit, read in GitLens — the Commit
Graph, or Search & Compare over `main..dev`. Nothing is checked out, so the
working tree stays free for the next round's rebase. Name the hash and subject;
one commit at a time is the point, because a range shows the whole chunk at once
and hides exactly the split just made.

Checking `main` out at step 6 fills the source-control view with hundreds of
untracked files, because the `.gitignore` there predates the rules covering
`node_modules/`, `__pycache__`, the photographs and `test/output`. Do not fix it
by adding rules to a chunk — that drags four later commits' work forward. Dev's
ignores are mirrored into `.git/info/exclude`, which is per-clone, uncommitted,
and applies on every branch.

## Sending firmware changes upstream

Cherry-pick the relevant commits onto a branch off `upstream/main`, by hand.
An occasional side activity, not a gate: whether upstream takes a patch, takes
it slowly, or never answers has no bearing on this fork's history — the commits
stay on `main` either way. We haven't done it yet; write down what it actually
takes once we have.
