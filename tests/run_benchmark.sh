#!/usr/bin/env bash
# D.1 benchmark entry point.
#
#   tests/run_benchmark.sh [--nodes N] [--frames N] [--rounds N] [--seed N]
#                          [--baseline FILE] [--out FILE] [--max-drift PCT]
#
# Runs d1_benchmark once (the binary already takes the median of --rounds
# internal rounds) and prints ns/frame + ns/node for three paths:
#   direct-call  plain ApplyOp() chain in the harness TU: no graph, no plan,
#                no scheduler, no node objects. This is a CONTROL and must
#                not be compared with the graph paths.
#   inline-off   KX_GraphInstance::Tick -> ExecuteValueNodes -> node->Execute
#                (KX_InlinePass disabled)
#   inline-on    same plan, foldable run wrapped in KX_InlinedBlock; entry
#                point is block->Execute -> ExecuteValueOnly -> node->Execute
#
# The harness prints executed_nodes_per_frame for both graph paths; they must
# be equal, and the direct-call value is compared bit-for-bit with the graph
# Sink value. A mismatch makes the run fail.
#
# With --baseline FILE the result is compared with a previous run file (the
# same format this script writes, or any file containing
# "path=<name> ns_per_frame=<value>" lines). The deviation is printed as
#   delta = (current - baseline) / baseline * 100 %
# and the run fails when any path drifts by more than --max-drift percent
# (default 30).
#
# Results are always saved to --out FILE (default
# tests/.bench_out/bench_<seed>_<nodes>_<frames>.txt).
#
# Exit code: 0 only when the benchmark ran and, if requested, stayed inside
# the drift budget.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tests/harness_config.sh"

NODES=100
FRAMES=1000
ROUNDS=5
SEED=12345
BASELINE=""
OUTFILE=""
MAX_DRIFT=30
OUTDIR="${BENCH_OUTDIR:-$ROOT/tests/.bench_out}"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --nodes) NODES="$2"; shift 2 ;;
    --frames) FRAMES="$2"; shift 2 ;;
    --rounds) ROUNDS="$2"; shift 2 ;;
    --seed) SEED="$2"; shift 2 ;;
    --baseline) BASELINE="$2"; shift 2 ;;
    --out) OUTFILE="$2"; shift 2 ;;
    --max-drift) MAX_DRIFT="$2"; shift 2 ;;
    -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

for value in "$NODES" "$FRAMES" "$ROUNDS"; do
  if ! printf '%s' "$value" | grep -Eq '^[0-9]+$'; then
    echo "nodes/frames/rounds must be non-negative integers" >&2
    exit 2
  fi
done

mkdir -p "$OUTDIR" "$ROOT/tests/.e2e_bin"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
[ -n "$OUTFILE" ] || OUTFILE="$OUTDIR/bench_${SEED}_${NODES}_${FRAMES}_$RUN_ID.txt"
if [ -e "$OUTFILE" ]; then
  echo "run_benchmark: REFUSE existing output: $OUTFILE" >&2
  exit 1
fi
BIN="$ROOT/tests/.e2e_bin/d1_benchmark"
SRC="$ROOT/tests/d1_benchmark.cpp"

echo "== benchmark: nodes=$NODES frames=$FRAMES rounds=$ROUNDS seed=$SEED =="

HARNESS_HARNESS_SRCS=("$SRC")
if ! harness_compile_tus || ! harness_build "$SRC" "$BIN" ||
   ! harness_check_required_symbols "$BIN"; then
  echo "FAIL: could not build $SRC" >&2
  exit 1
fi

if ! timeout 600 "$BIN" --nodes "$NODES" --frames "$FRAMES" \
      --rounds "$ROUNDS" --seed "$SEED" >"$OUTFILE" 2>&1; then
  echo "FAIL: benchmark run failed, see $OUTFILE" >&2
  tail -n 5 "$OUTFILE" >&2 || true
  exit 1
fi

grep -E '^(#|bench|path=|control_|RESULT)' "$OUTFILE"

# --- baseline comparison -----------------------------------------------------
drift_fail=0
if [ -n "$BASELINE" ]; then
  if [ ! -f "$BASELINE" ]; then
    echo "FAIL: baseline file not found: $BASELINE" >&2
    exit 2
  fi
  echo ""
  printf '%-12s %-14s %-14s %s\n' "PATH" "BASELINE" "CURRENT" "DELTA"
  printf '%-12s %-14s %-14s %s\n' "----" "--------" "-------" "-----"
  for path in direct-call inline-off inline-on; do
    current="$(grep -oE "path=$path ns_per_frame=[0-9.]+" "$OUTFILE" | tail -n 1 | cut -d= -f3)"
    base="$(grep -oE "path=$path ns_per_frame=[0-9.]+" "$BASELINE" | tail -n 1 | cut -d= -f3)"
    if [ -z "$current" ] || [ -z "$base" ]; then
      echo "FAIL: could not read path=$path from current/baseline" >&2
      drift_fail=1
      continue
    fi
    delta="$(awk -v c="$current" -v b="$base" 'BEGIN { if (b == 0) { print "n/a"; exit } printf "%+.1f%%", (c - b) / b * 100 }')"
    printf '%-12s %-14s %-14s %s\n' "$path" "$base" "$current" "$delta"
    if [ "$delta" != "n/a" ]; then
      over="$(awk -v d="$delta" -v m="$MAX_DRIFT" \
        'BEGIN { d += 0; m += 0; print ((d < 0 ? -d : d) > m) ? 1 : 0 }')"
      if [ "$over" -eq 1 ]; then
        drift_fail=1
      fi
    fi
  done
  if [ "$drift_fail" -eq 1 ]; then
    echo "drift budget exceeded (max ${MAX_DRIFT}%)" >&2
  else
    echo "drift within budget (max ${MAX_DRIFT}%)"
  fi
fi

failures="$(grep -oE 'RESULT failures=[0-9]+' "$OUTFILE" | tail -n 1 | cut -d= -f2)"
printf '\nBENCH nodes=%d frames=%d rounds=%d seed=%s failures=%s baseline=%s drift=%s\n' \
  "$NODES" "$FRAMES" "$ROUNDS" "$SEED" "${failures:-0}" \
  "${BASELINE:-none}" "$([ "$drift_fail" -eq 0 ] && echo ok || echo fail)"
echo "result: $OUTFILE"

if [ "${failures:-1}" -ne 0 ] || [ "$drift_fail" -ne 0 ]; then
  exit 1
fi
exit 0
