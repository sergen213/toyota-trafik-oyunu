#!/bin/sh
# Verifies both compilers accept the C++23 features this project relies on.
set -e
d=$(dirname "$0")
for c in g++ clang++; do
  "$c" -std=c++23 -Wall "$d/cxx23_probe.cpp" -o "/tmp/tto_probe_$c" && "/tmp/tto_probe_$c" >/dev/null && echo "$c: C++23 OK ($($c -dumpversion))"
done
