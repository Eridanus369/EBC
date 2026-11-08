#!/bin/bash
# B6.9-I engine-path E2E: real EngineInputAdapter + DEV_Joystick, SDL3 virtual pad.
set -e
cd "$(dirname "$0")/.."
OUT="tests/.e2e_out/b68_input_engine_bin"
mkdir -p tests/.e2e_out
G=source/gameengine/GameLogic/GameNodes
/usr/bin/c++ -I$G -DFMT_SHARED -DHAVE_EXECINFO_H -DNDEBUG -DWITH_ASSERT_ABORT -DWITH_AUDASPACE -DWITH_PYTHON -DWITH_SDL -DWITH_TBB -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D__LITTLE_ENDIAN__ -DKX_INPUT_TEST_HOOKS \
  -Isource/gameengine/GameLogic -Isource/gameengine/Common -Isource/gameengine/Converter -Isource/gameengine/Device -Isource/gameengine/Expressions \
  -Isource/gameengine/Ketsji -Isource/gameengine/Ketsji/KXNetworkMessage -Isource/gameengine/Physics/Common -Isource/gameengine/Rasterizer -Isource/gameengine/SceneGraph \
  -Isource/blender/blenkernel -Isource/blender/gpu -Isource/blender/blenfont -Isource/blender/imbuf -Isource/blender/blenloader -Isource/blender/blenmath \
  -Isource/blender/python/generic -Iintern/termcolor -Iintern/ghost -Iintern/profile -Iintern/guardedalloc -Isource/blender/makesrna -Isource/blender/blenlib \
  -Isource/blender/makesdna -Isource/blender/windowmanager -Ibuild_linux/source/blender/makesdna/intern \
  -isystem /usr/local/include/python3.13 -isystem intern/moto/include -isystem extern/recastnavigation/Recast/Include -isystem extern/recastnavigation/Detour/Include \
  -isystem extern/audaspace/include -isystem build_linux/extern/audaspace -isystem extern/audaspace/bindings -isystem lib/tbb/include -isystem extern/json/include \
  -funsigned-char -fno-strict-aliasing -ffp-contract=off -O1 -std=c++20 -march=x86-64-v2 \
  -Wl,--unresolved-symbols=ignore-all \
  tests/b68_input_engine_e2e.cpp \
  $G/KX_GameAdaptersEngine.cpp \
  $G/KX_GameAdapters.cpp \
  source/gameengine/Common/CM_Message.cpp \
  source/gameengine/Device/DEV_Joystick.cpp \
  source/gameengine/Device/DEV_JoystickEvents.cpp \
  source/gameengine/Device/DEV_JoystickVibration.cpp \
  $(pkg-config --cflags --libs sdl3) \
  -o "$OUT"
"$OUT"
