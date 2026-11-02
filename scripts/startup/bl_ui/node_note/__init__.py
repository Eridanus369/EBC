import bpy
from . import draw_gpu
from . import operators
from . import ui
from . import node_properties
from .preferences import NodeNoteAddonPreferences
from .translations import translations_dict

addon_keymaps = []

classes = [
    ui.NODE_PT_node_note_gpu_panel,
    NodeNoteAddonPreferences,
]

def register():
    bpy.types.NODE_MT_context_menu.append(ui.draw_to_context_menu)
    bpy.types.NODE_PT_overlay.append(ui.draw_to_overlay_panel)

    kc = bpy.context.window_manager.keyconfigs.addon
    if kc:
        km = kc.keymaps.new(name='Node Editor', space_type='NODE_EDITOR')
        kmi = km.keymap_items.new("node.note_quick_edit", 'D', 'PRESS', ctrl=True)
        addon_keymaps.append((km, kmi))

    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.node_note_prefs = bpy.props.PointerProperty(type=NodeNoteAddonPreferences)

    def safe_init():
        try:
            node_properties.init_props()
            draw_gpu.register_draw_handler()
            bpy.app.translations.register(__package__, translations_dict)
            operators.register()
            for w in bpy.context.window_manager.windows:
                for a in w.screen.areas:
                    a.tag_redraw()
        except Exception as e:
            print('Node Note Init Error:', e)
            
    bpy.app.timers.register(safe_init, first_interval=0.5)

def unregister():
    bpy.types.NODE_PT_overlay.remove(ui.draw_to_overlay_panel)
    bpy.types.NODE_MT_context_menu.remove(ui.draw_to_context_menu)

    kc = bpy.context.window_manager.keyconfigs.addon
    if kc:
        for km, kmi in addon_keymaps:
            km.keymap_items.remove(kmi)
    addon_keymaps.clear()

    try:
        draw_gpu.unregister_draw_handler()
        operators.unregister()
        del bpy.types.Scene.node_note_prefs
        for cls in reversed(classes):
            bpy.utils.unregister_class(cls)
        node_properties.delete_props()
        bpy.app.translations.unregister(__package__)
    except Exception:
        pass
