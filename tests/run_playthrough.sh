#!/bin/sh
# Integration test: drives the native game through a script against the user's game data.
# Skips (exit 77) without TTO_GAME_DIR or an X display. Usage: run_playthrough.sh <binary> <script> <outdir> [game options...]
[ -n "$TTO_GAME_DIR" ] || { echo "TTO_GAME_DIR not set"; exit 77; }
[ -n "$DISPLAY" ] || { echo "no DISPLAY (use xvfb-run)"; exit 77; }
bin=$1; script=$2; out=$3; shift 3
rm -rf "$out"; mkdir -p "$out"
SDL_AUDIO_DRIVER=${SDL_AUDIO_DRIVER:-dummy} SDL_VIDEO_DRIVER=${SDL_VIDEO_DRIVER:-x11} \
  "$bin" --game "$TTO_GAME_DIR" --save "$out/save" --fixed-step --seed 3 --script "$script" --shots "$out" "$@" >"$out/log.txt" 2>&1
rc=$?; tail -5 "$out/log.txt"; exit $rc
