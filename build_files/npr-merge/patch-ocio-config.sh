#!/usr/bin/env bash
# EBC 的 lib/opencolorio 是 2.4.1，但 5.3 的 config.ocio 是 2.5 格式。
# 2.4 读 2.5 config 会 fallback 崩溃。本脚本降版本号到 2.4 绕过。
# 幂等：已经是 2.4 就跳过。
# 注：这是临时绕过，正确修复是替换 lib/opencolorio 到 2.5。
set -euo pipefail

BUILD_DIR="${1:-$(dirname "$0")/../../build_linux}"
CONFIG="$BUILD_DIR/bin/5.3/datafiles/colormanagement/config.ocio"

if [[ ! -f "$CONFIG" ]]; then
  echo "ERR: $CONFIG 不存在" >&2
  exit 1
fi

if grep -q '^ocio_profile_version: 2\.5' "$CONFIG"; then
  cp -v "$CONFIG" "$CONFIG.2.5.bak"
  sed -i 's/^ocio_profile_version: 2\.5/ocio_profile_version: 2.4/' "$CONFIG"
  echo "OK: 已降到 2.4（备份 $CONFIG.2.5.bak）"
elif grep -q '^ocio_profile_version: 2\.4' "$CONFIG"; then
  echo "SKIP: 已经是 2.4"
else
  echo "WARN: 未找到 ocio_profile_version 行，请检查" >&2
  grep -n 'ocio_profile_version' "$CONFIG" || true
  exit 2
fi
