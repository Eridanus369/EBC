import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 256

# Filter Graph
fg = bpy.data.node_groups.new("fg", "EeveeFilterGraphNodeTree")
sc.eevee.filter_graph = fg
inp = None
print(f"[T7] filter_graph set, input node: {inp is not None}", file=sys.stderr)

# NPR 材质 + 多节点
mat = bpy.data.materials.new("npr")
mat.use_nodes = True
nt = mat.node_tree
nt.nodes.clear()
out = nt.nodes.new("ShaderNodeOutputMaterial")
npr = nt.nodes.new("ShaderNodePrincipledNPR")
rim = nt.nodes.new("ShaderNodeNPRRim")
sd = nt.nodes.new("ShaderNodeNPRSurfaceDiffusion")
nt.links.new(npr.outputs[0], out.inputs['Surface'])

w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.1, 0.3, 0.5, 1.0)
bpy.ops.object.camera_add(location=(0, -4, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.mesh.primitive_monkey_add()
bpy.context.object.data.materials.append(mat)
bpy.ops.object.light_add(type='SUN')
sc.render.filepath = "/tmp/reg_filter.png"
bpy.ops.render.render(write_still=True)
print("[T7] PASS Filter Graph + NPR nodes", file=sys.stderr)
