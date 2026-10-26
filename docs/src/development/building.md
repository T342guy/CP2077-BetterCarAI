# Building

The mod's scripts (`r6/`, redscript) and CET Lua (`bin/`) need no build: `./install.sh` copies them into the game. The native plugin (`red4ext/`) is C++ against the RED4ext SDK.

{{#include ../../../README.md:build}}

Checks before installing:
- **Lua:** `luajit -bl init.lua` (and the other modules) catches syntax errors; also list the globals each module reads (`luajit -bl init.lua | grep -o 'G[GS]ET.*"[A-Za-z_0-9]*"'`) and make sure each is a real CET or Lua global, with no global writes. A local called before it is defined compiles fine but fails every frame in game.
- **redscript:** compile the scripts with the game's own `engine/tools/scc.exe` (under Wine/Proton, with `TZ=UTC`: it can panic on a daylight-saving edge and write no log). Put `r6/scripts/BetterCarAI` and `red4ext/scripts/BetterCarAINative.reds` in a scratch `r6/scripts`, the game's `r6/cache/final.redscripts` in its `r6/cache` (also as `final.redscripts.bk`), run `scc.exe -compile <scripts> -customCacheDir <cache>` and read `r6/logs/redscript_rCURRENT.log` (check that it is new).
- **Driving changes:** judge them in the simulator over hundreds of drives ([The driving simulator](simulator.md)).
