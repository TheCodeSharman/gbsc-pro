# build/ — local build infrastructure

Upstream (gbs-control / GBSC-Pro) ships **no** build script — it's meant to be
opened in the Arduino IDE. Everything here is **local infrastructure** we
reverse-engineered from the prior Windows Arduino-IDE build (board = esp8266
`nodemcuv2`, core `esp8266@2.6.3`, 4M/2M flash, 80 MHz, the 5 external libs seen
in that build's ELF). None of it is intended for upstream.

## The build

**`Makefile` + `arduino-cli.yaml`** drive `arduino-cli` against an *isolated*
data dir (so it never touches your system Arduino install). This is what we
build and flash.

**Any `arduino-cli` and `make` will do.** The Makefile provisions nothing and
never shells out to a package manager, so the rules work on any host that has
them.

```
make -C build setup    # one-time: core + libs into data/, downloads/, user/
make -C build          # compile -> build/output/gbs-control.ino.bin
```

- `GBS_DEBUG` defaults to 1 (the lab's debug build); pass
  `make -C build GBS_DEBUG=0` for a quiet, release-shaped build.
- `make -C build clean` drops `output/` but keeps the core/lib caches.
- `make -C build help` lists targets.

PlatformIO does not work here: its `.ino` preprocessor does not auto-prototype
the sketch's `static inline writeOneByte()` before `clearFrame()` uses it, so
`pio run` fails with *"'writeOneByte' was not declared in this scope"*.
arduino-cli does the full Arduino auto-prototype pass.

## Gitignored (regenerable, large — see ../.gitignore)

`data/` (~310M esp8266 core + xtensa toolchain), `downloads/` (~91M package
archives), `user/` (~42M external libs), `output/` (~68M compiled artifacts).
Only `Makefile`, `arduino-cli.yaml`, and this README are tracked.
