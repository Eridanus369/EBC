import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 256
sc.eevee.caustics.use_spectral_caustics = True
sc.eevee.caustics.photon_count = 500000
sc.eevee.caustics.caustic_resolution = 512
sc.eevee.taa_render_samples = 32

# 1. 地面
bpy.ops.mesh.primitive_plane_add(size=10, location=(0, 0, -2.5))
mat_plane = bpy.data.materials.new("PlaneMat")
mat_plane.use_nodes = True
bpy.context.object.data.materials.append(mat_plane)

# 2. 真实玻璃球
bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, location=(0, 0, 0))
sphere = bpy.context.object
bpy.ops.object.shade_smooth()
mat_glass = bpy.data.materials.new("GlassMat")
mat_glass.use_nodes = True
bsdf = mat_glass.node_tree.nodes.get("Principled BSDF")
bsdf.inputs["Transmission Weight"].default_value = 1.0
bsdf.inputs["Roughness"].default_value = 0.0
bsdf.inputs["IOR"].default_value = 1.5
sphere.data.materials.append(mat_glass)
sphere.visible_shadow = False

# 3. 光源
bpy.ops.object.light_add(type='SPOT', location=(0, 0, 5))
light = bpy.context.object
light.data.energy = 3000
light.data.spot_size = 1.2

# 4. 相机 (俯视)
bpy.ops.object.camera_add(location=(4, -6, 4), rotation=(0.9, 0.0, 0.6))
sc.camera = bpy.context.object

sc.render.filepath = "/tmp/caustic_test.png"
bpy.ops.render.render(write_still=True)
print("[RENDER-TRACE] render finished", file=sys.stderr)
