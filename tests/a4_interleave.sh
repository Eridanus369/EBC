#!/bin/bash
# A.4 interleaved inline comparison: native vs BGE_GAMENODE_INLINE=off.
#
# PROBLEM: batch runs (a native batch, then an inline-off batch) cannot tell a
# real inline effect from a machine fast/slow segment drift, because both are
# about 4% (native 2618.3 vs off 2514.7, while the same binary drifts between
# roughly 2515 and 2620 ns/frame across segments).
#
# METHOD: in one session, alternate native -> inline_off -> native -> off ...
# for PAIRS pairs (default 6 = 12 processes). Each pair yields ratio
# off/native; segment drift is common-mode inside a pair, so it cancels in the
# ratio. No other task runs between the two processes of a pair.
#
# CRITERION (fixed BEFORE the run; do not tune after seeing the data):
#   ratios r_i = off_i / native_i over all pairs.
#   interval = [min(r_i), max(r_i)] (the non-parametric dispersion interval).
#   * 1 lies inside the interval        -> "cannot distinguish at this precision".
#   * 1 lies outside the interval (all
#     pairs on the same side of 1)      -> "inline has a directional effect".
#   Outliers are reported, never silently dropped. If any process is flagged
#   (>25% away from the median of its own path), a sensitivity run excludes the
#   affected pairs and reports the same criterion on that reduced set.
#
# LOG: /tmp/a4_interleave_<YYYYmmdd-HHMMSS>.log, created exclusively (an
# existing file is refused), then chmod 444. Existing evidence is never
# overwritten.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT=/home/eridanusepsilon/Code/EBC/EBC5.3
BDIR=$ROOT/build_clean_verify
G=$ROOT/source/gameengine/GameLogic/GameNodes
PAIRS="${PAIRS:-6}"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="${A4_INTERLEAVE_DIR:-/tmp/a4_interleave_$RUN_ID}"
LOG="${A4_INTERLEAVE_LOG:-/tmp/a4_interleave_$RUN_ID.log}"
if ! ( set -C; : > "$LOG" ) 2>/dev/null; then
  echo "a4_interleave: REFUSE existing log: $LOG" >&2
  exit 1
fi
if [ -e "$RUN_DIR" ]; then
  echo "a4_interleave: REFUSE existing run dir: $RUN_DIR" >&2
  exit 1
fi
mkdir -p "$RUN_DIR/har" "$RUN_DIR/clean"

FLAGS="-I$G -DFMT_SHARED -DHAVE_EXECINFO_H -DNDEBUG -DWITH_ASSERT_ABORT -DWITH_AUDASPACE -DWITH_PYTHON -DWITH_SDL -DWITH_TBB -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D__LITTLE_ENDIAN__ \
-I$ROOT/source/gameengine/GameLogic -I$ROOT/source/gameengine/Common -I$ROOT/source/gameengine/Converter -I$ROOT/source/gameengine/Device -I$ROOT/source/gameengine/Expressions \
-I$ROOT/source/gameengine/Ketsji -I$ROOT/source/gameengine/Ketsji/KXNetworkMessage -I$ROOT/source/gameengine/Physics/Common -I$ROOT/source/gameengine/Rasterizer -I$ROOT/source/gameengine/SceneGraph \
-I$ROOT/source/blender/blenkernel -I$ROOT/source/blender/gpu -I$ROOT/source/blender/blenfont -I$ROOT/source/blender/imbuf -I$ROOT/source/blender/blenloader -I$ROOT/source/blender/blenmath \
-I$ROOT/source/blender/python/generic -I$ROOT/intern/termcolor -I$ROOT/intern/ghost -I$ROOT/intern/profile -I$ROOT/intern/guardedalloc -I$ROOT/source/blender/makesrna -I$ROOT/source/blender/blenlib \
-I$ROOT/source/blender/makesdna -I$ROOT/source/blender/windowmanager -I$BDIR/source/blender/makesdna/intern \
-isystem /usr/local/include/python3.13 -isystem $ROOT/intern/moto/include -isystem $ROOT/extern/recastnavigation/Recast/Include -isystem $ROOT/extern/recastnavigation/Detour/Include \
-isystem $ROOT/extern/audaspace/include -isystem $BDIR/extern/audaspace -isystem $ROOT/extern/audaspace/bindings -isystem $ROOT/lib/tbb/include -isystem $ROOT/extern/json/include \
-funsigned-char -fno-strict-aliasing -ffp-contract=off -O1 -std=c++20 -march=x86-64-v2"

BUILD_ID="$(cd "$ROOT" && git rev-parse --short=12 HEAD)-clean_verify-$(sha256sum "$BDIR/lib/libge_logic_bricks.a" | cut -c1-12)"
BIN="$RUN_DIR/a4_bench"

{
  echo "# A.4 interleaved inline comparison"
  echo "# date: $(date -Is)"
  echo "# build_id: $BUILD_ID"
  echo "# method: $PAIRS pairs, native then inline_off inside each pair"
  echo "# criterion: 1 inside [min(off/native), max(off/native)] -> indistinguishable"
  echo "#           1 outside that interval -> directional inline effect"
  echo "# outlier rule: a process >25% away from its own path median is reported, never dropped"
  echo "# cpu: $(nproc)"
  echo "# loadavg at start: $(cat /proc/loadavg)"
} >> "$LOG"

echo "=== build ===" | tee -a "$LOG"
if ! /usr/bin/c++ $FLAGS -c "$HERE/a4_bench.cpp" -o "$RUN_DIR/har/a4_bench.o" >> "$LOG" 2>&1; then
  echo "A4I_COMPILE_FAIL" | tee -a "$LOG"; exit 1
fi
( cd "$RUN_DIR/clean" && ar x "$BDIR/lib/libge_logic_bricks.a" ) >> "$LOG" 2>&1
if ! /usr/bin/c++ -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition -lpthread -ldl -lm \
    -o "$BIN" "$RUN_DIR/clean"/*.o "$RUN_DIR/har/a4_bench.o" >> "$LOG" 2>&1; then
  echo "A4I_LINK_FAIL" | tee -a "$LOG"; exit 1
fi
echo "A4I_BUILD_OK $BIN" | tee -a "$LOG"

run_one()
{
  local mode=$1 pair=$2
  local out val
  if [ "$mode" = "native" ]; then
    out=$(env -u BGE_GAMENODE_INLINE A4_PATH_LABEL="native_p$pair" A4_BUILD_ID="$BUILD_ID" "$BIN" 2>&1)
  else
    out=$(BGE_GAMENODE_INLINE=off A4_PATH_LABEL="inline_off_p$pair" A4_BUILD_ID="$BUILD_ID" "$BIN" 2>&1)
  fi
  val=$(printf '%s\n' "$out" | grep -oE 'median_ns_per_frame=[0-9.]+' | tail -n 1 | cut -d= -f2)
  if [ -z "$val" ]; then
    echo "A4I_BAD_RUN mode=$mode pair=$pair" | tee -a "$LOG"
    exit 1
  fi
  printf '%s\n' "$out" >> "$LOG"
  printf '%s\n' "$val"
}

native_vals=()
off_vals=()
pair_native=()
pair_off=()

echo "=== interleaved pairs ===" | tee -a "$LOG"
for ((i = 1; i <= PAIRS; i++)); do
  load_before="$(cat /proc/loadavg)"
  n="$(run_one native "$i")"
  o="$(run_one inline_off "$i")"
  load_after="$(cat /proc/loadavg)"
  native_vals+=("$n")
  off_vals+=("$o")
  pair_native+=("$n")
  pair_off+=("$o")
  ratio=$(awk -v o="$o" -v n="$n" 'BEGIN { printf "%.4f", o / n }')
  printf 'PAIR %d native=%.1f off=%.1f ratio=%s load_before=[%s] load_after=[%s]\n' \
    "$i" "$n" "$o" "$ratio" "$load_before" "$load_after" | tee -a "$LOG"
done

median_of()
{
  printf '%s\n' "$@" | sort -n | awk '
    { a[NR] = $1 }
    END {
      if (NR == 0) { print "NA" }
      else if (NR % 2 == 1) { printf "%.4f\n", a[(NR + 1) / 2] }
      else { printf "%.4f\n", (a[NR / 2] + a[NR / 2 + 1]) / 2 }
    }'
}

min_of() { printf '%s\n' "$@" | sort -n | head -n 1; }
max_of() { printf '%s\n' "$@" | sort -n | tail -n 1; }

# --- per-pair ratios ---------------------------------------------------------
ratio_vals=()
for ((i = 0; i < PAIRS; i++)); do
  ratio_vals+=("$(awk -v o="${pair_off[$i]}" -v n="${pair_native[$i]}" 'BEGIN { printf "%.4f", o / n }')")
done
r_med="$(median_of "${ratio_vals[@]}")"
r_min="$(min_of "${ratio_vals[@]}")"
r_max="$(max_of "${ratio_vals[@]}")"
n_med="$(median_of "${native_vals[@]}")"
o_med="$(median_of "${off_vals[@]}")"
n_min="$(min_of "${native_vals[@]}")"; n_max="$(max_of "${native_vals[@]}")"
o_min="$(min_of "${off_vals[@]}")"; o_max="$(max_of "${off_vals[@]}")"

printf 'RATIOS min=%s median=%s max=%s\n' "$r_min" "$r_med" "$r_max" | tee -a "$LOG"
printf 'ABSOLUTE native median=%s range=[%s,%s] off median=%s range=[%s,%s]\n' \
  "$n_med" "$n_min" "$n_max" "$o_med" "$o_min" "$o_max" | tee -a "$LOG"

# --- outlier report (never dropped) -----------------------------------------
flagged=0
for ((i = 0; i < PAIRS; i++)); do
  for mode in native off; do
    if [ "$mode" = native ]; then v="${pair_native[$i]}"; m="$n_med"; else v="${pair_off[$i]}"; m="$o_med"; fi
    dev=$(awk -v v="$v" -v m="$m" 'BEGIN { d = (v - m) / m; if (d < 0) d = -d; printf "%.4f", d }')
    over=$(awk -v d="$dev" 'BEGIN { print (d > 0.25) ? 1 : 0 }')
    if [ "$over" -eq 1 ]; then
      printf 'OUTLIER pair=%d mode=%s value=%s path_median=%s deviation=%.2f%% (kept in main result)\n' \
        "$((i + 1))" "$mode" "$v" "$m" "$(awk -v d="$dev" 'BEGIN { printf "%.2f", d * 100 }')" | tee -a "$LOG"
      flagged=$((flagged + 1))
    fi
  done
done
[ "$flagged" -eq 0 ] && echo "OUTLIER none" | tee -a "$LOG"

# --- criterion ---------------------------------------------------------------
contains1=$(awk -v lo="$r_min" -v hi="$r_max" 'BEGIN { print (lo <= 1.0 && 1.0 <= hi) ? 1 : 0 }')
if [ "$contains1" -eq 1 ]; then
  verdict="cannot-distinguish"
else
  verdict="directional"
fi

# Sensitivity: drop pairs containing a flagged process, same criterion.
if [ "$flagged" -gt 0 ]; then
  keep_ratios=()
  for ((i = 0; i < PAIRS; i++)); do
    drop=0
    for mode in native off; do
      if [ "$mode" = native ]; then v="${pair_native[$i]}"; m="$n_med"; else v="${pair_off[$i]}"; m="$o_med"; fi
      dev=$(awk -v v="$v" -v m="$m" 'BEGIN { d = (v - m) / m; if (d < 0) d = -d; printf "%.4f", d }')
      over=$(awk -v d="$dev" 'BEGIN { print (d > 0.25) ? 1 : 0 }')
      [ "$over" -eq 1 ] && drop=1
    done
    [ "$drop" -eq 0 ] && keep_ratios+=("${ratio_vals[$i]}")
  done
  if [ "${#keep_ratios[@]}" -gt 0 ]; then
    k_min="$(min_of "${keep_ratios[@]}")"; k_max="$(max_of "${keep_ratios[@]}")"
    k_med="$(median_of "${keep_ratios[@]}")"
    k_contains=$(awk -v lo="$k_min" -v hi="$k_max" 'BEGIN { print (lo <= 1.0 && 1.0 <= hi) ? 1 : 0 }')
    [ "$k_contains" -eq 1 ] && k_verdict="cannot-distinguish" || k_verdict="directional"
    printf 'SENSITIVITY kept=%d median=%s range=[%s,%s] verdict=%s\n' \
      "${#keep_ratios[@]}" "$k_med" "$k_min" "$k_max" "$k_verdict" | tee -a "$LOG"
  fi
fi

printf 'RESULT pairs=%d ratio_median=%s ratio_min=%s ratio_max=%s contains1=%s verdict=%s\n' \
  "$PAIRS" "$r_med" "$r_min" "$r_max" "$contains1" "$verdict" | tee -a "$LOG"
echo "# loadavg at end: $(cat /proc/loadavg)" >> "$LOG"

chmod 444 "$LOG"
sha="$(sha256sum "$LOG" | cut -d' ' -f1)"
printf 'LOG %s bytes=%s sha256=%s\n' "$LOG" "$(stat -c %s "$LOG")" "$sha"

exit 0
