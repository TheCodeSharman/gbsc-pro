# The panel cannot drive the tree that draws on it

Selecting an input from the unit's panel with nothing on that connector left the
panel showing `Input RGBs` and answering no further press -- not the knob, not
the push. The input was then unreachable from the only display a sourceless unit
has, and `/input` or the remote were the only ways out.

It is not a stall. Nothing is frozen, no handler is waiting, and the loop is
turning over throughout.

## What is actually on the panel

The **described** tree -- the one the remote drives on the television -- draws
on the panel too, through `describedMenu.alsoDrawOn(Osd::Panel::renderer())`.
`Osd::Panel::row()` draws only the selected row, the level on one line and the
label under it, the panel having one item in view. So the described tree's Input
page with RGBs under the cursor renders as:

```
Input
RGBs
```

which is **indistinguishable from the icon tree's confirmation page** for the
same selection. That is what makes the fault read as a frozen confirmation, and
it is why `OLEDMenuImplementation.cpp`'s freeze mechanism was suspected: its
handlers do `manager->freeze()` and unfreeze behind a deadline, and a handler
that froze without being re-entered would look exactly like this.

**The freeze mechanism is innocent and was never reached.** `OLEDMenuManager::tick()`
re-enters the frozen item every pass as `enterItem(peakItem(), nav, false)`, the
item having been pushed when the state was `IDLE`, so a frozen handler does
reach its own unfreeze. `InputSwHandler` sets both deadline globals before its
body and freezes after it.

## Why no press is answered

The panel's only control is a rotary encoder. Both ISRs write one variable:

| gesture | writes |
|---|---|
| turn | `oledNav` = UP / DOWN |
| push | `oledNav` = ENTER |

`oledNav` has exactly one consumer, `oledMenu.tick(oledNav)`, and it sits behind
`if (NEW_OLED_MENU == true)`. The gate is re-asserted every pass as
`!describedMenu.isOpen()`, so an open described menu disconnects the encoder
entirely -- the ISRs go on setting a variable nobody reads. `drawIfNeeded()` is
*not* behind the gate, so the tree keeps painting the panel while its only local
input reaches nothing.

The described tree answers `Osd::Menu::press()`, which only the IR handler and
`/menu?key=` call.

## Why nothing closed it

`OSD_CLOSE_TIME` is 16 s and the timer honouring it is gated on
`oled_menuItem != 0`. All three sites that open the described menu set
`oled_menuItem = 0` immediately before `describedMenu.open()`, so the one menu
with no other way out is the one the timer excludes. `OSD_TIMEOUT` was defined
twice, 6000 in `OLEDMenuConfig.h` and 8000 in `OLEDMenuImplementation.cpp`, and
referenced nowhere.

## The measurement

With the panel stuck, `/menu` read `open: true`, `panel: described`, `depth: 2`,
and the Input page with RGBs selected and available. Two `/menu?key=down`
presses moved the selection to VGA and **the panel followed**, which is what
says the page is live rather than frozen; the knob and push still did nothing.
`/menu?key=exit` closed it and `panel` returned to `icons`.

After `Osd::Menu` was given the timeout: opened over `/menu?key=menu`, closed
itself at 16 s with the gate back to `icons`; with a press at 10 s it closed at
26 s, so the run is counted from the last press rather than from the open.

## What is still open

The timeout stops the unit being stranded. It does not make the described tree
drivable from the panel, which would need the encoder routed to whichever tree
owns the gate -- and the encoder has three gestures where the tree needs Up,
Down, Ok **and** Exit, so it wants either a Back row on every page or a
long-press. That is a design decision rather than a defect.
