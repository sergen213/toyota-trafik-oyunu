#!/usr/bin/env bash
# Diagnostic only: run the ORIGINAL TrafikOyunu.exe from a private working copy
# under an isolated Wine prefix inside a virtual desktop, take screenshots, then kill it.
# Usage: scripts/wine_probe.sh <v1|v2> <seconds> <outdir> [extra WINEDEBUG] [-- keys...]
set -euo pipefail
ver=$1 secs=$2 out=$3 dbg=${4:-err+all,fixme-all}
root=$(cd "$(dirname "$0")/.." && pwd)
export WINEPREFIX="$root/work/wine/prefix"
game="$root/work/wine/game/$ver"
mkdir -p "$out"; out=$(cd "$out" && pwd)
cd "$game"
WINEDEBUG=$dbg wine explorer /desktop=TTO,1024x768 TrafikOyunu.exe >"$out/wine.log" 2>&1 &
pid=$!
for i in $(seq 1 "$secs"); do
  sleep 1
  if (( i % 5 == 0 )); then
    w=$(xdotool search --name '^TTO' 2>/dev/null | head -1 || true)
    if [[ -n "$w" ]]; then
      import -window "$w" "$out/shot_$i.png" 2>/dev/null || true
    fi
  fi
done
wineserver -k || true
wait "$pid" 2>/dev/null || true
ls "$out"
