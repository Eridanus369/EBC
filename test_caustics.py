import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 256
sc.eevee.caustics.use_spectral_caustics = True
sc.eevee.caustics.photon_count = 200000
sc.eevee.caustics.caustic_resolution = 512
sc.eevee.taa_render_samples = 16

# 1. 地面
bpy.ops.mesh.primitive_plane_add(size=10, location=(0, 0, -1.5))
mat_plane = bpy.data.materials.new("PlaneMat")
mat_plane.use_nodes = True
bpy.context.object.data.materials.append(mat_plane)

# 2. 玻璃板（稍微倾斜）
bpy.ops.mesh.primitive_plane_add(size=2, location=(0, 0, 1.0), rotation=(0.5, 0.5, 0))
glass_plane = bpy.context.object
mat_glass = bpy.data.materials.new("GlassMat")
mat_glass.use_nodes = True
bsdf = mat_glass.node_tree.nodes.get("Principled BSDF")
bsdf.inputs["Transmission Weight"].default_value = 1.0
bsdf.inputs["Roughness"].default_value = 0.0
bsdf.inputs["IOR"].default_value = 1.5
glass_plane.data.materials.append(mat_glass)
glass_plane.visible_shadow = False # 关闭阴影

# 3. 光源（从正上方打下来）
bpy.ops.object.light_add(type='SPOT', location=(0, 0, 5))
light = bpy.context.object
light.data.energy = 2000
light.data.spot_size = 1.0

# 4. 相机（斜侧视，同时看到玻璃板和地面）
bpy.ops.object.camera_add(location=(3, -6, 3), rotation=(1.1, 0.0, 0.4))
sc.camera = bpy.context.object

sc.render.filepath = "/tmp/caustic_test.png"
bpy.ops.render.render(write_still=True)
print("[RENDER-TRACE] render finished", file=sys.stderr)
