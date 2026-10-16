import bpy, sys
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'
sc.cycles.device = 'GPU'
sc.cycles.samples = 16
sc.render.resolution_x = sc.render.resolution_y = 128
w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.2, 0.3, 0.5, 1.0)
bpy.ops.object.camera_add(location=(0, -4, 0), rotation=(1.5708, 0, 0))
sc.camera = bpy.context.object
bpy.ops.mesh.primitive_monkey_add()
bpy.ops.object.light_add(type='SUN')
sc.render.filepath = "/tmp/reg_cycles_gpu.png"
bpy.ops.render.render(write_still=True)
print("[T2] PASS Cycles GPU", file=sys.stderr)
