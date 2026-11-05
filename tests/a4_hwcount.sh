#!/bin/bash
# A.4 hardware-counter probe and (only if permitted) interleaved counter run.
#
# WHY: ns/frame on this machine is polluted by the powersave frequency double
# peak (fast segment ~2515, slow segment ~2620 ns/frame). Cycles / instructions
# are frequency invariant, so they can settle the inline question without
# touching CPU affinity (binding a core was measured to be counterproductive
# here: taskset -c 2 took b661_perf from 313 us to 552 us).
#
# CRITERION (fixed here, BEFORE running; do not tune after seeing the data):
#   The decision variable is the ratio r_i = off_cycles_i / native_cycles_i.
#   * 1 inside [min(r_i), max(r_i)] over all pairs -> "cannot distinguish at
#     this precision".
#   * all pairs on the same side of 1            -> "inline has a direction".
#   ns-based numbers are NOT used for the verdict; they are reported only as
#   the known-polluted quantity.
#
# LEVELS:
#   1. perf with cycles/instructions available -> build the A.4 binary and run
#      PAIRS interleaved pairs (native -> off, same order as a4_interleave.sh),
#      reporting cycles ratio, instructions ratio, IPC and cycles/frame.
#   2. perf unavailable, valgrind available -> NOT run automatically: the A.4
#      harness is a byte-identical evidence asset with 21x1000 frames baked in,
#      so an Ir run would need a reduced-frame variant whose protocol is not
#      comparable with the a4 baseline. The probe result is logged instead.
#   3. neither -> verdict "no-frequency-invariant-counters".
#
# LOG: /tmp/a4_hwcount_<YYYYmmdd-HHMMSS>.log plus /tmp/a4_hwcount_<...>/,
# created exclusively; an existing file is refused. Final log is chmod 444 and
# its sha256 is printed. Existing evidence is never overwritten.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT=/home/eridanusepsilon/Code/EBC/EBC5.3
BDIR=$ROOT/build_clean_verify
G=$ROOT/source/gameengine/GameLogic/GameNodes
PAIRS="${PAIRS:-6}"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="${A4_HWCOUNT_DIR:-/tmp/a4_hwcount_$RUN_ID}"
LOG="${A4_HWCOUNT_LOG:-/tmp/a4_hwcount_$RUN_ID.log}"
if ! ( set -C; : > "$LOG" ) 2>/dev/null; then
  echo "a4_hwcount: REFUSE existing log: $LOG" >&2
  exit 1
fi
if [ -e "$RUN_DIR" ]; then
  echo "a4_hwcount: REFUSE existing run dir: $RUN_DIR" >&2
  exit 1
fi
mkdir -p "$RUN_DIR"

{
  echo "# A.4 hardware-counter feasibility probe"
  echo "# date: $(date -Is)"
  echo "# criterion: ratio off_cycles/native_cycles; 1 inside [min,max] -> indistinguishable"
  echo "#            ns is reported but never used for the verdict"
  echo "# loadavg at start: $(cat /proc/loadavg)"
} >> "$LOG"

echo "=== capability probe ===" | tee -a "$LOG"

PERF_BIN="$(command -v perf 2>/dev/null || true)"
if [ -z "$PERF_BIN" ]; then
  echo "PROBE perf=missing" | tee -a "$LOG"
  PERF_VERSION="missing"
else
  PERF_VERSION="$("$PERF_BIN" --version 2>&1 | head -n 1)"
  echo "PROBE perf=$PERF_BIN" | tee -a "$LOG"
  echo "PROBE perf_version=$PERF_VERSION" | tee -a "$LOG"
fi
echo "PROBE perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>&1)" | tee -a "$LOG"
echo "PROBE caps=$(grep -E '^Cap(Prm|Eff):' /proc/self/status 2>/dev/null | tr '\n' ' ')" | tee -a "$LOG"

PERF_OK=0
if [ -n "$PERF_BIN" ]; then
  "$PERF_BIN" stat -e cycles,instructions -- true > "$RUN_DIR/probe_events.txt" 2>&1
  probe_rc=$?
  echo "PROBE perf_stat_cycles_instructions_rc=$probe_rc" | tee -a "$LOG"
  sed 's/^/PROBE | /' "$RUN_DIR/probe_events.txt" >> "$LOG"
  "$PERF_BIN" stat -- true > "$RUN_DIR/probe_default.txt" 2>&1
  sed 's/^/PROBE | /' "$RUN_DIR/probe_default.txt" | tee -a "$LOG"

  # Usable only if a real numeric cycles count comes back.
  if ! grep -q '<not supported>' "$RUN_DIR/probe_events.txt" 2>/dev/null &&
     "$PERF_BIN" stat -e cycles -- true 2>&1 | grep -qE '^[[:space:]]*[0-9,]+[[:space:]]+cycles'; then
    PERF_OK=1
  fi
  echo "PROBE perf_counters_usable=$PERF_OK" | tee -a "$LOG"
fi

VALGRIND_BIN="$(command -v valgrind 2>/dev/null || true)"
if [ -z "$VALGRIND_BIN" ]; then
  echo "PROBE valgrind=missing" | tee -a "$LOG"
else
  echo "PROBE valgrind=$VALGRIND_BIN ($("$VALGRIND_BIN" --version 2>&1 | head -n 1))" | tee -a "$LOG"
fi

verdict="no-frequency-invariant-counters"

if [ "$PERF_OK" -eq 1 ]; then
  echo "=== build (same flags and link set as a4_interleave.sh) ===" | tee -a "$LOG"
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
  echo "# build_id: $BUILD_ID" >> "$LOG"
  mkdir -p "$RUN_DIR/har" "$RUN_DIR/clean"
  if ! /usr/bin/c++ $FLAGS -c "$HERE/a4_bench.cpp" -o "$RUN_DIR/har/a4_bench.o" >> "$LOG" 2>&1; then
    echo "HWC_COMPILE_FAIL" | tee -a "$LOG"
    chmod 444 "$LOG"
    printf 'LOG %s\n' "$LOG"
    exit 1
  fi
  ( cd "$RUN_DIR/clean" && ar x "$BDIR/lib/libge_logic_bricks.a" ) >> "$LOG" 2>&1
  if ! /usr/bin/c++ -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition -lpthread -ldl -lm \
      -o "$BIN" "$RUN_DIR/clean"/*.o "$RUN_DIR/har/a4_bench.o" >> "$LOG" 2>&1; then
    echo "HWC_LINK_FAIL" | tee -a "$LOG"
    chmod 444 "$LOG"
    printf 'LOG %s\n' "$LOG"
    exit 1
  fi
  echo "HWC_BUILD_OK $BIN" | tee -a "$LOG"

  # cycles/instructions for one process; prints "cycles instructions frames rounds".
  hwc_run()
  {
    local mode=$1 pair=$2 out perf_out label
    label="native_p$pair"
    out="$RUN_DIR/run_${pair}_${mode}.stdout"
    perf_out="$RUN_DIR/run_${pair}_${mode}.perf"
    [ "$mode" = off ] && label="inline_off_p$pair"
    if [ "$mode" = native ]; then
      env -u BGE_GAMENODE_INLINE A4_PATH_LABEL="$label" A4_BUILD_ID="$BUILD_ID" \
        "$PERF_BIN" stat -e cycles,instructions -o "$perf_out" "$BIN" > "$out" 2>&1
    else
      BGE_GAMENODE_INLINE=off A4_PATH_LABEL="$label" A4_BUILD_ID="$BUILD_ID" \
        "$PERF_BIN" stat -e cycles,instructions -o "$perf_out" "$BIN" > "$out" 2>&1
    fi
    local cycles insn frames rounds
    cycles="$(awk -v ev="cycles" '
      { line=$0; gsub(/,/,"",line); n=split(line, a, /[[:space:]]+/)
        if (a[n] == ev) { for (i=1;i<=n;i++) if (a[i] ~ /^[0-9]+$/) { print a[i]; exit } } }' "$perf_out")"
    insn="$(awk -v ev="instructions" '
      { line=$0; gsub(/,/,"",line); n=split(line, a, /[[:space:]]+/)
        if (a[n] == ev) { for (i=1;i<=n;i++) if (a[i] ~ /^[0-9]+$/) { print a[i]; exit } } }' "$perf_out")"
    frames="$(grep -oE 'frames=[0-9]+' "$out" | tail -n 1 | cut -d= -f2)"
    rounds="$(grep -oE 'rounds=[0-9]+' "$out" | tail -n 1 | cut -d= -f2)"
    if [ -z "$cycles" ] || [ -z "$insn" ] || [ -z "$frames" ]; then
      echo "HWC_BAD_RUN mode=$mode pair=$pair" | tee -a "$LOG"
      exit 1
    fi
    cat "$out" >> "$LOG"
    cat "$perf_out" >> "$LOG"
    printf '%s %s %s %s\n' "$cycles" "$insn" "$frames" "$rounds"
  }

  echo "=== interleaved pairs (cycles / instructions) ===" | tee -a "$LOG"
  cyc_n=(); cyc_o=(); ins_n=(); ins_o=(); per_n=(); per_o=()
  for ((i = 1; i <= PAIRS; i++)); do
    read -r cn in fn rn <<< "$(hwc_run native "$i")"
    read -r co io fo ro <<< "$(hwc_run off "$i")"
    cyc_n+=("$cn"); cyc_o+=("$co"); ins_n+=("$in"); ins_o+=("$io")
    # executed frames per process = warmup 200 + rounds*frames
    tot_n=$((200 + fn * rn)); tot_o=$((200 + fo * ro))
    per_n+=("$(awk -v c="$cn" -v t="$tot_n" 'BEGIN { printf "%.1f", c / t }')")
    per_o+=("$(awk -v c="$co" -v t="$tot_o" 'BEGIN { printf "%.1f", c / t }')")
    rc="$(awk -v o="$co" -v n="$cn" 'BEGIN { printf "%.4f", o / n }')"
    ri="$(awk -v o="$io" -v n="$in" 'BEGIN { printf "%.4f", o / n }')"
    ipc_n="$(awk -v i="$in" -v c="$cn" 'BEGIN { printf "%.4f", i / c }')"
    ipc_o="$(awk -v i="$io" -v c="$co" 'BEGIN { printf "%.4f", i / c }')"
    printf 'PAIR %d cycles native=%s off=%s ratio=%s | instructions native=%s off=%s ratio=%s | ipc native=%s off=%s | cycles/frame native=%s off=%s\n' \
      "$i" "$cn" "$co" "$rc" "$in" "$io" "$ri" "$ipc_n" "$ipc_o" "${per_n[$((i-1))]}" "${per_o[$((i-1))]}" \
      | tee -a "$LOG"
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

  ratios=()
  for ((i = 0; i < PAIRS; i++)); do
    ratios+=("$(awk -v o="${cyc_o[$i]}" -v n="${cyc_n[$i]}" 'BEGIN { printf "%.4f", o / n }')")
  done
  iratios=()
  for ((i = 0; i < PAIRS; i++)); do
    iratios+=("$(awk -v o="${ins_o[$i]}" -v n="${ins_n[$i]}" 'BEGIN { printf "%.4f", o / n }')")
  done
  r_med="$(median_of "${ratios[@]}")"; r_min="$(min_of "${ratios[@]}")"; r_max="$(max_of "${ratios[@]}")"
  i_med="$(median_of "${iratios[@]}")"; i_min="$(min_of "${iratios[@]}")"; i_max="$(max_of "${iratios[@]}")"
  contains1=$(awk -v lo="$r_min" -v hi="$r_max" 'BEGIN { print (lo <= 1.0 && 1.0 <= hi) ? 1 : 0 }')
  if [ "$contains1" -eq 1 ]; then
    verdict="cannot-distinguish"
  else
    verdict="directional"
  fi
  printf 'CYCLES ratio min=%s median=%s max=%s contains1=%s\n' "$r_min" "$r_med" "$r_max" "$contains1" | tee -a "$LOG"
  printf 'INSTRUCTIONS ratio min=%s median=%s max=%s\n' "$i_min" "$i_med" "$i_max" | tee -a "$LOG"
  ipc_n_med="$(awk -v i="$(median_of "${ins_n[@]}")" -v c="$(median_of "${cyc_n[@]}")" 'BEGIN { printf "%.4f", i / c }')"
  ipc_o_med="$(awk -v i="$(median_of "${ins_o[@]}")" -v c="$(median_of "${cyc_o[@]}")" 'BEGIN { printf "%.4f", i / c }')"
  printf 'IPC native median=%s off median=%s\n' "$ipc_n_med" "$ipc_o_med" | tee -a "$LOG"
elif [ -n "$VALGRIND_BIN" ]; then
  echo "HWC_SKIP valgrind-present-but-reduced-frame-protocol-not-comparable-with-a4-baseline" | tee -a "$LOG"
  verdict="no-frequency-invariant-counters"
fi

printf 'RESULT perf_counters_usable=%s valgrind=%s verdict=%s\n' \
  "$PERF_OK" "${VALGRIND_BIN:-missing}" "$verdict" | tee -a "$LOG"
echo "# loadavg at end: $(cat /proc/loadavg)" >> "$LOG"

chmod 444 "$LOG"
printf 'LOG %s bytes=%s sha256=%s\n' "$LOG" "$(stat -c %s "$LOG")" "$(sha256sum "$LOG" | cut -d' ' -f1)"

exit 0
