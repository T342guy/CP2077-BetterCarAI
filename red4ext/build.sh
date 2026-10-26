#!/usr/bin/env bash
# Builds red4ext/build/BetterCarAI.dll.
# Needs clang-cl, lld-link, cmake and ninja, plus the MSVC CRT / Windows SDK
# from xwin (`xwin --accept-license splat --output ~/.local/share/xwin/sdk`).
# On an immutable distro, run it inside a toolbox that has those:
#   toolbox run -c bcai-build ./red4ext/build.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# A pip-installed cmake in ~/.local/bin can shadow the real one.
CMAKE=cmake
[[ -x /usr/bin/cmake ]] && CMAKE=/usr/bin/cmake

if [[ ! -f deps/RED4ext.SDK/CMakeLists.txt ]]; then
  git -C .. submodule update --init red4ext/deps/RED4ext.SDK
fi

# The SDK is written on Windows: a few includes differ in case from the files
# (RED4ext/rtti vs RED4ext/RTTI). Link the expected names in an overlay folder.
mkdir -p build/casefix
grep -rhoE '#include\s*[<"]RED4ext/[^>"]+[>"]' deps/RED4ext.SDK/include deps/RED4ext.SDK/src \
  | sed -E 's/#include\s*[<"]//; s/[>"]$//' | sort -u | while read -r inc; do
  [[ -e "deps/RED4ext.SDK/include/$inc" || -e "build/casefix/$inc" ]] && continue
  real=$(find deps/RED4ext.SDK/include -ipath "deps/RED4ext.SDK/include/$inc" | head -1)
  [[ -n "$real" ]] || { echo "missing include: $inc" >&2; continue; }
  mkdir -p "build/casefix/$(dirname "$inc")"
  ln -sf "$(realpath "$real")" "build/casefix/$inc"
done

"$CMAKE" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.cmake
"$CMAKE" --build build
echo "Built $(pwd)/build/BetterCarAI.dll"
