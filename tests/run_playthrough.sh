#!/bin/sh
# Integration test: drives the native game through a script against the user's game data.
# Skips (exit 77) without TTO_GAME_DIR or an X display. Usage: run_playthrough.sh <binary> <script> <outdir>
[ -n "$TTO_GAME_DIR" ] || { echo "TTO_GAME_DIR not set"; exit 77; }
[ -n "$DISPLAY" ] || { echo "no DISPLAY (use xvfb-run)"; exit 77; }
rm -rf "$3"; mkdir -p "$3"
SDL_AUDIO_DRIVER=${SDL_AUDIO_DRIVER:-dummy} SDL_VIDEO_DRIVER=${SDL_VIDEO_DRIVER:-x11} \
  "$1" --game "$TTO_GAME_DIR" --save "$3/save" --fixed-step --seed 3 --script "$2" --shots "$3" >"$3/log.txt" 2>&1
rc=$?; tail -5 "$3/log.txt"; exit $rc
