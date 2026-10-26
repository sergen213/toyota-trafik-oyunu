#!/usr/bin/env bash
# Reversible recovery of the ORIGINAL game under Wine (diagnostic / reference path).
# Never touches the source files: makes a working copy, transcodes only the copy's
# Indeo 4.1 intro (no IV41 decoder in Wine/modern Windows) to Cinepak, and runs the
# game with a Turkish code page inside a Wine virtual desktop (emulates the 800x600 switch).
#   wine_recover.sh setup <game-dir> <work-dir>   # game-dir = installed "Toyota Trafik Oyunu v2" folder
#   wine_recover.sh run   <work-dir> [log]
set -euo pipefail
cmd=$1
case $cmd in
setup)
  src=$2 dst=$3
  [[ -f "$src/TrafikOyunu.exe" && -f "$src/Video/cancan.avi" ]] || { echo "not a game folder: $src" >&2; exit 1; }
  mkdir -p "$dst"; cp -a "$src" "$dst/game"
  sha_before=$(sha256sum "$src/Video/cancan.avi" | cut -d' ' -f1)
  ffmpeg -loglevel error -y -i "$src/Video/cancan.avi" -c:v cinepak -c:a pcm_s16le "$dst/cancan_cvid.avi"
  mv "$dst/cancan_cvid.avi" "$dst/game/Video/cancan.avi"
  [[ $(sha256sum "$src/Video/cancan.avi" | cut -d' ' -f1) == "$sha_before" ]] || { echo "source changed!" >&2; exit 1; }
  WINEDLLOVERRIDES=winemenubuilder.exe=d WINEPREFIX="$dst/prefix" WINEDEBUG=-all wineboot -i >/dev/null 2>&1 || true
  echo "ready: $dst (original cancan.avi sha256 $sha_before unchanged)" ;;
run)
  dst=$(cd "$2" && pwd); log=$(realpath -m "${3:-$dst/wine.log}")
  cd "$dst/game"
  LC_ALL=tr_TR.UTF-8 WINEDLLOVERRIDES=winemenubuilder.exe=d WINEPREFIX="$dst/prefix" WINEDEBUG=${TTO_WINEDEBUG:-err+all,fixme-all} \
    wine explorer /desktop=TTO,1024x768 TrafikOyunu.exe >"$log" 2>&1 ;;
*) echo "usage: $0 setup <game-dir> <work-dir> | run <work-dir> [log]" >&2; exit 2 ;;
esac
