import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 256
sc.eevee.dlss5_mode = 'DLSS_SR'
w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.1, 0.3, 0.5, 1.0)
bpy.ops.object.camera_add(location=(0, -4, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.mesh.primitive_uv_sphere_add()
bpy.ops.object.light_add(type='SUN')
sc.render.filepath = "/tmp/reg_viewport.png"
try:
    bpy.ops.render.opengl(write_still=True)
    print("[T5] PASS opengl viewport render", file=sys.stderr)
except Exception as e:
    print(f"[T5] FAIL: {e}", file=sys.stderr)
