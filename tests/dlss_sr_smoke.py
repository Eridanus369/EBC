import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 256
sc.render.resolution_percentage = 100
sc.eevee.taa_render_samples = 4
sc.view_settings.view_transform = 'Standard'
sc.render.image_settings.file_format = 'PNG'
sc.eevee.dlss5_mode = 'DLSS_SR'
w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.0, 0.5, 0.0, 1.0)
bpy.ops.object.camera_add(location=(0, -4, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.mesh.primitive_uv_sphere_add()
bpy.ops.object.light_add(type='SUN', location=(2, -2, 4))
sc.render.filepath = "/tmp/npr-smoke/dlss_sr"
bpy.ops.render.render(write_still=True)
print("[SR] render ok", file=sys.stderr)
