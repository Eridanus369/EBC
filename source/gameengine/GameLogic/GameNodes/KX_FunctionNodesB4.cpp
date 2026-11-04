/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2026 EBC.
 * All rights reserved.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file KX_FunctionNodesB4.cpp
 *  \ingroup gamelogic
 *
 *  B4.1 UI node family: canvas, layout, label, image, button, slider,
 *  painter order and the generic attribute get/set pair.
 *
 *  Port order follows the legacy editor declarations in
 *  `scripts/addons_core/bge_netlogic/editor/nodes/ui/*.py`; the editor table in
 *  `native_b4.py` repeats the same order, and `port_contract.py` compares
 *  both sides.
 *
 *  Nodes never touch Ketsji UI internals: all side effects go through
 *  #KX_UIAdapter, so the mock adapter sees exactly the same calls.
 */

#include "KX_FunctionNode.h"

#include <string>

#include "KX_GameAdapters.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}

namespace {

/* ------------------------------------------------------------------------- */
/* Attribute vocabulary shared by Set/GetUIWidgetAttr                        */
/* ------------------------------------------------------------------------- */

/* Index order must stay identical to the `widget_attr` enum in native_b4.py. */
const char *const kWidgetAttrKeys[] = {
    "show",
    "bg_color",
    "opacity",
    "pos",
    "size",
    "angle",
    "width",
    "height",
    "border_width",
    "border_color",
    "hover_color",
    "text",
    "font",
    "font_color",
    "font_size",
    "line_height",
    "text_halign",
    "text_valign",
    "wrap",
    "shadow",
    "shadow_offset",
    "shadow_color",
    "texture",
    "orientation",
    "spacing",
};

const char *WidgetAttrKey(int index)
{
  const int count = int(sizeof(kWidgetAttrKeys) / sizeof(kWidgetAttrKeys[0]));
  if (index < 0 || index >= count) {
    return "";
  }
  return kWidgetAttrKeys[index];
}

/* Legacy alignment enums are bottom-up; the renderer is top-down. */
int RendererValign(int legacy_valign)
{
  if (legacy_valign == 0) {
    return 2;
  }
  if (legacy_valign == 2) {
    return 0;
  }
  return 1;
}

std::string InString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

void SetAttr(KX_FunctionNode &node,
             KX_ExecutionContext &ctx,
             int handle,
             const char *key,
             uint32_t index)
{
  if (handle < 0) {
    return;
  }
  ctx.UI().SetAttribute(handle, key, node.In(ctx, index));
}

void SetAttrValue(KX_FunctionNode &node,
                  KX_ExecutionContext &ctx,
                  int handle,
                  const char *key,
                  const KX_RuntimeValue &value)
{
  (void)node;
  if (handle >= 0) {
    ctx.UI().SetAttribute(handle, key, value);
  }
}

/* ------------------------------------------------------------------------- */
/* Ports                                                                     */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kCreateCanvasInputs[] = {
    TP("Condition"),
};
const KX_FuncPort kCreateCanvasOutputs[] = {
    TP("Done"),
    FP("Canvas", KX_VALUE_INT),
};

const KX_FuncPort kCreateLayoutInputs[] = {
    TP("Condition"),
    FP("Parent", KX_VALUE_INT),
    FP("Relative Position", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Relative Size", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Angle", KX_VALUE_FLOAT),
    FP("Color", KX_VALUE_COLOR),
    FP("Border Width", KX_VALUE_INT),
    FP("Border Color", KX_VALUE_COLOR),
    FP("Inverted", KX_VALUE_BOOL),
    FP("Spacing", KX_VALUE_INT),
    FP("Rows", KX_VALUE_INT),
    FP("Columns", KX_VALUE_INT),
    FP("Starting Angle", KX_VALUE_FLOAT),
    FP("Radius", KX_VALUE_FLOAT),
};
const KX_FuncPort kCreateLayoutOutputs[] = {
    TP("Done"),
    FP("Layout", KX_VALUE_INT),
};

const KX_FuncPort kCreateLabelInputs[] = {
    TP("Condition"),
    FP("Parent", KX_VALUE_INT),
    FP("Relative Position", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Angle", KX_VALUE_FLOAT),
    FP("Text", KX_VALUE_STRING),
    FP("", KX_VALUE_STRING),
    FP("Font Size", KX_VALUE_INT),
    FP("Line Height", KX_VALUE_FLOAT),
    FP("Font Color", KX_VALUE_COLOR),
    FP("Use Shadow", KX_VALUE_BOOL),
    FP("Shadow Offset", KX_VALUE_VECTOR),
    FP("Shadow Color", KX_VALUE_COLOR),
};
const KX_FuncPort kCreateLabelOutputs[] = {
    TP("Done"),
    FP("Label", KX_VALUE_INT),
};

const KX_FuncPort kCreateImageInputs[] = {
    TP("Condition"),
    FP("Parent", KX_VALUE_INT),
    FP("Relative Position", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Relative Size", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Angle", KX_VALUE_FLOAT),
    FP("", KX_VALUE_STRING),
};
const KX_FuncPort kCreateImageOutputs[] = {
    TP("Done"),
    FP("Image", KX_VALUE_INT),
};

const KX_FuncPort kCreateButtonInputs[] = {
    TP("Condition"),
    FP("Parent", KX_VALUE_INT),
    FP("Relative Position", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Relative Size", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Angle", KX_VALUE_FLOAT),
    FP("Color", KX_VALUE_COLOR),
    FP("Hover Color", KX_VALUE_COLOR),
    FP("Border Width", KX_VALUE_INT),
    FP("Border Color", KX_VALUE_COLOR),
    FP("Text", KX_VALUE_STRING),
    FP("Text Position", KX_VALUE_VECTOR),
    FP("", KX_VALUE_STRING),
    FP("Font Size", KX_VALUE_INT),
    FP("Line Height", KX_VALUE_FLOAT),
    FP("Font Color", KX_VALUE_COLOR),
};
const KX_FuncPort kCreateButtonOutputs[] = {
    TP("Done"),
    FP("Button", KX_VALUE_INT),
    TP("On Click"),
    TP("On Hover"),
    TP("On Release"),
};

const KX_FuncPort kCreateSliderInputs[] = {
    TP("Condition"),
    FP("Parent", KX_VALUE_INT),
    FP("Relative Position", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Relative Size", KX_VALUE_BOOL),
    FP("", KX_VALUE_VECTOR),
    FP("Angle", KX_VALUE_FLOAT),
    FP("Bar Width", KX_VALUE_FLOAT),
    FP("Bar Color", KX_VALUE_COLOR),
    FP("Bar Hover Color", KX_VALUE_COLOR),
    FP("Bar Texture", KX_VALUE_STRING),
    FP("Border Width", KX_VALUE_INT),
    FP("Border Color", KX_VALUE_COLOR),
    FP("Knob Size", KX_VALUE_FLOAT),
    FP("Knob Color", KX_VALUE_COLOR),
    FP("Knob Hover Color", KX_VALUE_COLOR),
    FP("Knob Texture", KX_VALUE_STRING),
    FP("Initial Value", KX_VALUE_FLOAT),
    FP("Steps", KX_VALUE_INT),
    FP("Bar Control", KX_VALUE_BOOL),
};
const KX_FuncPort kCreateSliderOutputs[] = {
    TP("Done"),
    FP("Slider", KX_VALUE_INT),
    FP("Slider Value", KX_VALUE_FLOAT),
    FP("Knob Position", KX_VALUE_VECTOR),
};

const KX_FuncPort kMoveWidgetInputs[] = {
    TP("Condition"),
    FP("Widget", KX_VALUE_INT),
};
const KX_FuncPort kMoveWidgetOutputs[] = {
    TP("Done"),
};

const KX_FuncPort kSetWidgetAttrInputs[] = {
    TP("Condition"),
    FP("Widget", KX_VALUE_INT),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kSetWidgetAttrOutputs[] = {
    TP("Done"),
};

const KX_FuncPort kGetWidgetAttrInputs[] = {
    FP("Widget", KX_VALUE_INT),
};
const KX_FuncPort kGetWidgetAttrOutputs[] = {
    FP("Visibility", KX_VALUE_BOOL),
    FP("Color", KX_VALUE_COLOR),
    FP("Alpha", KX_VALUE_FLOAT),
    FP("Vec2", KX_VALUE_VECTOR),
    FP("String", KX_VALUE_STRING),
    FP("Int", KX_VALUE_INT),
    FP("Float", KX_VALUE_FLOAT),
    FP("Font", KX_VALUE_STRING),
    FP("Image", KX_VALUE_STRING),
};

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

struct KX_UIHandleState {
  int handle = -1;
};

struct KX_UISliderState {
  int handle = -1;
  float value = 0.0f;
};

/* ------------------------------------------------------------------------- */
/* Executors                                                                 */
/* ------------------------------------------------------------------------- */

void FuncCreateCanvas(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_UIHandleState *state = node.StateBlock<KX_UIHandleState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && state->handle < 0) {
    state->handle = ctx.UI().CreateCanvas(std::string());
    if (state->handle >= 0) {
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "scale_mode",
                   KX_RuntimeValue::Int(node.ConfigInt("scale_mode", 0)));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "design_size",
                   KX_RuntimeValue::Vector(node.ConfigInt("design_w", 1920),
                                           node.ConfigInt("design_h", 1080),
                                           0.0f));
    }
  }
  node.OutTrigger(ctx, 0, trigger);
  node.OutInt(ctx, 1, state != nullptr ? state->handle : -1);
}

void FuncCreateLayout(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_UIHandleState *state = node.StateBlock<KX_UIHandleState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && state->handle < 0) {
    const int parent = node.InInt(ctx, 1);
    state->handle = ctx.UI().CreateLayout(parent, node.ConfigInt("layout_type", 0));
    if (state->handle >= 0) {
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "halign",
                   KX_RuntimeValue::Int(node.ConfigInt("halign", 0)));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "valign",
                   KX_RuntimeValue::Int(RendererValign(node.ConfigInt("valign", 0))));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "layout_type",
                   KX_RuntimeValue::Int(node.ConfigInt("layout_type", 0)));
      SetAttr(node, ctx, state->handle, "rel_pos", 2);
      float pos[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 3, pos);
      SetAttrValue(node, ctx, state->handle, "position", KX_RuntimeValue::Vector(pos[0], pos[1], 0.0f));
      SetAttr(node, ctx, state->handle, "rel_size", 4);
      float size[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 5, size);
      SetAttrValue(node, ctx, state->handle, "size", KX_RuntimeValue::Vector(size[0], size[1], 0.0f));
      SetAttr(node, ctx, state->handle, "angle", 6);
      SetAttr(node, ctx, state->handle, "color", 7);
      SetAttr(node, ctx, state->handle, "border_width", 8);
      SetAttr(node, ctx, state->handle, "border_color", 9);
      SetAttr(node, ctx, state->handle, "inverted", 10);
      SetAttr(node, ctx, state->handle, "spacing", 11);
      SetAttr(node, ctx, state->handle, "rows", 12);
      SetAttr(node, ctx, state->handle, "cols", 13);
      SetAttr(node, ctx, state->handle, "starting_angle", 14);
      SetAttr(node, ctx, state->handle, "radius", 15);
    }
  }
  node.OutTrigger(ctx, 0, trigger);
  node.OutInt(ctx, 1, state != nullptr ? state->handle : -1);
}

void FuncCreateLabel(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_UIHandleState *state = node.StateBlock<KX_UIHandleState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && state->handle < 0) {
    const int parent = node.InInt(ctx, 1);
    const std::string text = InString(node, ctx, 5);
    state->handle = ctx.UI().CreateLabel(parent, text);
    if (state->handle >= 0) {
      SetAttr(node, ctx, state->handle, "rel_pos", 2);
      float pos[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 3, pos);
      SetAttrValue(node, ctx, state->handle, "position", KX_RuntimeValue::Vector(pos[0], pos[1], 0.0f));
      SetAttr(node, ctx, state->handle, "angle", 4);
      SetAttr(node, ctx, state->handle, "text", 5);
      const std::string port_font = InString(node, ctx, 6);
      const std::string font = port_font.empty() ? node.ConfigString("font") : port_font;
      if (!font.empty()) {
        SetAttrValue(node, ctx, state->handle, "font", KX_RuntimeValue::String(font.c_str()));
      }
      SetAttr(node, ctx, state->handle, "font_size", 7);
      SetAttr(node, ctx, state->handle, "line_height", 8);
      SetAttr(node, ctx, state->handle, "font_color", 9);
      SetAttr(node, ctx, state->handle, "shadow", 10);
      SetAttr(node, ctx, state->handle, "shadow_offset", 11);
      SetAttr(node, ctx, state->handle, "shadow_color", 12);
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "halign",
                   KX_RuntimeValue::Int(node.ConfigInt("halign", 0)));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "valign",
                   KX_RuntimeValue::Int(RendererValign(node.ConfigInt("valign", 0))));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "text_halign",
                   KX_RuntimeValue::Int(node.ConfigInt("text_halign", 0)));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "text_valign",
                   KX_RuntimeValue::Int(RendererValign(node.ConfigInt("text_valign", 0))));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "wrap",
                   KX_RuntimeValue::Bool(node.ConfigBool("wrap", false)));
    }
  }
  node.OutTrigger(ctx, 0, trigger);
  node.OutInt(ctx, 1, state != nullptr ? state->handle : -1);
}

void FuncCreateImage(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_UIHandleState *state = node.StateBlock<KX_UIHandleState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && state->handle < 0) {
    const int parent = node.InInt(ctx, 1);
    std::string texture = InString(node, ctx, 7);
    if (texture.empty()) {
      texture = node.ConfigString("texture");
    }
    state->handle = ctx.UI().CreateImage(parent, texture);
    if (state->handle >= 0) {
      SetAttr(node, ctx, state->handle, "rel_pos", 2);
      float pos[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 3, pos);
      SetAttrValue(node, ctx, state->handle, "position", KX_RuntimeValue::Vector(pos[0], pos[1], 0.0f));
      SetAttr(node, ctx, state->handle, "rel_size", 4);
      float size[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 5, size);
      SetAttrValue(node, ctx, state->handle, "size", KX_RuntimeValue::Vector(size[0], size[1], 0.0f));
      SetAttr(node, ctx, state->handle, "angle", 6);
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "texture",
                   KX_RuntimeValue::String(texture.c_str()));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "tint",
                   KX_RuntimeValue::Color(node.ConfigFloat("tint_r", 1.0f),
                                          node.ConfigFloat("tint_g", 1.0f),
                                          node.ConfigFloat("tint_b", 1.0f),
                                          node.ConfigFloat("tint_a", 1.0f)));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "preserve_aspect",
                   KX_RuntimeValue::Bool(node.ConfigBool("preserve_aspect", false)));
    }
  }
  node.OutTrigger(ctx, 0, trigger);
  node.OutInt(ctx, 1, state != nullptr ? state->handle : -1);
}

void FuncCreateButton(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_UIHandleState *state = node.StateBlock<KX_UIHandleState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && state->handle < 0) {
    const int parent = node.InInt(ctx, 1);
    state->handle = ctx.UI().CreateButton(parent, InString(node, ctx, 11));
    if (state->handle >= 0) {
      SetAttr(node, ctx, state->handle, "rel_pos", 2);
      float pos[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 3, pos);
      SetAttrValue(node, ctx, state->handle, "position", KX_RuntimeValue::Vector(pos[0], pos[1], 0.0f));
      SetAttr(node, ctx, state->handle, "rel_size", 4);
      float size[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 5, size);
      SetAttrValue(node, ctx, state->handle, "size", KX_RuntimeValue::Vector(size[0], size[1], 0.0f));
      SetAttr(node, ctx, state->handle, "angle", 6);
      SetAttr(node, ctx, state->handle, "color", 7);
      SetAttr(node, ctx, state->handle, "hover_color", 8);
      SetAttr(node, ctx, state->handle, "border_width", 9);
      SetAttr(node, ctx, state->handle, "border_color", 10);
      SetAttr(node, ctx, state->handle, "text", 11);
      SetAttr(node, ctx, state->handle, "text_pos", 12);
      const std::string font = InString(node, ctx, 13);
      if (!font.empty()) {
        SetAttrValue(node, ctx, state->handle, "font", KX_RuntimeValue::String(font.c_str()));
      }
      else if (!node.ConfigString("font").empty()) {
        SetAttrValue(node,
                     ctx,
                     state->handle,
                     "font",
                     KX_RuntimeValue::String(node.ConfigString("font").c_str()));
      }
      SetAttr(node, ctx, state->handle, "font_size", 14);
      SetAttr(node, ctx, state->handle, "line_height", 15);
      SetAttr(node, ctx, state->handle, "font_color", 16);
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "halign",
                   KX_RuntimeValue::Int(node.ConfigInt("halign", 0)));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "valign",
                   KX_RuntimeValue::Int(RendererValign(node.ConfigInt("valign", 0))));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "text_halign",
                   KX_RuntimeValue::Int(node.ConfigInt("text_halign", 0)));
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "text_valign",
                   KX_RuntimeValue::Int(RendererValign(node.ConfigInt("text_valign", 0))));
    }
  }

  node.OutTrigger(ctx, 0, trigger);
  node.OutInt(ctx, 1, state != nullptr ? state->handle : -1);
  const int handle = state != nullptr ? state->handle : -1;
  node.OutTrigger(ctx, 2, handle >= 0 && ctx.UI().Event(handle, KX_UIEvent::Click));
  node.OutTrigger(ctx, 3, handle >= 0 && ctx.UI().Event(handle, KX_UIEvent::Hover));
  node.OutTrigger(ctx, 4, handle >= 0 && ctx.UI().Event(handle, KX_UIEvent::Release));
}

void FuncCreateSlider(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_UISliderState *state = node.StateBlock<KX_UISliderState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && state->handle < 0) {
    const int parent = node.InInt(ctx, 1);
    const float initial = node.InFloat(ctx, 17);
    state->handle = ctx.UI().CreateSlider(parent, initial, node.InInt(ctx, 18), node.InBool(ctx, 19));
    state->value = initial;
    if (state->handle >= 0) {
      SetAttr(node, ctx, state->handle, "rel_pos", 2);
      float pos[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 3, pos);
      SetAttrValue(node, ctx, state->handle, "position", KX_RuntimeValue::Vector(pos[0], pos[1], 0.0f));
      SetAttr(node, ctx, state->handle, "rel_size", 4);
      float size[3] = {0.0f, 0.0f, 0.0f};
      node.InVector(ctx, 5, size);
      SetAttrValue(node, ctx, state->handle, "size", KX_RuntimeValue::Vector(size[0], size[1], 0.0f));
      SetAttr(node, ctx, state->handle, "angle", 6);
      SetAttr(node, ctx, state->handle, "bar_width", 7);
      SetAttr(node, ctx, state->handle, "bar_color", 8);
      SetAttr(node, ctx, state->handle, "bar_hover_color", 9);
      SetAttr(node, ctx, state->handle, "bar_texture", 10);
      SetAttr(node, ctx, state->handle, "border_width", 11);
      SetAttr(node, ctx, state->handle, "border_color", 12);
      SetAttr(node, ctx, state->handle, "knob_size", 13);
      SetAttr(node, ctx, state->handle, "knob_color", 14);
      SetAttr(node, ctx, state->handle, "knob_hover_color", 15);
      SetAttr(node, ctx, state->handle, "knob_texture", 16);
      SetAttrValue(node,
                   ctx,
                   state->handle,
                   "orientation",
                   KX_RuntimeValue::Int(node.ConfigInt("orientation", 0)));
    }
  }

  node.OutTrigger(ctx, 0, trigger);
  const int handle = state != nullptr ? state->handle : -1;
  node.OutInt(ctx, 1, handle);
  float value = state != nullptr ? state->value : 0.0f;
  if (handle >= 0) {
    value = ctx.UI().SliderValue(handle);
    if (state != nullptr) {
      state->value = value;
    }
  }
  node.OutFloat(ctx, 2, value);
  const bool vertical = node.ConfigInt("orientation", 0) != 0;
  node.OutVector(ctx, 3, vertical ? 0.5f : value, vertical ? value : 0.5f, 0.0f);
  node.OutTrigger(ctx, 0, trigger || (handle >= 0 && ctx.UI().Event(handle, KX_UIEvent::Changed)));
}

void FuncMoveWidget(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger) {
    const int widget = node.InInt(ctx, 1);
    if (widget >= 0) {
      ctx.UI().MoveElement(widget, node.ConfigInt("mode", 0));
    }
  }
  node.OutTrigger(ctx, 0, trigger);
}

void FuncSetWidgetAttr(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger) {
    const int widget = node.InInt(ctx, 1);
    const char *key = WidgetAttrKey(node.ConfigInt("widget_attr", 0));
    if (widget >= 0 && key[0] != '\0') {
      ctx.UI().SetAttribute(widget, key, node.In(ctx, 2));
    }
  }
  node.OutTrigger(ctx, 0, trigger);
}

void FuncGetWidgetAttr(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int widget = node.InInt(ctx, 0);
  const char *key = WidgetAttrKey(node.ConfigInt("widget_attr", 0));
  KX_RuntimeValue value;
  if (widget >= 0 && key[0] != '\0' && ctx.UI().GetAttribute(widget, key, value)) {
    if (key == std::string("show") || key == std::string("wrap") ||
        key == std::string("shadow"))
    {
      node.OutBool(ctx, 0, value.AsBool());
    }
    else if (key == std::string("bg_color") || key == std::string("border_color") ||
             key == std::string("hover_color") || key == std::string("font_color") ||
             key == std::string("shadow_color"))
    {
      float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      value.ColorTo(color);
      node.OutColor(ctx, 1, color[0], color[1], color[2], color[3]);
      node.OutFloat(ctx, 2, color[3]);
    }
    else if (key == std::string("pos") || key == std::string("size") ||
             key == std::string("shadow_offset"))
    {
      node.OutVector(ctx, 3, value.AsFloat(), value.data.vec[1], 0.0f);
    }
    else if (key == std::string("text"))
    {
      node.OutString(ctx, 4, value.AsString() != nullptr ? std::string(value.AsString())
                                                         : std::string());
    }
    else if (key == std::string("font"))
    {
      node.OutString(ctx, 7, value.AsString() != nullptr ? std::string(value.AsString())
                                                         : std::string());
      node.OutString(ctx, 4, value.AsString() != nullptr ? std::string(value.AsString())
                                                         : std::string());
    }
    else if (key == std::string("texture"))
    {
      node.OutString(ctx, 8, value.AsString() != nullptr ? std::string(value.AsString())
                                                         : std::string());
    }
    else if (key == std::string("text_halign") || key == std::string("text_valign") ||
             key == std::string("orientation") || key == std::string("spacing"))
    {
      node.OutInt(ctx, 5, value.AsInt());
    }
    else
    {
      node.OutFloat(ctx, 6, value.AsFloat());
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Table                                                                     */
/* ------------------------------------------------------------------------- */

#define PORT_COUNT(array) static_cast<uint32_t>(sizeof(array) / sizeof(array[0]))

#define B4_STATE(id, ins, outs, exec, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, false, false}

#define B4_VALUE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false}

#define B4_DYNAMIC(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false, nullptr, nullptr, nullptr, true}

const KX_FunctionSpec kB4Specs[] = {
    B4_STATE("CreateUICanvas", kCreateCanvasInputs, kCreateCanvasOutputs, FuncCreateCanvas,
             KX_UIHandleState),
    B4_STATE("CreateUILayout", kCreateLayoutInputs, kCreateLayoutOutputs, FuncCreateLayout,
             KX_UIHandleState),
    B4_STATE("CreateUILabel", kCreateLabelInputs, kCreateLabelOutputs, FuncCreateLabel,
             KX_UIHandleState),
    B4_STATE("CreateUIImage", kCreateImageInputs, kCreateImageOutputs, FuncCreateImage,
             KX_UIHandleState),
    B4_STATE("CreateUIButton", kCreateButtonInputs, kCreateButtonOutputs, FuncCreateButton,
             KX_UIHandleState),
    B4_STATE("CreateUISliderWidget", kCreateSliderInputs, kCreateSliderOutputs,
             FuncCreateSlider, KX_UISliderState),
    B4_STATE("MoveWidget", kMoveWidgetInputs, kMoveWidgetOutputs, FuncMoveWidget,
             KX_UIHandleState),
    B4_DYNAMIC("SetUIWidgetAttr", kSetWidgetAttrInputs, kSetWidgetAttrOutputs,
               FuncSetWidgetAttr),
    B4_VALUE("GetUIWidgetAttr", kGetWidgetAttrInputs, kGetWidgetAttrOutputs,
             FuncGetWidgetAttr),
};

#undef PORT_COUNT

}  // namespace

namespace {

KX_NodeSignature MakeB4Signature(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    if (spec.inputs[i].trigger) {
      port.kind = IRPortKind::Trigger;
      port.value_type = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      port.value_type = spec.inputs[i].type;
    }
    signature.inputs.push_back(port);
  }
  for (uint32_t i = 0; i < spec.output_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.outputs[i].name;
    if (spec.outputs[i].trigger) {
      port.kind = IRPortKind::Trigger;
      port.value_type = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      port.value_type = spec.outputs[i].type;
    }
    signature.outputs.push_back(port);
  }
  signature.state_size = spec.state_size;
  signature.state_alignment = spec.state_alignment;
  signature.needs_tick = spec.needs_tick || spec.tick != nullptr;
  signature.breaks_trigger_cycles = spec.breaks_trigger_cycles;
  return signature;
}

}  // namespace

void RegisterB4UIFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB4Specs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB4UI_") + spec.type_id,
        MakeB4Signature(*entry),
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
