import bpy, sys
argv = sys.argv
if "--" not in argv:
    print("usage: blender ... -- /path/to/image.png", file=sys.stderr)
    sys.exit(1)
img_path = argv[argv.index("--") + 1]

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = 512
sc.render.resolution_y = 512
sc.render.resolution_percentage = 50  # input 256, DLSS out 512
sc.eevee.taa_render_samples = 1
sc.view_settings.view_transform = 'Standard'
sc.render.image_settings.file_format = 'PNG'
sc.eevee.dlss5_mode = 'DLSS_SR'

bpy.ops.mesh.primitive_plane_add(size=2)
plane = bpy.context.object
mat = bpy.data.materials.new("img")
mat.use_nodes = True
nt = mat.node_tree
nt.nodes.clear()
tex = nt.nodes.new("ShaderNodeTexImage")
tex.image = bpy.data.images.load(img_path)
tex.interpolation = 'Linear'
tex.extension = 'EXTEND'
em = nt.nodes.new("ShaderNodeEmission")
out = nt.nodes.new("ShaderNodeOutputMaterial")
nt.links.new(tex.outputs['Color'], em.inputs['Color'])
nt.links.new(em.outputs['Emission'], out.inputs['Surface'])
plane.data.materials.append(mat)

cam_data = bpy.data.cameras.new("cam")
cam_data.type = 'ORTHO'
cam_data.ortho_scale = 2.0
cam = bpy.data.objects.new("cam", cam_data)
bpy.context.collection.objects.link(cam)
cam.location = (0, 0, 2)
cam.rotation_euler = (0, 0, 0)
sc.camera = cam

sc.render.filepath = "/tmp/npr-smoke/dlss_sr_image"
bpy.ops.render.render(write_still=True)
print("[SR] image test done", file=sys.stderr)
