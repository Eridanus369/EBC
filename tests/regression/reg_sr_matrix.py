import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_EEVEE'
sc.view_settings.view_transform = 'Standard'
sc.eevee.dlss5_mode = 'DLSS_SR'
w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.1, 0.3, 0.5, 1.0)
bpy.ops.object.camera_add(location=(0, -4, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.mesh.primitive_monkey_add()
bpy.ops.object.light_add(type='SUN')

for res, pct, samples in [(128,50,1),(128,100,4),(256,100,16),(512,100,4)]:
    sc.render.resolution_x = sc.render.resolution_y = res
    sc.render.resolution_percentage = pct
    sc.eevee.taa_render_samples = samples
    sc.render.filepath = f"/tmp/reg_sr_{res}_{pct}_{samples}"
    bpy.ops.render.render(write_still=True)
    print(f"[T6] {res}x{res}@{pct}% samples={samples} ok", file=sys.stderr)
print("[T6] PASS", file=sys.stderr)
