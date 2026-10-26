import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.render.resolution_x = sc.render.resolution_y = 128
sc.eevee.caustics.use_spectral_caustics = True
sc.eevee.caustics.photon_count = 50000
sc.eevee.caustics.caustic_resolution = 512
sc.eevee.taa_render_samples = 4

bpy.ops.object.camera_add(location=(0, -5, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.object.light_add(type='SUN')
bpy.ops.mesh.primitive_monkey_add()

sc.render.filepath = "/tmp/caustic_test.png"
bpy.ops.render.render(write_still=True)
print("[RENDER-TRACE] render finished", file=sys.stderr)
