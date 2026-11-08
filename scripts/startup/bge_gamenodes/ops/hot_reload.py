"""Signal a running player to rebuild its native graph instances.

The editor cannot reach another process, so the hand-off is two files next to
the .blend:

* ``.hot_reload_manifest.json`` -- the freshly exported scene manifest,
* ``.hot_reload.flag`` -- touched last, its mtime is the actual trigger.

``KX_GraphSceneManager::CheckHotReload`` polls the flag once per frame; a
changed mtime makes it parse the manifest and stage a rebuild of every mounted
instance. An embedded player that shares bpy data still works: the manifest is
also written into ``scene["game_graph_manifest"]`` first.
"""

import json
import os
import time

import bpy
from bpy.types import Operator

from ..generator.ir_export import write_scene_manifest
from ..utilities import notify, warn
from .operator import operator

MANIFEST_FILE = '.hot_reload_manifest.json'
FLAG_FILE = '.hot_reload.flag'


def _blend_dir():
    path = bpy.data.filepath
    if not path:
        return ''
    return os.path.dirname(path)


@operator
class LOGIC_NODES_OT_hot_reload(Operator):
    bl_idname = "gamenodes.hot_reload"
    bl_label = "Hot Reload Game Graphs"
    bl_options = {'REGISTER'}
    bl_description = (
        "Re-export the active scene graph manifest and tell the running "
        "player to rebuild its graphs on the next frame"
    )

    @classmethod
    def poll(cls, context):
        return context.scene is not None

    def execute(self, context):
        scene = context.scene
        manifest = write_scene_manifest(scene)
        if manifest is None:
            warn('Hot reload skipped: native runtime is off or the scene exports no graphs')
            return {'CANCELLED'}

        directory = _blend_dir()
        if not directory:
            warn('Hot reload skipped: save the .blend first, the flag lives next to it')
            return {'CANCELLED'}

        manifest_path = os.path.join(directory, MANIFEST_FILE)
        flag_path = os.path.join(directory, FLAG_FILE)
        with open(manifest_path, 'w', encoding='utf-8') as handle:
            json.dump(manifest, handle)
        # Touch the flag last: its mtime is what the runtime polls.
        with open(flag_path, 'w', encoding='utf-8') as handle:
            handle.write(str(time.time()))

        graph_count = len(manifest.get('graphs', ()))
        mount_count = len(manifest.get('mounts', ()))
        notify(
            'Hot reload signalled: {} graph(s), {} mount(s), flag {}'.format(
                graph_count, mount_count, flag_path
            )
        )
        return {'FINISHED'}
