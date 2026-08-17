# tv5725-header — read Tv5725.h as data

The register header is ~2000 lines of

```c
typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;   // Test control test module select
```

and each of those lines carries three separable things:

| | |
|---|---|
| **registers** | `seg, reg, offset, width` — the bit-slice the firmware reads and writes |
| **wording** | what the comment says |
| **layout** | where the comment sits |

Only the first can change behaviour, and it changes it *silently* — a shifted
width or a dropped typedef compiles perfectly and then writes the wrong bits.
That asymmetry is the whole reason this exists: a pass that reflows 500 comments
must be provably incapable of moving a single field, and "I was careful" is not
proof.

```sh
cd tools/tv5725-header && python3 -m pytest -q
```

## Checking a change to the header

```python
import header
before, after = open("a.h").read(), open("b.h").read()

header.compare(before, after)    # [] means no register changed
header.wordings(before)          # name -> comment text, whitespace and position stripped
header.duplicates(after)         # names declared twice, which compare() would collapse
```

`compare` returning `[]` is what makes "this commit only reformats" a verified
claim. `wordings` is the same trick one level up: identical wordings across a
commit means the comments only *moved*, and different wordings means something
was actually said.

## Is the declaration order load-bearing? No.

Worth settling, because the answer decides whether reordering 793 declarations is
safe or catastrophic. It would be catastrophic if the header were a
memory-mapped struct, where position *is* the address:

```c
struct Regs { uint8_t status; uint8_t control; };   // reorder and every field repoints
```

It is not that. `TV5725` declares no data members — only typedefs and static
consts — and no instance is ever created. Each line is a type alias whose
address rides in its template arguments:

```c
    typedef UReg<0x00, 0x41, 0, 2> PLL_VS;
//               seg   byte  bit  width
```

`PLL_VS::read()` expands to `regRead<0, 2>(0x17, 0x41)`, and the destination is
an **I²C transaction** to device `0x17`, not a location in ESP8266 RAM. Every
entry carries its own address, so the file is a catalogue and the order of a
catalogue does not change what is in it.

Confirmed three ways, strongest last: no typedef names another (all 793 take
integer literals, so nothing must be declared before anything else); it
compiles; and the firmware binary built from the pre-regrouping header is
**byte-identical** to the one after — same MD5, not merely the same size.

The one genuine ordering constraint in the file is the OSD constants and
`osdIcon()`, since static member initialisers are order-dependent within a
class, so that block sits below the registers as a unit.

## How the header is grouped

The fields are in RD-5725-1.1's own order: thirteen chapters, then address, then
bit offset. The grouping is **functional, not by segment**, which is the reason
it is worth having — segment 0 carries STATUS, MISCELLANEOUS and OSD; segment 1
carries INPUT FORMATTER, HD_BYPS and MODE_DETECT; segment 5 carries ADC and
SYNC_PROC. Sorting by address would interleave them.

`regdef.txt` is `pdftotext -layout` output from
`docs/Tvia TrueView 5725 Registers Definition (RD-5725-1.1).pdf`. It is
committed because `pdftotext` is not in the dev shell, so this copy is the only
one, and every annotation in the header traces back to it.

## Where the wording measure is honest and where it is not

A comment-only line sitting between two typedefs is genuinely ambiguous: it
either wraps the declaration above it or introduces the one below. `wordings`
resolves it by asking whether the typedef above already started a comment on its
own line, which is right for this header but is a heuristic, not a fact about C.

So across a commit that converted the whole file from comments-above to
comments-trailing, expect `wordings` to report changes on a hundred or so fields
where nothing was rewritten — the text moved from one side of the boundary to
the other. The check that the conversion was faithful is the total: 58335
characters of comment before, 58339 after. Per-field it disagrees; in bulk it
does not.

Use `compare` as a gate. Use `wordings` as evidence, and read it.
