import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 64

# 检查 bake API 是否存在
props = [p for p in dir(sc.eevee) if 'bake' in p.lower()]
print(f"[T8] eevee bake props: {props}", file=sys.stderr)
props2 = [p for p in dir(bpy.ops.object) if 'bake' in p.lower()]
print(f"[T8] object bake ops: {props2}", file=sys.stderr)

# 建 mesh + material，尝试 bake
bpy.ops.mesh.primitive_plane_add()
obj = bpy.context.object
mat = bpy.data.materials.new("m")
mat.use_nodes = True
obj.data.materials.append(mat)
bpy.context.view_layer.objects.active = obj
try:
    bpy.ops.object.bake(type='EMIT')
    print("[T8] bake emit ok", file=sys.stderr)
except Exception as e:
    print(f"[T8] bake emit: {e}", file=sys.stderr)

print("[T8] DONE", file=sys.stderr)
