import bpy
import traceback

LOG_PATH = '/tmp/b45_gui_run.log'
log_file = open(LOG_PATH, 'w')


def log(*args):
    print(*args)
    print(*args, file=log_file)
    log_file.flush()


state = {'step': 0, 'tree': None, 'area': None, 'win': None, 'added': 0}


def setup():
    from bge_netlogic.utilities import preferences
    from bge_netlogic.editor.nodes.node import fit_enum_width, _enum_needed_width

    win = bpy.context.window_manager.windows[0]
    state['win'] = win
    try:
        with bpy.context.temp_override(window=win, screen=win.screen):
            bpy.ops.wm.window_fullscreen_toggle()
        log('FULLSCREEN', win.width, win.height)
    except Exception:
        log('FULLSCREEN_FAIL', traceback.format_exc())
    screen = win.screen
    area = next((a for a in screen.areas if a.type == 'VIEW_3D'), screen.areas[0])
    state['area'] = area
    area.type = 'NODE_EDITOR'
    space = area.spaces[0]
    space.show_region_ui = True

    tree = bpy.data.node_groups.new('B45 GUI Tree', 'BGELogicTree')
    state['tree'] = tree
    button = tree.nodes.new('GameNodeCreateUIButton')
    button.location = (0, 0)
    readfile = tree.nodes.new('GameNodeReadFile')
    readfile.location = (420, 0)
    for node in tree.nodes:
        node.select = False
    button.select = True
    tree.nodes.active = button
    try:
        space.node_tree = tree
        space.tree_type = 'BGELogicTree'
    except Exception:
        log('SET_NODE_TREE_FAIL', traceback.format_exc())
    area.tag_redraw()
    log('SPACE node_tree_set=', getattr(space, 'node_tree', None) is tree,
        'tree_type=', getattr(space, 'tree_type', ''),
        'edit_tree=', getattr(space, 'edit_tree', None) is tree)

    prefs = preferences()
    for key in ('trigger', 'input', 'object', 'physics', 'logic', 'value',
                'uiscene', 'utility'):
        setattr(prefs, f'palette_{key}_open', False)
    collapsed = getattr(prefs, 'palette_trigger_open')
    log('PERSIST collapse palette_trigger_open=', collapsed)
    setattr(prefs, 'palette_trigger_open', True)
    log('PERSIST restore palette_trigger_open=', getattr(prefs, 'palette_trigger_open'))

    # Palette entries insert through node.add_node (the same path as a click in
    # the sidebar); exercise it for one UI and one File node.
    region = next(r for r in area.regions if r.type == 'WINDOW')
    before = len(tree.nodes)
    for bl_idname in ('GameNodeCreateUISliderWidget', 'GameNodeJSONParse'):
        try:
            with bpy.context.temp_override(window=win, screen=screen, area=area,
                                           region=region, space_data=space):
                bpy.ops.node.add_node(type=bl_idname, use_transform=False)
            state['added'] += 1
        except Exception:
            log('ADD_NODE_FAIL', bl_idname, traceback.format_exc())
    log('ADD_NODE added=', state['added'], 'nodes_before=', before,
        'nodes_after=', len(tree.nodes))

    # fit_enum_width regression on a node with long enum entries.
    target = tree.nodes.new('GameNodeSetUIWidgetAttr')
    target.location = (0, -300)
    needed = _enum_needed_width(type(target))
    before_width = target.width
    fit_enum_width(target)
    log('FIT_ENUM needed=', round(needed, 1), 'width_before=', round(before_width, 1),
        'width_now=', round(target.width, 1))
    legacy = tree.nodes.new('NLObjectAttributeParameterNode')
    legacy.location = (700, -300)
    legacy_needed = _enum_needed_width(type(legacy))
    legacy_before = legacy.width
    fit_enum_width(legacy)
    state['legacy'] = legacy
    log('FIT_ENUM_LEGACY needed=', round(legacy_needed, 1),
        'width_before=', round(legacy_before, 1), 'width_now=', round(legacy.width, 1))
    tree.nodes.active = button
    area.tag_redraw()


def write_inspector_log(node_name):
    from bge_netlogic.ui.native_inspector import _tree_ir, audit_metadata

    tree = state['tree']
    ir, warnings = _tree_ir(tree)
    target = None
    for node in ir['nodes']:
        if node['debug_name'] == node_name:
            target = node
            break
    audit = audit_metadata()
    lines = [
        'INSPECTOR GUI AUDIT',
        f'missing={audit["missing"]} unknown={audit["unknown"]} '
        f'port_mismatch={audit["port_mismatch"]} '
        f'config_mismatch={audit["config_mismatch"]}',
        f'checked={len(audit["checked"])}',
        f'SELECTED node={node_name}',
        f'type_id={target["type_id"]}',
        f'config={target["config"]}',
        'INPUTS',
    ]
    for index, port in enumerate(target['inputs']):
        lines.append(
            f'  [{index}] name={port.get("name")!r} kind={port.get("kind")} '
            f'value_tag={port.get("value_tag")} tag={port.get("tag")}')
    lines.append('OUTPUTS')
    for index, port in enumerate(target['outputs']):
        lines.append(
            f'  [{index}] name={port.get("name")!r} kind={port.get("kind")} '
            f'value_tag={port.get("value_tag")} tag={port.get("tag")}')
    lines.append(f'WARNINGS {warnings}')
    with open('/tmp/b45_inspector.log', 'w') as handle:
        handle.write('\n'.join(lines) + '\n')
    log('INSPECTOR_LOG', len(target['inputs']), 'inputs', len(target['outputs']),
        'outputs', target['config'])


def activate_category():
    try:
        bpy.context.preferences.view.ui_scale = 0.6
        log('UI_SCALE', bpy.context.preferences.view.ui_scale)
    except Exception:
        log('UI_SCALE_FAIL', traceback.format_exc())
    area = state['area']
    ui_regions = [r for r in area.regions if r.type == 'UI']
    log('UI_REGIONS', len(ui_regions))
    for region in ui_regions:
        log('UI_REGION_CATEGORIES', getattr(region, 'active_panel_category', None))
        region.active_panel_category = 'Game Nodes'
        log('UI_REGION_ACTIVE', getattr(region, 'active_panel_category', None))
    region = next(r for r in area.regions if r.type == 'WINDOW')
    win = state['win']
    try:
        with bpy.context.temp_override(window=win, screen=win.screen, area=area,
                                       region=region):
            bpy.ops.node.view_all()
    except Exception:
        log('VIEW_ALL_FAIL', traceback.format_exc())
    area.tag_redraw()


def screenshot(path):
    win = state['win']
    area = state['area']
    with bpy.context.temp_override(window=win, screen=win.screen, area=area):
        bpy.ops.screen.screenshot_area(filepath=path)
    log('SCREENSHOT', path)


def tick():
    state['step'] += 1
    step = state['step']
    try:
        if step == 1:
            setup()
        elif step == 2:
            activate_category()
        elif step == 3:
            button = state['tree'].nodes.active
            log('WIDTH_AFTER_TIMER', button.name, round(button.width, 1),
                'legacy=', round(state['legacy'].width, 1))
            write_inspector_log(button.name)
            screenshot('/tmp/b45_inspector_gui.png')
        elif step == 4:
            state['tree'].nodes.active = None
            for node in state['tree'].nodes:
                node.select = False
            state['area'].tag_redraw()
        elif step == 5:
            screenshot('/tmp/b45_palette_gui.png')
            log('DONE')
            log_file.close()
            bpy.ops.wm.quit_blender()
            return None
    except Exception:
        log('TICK_FAIL', step, traceback.format_exc())
        try:
            log_file.close()
            bpy.ops.wm.quit_blender()
        except Exception:
            pass
        return None
    return 0.4


bpy.app.timers.register(tick, first_interval=0.3)
