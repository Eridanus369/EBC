import bpy, os

OUT = "/tmp/npr-smoke"
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)

bpy.context.scene.render.engine = "BLENDER_EEVEE"
scene = bpy.context.scene
scene.render.resolution_x = 64
scene.render.resolution_y = 64
scene.render.image_settings.file_format = 'PNG'

# 场景：Cube + 相机 + 灯光
bpy.ops.mesh.primitive_cube_add()
cube = bpy.context.active_object

bpy.ops.object.camera_add(location=(3, -3, 3))
cam = bpy.context.active_object
cam.rotation_euler = (1.1, 0.0, 0.785)
scene.camera = cam

bpy.ops.object.light_add(type='SUN', location=(3, -3, 5))
bpy.context.active_object.data.energy = 3.0

mat = bpy.data.materials.new("t")
mat.use_nodes = True
cube.data.materials.append(mat)
nt = mat.node_tree

for n in list(nt.nodes):
    nt.nodes.remove(n)
out = nt.nodes.new("ShaderNodeOutputMaterial")
em = nt.nodes.new("ShaderNodeEmission")
nt.links.new(em.outputs[0], out.inputs['Surface'])

NODES = [
    "ShaderNodeTwirl",
    "ShaderNodeWaterRipples",
    "ShaderNodeTexHexagon",
    "ShaderNodeSdfPrimitive",
    "ShaderNodeSdfOp",
    "ShaderNodeSdfVectorOp",
    "ShaderNodeBasisTransform",
    "ShaderNodeWorldToTangent",
    "ShaderNodeScreenDerivative",
    "ShaderNodeCurvature",
    "ShaderNodeBevel",
    "ShaderNodeRenderInfo",
    "ShaderNodeOKLabColorRamp",
]

for node_type in NODES:
    print(f"=== {node_type} ===")
    for n in list(nt.nodes):
        if n not in (out, em):
            nt.nodes.remove(n)

    try:
        npr = nt.nodes.new(node_type)
    except Exception as e:
        print(f"  CREATE FAIL: {e}")
        continue

    if not npr.outputs:
        print("  SKIP: no outputs")
        continue

    sock = npr.outputs[0]
    try:
        if sock.type == 'VALUE':
            nt.links.new(sock, em.inputs['Strength'])
        else:
            nt.links.new(sock, em.inputs['Color'])
    except Exception as e:
        print(f"  LINK FAIL: {e}")
        continue

    scene.render.filepath = f"{OUT}/{node_type}.png"
    try:
        bpy.ops.render.render(write_still=True)
        print("  OK")
    except Exception as e:
        print(f"  RENDER FAIL: {type(e).__name__}: {e}")

print("SMOKE DONE")
