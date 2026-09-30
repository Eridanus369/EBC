#!/usr/bin/env bash
# NPR 节点 EEVEE headless 冒烟测试。
# 每个已移植节点单独建材质 + 渲染 64x64，输出 PNG 到 /tmp/npr-smoke/。
set -e
cd "$(dirname "$0")/../.."
export LD_LIBRARY_PATH="$PWD/build_linux/bin/lib:$PWD/lib/imath/lib:$PWD/lib/openexr/lib:$PWD/lib/openimageio/lib:$PWD/lib/openvdb/lib:$PWD/lib/tbb/lib:$PWD/lib/materialx/lib:$PWD/lib/usd/lib:$PWD/lib/opencolorio/lib:$PWD/lib/shaderc/lib:$PWD/lib/vulkan/lib:$PWD/lib/dpcpp/lib:$PWD/lib/openimagedenoise/lib:$PWD/lib/opensubdiv/lib:$PWD/lib/embree/lib:$PWD/lib/openpgl/lib:/usr/local/lib"
mkdir -p /tmp/npr-smoke
./build_linux/bin/blender --background --factory-startup --python build_files/npr-merge/smoke-test.py 2>&1 \
  | grep -E "=== |OK$|FAIL|SMOKE DONE|Saved:"
ls -la /tmp/npr-smoke/
