from bpy.types import Operator
from ..utilities import error, preferences
import bpy
import os


_operators = []


def operator(obj: Operator) -> Operator:
    _operators.append(obj)
    return obj


def _enum_components(self, context):
    select_text = context.scene.componenthelper
    items = []
    for line in select_text.lines:
        if 'class ' in line.body:
            cname = line.body.split('(')[0]
            cname = cname.split('class ')[-1]
            cname = cname.replace(':', '')
            cname = cname.replace(' ', '')
            items.append((cname, cname, cname))
    return items


def reload_texts():
    for t in bpy.data.texts:
        if t.filepath:
            path = (
                os.path.join(bpy.path.abspath('//'), t.filepath[2:])
                if t.filepath.startswith('//')
                else t.filepath
            )
            with open(path) as f:
                t.clear()
                t.write(f.read())
