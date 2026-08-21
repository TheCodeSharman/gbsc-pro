# gbsc-pro (fork)

A personal fork of [RetroScaler/gbsc-pro](https://github.com/RetroScaler/gbsc-pro).

Offered as-is — a personal working fork, not a supported product, and not
affiliated with RetroScaler.

## Upstream

- [RetroScaler/gbsc-pro](https://github.com/RetroScaler/gbsc-pro) — the GBSC Pro
  product, its firmware, hardware design files, manuals and support channels. Go
  here for anything about the device itself.
- [gbs-control](https://github.com/ramapcsx2/gbs-control) by ramapcsx2 — the
  original project both are built on.

## Licensing

Everything original to this fork — `tools/`, `docs/`, `test/`, `build/` and the
firmware changes — is GPL-3.0, matching the firmware it modifies. `LICENSE` at
the root is that text.

| Path | Licence |
|---|---|
| `GBSC-Pro-Source code/gbs-control/` | GPL-3.0, derived from gbs-control by ramapcsx2 |
| `GBSC-Pro-Source code/gbs-control/src/` | LGPL-2.1 |
| `GBSC-Pro-Source code/gbs-control/3rdparty/` | per library, see each `LICENSE` |
| `GBSC-Pro-Source code/usart_uart_dma …/` | BSD 3-Clause, Xiaohua Semiconductor; the AV module additions state none |
| `GBSC-AV-IR-*.pdf`, `Gerber/`, `BOM/`, `source/` | none stated, RetroScaler |

The hardware artefacts carry no licence grant. They are here because this is a
GitHub fork of the public repository that publishes them, which GitHub's terms
of service cover — a public repository grants other GitHub users the right to
view and fork it. No wider redistribution right is claimed. Go to upstream for
the design files themselves.
