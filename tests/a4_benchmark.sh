#!/bin/bash
# A.4 performance baseline on the clean build.
# Same 100-node graph, 1000 frames per round, 21 rounds per process, 3 process
# repeats per path. Paths: native (inline on), inline off, pure Python.
set -u

# Repo-local sources (the /tmp copies are original evidence only).
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT=/home/eridanusepsilon/Code/EBC/EBC5.3
BDIR=$ROOT/build_clean_verify
G=$ROOT/source/gameengine/GameLogic/GameNodes
OBJ=/tmp/a1_clean_obj
HAR=$OBJ/a4
CLEAN=$OBJ/clean
# Evidence logs/binaries are never reused: every run gets a unique timestamped
# path, and an existing file is refused instead of silently truncated.
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG="${A4_LOG:-/tmp/a4_benchmark_$RUN_ID.log}"
BIN="${A4_BIN:-/tmp/a4_bench_$RUN_ID}"
if ! ( set -C; : > "$LOG" ) 2>/dev/null; then
  echo "a4_benchmark: REFUSE existing log: $LOG" >&2
  exit 1
fi
if [ -e "$BIN" ]; then
  echo "a4_benchmark: REFUSE existing binary: $BIN" >&2
  exit 1
fi
mkdir -p "$HAR" "$CLEAN"

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
{
  echo "# A.4 benchmark baseline log"
  echo "# date: $(date -Is)"
  echo "# build_id: $BUILD_ID"
  echo "# binary: $BDIR/bin/blender"
  echo "# clean archive: $BDIR/lib/libge_logic_bricks.a"
  echo "# method: 100-node graph (1 OnUpdate + 2 ConstFloat + 96 FloatMath + 1 Sink),"
  echo "#         1000 frames/round, 21 rounds/run, 3 runs/path, median of run medians."
  echo "# cpu: $(nproc)"
} >> "$LOG"

# ---------------------------------------------------------------- build
echo "=== compile a4_bench ===" | tee -a "$LOG"
if ! /usr/bin/c++ $FLAGS -c "$HERE/a4_bench.cpp" -o "$HAR/a4_bench.o" >> "$LOG" 2>&1; then
  echo "A4_COMPILE_FAIL" | tee -a "$LOG"; exit 1
fi
( cd "$CLEAN" && ar x "$BDIR/lib/libge_logic_bricks.a" ) >> "$LOG" 2>&1
if ! /usr/bin/c++ -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition -lpthread -ldl -lm -o "$BIN" "$CLEAN"/*.o "$HAR/a4_bench.o" >> "$LOG" 2>&1; then
  echo "A4_LINK_FAIL" | tee -a "$LOG"; exit 1
fi
echo "A4_BUILD_OK $BIN" | tee -a "$LOG"

# ---------------------------------------------------------------- run
run_native() {
  local label=$1 runs=$2
  for i in $(seq 1 "$runs"); do
    env -u BGE_GAMENODE_INLINE A4_PATH_LABEL="$label" A4_BUILD_ID="$BUILD_ID" \
      "$BIN" 2>&1 | tee -a "$LOG" | grep BENCH
  done
}

run_inline_off() {
  local label=$1 runs=$2
  for i in $(seq 1 "$runs"); do
    BGE_GAMENODE_INLINE=off A4_PATH_LABEL="$label" A4_BUILD_ID="$BUILD_ID" \
      "$BIN" 2>&1 | tee -a "$LOG" | grep BENCH
  done
}

run_python() {
  local label=$1 runs=$2
  for i in $(seq 1 "$runs"); do
    A4_PATH_LABEL="$label" A4_BUILD_ID="$BUILD_ID" \
      "$BDIR/bin/blender" -b --python "$HERE/a4_bench_py.py" 2>&1 | tee -a "$LOG" | grep BENCH
  done
}

echo "=== path (a): native / inline on ===" | tee -a "$LOG"
run_native native 3
echo "=== path (b): BGE_GAMENODE_INLINE=off ===" | tee -a "$LOG"
run_inline_off inline_off 3
echo "=== path (c): pure Python reference ===" | tee -a "$LOG"
run_python python_equiv 3

echo "=== SUMMARY (median of the three per-run medians) ===" | tee -a "$LOG"
for label in native inline_off python_equiv; do
  vals=$(grep "BENCH path=$label " "$LOG" | grep -oE 'median_ns_per_frame=[0-9.]+' | cut -d= -f2)
  count=$(printf '%s\n' "$vals" | grep -c .)
  med=$(printf '%s\n' "$vals" | sort -n | awk '{a[NR]=$1} END {if (NR==0) print "NA"; else if (NR%2==1) print a[(NR+1)/2]; else print (a[NR/2]+a[NR/2+1])/2}')
  echo "RESULT path=$label runs=$count median_ns_per_frame=$med" | tee -a "$LOG"
done
echo "=== DONE ===" | tee -a "$LOG"
