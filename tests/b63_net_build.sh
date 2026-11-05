#!/bin/bash
# Legacy C-phase harness build entry. Kept for compatibility; it now defers the
# GameNodes TU list to tests/harness_config.sh (derived from build.ninja) and
# fails at build time if a critical TU is missing.
set -e

ROOT=/home/eridanusepsilon/Code/EBC/EBC5.3
source "$ROOT/tests/harness_config.sh"

if [ "$#" -lt 1 ]; then
  echo "usage: b63_net_build.sh <harness.cpp> [more sources...]" >&2
  exit 2
fi

SRC="$1"
shift
# Built binaries are timestamped and never overwrite an earlier run.
OUT="${OUT:-/tmp/b63_net_e2e_$(date +%Y%m%d-%H%M%S)}"
if [ -e "$OUT" ]; then
  echo "b63_net_build: REFUSE existing output: $OUT" >&2
  exit 1
fi

HARNESS_HARNESS_SRCS=("$SRC" "$@")
harness_compile_tus
harness_build "$SRC" "$OUT" "$@"
harness_check_required_symbols "$OUT"
echo "built $OUT ($(harness_resolve_tus "$SRC" "$@" | wc -l) TUs)"
