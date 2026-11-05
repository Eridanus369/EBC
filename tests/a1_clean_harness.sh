#!/bin/bash
# A.1 clean-rebuild verification: relink the existing harnesses against the
# objects that were compiled into build_clean_verify/lib/libge_logic_bricks.a
# (the clean build), then run all of them.
#
# The harness sources still carry their old Register*FunctionGameNodes stubs.
# Linking the clean GameNodes objects first plus --allow-multiple-definition
# makes the real (clean) registrations win and keeps the stubs unused.
set -u

# Harness sources are repo-local now; /tmp only holds build/run artifacts.
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT=/home/eridanusepsilon/Code/EBC/EBC5.3
BDIR=$ROOT/build_clean_verify
G=$ROOT/source/gameengine/GameLogic/GameNodes
OBJ=/tmp/a1_clean_obj
HAR=$OBJ/h
CLEAN=$OBJ/clean
# Evidence logs/binaries are never reused: every run gets a unique timestamped
# path, and an existing file is refused instead of silently truncated.
RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="${A1_CLEAN_RUN_DIR:-/tmp/a1_clean_$RUN_ID}"
LOG="${A1_CLEAN_LOG:-/tmp/a1_clean_harness_$RUN_ID.log}"
if ! ( set -C; : > "$LOG" ) 2>/dev/null; then
  echo "a1_clean_harness: REFUSE existing log: $LOG" >&2
  exit 1
fi
if [ -e "$RUN_DIR" ]; then
  echo "a1_clean_harness: REFUSE existing run dir: $RUN_DIR" >&2
  exit 1
fi
mkdir -p "$OBJ" "$HAR" "$CLEAN" "$RUN_DIR"

FLAGS="-I$G -DFMT_SHARED -DHAVE_EXECINFO_H -DNDEBUG -DWITH_ASSERT_ABORT -DWITH_AUDASPACE -DWITH_PYTHON -DWITH_SDL -DWITH_TBB -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D__LITTLE_ENDIAN__ \
-I$ROOT/source/gameengine/GameLogic -I$ROOT/source/gameengine/Common -I$ROOT/source/gameengine/Converter -I$ROOT/source/gameengine/Device -I$ROOT/source/gameengine/Expressions \
-I$ROOT/source/gameengine/Ketsji -I$ROOT/source/gameengine/Ketsji/KXNetworkMessage -I$ROOT/source/gameengine/Physics/Common -I$ROOT/source/gameengine/Rasterizer -I$ROOT/source/gameengine/SceneGraph \
-I$ROOT/source/blender/blenkernel -I$ROOT/source/blender/gpu -I$ROOT/source/blender/blenfont -I$ROOT/source/blender/imbuf -I$ROOT/source/blender/blenloader -I$ROOT/source/blender/blenmath \
-I$ROOT/source/blender/python/generic -I$ROOT/intern/termcolor -I$ROOT/intern/ghost -I$ROOT/intern/profile -I$ROOT/intern/guardedalloc -I$ROOT/source/blender/makesrna -I$ROOT/source/blender/blenlib \
-I$ROOT/source/blender/makesdna -I$ROOT/source/blender/windowmanager -I$BDIR/source/blender/makesdna/intern \
-isystem /usr/local/include/python3.13 -isystem $ROOT/intern/moto/include -isystem $ROOT/extern/recastnavigation/Recast/Include -isystem $ROOT/extern/recastnavigation/Detour/Include \
-isystem $ROOT/extern/audaspace/include -isystem $BDIR/extern/audaspace -isystem $ROOT/extern/audaspace/bindings -isystem $ROOT/lib/tbb/include -isystem $ROOT/extern/json/include \
-funsigned-char -fno-strict-aliasing -ffp-contract=off -O1 -std=c++20 -march=x86-64-v2"

HARNESSES="reg_b2_b3 reg_b4_e2e p41_rebuild p42_hot_reload p43_trace p44_stability \
b61_e2e b62_e2e b63_nodes_e2e b63_network_e2e b64_e2e b65a_e2e \
b661_e2e b661_logicgate b661_query_e2e b661_bridge b661_perf"

# ---------------------------------------------------------------- clean objects
echo "=== extract clean archive objects ===" | tee -a "$LOG"
( cd "$CLEAN" && ar x "$BDIR/lib/libge_logic_bricks.a" ) >> "$LOG" 2>&1
echo "clean_objects=$(find "$CLEAN" -name '*.o' | wc -l)" | tee -a "$LOG"

# ---------------------------------------------------------------- compile
echo "=== compile harness sources ===" | tee -a "$LOG"
for h in $HARNESSES; do
  if [ ! -f "$HERE/$h.cpp" ]; then echo "MISSING_SRC $h" | tee -a "$LOG"; continue; fi
  if /usr/bin/c++ $FLAGS -c "$HERE/$h.cpp" -o "$HAR/$h.o" >> "$LOG" 2>&1; then
    echo "CFLAGS_OK $h" | tee -a "$LOG"
  else
    echo "CFAIL $h" | tee -a "$LOG"
  fi
done

# ---------------------------------------------------------------- link
echo "=== link against clean objects ===" | tee -a "$LOG"
LINK="/usr/bin/c++ -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition -lpthread -ldl -lm -o"
for h in $HARNESSES; do
  [ -f "$HAR/$h.o" ] || { echo "NO_OBJ $h" | tee -a "$LOG"; continue; }
  if $LINK "$RUN_DIR/$h" "$CLEAN"/*.o "$HAR/$h.o" >> "$LOG" 2>&1; then
    echo "LINK_OK $RUN_DIR/$h" | tee -a "$LOG"
  else
    echo "LINK_FAIL $h" | tee -a "$LOG"
  fi
done

# ---------------------------------------------------------------- run
echo "=== run harnesses ===" | tee -a "$LOG"
for h in $HARNESSES; do
  [ -x "$RUN_DIR/$h" ] || { echo "SKIP_RUN $h" | tee -a "$LOG"; continue; }
  timeout 300 "$RUN_DIR/$h" > "$RUN_DIR/$h.log" 2>&1
  rc=$?
  result=$(grep -oE 'RESULT[^ ]* (assertions=|failures=).*|(PASS|FAIL) total failures=[0-9]+|B6\.6 harness: [0-9]+ PASS, [0-9]+ FAIL' "$RUN_DIR/$h.log" | tail -1)
  pass=$(grep -cE '^PASS ' "$RUN_DIR/$h.log")
  fail=$(grep -cE '^FAIL ' "$RUN_DIR/$h.log")
  echo "RUN $h rc=$rc pass_lines=$pass fail_lines=$fail result=[$result]" | tee -a "$LOG"
done

# ---------------------------------------------------------------- GUI (bpy)
echo "=== run B4.5 GUI script on clean blender ===" | tee -a "$LOG"
timeout 300 "$BDIR/bin/blender" --python "$HERE/b45_gui.py" > "$RUN_DIR/b45_gui.log" 2>&1
rc=$?
echo "RUN b45_gui rc=$rc" | tee -a "$LOG"
tail -5 "$RUN_DIR/b45_gui.log" | tee -a "$LOG"

echo "=== DONE ===" | tee -a "$LOG"
