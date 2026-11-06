# The OLED menu and the OSD are two different menus, told apart by their INPUT

**Guessing this from which screen a page appears on is wrong, and it has cost a
session.** There are TWO menu trees and THREE renderers, and the mapping is not
one to one -- one tree draws on both screens.

| | driven by | tree | drawn on |
|---|---|---|---|
| **the described menu** | the IR remote, and `/menu?key=` | `Osd::Menu`, `src/osd/`, rows in `MenuTree.cpp` | the television **AND** the OLED panel -- `describedMenu.alsoDrawOn(Osd::Panel::renderer())` |
| **the icon tree** | the knob on the device | `OLEDMenu*.cpp`, `InputSwHandler` and friends | the OLED panel only |

So **`/menu?key=` is the REMOTE's menu and appears on the television**, which is
what makes it testable from a session; it is not a way to drive the knob's tree,
and there is no HTTP route that is. **A page seen on the OLED panel says nothing
about which tree drew it** -- `NEW_OLED_MENU` and `oled_menuItem` arbitrate,
`whichever is open owns the panel`, and the icon tree's `tick()` would otherwise
paint over a described page.

Three things answer to the name OSD and only the first reaches the television:
`Osd::OSD` is the STV9426's three rows of 28 cells; `Osd::Panel` is the same
described menu rendered one item at a time on the 128x64 panel; and
`SFTRST_OSD_RSTZ` is the TV5725's own OSD generator, which nothing uses.

**All three input-selection paths already converge**, and that is worth knowing
before blaming a divergence: a described-menu row, a knob press through
`InputSwHandler`, and `/input?src=` all reach `pendingInputSelection` ->
`InputXXX()` -> `applyInputSelection()` -> `VideoSourceSelector`. What does NOT
converge is the rest of the surface -- `docs/ui-command-objects.md`.
