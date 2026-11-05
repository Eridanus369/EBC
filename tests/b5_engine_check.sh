#!/bin/bash
set -e
cd /home/eridanusepsilon/Code/EBC/EBC5.3
# Build artifacts are timestamped and never overwrite an earlier run.
OUT="${OUT:-/tmp/b5_engine_$(date +%Y%m%d-%H%M%S).o}"
if [ -e "$OUT" ]; then
  echo "b5_engine_check: REFUSE existing output: $OUT" >&2
  exit 1
fi
G=source/gameengine/GameLogic/GameNodes
/usr/bin/c++ -I$G -DFMT_SHARED -DHAVE_EXECINFO_H -DNDEBUG -DWITH_ASSERT_ABORT -DWITH_AUDASPACE -DWITH_PYTHON -DWITH_SDL -DWITH_TBB -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D__LITTLE_ENDIAN__ \
  -Isource/gameengine/GameLogic -Isource/gameengine/Common -Isource/gameengine/Converter -Isource/gameengine/Device -Isource/gameengine/Expressions \
  -Isource/gameengine/Ketsji -Isource/gameengine/Ketsji/KXNetworkMessage -Isource/gameengine/Physics/Common -Isource/gameengine/Rasterizer -Isource/gameengine/SceneGraph \
  -Isource/blender/blenkernel -Isource/blender/gpu -Isource/blender/blenfont -Isource/blender/imbuf -Isource/blender/blenloader -Isource/blender/blenmath \
  -Isource/blender/python/generic -Iintern/termcolor -Iintern/ghost -Iintern/profile -Iintern/guardedalloc -Isource/blender/makesrna -Isource/blender/blenlib \
  -Isource/blender/makesdna -Isource/blender/windowmanager -Ibuild_linux/source/blender/makesdna/intern \
  -isystem /usr/local/include/python3.13 -isystem intern/moto/include -isystem extern/recastnavigation/Recast/Include -isystem extern/recastnavigation/Detour/Include \
  -isystem extern/audaspace/include -isystem build_linux/extern/audaspace -isystem extern/audaspace/bindings -isystem lib/tbb/include -isystem extern/json/include \
  -funsigned-char -fno-strict-aliasing -ffp-contract=off -O1 -std=c++20 -march=x86-64-v2 -c \
  $G/KX_GameAdaptersEngine.cpp -o "$OUT"
echo "B5_ENGINE_BUILD_OK $OUT"
