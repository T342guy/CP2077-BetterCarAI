#!/usr/bin/env bash
# Install / uninstall Better Car AI into a Cyberpunk 2077 game folder.
#   ./install.sh [GAME_DIR]
#   ./install.sh --uninstall [GAME_DIR]
set -euo pipefail

DEFAULT_GAME_DIR="/run/media/system/NVME256/SteamLibrary/steamapps/common/Cyberpunk 2077"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

uninstall=false
if [[ "${1:-}" == "--uninstall" ]]; then
  uninstall=true
  shift
fi
GAME_DIR="${1:-$DEFAULT_GAME_DIR}"

if [[ ! -f "$GAME_DIR/bin/x64/Cyberpunk2077.exe" ]]; then
  echo "Not a Cyberpunk 2077 folder: $GAME_DIR" >&2
  exit 1
fi

SCRIPTS_DST="$GAME_DIR/r6/scripts/BetterCarAI"
CET_DST="$GAME_DIR/bin/x64/plugins/cyber_engine_tweaks/mods/BetterCarAI"
PLUGIN_DST="$GAME_DIR/red4ext/plugins/BetterCarAI"

if $uninstall; then
  rm -rf "$SCRIPTS_DST" "$CET_DST" "$PLUGIN_DST"
  echo "Removed Better Car AI from $GAME_DIR"
  exit 0
fi

if [[ ! -d "$GAME_DIR/bin/x64/plugins/cyber_engine_tweaks" ]]; then
  echo "Cyber Engine Tweaks not found - install CET first." >&2
  exit 1
fi

mkdir -p "$SCRIPTS_DST" "$CET_DST"
cp "$REPO_DIR"/r6/scripts/BetterCarAI/*.reds "$SCRIPTS_DST/"
# Copy only the Lua sources so the user's settings.json survives reinstalls.
cp "$REPO_DIR"/bin/x64/plugins/cyber_engine_tweaks/mods/BetterCarAI/*.lua "$CET_DST/"
# The optional RED4ext plugin, if it has been built (red4ext/build.sh).
PLUGIN_DLL="$REPO_DIR/red4ext/build/BetterCarAI.dll"
if [[ -f "$PLUGIN_DLL" && -d "$GAME_DIR/red4ext/plugins" ]]; then
  mkdir -p "$PLUGIN_DST"
  # Never overwrite the DLL in place: a running game (under Proton) reads it
  # from the file, so changing the file underneath it breaks the loaded
  # plugin (it crashed on quit). Write a new file and rename it over the old
  # one; the running game keeps the old file until it exits.
  cp "$PLUGIN_DLL" "$PLUGIN_DST/BetterCarAI.dll.new"
  mv -f "$PLUGIN_DST/BetterCarAI.dll.new" "$PLUGIN_DST/BetterCarAI.dll"
  cp "$REPO_DIR/red4ext/scripts/BetterCarAINative.reds" "$PLUGIN_DST/"
  echo "Installed the native plugin into $PLUGIN_DST"
  if pgrep -f Cyberpunk2077.exe >/dev/null 2>&1; then
    echo "The game is running: the new plugin loads the next time it starts."
  fi
fi
echo "Installed Better Car AI into $GAME_DIR"
