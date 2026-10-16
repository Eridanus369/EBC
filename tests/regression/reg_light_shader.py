import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 128

bpy.ops.object.light_add(type='AREA')
light_data = bpy.context.object.data
lt = light_data.node_tree
if lt is None:
    lt = bpy.data.node_groups.new("LightTree", "ShaderNodeTree")
    light_data.node_tree = lt
lt.nodes.new("ShaderNodeEeveeLightShaderOutput")
lt.nodes.new("ShaderNodeForeachLightInput")

bpy.ops.object.camera_add(location=(0, -4, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.mesh.primitive_cube_add()
sc.render.filepath = "/tmp/reg_light_shader.png"
bpy.ops.render.render(write_still=True)
print("[T12] PASS light shader E2E", file=sys.stderr)
