import bpy
import inspect
import json
import os


_registered_custom_classes = []
_custom_game_nodes = None

_CUSTOM_NODE_FIELDS = ('idname', 'label', 'modname', 'ui_code', 'logic_code')


class CustomNodeReference:
    """Plain POD replacing the old add-on PropertyGroup.

    Custom node definitions are user content and are stored globally in the
    user configuration directory instead of in the user preferences DNA.
    """

    def __init__(self, idname='', label='', modname='', ui_code='', logic_code=''):
        self.idname = idname
        self.label = label
        self.modname = modname
        self.ui_code = ui_code
        self.logic_code = logic_code

    def to_dict(self):
        return {name: getattr(self, name) for name in _CUSTOM_NODE_FIELDS}


def _custom_nodes_path():
    directory = bpy.utils.user_resource('CONFIG', path='game_nodes', create=True)
    return os.path.join(directory, 'custom_nodes.json')


def _load_custom_game_nodes():
    path = _custom_nodes_path()
    try:
        with open(path, encoding='utf-8') as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return []
    if isinstance(data, dict):
        data = data.get('custom_game_nodes', [])
    nodes = []
    for entry in data:
        if not isinstance(entry, dict):
            continue
        nodes.append(
            CustomNodeReference(**{name: str(entry.get(name, '')) for name in _CUSTOM_NODE_FIELDS})
        )
    return nodes


def get_custom_game_nodes():
    global _custom_game_nodes
    if _custom_game_nodes is None:
        _custom_game_nodes = _load_custom_game_nodes()
    return _custom_game_nodes


def save_custom_game_nodes():
    nodes = get_custom_game_nodes()
    path = _custom_nodes_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temp_path = path + '.tmp'
    with open(temp_path, 'w', encoding='utf-8') as handle:
        json.dump({'custom_game_nodes': [node.to_dict() for node in nodes]}, handle, indent=2)
    os.replace(temp_path, path)


def custom_node(cls):
    nodes = get_custom_game_nodes()
    for node in list(_registered_custom_classes):
        if cls.bl_idname == node.bl_idname:
            bpy.utils.unregister_class(node)
            _registered_custom_classes.remove(node)
            for i, entry in enumerate(nodes):
                if entry.idname == cls.bl_idname:
                    nodes.pop(i)
                    save_custom_game_nodes()
                    break
    modname = cls.nl_module[1:] + '.py'
    if cls.bl_idname not in [n.idname for n in nodes]:
        logic_code = bpy.data.texts.get(modname, None)
        if logic_code is None:
            def error_log(self, context):
                self.layout.label(text=f'Text Block "{modname}" not found!', icon='CONSOLE')

            bpy.context.window_manager.popup_menu(error_log, title=f'Custom Logic Node could not register!', icon='INFO')

            print(f'Custom Logic Node could not register! Text Block "{modname}" not found!')
            return
        ui_code = getattr(bpy.context.space_data, 'text', None)
        if ui_code is None:
            ui_code = bpy.data.texts['__customnodetemp__']
        nodes.append(CustomNodeReference(
            idname=cls.bl_idname,
            label=cls.bl_label,
            modname=modname,
            ui_code=ui_code.as_string(),
            logic_code=logic_code.as_string(),
        ))
        save_custom_game_nodes()
    bpy.utils.register_class(cls)
    _registered_custom_classes.append(cls)
    return cls
