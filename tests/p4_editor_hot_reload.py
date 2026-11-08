"""Headless check of bpy.ops.gamenodes.hot_reload()."""
import json
import os
import sys

import bpy

DIR = '/tmp/p4_editor'
FLAG = os.path.join(DIR, '.hot_reload.flag')
MANIFEST = os.path.join(DIR, '.hot_reload_manifest.json')


def fail(message):
    print('FAIL', message)
    sys.exit(1)


def ok(message):
    print('PASS', message)


from bge_gamenodes.editor.nodetree import GameNodeTree  # noqa: E402
from bge_gamenodes.utilities import preferences  # noqa: E402

if preferences() is None:
    fail('bge_gamenodes preferences are missing')

for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)

try:
    tree = bpy.data.node_groups.new('TestGraph', GameNodeTree.bl_idname)
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

result = bpy.ops.gamenodes.hot_reload()
if 'FINISHED' not in result:
    fail('operator returned %r' % (result,))
ok('gamenodes.hot_reload finished')

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
