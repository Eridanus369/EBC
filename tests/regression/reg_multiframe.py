import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 128
sc.render.fps = 24
sc.eevee.taa_render_samples = 2
sc.eevee.dlss5_mode = 'DLSS_SR'
sc.frame_start = 1
sc.frame_end = 5

w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.1, 0.3, 0.5, 1.0)
bpy.ops.object.camera_add(location=(0, -4, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.mesh.primitive_monkey_add()
cube = bpy.context.object
# 加一个动画，让物体每帧移动
cube.location = (0, 0, 0)
cube.keyframe_insert(data_path="location", frame=1)
cube.location = (2, 0, 0)
cube.keyframe_insert(data_path="location", frame=5)
bpy.ops.object.light_add(type='SUN')

sc.render.filepath = "/tmp/reg_multiframe_"
bpy.ops.render.render(animation=True)
print("[T10] PASS 5-frame animation with DLSS SR", file=sys.stderr)
