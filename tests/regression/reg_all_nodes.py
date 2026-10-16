import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
mat = bpy.data.materials.new("allnpr")
mat.use_fake_user = True
mat.use_nodes = True
nt = mat.node_tree
nt.nodes.clear()
nt.nodes.new("ShaderNodeOutputMaterial")

npr_types = ['ShaderNodeNPR_Input', 'ShaderNodeNPR_Output', 'ShaderNodeNPR_Refraction',
             'ShaderNodeNPRRim', 'ShaderNodeNPRSurfaceDiffusion', 'ShaderNodeNPRImageSample',
             'ShaderNodeNPRLightInfo', 'ShaderNodePrincipledNPR',
             'ShaderNodeForeachLightInput', 'ShaderNodeForeachLightOutput',
             'ShaderNodeLightAccumulation', 'ShaderNodeLightEvaluation',
             'ShaderNodeLightInfo', 'ShaderNodeLightProbeColor',
             'ShaderNodeEeveeLightShaderInfo', 'ShaderNodeEeveeLightShaderOutput',
             'ShaderNodeParallax', 'ShaderNodeShaderInfo', 'ShaderNodeRenderTexture']

ok = []
fail = []
for t in npr_types:
    try:
        nt.nodes.new(t)
        ok.append(t)
    except Exception as e:
        fail.append((t, str(e)))

print(f"[T9] ok={len(ok)} fail={len(fail)}", file=sys.stderr)
for t, e in fail:
    print(f"[T9] FAIL {t}: {e}", file=sys.stderr)

# 存盘 + 加载
bpy.ops.wm.save_as_mainfile(filepath="/tmp/reg_allnodes.blend")
bpy.ops.wm.open_mainfile(filepath="/tmp/reg_allnodes.blend")
mat2 = bpy.data.materials.get("allnpr")
nodes2 = [n.bl_idname for n in mat2.node_tree.nodes]
missing = [t for t in ok if t not in nodes2]
if missing:
    print(f"[T9] FAIL after reload, missing: {missing}", file=sys.stderr)
else:
    print(f"[T9] PASS reload preserved all {len(ok)} nodes", file=sys.stderr)
