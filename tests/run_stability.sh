#!/usr/bin/env bash
# D.1 stability entry point.
#
#   tests/run_stability.sh [N] [--with-ninja]
#
# N (default 5) is the number of rebuild-loop rounds; each round runs the
# P4.4 harness (100 graph rebuilds). After the normal build it also builds the
# same harness with AddressSanitizer in tests/build_asan (build_linux is never
# touched) and scans for UAF / leak reports.
#
# build_linux is not modified at all by default. --with-ninja additionally
# runs an incremental `ninja -C build_linux -j2` first; if another line holds
# the ninja lock the script waits, it never deletes anything.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tests/harness_config.sh"

N=""
WITH_NINJA=0
for arg in "$@"; do
  case "$arg" in
    --with-ninja) WITH_NINJA=1 ;;
    -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
    *) N="$arg" ;;
  esac
done
N="${N:-5}"
if ! printf '%s' "$N" | grep -Eq '^[0-9]+$'; then
  echo "usage: $0 [N] [--with-ninja]   (N = rebuild rounds, default 5)" >&2
  exit 2
fi

BINDIR="$ROOT/tests/.e2e_bin"
OUTDIR="${STABILITY_OUTDIR:-$ROOT/tests/.stability_out}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
ASAN_DIR="$ROOT/tests/build_asan"
SUBJECT="$ROOT/tests/p44_stability.cpp"
mkdir -p "$BINDIR" "$OUTDIR" "$ASAN_DIR"

echo "== stability: N=$N =="

# --- 1. optional incremental build of the real tree (lock aware) ----------
if [ "$WITH_NINJA" -eq 1 ]; then
  echo "-- waiting for ninja lock (does not delete build_linux)"
  harness_wait_ninja_lock 300 || echo "WARN: ninja lock still held, skipping tree build"
  echo "-- ninja -C build_linux -j2 (incremental)"
  if ! ninja -C "$ROOT/build_linux" -j2 >"$OUTDIR/ninja_$RUN_ID.log" 2>&1; then
    echo "WARN: incremental ninja failed (other line?), see $OUTDIR/ninja_$RUN_ID.log"
  fi
else
  echo "-- build_linux untouched (pass --with-ninja for an incremental tree build)"
fi

# --- 2. normal rebuild-loop binary ---------------------------------------
echo "-- building rebuild-loop subject"
HARNESS_HARNESS_SRCS=("$SUBJECT")
if ! harness_compile_tus || ! harness_build "$SUBJECT" "$BINDIR/stability" ||
   ! harness_check_required_symbols "$BINDIR/stability"; then
  echo "FAIL: could not build the stability subject" >&2
  exit 1
fi

loop_fail=0
for round in $(seq 1 "$N"); do
  log="$OUTDIR/rebuild_round_${RUN_ID}_$round.log"
  if ! "$BINDIR/stability" >"$log" 2>&1; then
    loop_fail=$((loop_fail + 1))
    echo "round $round: FAIL (see $log)"
  else
    echo "round $round: PASS"
  fi
done

# --- 3. ASan build in an isolated directory ------------------------------
echo "-- building ASan subject in tests/build_asan (build_linux untouched)"
OBJ_ASAN="$ASAN_DIR/obj"
mkdir -p "$OBJ_ASAN"
asan_build_fail=0
# Compile the same manifest with ASan flags.
for tu in $(harness_resolve_tus "$SUBJECT"); do
  obj="$OBJ_ASAN/${tu%.cpp}.o"
  if [ -f "$obj" ] && [ "$obj" -nt "$HARNESS_G/$tu" ]; then
    continue
  fi
  if ! "$HARNESS_CXX" "${HARNESS_ASAN_FLAGS[@]}" -c "$HARNESS_G/$tu" -o "$obj" \
        >"$ASAN_DIR/${tu%.cpp}_$RUN_ID.log" 2>&1; then
    echo "ASAN-COMPILE-FAIL: $tu"
    asan_build_fail=1
  fi
done

asan_bin="$ASAN_DIR/asan_stability"
if [ "$asan_build_fail" -eq 0 ]; then
  if ! "$HARNESS_CXX" "${HARNESS_CXXFLAGS[@]}" -fsanitize=address -g "$SUBJECT" \
        "$OBJ_ASAN"/*.o "${HARNESS_ASAN_LDFLAGS[@]}" -o "$asan_bin" \
        >"$ASAN_DIR/link_$RUN_ID.log" 2>&1; then
    echo "ASAN-LINK-FAIL (see $ASAN_DIR/link_$RUN_ID.log)"
    asan_build_fail=1
  fi
fi

asan_reports=0
if [ "$asan_build_fail" -eq 0 ]; then
  asan_log="$OUTDIR/asan_$RUN_ID.log"
  ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:print_summary=1" \
    "$asan_bin" >"$asan_log" 2>&1
  asan_reports="$(grep -cE 'ERROR: (Address|Leak)Sanitizer' "$asan_log" || true)"
  if [ "$asan_reports" -gt 0 ]; then
    echo "ASAN: $asan_reports report(s), see $asan_log"
    grep -E 'ERROR: (Address|Leak)Sanitizer' "$asan_log" | head -n 5
  else
    echo "ASAN: clean (no UAF/leak report)"
  fi
else
  echo "ASAN: build failed, not run"
fi

printf '\nSTABILITY rounds=%d round_failures=%d asan_build_fail=%d asan_reports=%d\n' \
  "$N" "$loop_fail" "$asan_build_fail" "$asan_reports"
echo "logs: $OUTDIR"
if [ "$loop_fail" -gt 0 ] || [ "$asan_build_fail" -gt 0 ] || [ "$asan_reports" -gt 0 ]; then
  exit 1
fi
exit 0
