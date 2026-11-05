#!/usr/bin/env bash
# D.1 JIT equivalence entry point.
#
#   tests/run_jit_equivalence.sh [--graphs N] [--nodes N] [--seed N]
#
# The same random pure-value graph is built and ticked twice: once with
# BGE_GAMENODE_INLINE=0 (KX_InlinePass off) and once with =1 (on). The two
# serialised float bit patterns must be identical. The first line of each log
# (inline=<0|1>) is dropped before diffing because it is expected to differ.
#
# Defaults are the D.1 full run (20 graphs x 100 nodes). Use
# --graphs 3 while iterating.
#
# Exit code: 0 only when both runs succeed, the inline switch really toggled,
# every graph has the same bits and there are no load failures.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tests/harness_config.sh"

GRAPHS=20
NODES=100
SEED=12345
OUTDIR="${JIT_OUTDIR:-$ROOT/tests/.jit_out}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$OUTDIR/run_$RUN_ID"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --graphs) GRAPHS="$2"; shift 2 ;;
    --nodes) NODES="$2"; shift 2 ;;
    --seed) SEED="$2"; shift 2 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

for value in "$GRAPHS" "$NODES"; do
  if ! printf '%s' "$value" | grep -Eq '^[0-9]+$'; then
    echo "graphs/nodes must be non-negative integers" >&2
    exit 2
  fi
done

mkdir -p "$RUN_DIR" "$ROOT/tests/.e2e_bin"
BIN="$ROOT/tests/.e2e_bin/d1_jit_equivalence"
SRC="$ROOT/tests/d1_jit_equivalence.cpp"

echo "== jit equivalence: graphs=$GRAPHS nodes=$NODES seed=$SEED =="

HARNESS_HARNESS_SRCS=("$SRC")
if ! harness_compile_tus || ! harness_build "$SRC" "$BIN" ||
   ! harness_check_required_symbols "$BIN"; then
  echo "FAIL: could not build $SRC" >&2
  exit 1
fi

run_path()
{
  local inline="$1"
  local log="$2"
  BGE_GAMENODE_INLINE="$inline" timeout 600 "$BIN" \
    --graphs "$GRAPHS" --nodes "$NODES" --seed "$SEED" >"$log" 2>&1
}

run_path 0 "$RUN_DIR/offline.log"
off_rc=$?
run_path 1 "$RUN_DIR/online.log"
on_rc=$?

if [ "$off_rc" -ne 0 ] || [ "$on_rc" -ne 0 ]; then
  echo "FAIL: harness run failed (offline rc=$off_rc online rc=$on_rc)" >&2
  tail -n 5 "$RUN_DIR/offline.log" "$RUN_DIR/online.log" >&2 || true
  exit 1
fi

off_flag="$(grep -oE 'inline=[01]' "$RUN_DIR/offline.log" | head -n 1 | cut -d= -f2)"
on_flag="$(grep -oE 'inline=[01]' "$RUN_DIR/online.log" | head -n 1 | cut -d= -f2)"
if [ "$off_flag" != "0" ] || [ "$on_flag" != "1" ]; then
  echo "FAIL: inline switch did not toggle (offline=$off_flag online=$on_flag)" >&2
  exit 1
fi

# Drop the expected-different header line and diff the bit patterns.
grep -v '^inline=' "$RUN_DIR/offline.log" >"$RUN_DIR/offline.norm"
grep -v '^inline=' "$RUN_DIR/online.log" >"$RUN_DIR/online.norm"
diff -u "$RUN_DIR/offline.norm" "$RUN_DIR/online.norm" >"$RUN_DIR/equivalence.diff"
diff_rc=$?

graphs_seen="$(grep -c '^graph=' "$RUN_DIR/offline.norm" || true)"
failures="$(grep -oE 'failures=[0-9]+' "$RUN_DIR/online.norm" | tail -n 1 | cut -d= -f2)"

echo "inline: offline=0 online=1"
echo "graphs compared: $graphs_seen"
if [ "$diff_rc" -ne 0 ]; then
  echo "DIFF (first 20 lines of $RUN_DIR/equivalence.diff):"
  sed -n '1,20p' "$RUN_DIR/equivalence.diff"
fi

printf '\nJIT_EQUIV graphs=%d nodes=%d seed=%s identical=%s failures=%s\n' \
  "$graphs_seen" "$NODES" "$SEED" \
  "$([ "$diff_rc" -eq 0 ] && echo yes || echo no)" "${failures:-0}"
echo "logs: $OUTDIR"

if [ "$diff_rc" -ne 0 ] || [ "${failures:-0}" -ne 0 ] || [ "$graphs_seen" -ne "$GRAPHS" ]; then
  exit 1
fi
exit 0
