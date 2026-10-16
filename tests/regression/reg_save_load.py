import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.eevee.dlss5_mode = 'NPR_DLSSNR'
sc.eevee.dlss5_intensity = 0.7
sc.eevee.dlss5_local_tone_strength = 0.5
sc.eevee.dlss5_realtime = True

mat = bpy.data.materials.new("npr")
mat.use_fake_user = True
mat.use_nodes = True
nt = mat.node_tree
nt.nodes.clear()
out = nt.nodes.new("ShaderNodeOutputMaterial")
npr = nt.nodes.new("ShaderNodePrincipledNPR")
nt.links.new(npr.outputs[0], out.inputs['Surface'])
bpy.ops.mesh.primitive_cube_add()
bpy.context.object.data.materials.append(mat)

path = "/tmp/reg_roundtrip.blend"
bpy.ops.wm.save_as_mainfile(filepath=path)
bpy.ops.wm.open_mainfile(filepath=path)
sc2 = bpy.context.scene
assert sc2.eevee.dlss5_mode == 'NPR_DLSSNR'
assert abs(sc2.eevee.dlss5_intensity - 0.7) < 1e-5
assert sc2.eevee.dlss5_realtime == True
mat2 = bpy.data.materials.get("npr")
assert mat2 is not None
nodes = [n.bl_idname for n in mat2.node_tree.nodes]
assert "ShaderNodePrincipledNPR" in nodes
print("[T1] PASS save/load roundtrip", file=sys.stderr)
