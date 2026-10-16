import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 128
sc.render.fps = 24
sc.eevee.taa_render_samples = 2
sc.eevee.dlss5_mode = 'NPR_DLSSNR'
sc.eevee.dlss5_intensity = 1.0
sc.frame_start = 1
sc.frame_end = 3

mat = bpy.data.materials.new("npr")
mat.use_nodes = True
nt = mat.node_tree
nt.nodes.clear()
out = nt.nodes.new("ShaderNodeOutputMaterial")
npr = nt.nodes.new("ShaderNodePrincipledNPR")
nt.links.new(npr.outputs[0], out.inputs['Surface'])

w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.1, 0.3, 0.5, 1.0)
bpy.ops.object.camera_add(location=(0, -4, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.mesh.primitive_monkey_add()
cube = bpy.context.object
cube.data.materials.append(mat)
cube.location = (0, 0, 0)
cube.keyframe_insert(data_path="location", frame=1)
cube.location = (2, 0, 0)
cube.keyframe_insert(data_path="location", frame=3)
bpy.ops.object.light_add(type='SUN')

sc.render.filepath = "/tmp/reg_nr_anim_"
bpy.ops.render.render(animation=True)
print("[T11] PASS 3-frame NR animation", file=sys.stderr)
