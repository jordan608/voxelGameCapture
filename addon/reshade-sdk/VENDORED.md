# ReShade add-on SDK (fetched, not in git)

Headers only — the public add-on API. No ReShade source, no binaries. The add-on builds against them, so after
cloning, fetch them into this folder (*Fetching*, below). Only this note is in git.

| | |
|---|---|
| Source | https://github.com/crosire/reshade |
| Tag | `v6.7.3` |
| Commit | `4a50d1eddace85734871d91792ff214f13f66c01` |
| Path taken | `include/` (8 headers, nothing else) |
| Licence | BSD-3-Clause OR MIT (see the header of each file) |
| Fetched | 2026-09-23 |

## Why they are not committed

Only our own work goes in git (2026-10-21; `../../docs/DEVELOPER_GUIDE.md`). The headers are
dual-licensed BSD-3-Clause/MIT so add-ons can be built against them, but they are ReShade's, not
ours: `.gitignore` keeps everything here but this note out.

## Version matters

ReShade's add-on API is versioned, and the DLL refuses to load an add-on built against a
version it does not recognise. This machine runs **ReShade 6.7.3**, which its own log
reports as **add-on API version 18**:

```
Initializing crosire's ReShade version '6.7.3.2148' (64-bit)
Registered add-on "Voxon Tiny Glade Tap" v0.0.0.0 using ReShade API version 18.
```

If ReShade is updated and the add-on stops loading, re-fetch these headers at the matching
tag rather than guessing — the failure is silent apart from a line in `ReShade.log`.

## Fetching

```powershell
git clone --depth 1 --branch v6.7.3 --filter=blob:none --sparse https://github.com/crosire/reshade.git reshade
cd reshade; git sparse-checkout set include
```

Copy `include/*.hpp` here. When moving to another ReShade version, update the tag and commit above.
