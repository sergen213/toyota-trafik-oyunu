#!/usr/bin/env bash
# Diagnostic helper for the ORIGINAL game running under Wine on a private Xvfb
# display (default :77, override with TTO_DISPLAY). Never touches the user desktop.
#   tto_ctl.sh start <v1|v2> <log>     launch TrafikOyunu.exe from the working copy
#   tto_ctl.sh snap <out.png>          capture the root window
#   tto_ctl.sh click <x> <y>           left click at screen coords
#   tto_ctl.sh key <keysym...>         tap keys
#   tto_ctl.sh hold <keysym> <secs>    hold a key
#   tto_ctl.sh type <text>
#   tto_ctl.sh stop
set -euo pipefail
export DISPLAY=${TTO_DISPLAY:-:77}
root=$(cd "$(dirname "$0")/.." && pwd)
export WINEPREFIX="$root/work/wine/prefix"
case $1 in
  start) cd "$root/work/wine/game/$2" && (LC_ALL=${TTO_LOCALE:-tr_TR.UTF-8} WINEDEBUG=${TTO_WINEDEBUG:-err+all,fixme-all} setsid wine explorer /desktop=TTO,1024x768 TrafikOyunu.exe >"$3" 2>&1 &) ;;
  snap) import -window root "$2" ;;
  click) xdotool mousemove "$2" "$3" sleep 0.2 mousedown 1 sleep 0.1 mouseup 1 ;;
  key) shift; xdotool key --delay 150 "$@" ;;
  hold) xdotool keydown "$2" sleep "$3" keyup "$2" ;;
  type) xdotool type --delay 80 "$2" ;;
  stop) wineserver -k || true ;;
esac
