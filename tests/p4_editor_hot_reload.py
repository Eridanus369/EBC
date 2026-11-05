"""Headless check of bpy.ops.netlogic.hot_reload()."""
import json
import os
import sys

import addon_utils
import bpy

DIR = '/tmp/p4_editor'
FLAG = os.path.join(DIR, '.hot_reload.flag')
MANIFEST = os.path.join(DIR, '.hot_reload_manifest.json')


def fail(message):
    print('FAIL', message)
    sys.exit(1)


def ok(message):
    print('PASS', message)


addon_utils.enable('bge_netlogic')
from bge_netlogic.editor.nodetree import LogicNodeTree  # noqa: E402
from bge_netlogic.utilities import preferences  # noqa: E402

prefs = preferences()
if prefs is None:
    fail('bge_netlogic preferences are missing')
prefs.use_native_graph_runtime = True

for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)

try:
    tree = bpy.data.node_groups.new('TestGraph', LogicNodeTree.bl_idname)
except Exception as error:  # noqa: BLE001
    fail('node tree could not be created: %r' % (error,))

obj = bpy.data.objects.new('Player', None)
bpy.context.scene.collection.objects.link(obj)
ref = obj.logic_trees.add()
ref.tree_name = tree.name

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(DIR, 'p4_editor.blend'))

for path in (FLAG, MANIFEST):
    if os.path.exists(path):
        os.remove(path)

result = bpy.ops.netlogic.hot_reload()
if 'FINISHED' not in result:
    fail('operator returned %r' % (result,))
ok('netlogic.hot_reload finished')

if not os.path.exists(MANIFEST):
    fail('manifest file was not written')
if not os.path.exists(FLAG):
    fail('flag file was not written')
with open(MANIFEST, encoding='utf-8') as handle:
    data = json.load(handle)
if not data.get('graphs'):
    fail('manifest contains no graphs')
if not data.get('mounts'):
    fail('manifest contains no mounts')
ok('manifest and flag written next to the .blend')

if 'game_graph_manifest' not in bpy.context.scene:
    fail('scene property was not refreshed')
ok('scene manifest property refreshed')

print('RESULT failures=0')
