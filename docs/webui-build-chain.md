# The web UI build chain

The web UI is compiled into the firmware, not served from the filesystem, and
reaches it through four files of which **three are checked-in build artefacts**.
Each one looks editable, and edits to the wrong one are discarded without a
word. Read this before changing anything under `public/`.

## The chain

```
public/src/index.ts        ← behaviour.  Edit this, never index.js.
public/src/index.html.tpl  ← markup.     Edit this, never webui.html.
public/src/style.css       ← styling.
   │  tsc --target ES6, then public/scripts/build.js
   │  (build.js inlines the template, the stylesheet, the manifest and the assets)
webui.html                 ← generated
   │  public/scripts/html2h.sh   (gzip -c9 | xxd -i, then two seds)
webui_html.h               ← generated, and this is what the firmware compiles
```

The `server.on("/")` handler sends `webui_html` as a gzipped PROGMEM array with
a `Content-Encoding: gzip` header. Nothing on the device reads `webui.html`.

## Building

```sh
cd "GBSC-Pro-Source code/gbs-control/public"
npm ci            # once — installs the pinned tsc, see below
npm run build     # the whole chain
```

`make -C build webui` regenerates only the last link, and `make -C build
webui-check` fails if `webui_html.h` is stale. The `build` target depends on
that check, so that one link cannot rot silently. **Nothing checks the earlier
links.**

## Check that the sources still generate what ships

Nothing enforces it, so do it by hand after any UI change:

```sh
cp "GBSC-Pro-Source code/gbs-control/webui.html" /tmp/ref.html
cd "GBSC-Pro-Source code/gbs-control/public" && npm run build
diff /tmp/ref.html ../webui.html      # empty means the sources still generate what ships
```

A non-empty diff means someone edited an artefact instead of a source, and the
diff is the list of work about to be lost. `npm run build` reports no error when
this happens — the only symptom is UI features disappearing.

This is also why the sources reproduce the artefact's whitespace oddities
exactly, indentation slips and a stray `<td>` included. Tidying them changes the
output and costs you the check.

## The pinned compiler

`package.json` wants typescript `^4.1.3`. nixpkgs ships 5.9.3, which rejects this
source outright — `index.ts:35`, TS2322, `Uint8Array` vs `ArrayBuffer`. So the
flake provides `nodejs` and `xxd` but deliberately **not** `pkgs.typescript`: a
missing `tsc` is a better failure than one that type-errors on code that
compiles. `npm ci` supplies the right one, from `package-lock.json`.
