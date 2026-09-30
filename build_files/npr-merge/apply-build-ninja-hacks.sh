#!/usr/bin/env bash
# 打回 build.ninja 的所有本地 hack。
# 每次 cmake regen（任何 CMakeLists.txt 改动触发）后必须跑一遍。
# 幂等：已打过就跳过，重复执行无副作用。
set -euo pipefail

BUILD_DIR="$(realpath "${1:-$(dirname "$0")/../../build_linux}")"
NINJA="$BUILD_DIR/build.ninja"

if [[ ! -f "$NINJA" ]]; then
  echo "ERR: $NINJA 不存在" >&2
  exit 1
fi

cd "$BUILD_DIR"

echo "[1/6] X11 .a -> .so"
sed -i 's|/usr/lib/x86_64-linux-gnu/libX11\.a|/usr/lib/x86_64-linux-gnu/libX11.so|g; s|/usr/lib/x86_64-linux-gnu/libXrender\.a|/usr/lib/x86_64-linux-gnu/libXrender.so|g; s|/usr/lib/x86_64-linux-gnu/libXfixes\.a|/usr/lib/x86_64-linux-gnu/libXfixes.so|g; s|/usr/lib/x86_64-linux-gnu/libXi\.a|/usr/lib/x86_64-linux-gnu/libXi.so|g' "$NINJA"

echo "[2/6] Xxf86vm 后追加 X11 全家桶"
sed -i 's|/usr/lib/x86_64-linux-gnu/libXxf86vm\.so /usr/lib/x86_64-linux-gnu/libXfixes\.so /usr/lib/x86_64-linux-gnu/libXi\.so|/usr/lib/x86_64-linux-gnu/libXxf86vm.so /usr/lib/x86_64-linux-gnu/libXext.so /usr/lib/x86_64-linux-gnu/libX11.so /usr/lib/x86_64-linux-gnu/libXrender.so /usr/lib/x86_64-linux-gnu/libXfixes.so /usr/lib/x86_64-linux-gnu/libXi.so /usr/lib/x86_64-linux-gnu/libXfixes.so /usr/lib/x86_64-linux-gnu/libXi.so|g' "$NINJA"

echo "[3/6] OIIO 3.0.19 加在系统 3.1 前面"
sed -i 's|/usr/local/lib/libOpenImageIO\.so\.3\.1\.6|'"$HOME"'/Code/EBC/EBC5.3/lib/openimageio/lib/libOpenImageIO.so.3.0.19 /usr/local/lib/libOpenImageIO.so.3.1.6|g; s|/usr/local/lib/libOpenImageIO_Util\.so\.3\.1\.6|'"$HOME"'/Code/EBC/EBC5.3/lib/openimageio/lib/libOpenImageIO_Util.so.3.0.19 /usr/local/lib/libOpenImageIO_Util.so.3.1.6|g' "$NINJA"

echo "[4/6] OpenEXR 3.3 追加"
sed -i 's|/usr/lib/x86_64-linux-gnu/libOpenEXR-3_1\.so\.30\.13\.1|/usr/lib/x86_64-linux-gnu/libOpenEXR-3_1.so.30.13.1 '"$HOME"'/Code/EBC/EBC5.3/lib/openexr/lib/libOpenEXR.so.32.3.3.11 '"$HOME"'/Code/EBC/EBC5.3/lib/openexr/lib/libOpenEXRCore.so.32.3.3.11|g; s|/usr/lib/x86_64-linux-gnu/libIlmThread-3_1\.so\.30\.13\.1|/usr/lib/x86_64-linux-gnu/libIlmThread-3_1.so.30.13.1 '"$HOME"'/Code/EBC/EBC5.3/lib/openexr/lib/libIlmThread.so.32.3.3.11|g; s|/usr/lib/x86_64-linux-gnu/libIex-3_1\.so\.30\.13\.1|/usr/lib/x86_64-linux-gnu/libIex-3_1.so.30.13.1 '"$HOME"'/Code/EBC/EBC5.3/lib/openexr/lib/libIex.so.32.3.3.11|g' "$NINJA"

echo "[5/6] libtiff 补在 hpdfs 后"
sed -i 's|'"$HOME"'/Code/EBC/EBC5.3/lib/haru/lib/libhpdfs\.a  lib/libbf_functions\.a|'"$HOME"'/Code/EBC/EBC5.3/lib/haru/lib/libhpdfs.a  '"$HOME"'/Code/EBC/EBC5.3/lib/tiff/lib/libtiff.a  lib/libbf_functions.a|g' "$NINJA"

echo "[6/6] 自检"
x11a=$(grep -c 'libX11\.a\|libXrender\.a\|libXfixes\.a\|libXi\.a' "$NINJA" || true)
oiio=$(grep -c 'libOpenImageIO\.so\.3\.0\.19' "$NINJA" || true)
exr=$(grep -c 'libOpenEXR\.so\.32\.3\.3\.11' "$NINJA" || true)
tiff=$(grep -c 'libhpdfs\.a  '"$HOME"'/Code/EBC/EBC5.3/lib/tiff/lib/libtiff\.a' "$NINJA" || true)
echo "  X11 .a 残留（应为 0）: $x11a"
echo "  OIIO 3.0（应 >0）:     $oiio"
echo "  OpenEXR 3.3（应 >0）:  $exr"
echo "  libtiff（应 >0）:      $tiff"
[[ "$x11a" == "0" && "$oiio" -gt 0 && "$exr" -gt 0 && "$tiff" -gt 0 ]] && echo "OK" || { echo "WARN: 自检未通过，请检查" >&2; exit 2; }
