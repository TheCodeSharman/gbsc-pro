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
   working; rebase to fold fixups, split and reorder into a clean, narrated
   series.
4. **Docs live in the patch.** The commit message carries the *why*; any
   user-facing doc is updated in the same commit, so the two never drift.

## When to squash, and when not to

`main` is fast-forward-only and is never rewritten, so the question a queued
commit has to answer is not "who wrote this file" but **what does `main` need to
see**. Three rules, in order:

1. **A file that is new since `main` collapses.** `main` only ever sees its
   final state, so every intermediate state of it is a temporary side quest
   with no reader. Fold the lot.
2. **A file already on `main` is a correction to published work.** Read it, and
   fold any commit that RETRACTS an earlier one, so the wrong claim is never
   published. An addition is fine and may stay separate.
3. **Unless the result is a massive commit** — then split it by intent or theme.
   Rule 3 is what stops rule 1 fusing unrelated work, and it fires often enough
   to need stating.

Evidence is not lost by squashing — it lives in the `docs/` page, not the commit
sequence. `docs/investigations/hscale-tearing-characterisation.md` still carries
every refuted model after its commits are folded. Move detail to the doc
*before* folding.

### Rule 1 applies to files that DIE, not to every new file

Linking every commit that shares a new file over-merges badly. Measured on
`main..dev` at 226 commits, it gave 18 components and the largest swallowed
**31** — three days of unrelated work chained together by long-lived classes:

    13 commits  src/tv5725/SourceMeasurement.h
    11 commits  src/tv5725/SourceMeasurement.cpp
    10 commits  test/BenchGeometry.h

A new class extended later by an unrelated feature is the class doing its job,
not churn. So the mandatory link is files on **neither `main` nor `dev`** — born
and died inside the range:

```sh
git ls-tree -r --name-only main > /tmp/mainfiles
git ls-tree -r --name-only dev  > /tmp/devfiles
git log --format=%h --reverse main..dev | while read -r h; do
  git diff-tree --no-commit-id --name-only -r "$h"
done | sort -u | grep -vxF -f /tmp/mainfiles | grep -vxF -f /tmp/devfiles
```

That found 27 files of 66, giving 8 mandatory groups over 25 commits, none
larger than five. The recurring shapes: a class added then renamed, a class
added then dropped, a test layout tried and replaced, a snapshot committed to
the wrong directory then moved, and **a handover page committed and taken back
out** — which CLAUDE.md forbids outright, so it must not appear even briefly.

**Where a side-quest file's creator and its deleter fall in different thematic
groups, drop the file from the CREATING commit.** Do not merge the two groups to
satisfy the link: that fuses unrelated themes to hide one file. Three of the
eight needed this — the group that created a class creates its successor
directly instead.

### Deciding which a commit is

**File provenance**, as the screen for rule 2:

```sh
git ls-tree -r --name-only main > /tmp/mainfiles
git diff-tree --no-commit-id --name-only -r <commit> | grep -qxF -f /tmp/mainfiles \
  && echo "changes something already published — read it" || echo "new since main — folds"
```

Two traps, both hit:

- **`git blame` at the parent does not work.** It returns `PURE-ADD` for most of
  a refactor's commits — this work appends rather than edits, so a commit adding
  a missing register write to a new class is invisible to it. Do not rebuild it.
- **`GBSC-Pro-Source code/` contains a space.** Unquoted `for f in $(git
  diff-tree …)` splits the path and reports that nothing touches an existing file.

**Do not screen against `upstream/main` for this.** It answers a different
question — whether a patch is worth offering upstream — and `main` has long
since diverged far enough that the two give unrelated answers. On the same 226
commits: 189 change a file already on `main`, while only 51 touch a file
upstream has, and fifty of those fifty-one are our own edits to
`gbs-control.ino`. Upstream provenance matters when sending patches out, not
when advancing `main`.

## Executing a bulk squash

**The proof is the TREE, not the tests.** A rewrite that loses a file still
builds, still passes every suite, and still produces a working binary — the
stale file simply sits there unreferenced. The only check that catches it:

```sh
git tag pre-squash <dev>
...rewrite onto a scratch branch...
git diff pre-squash squash        # must be EMPTY
```

An empty diff is the whole guarantee. Run it before moving `dev`, and treat a
green test run with a non-empty diff as a failed rewrite.

**The one-kind rule constrains the grouping, and it costs commits.** Folding a
subsystem change together with its test, its Python coverage and its `docs/`
page reads best and is exactly what `## The rules` forbids. Measured over one
227-commit pass:

    227  before
    113  one kind per commit, strictly
     78  firmware separated, tools+docs+project folded per theme
     50  thematic, kinds mixed

The middle row is the one that satisfies the rule: split each thematic group
into a firmware half (`GBSC-Pro-Source code/`, `test/`) and everything else, so
no firmware commit ever carries a Python test or a doc page and stays liftable
on its own. Splitting is per PATH, not per commit — one original commit
routinely changes a class and its doc together.

**Reordering is bounded by shared files.** A commit editing `Geometry.cpp`
cannot move past twenty others editing `Geometry.cpp`, so a group whose members
are separated by such a commit stays separated. Check before planning a move —
a fragment is free to merge only when every commit in the gap touches a disjoint
set of files. On the same pass, 20 of 32 fragment merges were free and 12 were
blocked; taking the 12 would have meant hand-resolving conflicts on the busiest
files in the tree, which is where a resolution silently pulls later work into an
earlier commit.

### Two traps that make a rewrite lose files silently

**`git diff --cached --name-only` collapses a rename to the new path alone.**
Staging per-path from that list drops the deletion side of every rename, so the
old file survives into the rewrite: seven files did on one pass, and the build,
the host suites and the binary size were all unchanged with them present. Pass
`--no-renames` wherever a path list drives staging.

**Generate the input list from a tag, never from `HEAD`.** Building it while the
scratch branch is checked out writes the partly-rewritten log into the list that
drives the rewrite. Anchor every range on the tag taken before starting.

### Afterwards

```sh
git diff pre-squash dev                  # empty, or stop
git push --force-with-lease origin dev
git tag -d pre-squash                    # or the graph shows two histories
git for-each-ref 'refs/original/**'      # must be empty
```

The old commits stay in the reflog for 90 days, so the tag is not the only way
back — and leaving it is worse than losing it, because a reviewer opening the
stale line reviews commits that no longer exist.

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
5. **Apply review changes** by rebasing onto `main` with the target commit
   stopped at — never `--fixup` at the tip, which folds a patch written against
   today's code hundreds of commits back. Mark every fix for the chunk in one
   rebase so the stack replays once.

   **`rebase -i` is not always available.** Where the interactive editor cannot
   be driven, a message-only change is `git filter-branch --msg-filter` over
   `<commit>~1..HEAD`, keyed on `$GIT_COMMIT`. It rewrites the SHA of every
   commit after the target, so re-quote any hash already given to the reviewer,
   and it leaves `refs/original/**` behind — see the cleanup below.
6. **`git merge --ff-only`** into `main` once the review passes.
7. Repeat.

**Any rewrite invalidates every hash already quoted.** Rewriting one commit
rewrites the SHA of everything after it, so a hash from an earlier message opens
nothing. Give the old to new mapping for anything already named.

### Finding what a later commit retracts

Listing the files a queue changes repeatedly tells you where to look:

```sh
git ls-tree -r --name-only main > /tmp/mainfiles
git diff --name-only main dev | while IFS= read -r f; do
  grep -qxF "$f" /tmp/mainfiles && echo "$(git log --oneline main..dev -- "$f" | wc -l) $f"
done | sort -rn
```

Code evolving across commits is ordinary. A **document** doing it is the signal.
But the list only says *where*, and reading every diff by hand is how two
retractions reached `main` before this check existed.

**What finds them is a line-level pass: text one queued commit adds and another
queued commit removes.** Nothing outside the queue is involved, so every hit is a
claim written and withdrawn before publication:

```sh
python3 - <<'EOF'
import subprocess, collections
sh=lambda *a: subprocess.run(a,capture_output=True,text=True).stdout
for doc in sh('git','diff','--name-only','main','dev').split('\n'):
    if not doc.endswith('.md'): continue
    addedby={}; churn=[]
    for h in sh('git','log','--format=%h','--reverse','main..dev').split():
        for line in sh('git','show','--format=','-U0',h,'--',doc).splitlines():
            body=line[1:].strip()
            if line[:3] in ('+++','---') or len(body)<40: continue
            if line[0]=='+': addedby.setdefault(body,h)
            elif line[0]=='-' and addedby.get(body,h)!=h: churn.append((addedby[body],h))
    for (a,b),n in collections.Counter(churn).most_common(5):
        print('%-40s %s -> %s  (%d lines)'%(doc,a,b,n))
EOF
```

On the 226-commit pass it found five, the largest **40 lines of
`CODING_STYLE.md`** — three separate attempts at the comment rule, all rewritten
by a fourth commit. A retraction folds into the commit that made the claim, so
the claim is never published.

**It sees only files that exist on both ends**, so prose in a file the queue also
deletes is invisible to it, and so is a claim in a *new* file's own comments and
docstrings — which is where a chunk's claims usually live. A register panel
docstring said "only `/uc?4` writes to flash"; two commits later came the finding
that `/uc?f`, `?g`, `?h`, `?p` and `?s` all end in `saveUserPrefs()`, which
corrected the README and not the panel, so the wrong claim survived to the tip.
Read the chunk's own comments against what the rest of the queue says.

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

A chunk goes in front of the reviewer as a commit, read in the editor's own git
view. Do not name an extension: which one provides the view changes. Nothing is
checked out, so the
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
