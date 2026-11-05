#!/bin/bash
# B6.6 交叉验收：用当前树一次性重编所有 GameNodes 源 + 所有现存 E2E harness
cd /home/eridanusepsilon/Code/EBC/EBC5.3
HERE="$(cd "$(dirname "$0")" && pwd)"

# A harness that stubs Register*GameNodes() must NOT also link the real TU that
# defines it: the two definitions are identical in name and the linker would
# silently pick whichever comes first (that is how "call to address 0" happens).
# Reuse the stub -> TU map from tests/harness_config.sh and drop those objects.
source "$HERE/harness_config.sh"
REG_MAP="$(_harness_registration_map)"

G=source/gameengine/GameLogic/GameNodes
OBJ=/tmp/b661_cross_obj
GAME=$OBJ/game
HAR=$OBJ/h
# Evidence logs/binaries are never reused: every run gets a unique timestamped
# path, and an existing file is refused instead of silently truncated.
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG="${B661_CROSS_LOG:-/tmp/b661_cross_compile_$RUN_ID.log}"
RUN_DIR="${B661_CROSS_RUN_DIR:-/tmp/b661_cross_$RUN_ID}"
if ! ( set -C; : > "$LOG" ) 2>/dev/null; then
  echo "b661_cross_build_all: REFUSE existing log: $LOG" >&2
  exit 1
fi
if [ -e "$RUN_DIR" ]; then
  echo "b661_cross_build_all: REFUSE existing run dir: $RUN_DIR" >&2
  exit 1
fi
mkdir -p "$GAME" "$HAR" "$RUN_DIR"
FLAGS="-I$G -DFMT_SHARED -DHAVE_EXECINFO_H -DNDEBUG -DWITH_ASSERT_ABORT -DWITH_AUDASPACE -DWITH_PYTHON -DWITH_SDL -DWITH_TBB -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D__LITTLE_ENDIAN__ \
-Isource/gameengine/GameLogic -Isource/gameengine/Common -Isource/gameengine/Converter -Isource/gameengine/Device -Isource/gameengine/Expressions \
-Isource/gameengine/Ketsji -Isource/gameengine/Ketsji/KXNetworkMessage -Isource/gameengine/Physics/Common -Isource/gameengine/Rasterizer -Isource/gameengine/SceneGraph \
-Isource/blender/blenkernel -Isource/blender/gpu -Isource/blender/blenfont -Isource/blender/imbuf -Isource/blender/blenloader -Isource/blender/blenmath \
-Isource/blender/python/generic -Iintern/termcolor -Iintern/ghost -Iintern/profile -Iintern/guardedalloc -Isource/blender/makesrna -Isource/blender/blenlib \
-Isource/blender/makesdna -Isource/blender/windowmanager -Ibuild_linux/source/blender/makesdna/intern \
-isystem /usr/local/include/python3.13 -isystem intern/moto/include -isystem extern/recastnavigation/Recast/Include -isystem extern/recastnavigation/Detour/Include \
-isystem extern/audaspace/include -isystem build_linux/extern/audaspace -isystem extern/audaspace/bindings -isystem lib/tbb/include -isystem extern/json/include \
-funsigned-char -fno-strict-aliasing -ffp-contract=off -O1 -std=c++20 -march=x86-64-v2"

export FLAGS GAME LOG
find "$G" -maxdepth 1 -name '*.cpp' -printf '%f\n' | sed 's/\.cpp$//' | \
  xargs -P 6 -I{} bash -c '/usr/bin/c++ $FLAGS -c "$0/{}".cpp -o "$GAME/{}".o 2>>"$LOG" || echo "CFAIL {}" >> "$LOG"' "$G"

LINK="/usr/bin/c++ -Wl,--unresolved-symbols=ignore-all -lpthread -ldl -lm -o"
for harness in reg_b2_b3 reg_b4_e2e p41_rebuild p42_hot_reload p43_trace p44_stability \
                b61_e2e b62_e2e b63_nodes_e2e b63_network_e2e b64_e2e b65a_e2e \
                b661_e2e b661_logicgate b661_query_e2e b661_perf; do
  [ -f "$HERE/$harness.cpp" ] || { echo "MISSING_SRC $harness"; continue; }
  if ! /usr/bin/c++ $FLAGS -c "$HERE/$harness.cpp" -o "$HAR/$harness.o" 2>>"$LOG"; then
    echo "HARNESS_CFAIL $harness"; continue
  fi

  # Drop the real TUs whose Register*GameNodes() this harness stubs out.
  declare -A exclude=()
  unmapped=""
  while read -r fn; do
    [ -n "$fn" ] || continue
    tu="$(printf '%s\n' "$REG_MAP" | awk -F'|' -v f="$fn" '$1==f{print $2; exit}')"
    if [ -z "$tu" ]; then unmapped="$unmapped $fn"; else exclude["${tu%.cpp}.o"]=1; fi
  done < <(harness_stubbed_registrations "$HERE/$harness.cpp")
  if [ -n "$unmapped" ]; then
    echo "UNMAPPED_STUB $harness$unmapped"; continue
  fi
  # Static exclusions (same table as the unified harness build). Notably
  # KX_ExecutionContextPhysics.cpp, whose KX_ExecutionContext::Raycast is
  # re-defined locally by b65a_e2e.cpp.
  while IFS='|' read -r tu _reason _owner; do
    [ -n "$tu" ] && exclude["${tu%.cpp}.o"]=1
  done < <(harness_exclusions_for "$HERE/$harness.cpp")
  objs=()
  for o in "$GAME"/*.o; do
    [ -n "${exclude[$(basename "$o")]:-}" ] || objs+=("$o")
  done
  echo "EXCLUDE $harness ${#exclude[@]}"

  if $LINK "$RUN_DIR/$harness" "$HAR/$harness.o" "${objs[@]}" 2>>"$LOG"; then
    echo "BUILT $RUN_DIR/$harness"
  else
    echo "LINK_FAIL $harness"
  fi
done
echo "BUILD_ALL_DONE"
