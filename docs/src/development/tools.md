# Tools

Scripts in `tools/` for what the mod writes in game (Python 3):

- `tools/drive_log.py BetterCarAI.log [--last N] [--events] [--traffic] [--routes]`: sums up each Autodrive+ drive from the CET log (style, route, how it ended, its decisions counted by reason); `--events` lists every decision, `--traffic` every NPC reaction, `--routes` every route planned. See [The log](../log.md).
- `tools/drive_test.py <mod folder> [--id ID]`: compares a driving test's drives (yours and Autodrive+'s): times and speeds, braking and cornering, slip, the sharpest bends one by one, and cornering against slip angle. See [Driving test](../driving-test.md).
- `tools/analyze_recording.py rec_....csv [--png out.png]`: explains a drive recording (the 60 s before a wipe-out): what happened, every steering snap, a second-by-second table, and with `--png` a map and graphs (needs Pillow). See [Overlay and drive recorder](../overlay.md).
