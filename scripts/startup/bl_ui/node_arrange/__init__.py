# SPDX-License-Identifier: GPL-2.0-or-later

import sys
import os
# 强行把当前插件目录加入 Python 搜索路径，以便找到 networkx
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from . import keymaps, operators, properties, ui


def register() -> None:
    properties.register()
    operators.register()
    try:
        keymaps.register()
    except Exception as e:
        print("[Node Arrange] Keymap registration failed:", e)
    ui.register()


def unregister() -> None:
    ui.unregister()
    keymaps.unregister()
    operators.unregister()
    properties.unregister()
