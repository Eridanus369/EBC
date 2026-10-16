# EBC 5.3 regression scripts

Headless smoke tests for the NPR work and the two DLSS paths. Each
script prints a `[Tn] PASS` line on success and exits non-zero on a
Python exception.

## Run

    cd ~/Code/EBC/EBC5.3
    unset LD_LIBRARY_PATH
    ./build_linux/bin/blender --background --factory-startup \
        --python tests/regression/<script>.py

GPU tests (DLSS SR, DLSS NR) also need `--gpu-backend vulkan`. The NR
tests additionally need `opendlss-server` running; see
`tools/opendlss-server/README.md`.

## What each covers

- `reg_save_load.py` (T1): scene.eevee DLSS NR settings and an NPR
  material survive a .blend save + reload.
- `reg_cycles_gpu.py` (T2): Cycles GPU render.
- `reg_sr_npr.py` (T3): DLSS SR and PrincipledNPR together.
- `reg_nr_npr.py` (T4): DLSS NR and PrincipledNPR together. Needs the
  helper.
- `reg_viewport.py` (T5): opengl viewport render. Skipped in
  `--background`, only useful in a real GUI session.
- `reg_sr_matrix.py` (T6): SR at four resolution / sample combinations.
- `reg_filter.py` (T7): Filter Graph assignment plus a material that
  instantiates PrincipledNPR, NPRRim and NPRSurfaceDiffusion.
- `reg_bake.py` (T8): `bpy.ops.object.bake` on an EEVEE scene.
- `reg_all_nodes.py` (T9): instantiates 19 NPR nodes, saves the file
  and checks they all come back after reload.
- `reg_multiframe.py` (T10): 5-frame animation with DLSS SR.
- `reg_nr_anim.py` (T11): 3-frame animation with DLSS NR. Needs the
  helper.
- `reg_light_shader.py` (T12): light shader node tree end to end.

## Known non-issues

- `ValueError: register_class(...): already registered as a subclass
  'LogicNodeTree'` is emitted by the bge_netlogic addon on every
  background start and is harmless.
- `Error: Not freed memory blocks: N` at exit is Blender's leak report
  and does not indicate a regression in these paths.
- `NGX Updater reported an error: ... Unable to download config files`
  is the NVIDIA NGX model server being empty on Linux; it does not
  affect DLSS SR once the SDK libs are in place.
