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

/** \file KX_FunctionNodesB6UI.cpp
 *  \ingroup gamelogic
 *
 *  B6.4 advanced UI nodes: scroll view, list view, tab view, property
 *  animation, screen anchors and a screen stack.
 *
 *  Everything is created, queried and updated through the existing
 *  KX_UIAdapter. List virtualization is the only new renderer capability: the
 *  node tags items `virtualized`, and KX_UIRenderer skips layout/draw for items
 *  fully outside their clipping ancestor. Selection is computed from
 *  item_height + scroll offset in both modes, so hit results and OnItemClick
 *  indices are identical whether or not virtualization is on.
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"
#include "KX_UIElement.h"
#include "KX_ValuePool.h"

#define B6UI_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}
#define B6UI_COUNTS(array) static_cast<uint32_t>(B6UI_ARRAY_LEN(array))
#define B6UI_ACTION(id, ins, outs, exec, tick, state_type) \
  { id, ins, B6UI_COUNTS(ins), outs, B6UI_COUNTS(outs), exec, nullptr, tick, \
    static_cast<uint32_t>(sizeof(state_type)), \
    static_cast<uint32_t>(alignof(state_type)), false, tick != nullptr, false }
/* Ports also come from the IR (`ports_from_ir`) for the dynamic value node. */
#define B6UI_ACTION_IR(id, ins, outs, exec, tick, state_type) \
  { id, ins, B6UI_COUNTS(ins), outs, B6UI_COUNTS(outs), exec, nullptr, tick, \
    static_cast<uint32_t>(sizeof(state_type)), \
    static_cast<uint32_t>(alignof(state_type)), false, tick != nullptr, false, \
    nullptr, nullptr, nullptr, true }

namespace {

const int kMaxListItems = 64;
const int kMaxScreens = 8;
const int kScreenNameLength = 64;
const float kScrollbarWidth = 12.0f;
const float kDefaultItemHeight = 24.0f;

const char *B6UIInString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue &value = node.In(ctx, index);
  return value.AsString();
}

bool B6UIReadString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index,
                    std::string *out)
{
  const char *text = B6UIInString(node, ctx, index);
  if (text == nullptr) {
    return false;
  }
  *out = text;
  return true;
}

void B6UISetVector(KX_FunctionNode &node, KX_ExecutionContext &ctx, int handle,
                   const char *key, float x, float y)
{
  if (handle < 0) {
    return;
  }
  ctx.UI().SetAttribute(handle, key, KX_RuntimeValue::Vector(x, y, 0.0f));
}

bool B6UIGetSize(KX_FunctionNode &node, KX_ExecutionContext &ctx, int handle,
                 float out[2])
{
  if (handle < 0) {
    return false;
  }
  KX_RuntimeValue value;
  if (!ctx.UI().GetAttribute(handle, "size", value)) {
    return false;
  }
  float vector[3];
  value.VectorTo(vector);
  out[0] = std::fabs(vector[0]);
  out[1] = std::fabs(vector[1]);
  return true;
}

/** Create the overflow slider for one axis and park it at the edge of the view. */
int B6UICreateScrollbar(KX_FunctionNode &node, KX_ExecutionContext &ctx, int view,
                        float pos_x, float pos_y, float width, float height, bool vertical,
                        bool horizontal)
{
  if (vertical && horizontal) {
    return -1;
  }
  const int slider = ctx.UI().CreateSlider(view, 0.0f, 0, true);
  if (slider < 0) {
    return -1;
  }
  if (vertical) {
    B6UISetVector(node, ctx, slider, "position",
                  pos_x + width - kScrollbarWidth, pos_y);
    B6UISetVector(node, ctx, slider, "size", kScrollbarWidth, height);
    ctx.UI().SetAttribute(slider, "orientation", KX_RuntimeValue::Int(1));
  }
  else {
    B6UISetVector(node, ctx, slider, "position",
                  pos_x, pos_y + height - kScrollbarWidth);
    B6UISetVector(node, ctx, slider, "size", width, kScrollbarWidth);
    ctx.UI().SetAttribute(slider, "orientation", KX_RuntimeValue::Int(0));
  }
  ctx.UI().SetAttribute(slider, "visible", KX_RuntimeValue::Bool(false));
  return slider;
}

/* ------------------------------------------------------------------------- */
/* CreateScrollView                                                          */

struct B6ScrollViewState {
  int view = -1;
  int content = -1;
  int bar_vertical = -1;
  int bar_horizontal = -1;
  bool created = false;
  int direction = 0; /* 0 vertical, 1 horizontal, 2 both */
  float pos[2] = {0.0f, 0.0f};
  float offset[2] = {0.0f, 0.0f};
  float applied[2] = {0.0f, 0.0f};
  bool bar_visible[2] = {false, false};
};

void FuncCreateScrollView(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6ScrollViewState *state = node.StateBlock<B6ScrollViewState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && !state->created) {
    const int parent = node.InInt(ctx, 1);
    float pos[3] = {0.0f, 0.0f, 0.0f};
    node.InVector(ctx, 2, pos);
    float size[3] = {0.0f, 0.0f, 0.0f};
    node.InVector(ctx, 3, size);
    const std::string direction = node.ConfigString("direction");
    state->direction = direction == "horizontal" ? 1 : (direction == "both" ? 2 : 0);
    state->pos[0] = pos[0];
    state->pos[1] = pos[1];

    state->view = ctx.UI().CreateLayout(parent, 0);
    state->content = ctx.UI().CreateLayout(state->view, 0);
    state->created = state->view >= 0 && state->content >= 0;
    if (state->created) {
      B6UISetVector(node, ctx, state->view, "position", pos[0], pos[1]);
      B6UISetVector(node, ctx, state->view, "size", size[0], size[1]);
      ctx.UI().SetAttribute(state->view, "clip", KX_RuntimeValue::Bool(true));
      B6UISetVector(node, ctx, state->content, "position", pos[0], pos[1]);
      B6UISetVector(node, ctx, state->content, "size", size[0], size[1]);
      if (state->direction != 1) {
        state->bar_vertical = B6UICreateScrollbar(
            node, ctx, state->view, pos[0], pos[1], size[0], size[1], true, false);
      }
      if (state->direction != 0) {
        state->bar_horizontal = B6UICreateScrollbar(
            node, ctx, state->view, pos[0], pos[1], size[0], size[1], false, true);
      }
    }
    else {
      std::fprintf(stderr,
                   "GameNode: CreateScrollView: adapter rejected view/content creation\n");
    }
  }
  node.OutTrigger(ctx, 0, trigger && state != nullptr && state->created);
  node.OutInt(ctx, 1, state != nullptr ? state->view : -1);
  node.OutInt(ctx, 2, state != nullptr ? state->content : -1);
}

void FuncScrollViewTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta)
{
  (void)delta;
  B6ScrollViewState *state = node.StateBlock<B6ScrollViewState>(ctx);
  if (state == nullptr || !state->created) {
    return;
  }
  float view_size[2] = {0.0f, 0.0f};
  float content_size[2] = {0.0f, 0.0f};
  if (!B6UIGetSize(node, ctx, state->view, view_size) ||
      !B6UIGetSize(node, ctx, state->content, content_size))
  {
    return;
  }

  for (int axis = 0; axis < 2; ++axis) {
    /* Axis 0 is X (horizontal bar), axis 1 is Y (vertical bar). */
    const int bar = axis == 0 ? state->bar_horizontal : state->bar_vertical;
    if (bar < 0) {
      continue;
    }
    const float extent = content_size[axis] - view_size[axis];
    const bool overflow = extent > 0.5f;
    if (overflow != state->bar_visible[axis]) {
      ctx.UI().SetAttribute(bar, "visible", KX_RuntimeValue::Bool(overflow));
      state->bar_visible[axis] = overflow;
    }
    if (!overflow) {
      state->offset[axis] = 0.0f;
      continue;
    }
    float value = ctx.UI().SliderValue(bar);
    if (!(value >= 0.0f)) {
      value = 0.0f;
    }
    if (value > 1.0f) {
      value = 1.0f;
    }
    /* Clamp to the scrollable range; the content may never leave the view. */
    state->offset[axis] = value * extent;
    if (state->offset[axis] > extent) {
      state->offset[axis] = extent;
    }
  }

  const float target[2] = {
      state->pos[0] - (state->direction == 0 ? 0.0f : state->offset[0]),
      state->pos[1] - (state->direction == 1 ? 0.0f : state->offset[1]),
  };
  if (target[0] != state->applied[0] || target[1] != state->applied[1]) {
    B6UISetVector(node, ctx, state->content, "position", target[0], target[1]);
    state->applied[0] = target[0];
    state->applied[1] = target[1];
  }
}

/* ------------------------------------------------------------------------- */
/* CreateListView                                                            */

struct B6ListViewState {
  int view = -1;
  int bar = -1;
  bool created = false;
  bool virtualize = false;
  float item_height = kDefaultItemHeight;
  float pos[2] = {0.0f, 0.0f};
  float view_size[2] = {0.0f, 0.0f};
  int item_count = 0;
  int item_handles[kMaxListItems];
  int item_index[kMaxListItems];
  /** Latched selection: kept until the next click or a Clear trigger. */
  int selected_index = -1;
  bool clear_was_set = false;
  float offset = 0.0f;
  bool bar_visible = false;
  bool was_down = false;
};

void B6ListSyncItems(KX_FunctionNode &node, KX_ExecutionContext &ctx,
                     B6ListViewState *state, const KX_ValueList *items)
{
  const int count = items != nullptr
                        ? static_cast<int>(items->size() > kMaxListItems ? kMaxListItems
                                                                        : items->size())
                        : 0;
  state->item_count = count;
  const float view_height = state->view_size[1];
  const int visible = state->item_height > 0.0f
                          ? static_cast<int>(std::ceil(view_height / state->item_height)) + 1
                          : 0;
  const int needed = state->virtualize ? (visible > 0 ? visible : 0) : count;
  const int first = state->virtualize && state->item_height > 0.0f
                        ? static_cast<int>(std::floor(state->offset / state->item_height))
                        : 0;

  for (int slot = 0; slot < kMaxListItems; ++slot) {
    if (slot >= needed) {
      if (state->item_handles[slot] >= 0) {
        /* Keep handles; hidden items would still be hit tested. Destroying is
         * cheap for a label and required for consistent headless results. */
        ctx.UI().DestroyElement(state->item_handles[slot]);
        state->item_handles[slot] = -1;
      }
      state->item_index[slot] = -1;
      continue;
    }
    if (state->item_handles[slot] < 0) {
      state->item_handles[slot] = ctx.UI().CreateLabel(state->view, std::string());
      state->item_index[slot] = -1;
      if (state->item_handles[slot] >= 0) {
        ctx.UI().SetAttribute(state->item_handles[slot],
                              "size",
                              KX_RuntimeValue::Vector(state->view_size[0], state->item_height, 0.0f));
        if (state->virtualize) {
          ctx.UI().SetAttribute(state->item_handles[slot],
                                "virtualized",
                                KX_RuntimeValue::Bool(true));
        }
      }
    }
  }

  for (int slot = 0; slot < needed; ++slot) {
    const int item_index = state->virtualize ? first + slot : slot;
    if (state->item_handles[slot] < 0) {
      continue;
    }
    if (item_index >= count) {
      continue;
    }
    if (state->item_index[slot] != item_index) {
      const KX_RuntimeValue &value = (*items)[item_index];
      std::string text;
      if (value.type == KX_VALUE_STRING && value.data.ptr != nullptr) {
        text = static_cast<const char *>(value.data.ptr);
      }
      else if (value.type == KX_VALUE_INT) {
        text = std::to_string(value.data.i);
      }
      else if (value.type == KX_VALUE_FLOAT) {
        text = std::to_string(value.data.f);
      }
      else if (value.type == KX_VALUE_BOOL) {
        text = value.data.b ? "True" : "False";
      }
      ctx.UI().SetAttribute(state->item_handles[slot],
                            "text",
                            KX_RuntimeValue::String(ctx.Pool().Intern(text)));
      state->item_index[slot] = item_index;
    }
    const float y = state->pos[1] - state->offset + float(item_index) * state->item_height;
    B6UISetVector(node, ctx, state->item_handles[slot], "position", state->pos[0], y);
  }
}

void FuncCreateListView(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6ListViewState *state = node.StateBlock<B6ListViewState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && !state->created) {
    const int parent = node.InInt(ctx, 1);
    float pos[3] = {0.0f, 0.0f, 0.0f};
    node.InVector(ctx, 2, pos);
    float size[3] = {0.0f, 0.0f, 0.0f};
    node.InVector(ctx, 3, size);
    state->pos[0] = pos[0];
    state->pos[1] = pos[1];
    state->view_size[0] = std::fabs(size[0]);
    state->view_size[1] = std::fabs(size[1]);
    state->item_height = node.ConfigFloat("item_height", kDefaultItemHeight);
    if (state->item_height <= 0.0f) {
      state->item_height = kDefaultItemHeight;
    }
    state->virtualize = node.ConfigBool("virtualize", false);
    for (int slot = 0; slot < kMaxListItems; ++slot) {
      state->item_handles[slot] = -1;
      state->item_index[slot] = -1;
    }
    state->view = ctx.UI().CreateLayout(parent, 0);
    state->created = state->view >= 0;
    if (state->created) {
      B6UISetVector(node, ctx, state->view, "position", pos[0], pos[1]);
      B6UISetVector(node, ctx, state->view, "size", state->view_size[0], state->view_size[1]);
      ctx.UI().SetAttribute(state->view, "clip", KX_RuntimeValue::Bool(true));
      state->bar = B6UICreateScrollbar(node,
                                     ctx,
                                     state->view,
                                     pos[0],
                                     pos[1],
                                     state->view_size[0],
                                     state->view_size[1],
                                     true,
                                     false);
    }
    else {
      std::fprintf(stderr, "GameNode: CreateListView: adapter rejected view creation\n");
    }
  }

  B6ListViewState *live = state;
  if (live != nullptr && live->created) {
    const KX_RuntimeValue &items_value = node.In(ctx, 4);
    const KX_ValueList *items = static_cast<const KX_ValueList *>(items_value.AsList());
    live->item_count = items != nullptr
                           ? static_cast<int>(items->size() > kMaxListItems ? kMaxListItems
                                                                           : items->size())
                           : 0;
    live->offset = 0.0f;
    if (live->bar >= 0) {
      const float extent = float(live->item_count) * live->item_height - live->view_size[1];
      const bool overflow = extent > 0.5f;
      if (live->bar_visible != overflow) {
        ctx.UI().SetAttribute(live->bar, "visible", KX_RuntimeValue::Bool(overflow));
        live->bar_visible = overflow;
      }
      if (overflow) {
        float value = ctx.UI().SliderValue(live->bar);
        value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        live->offset = value * extent;
      }
    }
    B6ListSyncItems(node, ctx, live, items);
  }

  if (live != nullptr && live->created) {
    const bool down = ctx.UI().MouseDown();
    if (down && !live->was_down && live->view >= 0) {
      const int hovered = ctx.UI().HitTestPoint(ctx.UI().MouseX(), ctx.UI().MouseY());
      const bool on_bar = hovered >= 0 && hovered == live->bar;
      if (!on_bar &&
          ctx.UI().HitTest(live->view, ctx.UI().MouseX(), ctx.UI().MouseY()))
      {
        const float local = ctx.UI().MouseY() - (live->pos[1] - live->offset);
        if (local >= 0.0f && live->item_height > 0.0f) {
          const int index = static_cast<int>(std::floor(local / live->item_height));
          if (index >= 0 && index < live->item_count) {
            live->selected_index = index;
          }
        }
      }
    }
    live->was_down = down;
  }

  /* Clear is edge triggered: an unconnected trigger stays true every frame and
   * must not wipe the selection continuously. */
  const bool clear = node.InTrigger(ctx, 5);
  if (live != nullptr && clear && !live->clear_was_set) {
    live->selected_index = -1;
  }
  if (live != nullptr) {
    live->clear_was_set = clear;
  }

  node.OutTrigger(ctx, 0, trigger && state != nullptr && state->created);
  node.OutInt(ctx, 1, state != nullptr ? state->view : -1);
  node.OutInt(ctx, 2, state != nullptr ? state->selected_index : -1);
}

/* ------------------------------------------------------------------------- */
/* CreateTabView                                                             */

struct B6TabViewState {
  int view = -1;
  bool created = false;
  int tab_count = 0;
  int tab_handles[kMaxListItems];
  int current = -1;
};

void FuncCreateTabView(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6TabViewState *state = node.StateBlock<B6TabViewState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr && !state->created) {
    const int parent = node.InInt(ctx, 1);
    float pos[3] = {0.0f, 0.0f, 0.0f};
    node.InVector(ctx, 2, pos);
    float size[3] = {0.0f, 0.0f, 0.0f};
    node.InVector(ctx, 3, size);
    const KX_RuntimeValue &names_value = node.In(ctx, 4);
    const KX_ValueList *names = static_cast<const KX_ValueList *>(names_value.AsList());
    const int count = names != nullptr
                          ? static_cast<int>(names->size() > kMaxListItems ? kMaxListItems
                                                                          : names->size())
                          : 0;
    for (int slot = 0; slot < kMaxListItems; ++slot) {
      state->tab_handles[slot] = -1;
    }
    state->tab_count = count;
    state->view = ctx.UI().CreateLayout(parent, 0);
    state->created = state->view >= 0;
    if (state->created) {
      B6UISetVector(node, ctx, state->view, "position", pos[0], pos[1]);
      B6UISetVector(node, ctx, state->view, "size", size[0], size[1]);
      const float tab_width = count > 0 ? std::fabs(size[0]) / float(count) : std::fabs(size[0]);
      for (int index = 0; index < count; ++index) {
        const KX_RuntimeValue &value = (*names)[index];
        const char *text = value.AsString();
        state->tab_handles[index] =
            ctx.UI().CreateButton(state->view, text != nullptr ? std::string(text) : std::string());
        if (state->tab_handles[index] >= 0) {
          B6UISetVector(node, ctx, state->tab_handles[index], "position",
                        pos[0] + float(index) * tab_width, pos[1]);
          B6UISetVector(node, ctx, state->tab_handles[index], "size",
                        tab_width, std::fabs(size[1]));
        }
      }
    }
    else {
      std::fprintf(stderr, "GameNode: CreateTabView: adapter rejected view creation\n");
    }
  }

  if (state != nullptr && state->created) {
    for (int index = 0; index < state->tab_count; ++index) {
      if (state->tab_handles[index] >= 0 &&
          ctx.UI().Event(state->tab_handles[index], KX_UIEvent::Click))
      {
        state->current = index;
      }
    }
  }

  node.OutTrigger(ctx, 0, trigger && state != nullptr && state->created);
  node.OutInt(ctx, 1, state != nullptr ? state->view : -1);
  node.OutInt(ctx, 2, state != nullptr ? state->current : -1);
}

/* ------------------------------------------------------------------------- */
/* UIAnimation                                                               */

enum class B6AnimType : int {
  Float = 0,
  Vector = 1,
  Color = 2,
};

struct B6AnimationState {
  int widget = -1;
  int type = 0;
  int easing = 0;
  float duration = 1.0f;
  float elapsed = 0.0f;
  float from[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float to[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  bool active = false;
  bool emitted = false;
  char property[48] = {0};
};

int B6UIEasingFromString(const std::string &easing)
{
  if (easing == "ease_in") {
    return 1;
  }
  if (easing == "ease_out") {
    return 2;
  }
  if (easing == "ease_in_out") {
    return 3;
  }
  return 0;
}

float B6UIEase(int easing, float t)
{
  if (t <= 0.0f) {
    return 0.0f;
  }
  if (t >= 1.0f) {
    return 1.0f;
  }
  switch (easing) {
    case 1:
      return t * t;
    case 2:
      return 1.0f - (1.0f - t) * (1.0f - t);
    case 3:
      return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    default:
      return t;
  }
}

B6AnimType B6UIPropertyType(const std::string &property, const KX_RuntimeValue &from_value,
                            const KX_RuntimeValue &to_value)
{
  if (from_value.type == KX_VALUE_COLOR || to_value.type == KX_VALUE_COLOR) {
    return B6AnimType::Color;
  }
  if (from_value.type == KX_VALUE_VECTOR || to_value.type == KX_VALUE_VECTOR) {
    return B6AnimType::Vector;
  }
  if (property == "color" || property == "font_color" || property == "bg_color") {
    return B6AnimType::Color;
  }
  if (property == "position" || property == "size" || property == "pos") {
    return B6AnimType::Vector;
  }
  return B6AnimType::Float;
}

void FuncUIAnimation(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6AnimationState *state = node.StateBlock<B6AnimationState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr) {
    /* Re-entry cancels the running animation before the new one starts. */
    state->active = false;
    state->emitted = false;
    state->elapsed = 0.0f;
    state->widget = node.InInt(ctx, 1);
    state->duration = node.InFloat(ctx, 2);
    if (state->duration < 0.0f) {
      state->duration = 0.0f;
    }
    const char *property = B6UIInString(node, ctx, 3);
    std::snprintf(state->property, sizeof(state->property), "%s",
                  property != nullptr ? property : "");
    const KX_RuntimeValue &from_value = node.In(ctx, 4);
    const KX_RuntimeValue &to_value = node.In(ctx, 5);
    state->type = static_cast<int>(
        B6UIPropertyType(std::string(state->property), from_value, to_value));
    from_value.ColorTo(state->from);
    to_value.ColorTo(state->to);
    state->easing = B6UIEasingFromString(node.ConfigString("easing"));
    state->active = state->widget >= 0 && state->property[0] != '\0';
  }
  node.OutTrigger(ctx, 0, false);
}

void FuncUIAnimationTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta)
{
  B6AnimationState *state = node.StateBlock<B6AnimationState>(ctx);
  if (state == nullptr || !state->active) {
    return;
  }
  state->elapsed += delta < 0.0f ? 0.0f : delta;
  float t = state->duration > 0.0f ? state->elapsed / state->duration : 1.0f;
  const bool finished = t >= 1.0f;
  if (finished) {
    t = 1.0f;
  }
  const float eased = B6UIEase(state->easing, t);

  if (state->type == static_cast<int>(B6AnimType::Float)) {
    ctx.UI().SetAttribute(
        state->widget,
        state->property,
        KX_RuntimeValue::Float(state->from[0] + (state->to[0] - state->from[0]) * eased));
  }
  else if (state->type == static_cast<int>(B6AnimType::Vector)) {
    ctx.UI().SetAttribute(state->widget,
                          state->property,
                          KX_RuntimeValue::Vector(
                              state->from[0] + (state->to[0] - state->from[0]) * eased,
                              state->from[1] + (state->to[1] - state->from[1]) * eased,
                              state->from[2] + (state->to[2] - state->from[2]) * eased));
  }
  else {
    ctx.UI().SetAttribute(state->widget,
                          state->property,
                          KX_RuntimeValue::Color(
                              state->from[0] + (state->to[0] - state->from[0]) * eased,
                              state->from[1] + (state->to[1] - state->from[1]) * eased,
                              state->from[2] + (state->to[2] - state->from[2]) * eased,
                              state->from[3] + (state->to[3] - state->from[3]) * eased));
  }

  if (finished) {
    state->active = false;
    if (!state->emitted) {
      state->emitted = true;
      node.PendingTrigger(ctx, 0);
    }
  }
}

/* ------------------------------------------------------------------------- */
/* UIScreenAnchor                                                            */

struct B6ScreenAnchorState {
  int widget = -1;
  int screen_w = 0;
  int screen_h = 0;
  bool initialized = false;
};

void B6UIApplyAnchor(KX_FunctionNode &node, KX_ExecutionContext &ctx, int widget,
                     const std::string &anchor)
{
  int anchor_x = 0;
  int anchor_y = 0;
  if (anchor == "top_left") {
    anchor_x = -1;
    anchor_y = -1;
  }
  else if (anchor == "top_center" || anchor == "top") {
    anchor_y = -1;
  }
  else if (anchor == "top_right") {
    anchor_x = 1;
    anchor_y = -1;
  }
  else if (anchor == "middle_left" || anchor == "left") {
    anchor_x = -1;
  }
  else if (anchor == "middle_right" || anchor == "right") {
    anchor_x = 1;
  }
  else if (anchor == "bottom_left") {
    anchor_x = -1;
    anchor_y = 1;
  }
  else if (anchor == "bottom_center" || anchor == "bottom") {
    anchor_y = 1;
  }
  else if (anchor == "bottom_right") {
    anchor_x = 1;
    anchor_y = 1;
  }
  if (widget < 0) {
    return;
  }
  ctx.UI().SetAttribute(widget, "anchor_x", KX_RuntimeValue::Int(anchor_x));
  ctx.UI().SetAttribute(widget, "anchor_y", KX_RuntimeValue::Int(anchor_y));
}

void FuncUIScreenAnchor(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6ScreenAnchorState *state = node.StateBlock<B6ScreenAnchorState>(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  if (trigger && state != nullptr) {
    state->widget = node.InInt(ctx, 1);
    B6UIApplyAnchor(node, ctx, state->widget, node.ConfigString("anchor"));
  }
  node.OutTrigger(ctx, 0, false);
  node.OutFloat(ctx, 1, state != nullptr ? float(state->screen_w) : 0.0f);
  node.OutFloat(ctx, 2, state != nullptr ? float(state->screen_h) : 0.0f);
}

void FuncUIScreenAnchorTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta)
{
  (void)delta;
  B6ScreenAnchorState *state = node.StateBlock<B6ScreenAnchorState>(ctx);
  if (state == nullptr) {
    return;
  }
  const int width = ctx.UI().ScreenWidth();
  const int height = ctx.UI().ScreenHeight();
  if (width <= 0 || height <= 0) {
    return;
  }
  if (width == state->screen_w && height == state->screen_h) {
    return;
  }
  const bool resized = state->initialized;
  state->screen_w = width;
  state->screen_h = height;
  state->initialized = true;
  node.OutFloat(ctx, 1, float(width));
  node.OutFloat(ctx, 2, float(height));
  B6UIApplyAnchor(node, ctx, state->widget, node.ConfigString("anchor"));
  if (resized) {
    node.PendingTrigger(ctx, 0);
  }
}

/* ------------------------------------------------------------------------- */
/* UIScreenStack                                                             */

struct B6ScreenEntry {
  char name[kScreenNameLength];
  int handle;
};

struct B6ScreenStackState {
  B6ScreenEntry entries[kMaxScreens];
  int entry_count = 0;
  int stack[kMaxScreens];
  int depth = 0;
};

int B6ScreenFindEntry(B6ScreenStackState *state, const std::string &name)
{
  for (int index = 0; index < state->entry_count; ++index) {
    if (name == state->entries[index].name) {
      return index;
    }
  }
  return -1;
}

/** One stack per graph instance: the state lives on the execution context. */
B6ScreenStackState *B6ScreenStackFor(KX_ExecutionContext &ctx)
{
  std::shared_ptr<void> &slot = ctx.UISharedState();
  if (!slot) {
    slot = std::make_shared<B6ScreenStackState>();
  }
  return static_cast<B6ScreenStackState *>(slot.get());
}

void FuncUIScreenStack(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6ScreenStackState *state = B6ScreenStackFor(ctx);
  const bool trigger = node.InTrigger(ctx, 0);
  bool done = false;
  if (trigger && state != nullptr) {
    std::string name;
    if (B6UIReadString(node, ctx, 1, &name) && !name.empty()) {
      const std::string mode = node.ConfigString("mode");
      int entry = B6ScreenFindEntry(state, name);
      if (entry < 0 && state->entry_count < kMaxScreens) {
        const int handle = ctx.UI().CreateCanvas(name);
        entry = state->entry_count;
        state->entries[entry].handle = handle;
        std::snprintf(state->entries[entry].name,
                      sizeof(state->entries[entry].name),
                      "%s",
                      name.c_str());
        ++state->entry_count;
        /* Created hidden; push/pop decides visibility. */
        ctx.UI().SetAttribute(handle, "visible", KX_RuntimeValue::Bool(false));
      }
      const int current = state->depth > 0 ? state->stack[state->depth - 1] : -1;
      if (entry >= 0) {
        if (mode == "pop") {
          if (state->depth > 0) {
            ctx.UI().SetAttribute(state->entries[current].handle,
                                  "visible",
                                  KX_RuntimeValue::Bool(false));
            --state->depth;
            if (state->depth > 0) {
              ctx.UI().SetAttribute(state->entries[state->stack[state->depth - 1]].handle,
                                    "visible",
                                    KX_RuntimeValue::Bool(true));
            }
            done = true;
          }
          else {
            std::fprintf(stderr, "GameNode: UIScreenStack: pop on an empty stack\n");
            done = true;
          }
        }
        else if (mode == "replace") {
          if (state->depth > 0) {
            ctx.UI().SetAttribute(state->entries[current].handle,
                                  "visible",
                                  KX_RuntimeValue::Bool(false));
            state->stack[state->depth - 1] = entry;
          }
          else {
            state->stack[0] = entry;
            state->depth = 1;
          }
          ctx.UI().SetAttribute(state->entries[entry].handle,
                                "visible",
                                KX_RuntimeValue::Bool(true));
          done = true;
        }
        else {
          if (current >= 0 && current != entry) {
            ctx.UI().SetAttribute(state->entries[current].handle,
                                  "visible",
                                  KX_RuntimeValue::Bool(false));
          }
          if (state->depth < kMaxScreens) {
            /* Re-pushing the current screen keeps the stack unchanged. */
            if (current != entry) {
              state->stack[state->depth++] = entry;
            }
            ctx.UI().SetAttribute(state->entries[entry].handle,
                                  "visible",
                                  KX_RuntimeValue::Bool(true));
            done = true;
          }
          else {
            std::fprintf(stderr, "GameNode: UIScreenStack: screen stack overflow\n");
          }
        }
      }
    }
    else {
      std::fprintf(stderr, "GameNode: UIScreenStack: empty screen name\n");
    }
  }
  node.OutTrigger(ctx, 0, trigger && done);
}

/* ------------------------------------------------------------------------- */

const KX_FuncPort kCreateScrollViewInputs[] = {
    TP("OnEnter"),
    FP("Parent", KX_VALUE_INT),
    FP("Pos", KX_VALUE_VECTOR),
    FP("Size", KX_VALUE_VECTOR),
};
const KX_FuncPort kCreateScrollViewOutputs[] = {
    TP("Done"),
    FP("View", KX_VALUE_INT),
    FP("Content", KX_VALUE_INT),
};

const KX_FuncPort kCreateListViewInputs[] = {
    TP("OnEnter"),
    FP("Parent", KX_VALUE_INT),
    FP("Pos", KX_VALUE_VECTOR),
    FP("Size", KX_VALUE_VECTOR),
    FP("Items", KX_VALUE_LIST),
    TP("Clear"),
};
const KX_FuncPort kCreateListViewOutputs[] = {
    TP("Done"),
    FP("ListView", KX_VALUE_INT),
    FP("OnItemClick", KX_VALUE_INT),
};

const KX_FuncPort kCreateTabViewInputs[] = {
    TP("OnEnter"),
    FP("Parent", KX_VALUE_INT),
    FP("Pos", KX_VALUE_VECTOR),
    FP("Size", KX_VALUE_VECTOR),
    FP("TabNames", KX_VALUE_LIST),
};
const KX_FuncPort kCreateTabViewOutputs[] = {
    TP("Done"),
    FP("TabView", KX_VALUE_INT),
    FP("OnTabChanged", KX_VALUE_INT),
};

const KX_FuncPort kUIAnimationInputs[] = {
    TP("OnEnter"),
    FP("Widget", KX_VALUE_INT),
    FP("Duration", KX_VALUE_FLOAT),
    FP("Property", KX_VALUE_STRING),
    FP("FromValue", KX_VALUE_NONE),
    FP("ToValue", KX_VALUE_NONE),
};
const KX_FuncPort kUIAnimationOutputs[] = {
    TP("OnDone"),
};

const KX_FuncPort kUIScreenAnchorInputs[] = {
    TP("OnEnter"),
    FP("Widget", KX_VALUE_INT),
};
const KX_FuncPort kUIScreenAnchorOutputs[] = {
    TP("OnResize"),
    FP("Width", KX_VALUE_FLOAT),
    FP("Height", KX_VALUE_FLOAT),
};

const KX_FuncPort kUIScreenStackInputs[] = {
    TP("OnEnter"),
    FP("ScreenName", KX_VALUE_STRING),
};
const KX_FuncPort kUIScreenStackOutputs[] = {
    TP("OnDone"),
};

const KX_FunctionSpec kB6UISpecs[] = {
    B6UI_ACTION("CreateScrollView",
                kCreateScrollViewInputs,
                kCreateScrollViewOutputs,
                FuncCreateScrollView,
                FuncScrollViewTick,
                B6ScrollViewState),
    B6UI_ACTION("CreateListView",
                kCreateListViewInputs,
                kCreateListViewOutputs,
                FuncCreateListView,
                nullptr,
                B6ListViewState),
    B6UI_ACTION("CreateTabView",
                kCreateTabViewInputs,
                kCreateTabViewOutputs,
                FuncCreateTabView,
                nullptr,
                B6TabViewState),
    B6UI_ACTION_IR("UIAnimation",
                   kUIAnimationInputs,
                   kUIAnimationOutputs,
                   FuncUIAnimation,
                   FuncUIAnimationTick,
                   B6AnimationState),
    B6UI_ACTION("UIScreenAnchor",
                kUIScreenAnchorInputs,
                kUIScreenAnchorOutputs,
                FuncUIScreenAnchor,
                FuncUIScreenAnchorTick,
                B6ScreenAnchorState),
    B6UI_ACTION("UIScreenStack",
                kUIScreenStackInputs,
                kUIScreenStackOutputs,
                FuncUIScreenStack,
                nullptr,
                B6ScreenStackState),
};

KX_NodeSignature MakeB6UISignature(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    port.kind = spec.inputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.inputs[i].trigger ? KX_VALUE_NONE : spec.inputs[i].type;
    signature.inputs.push_back(port);
  }
  for (uint32_t i = 0; i < spec.output_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.outputs[i].name;
    port.kind = spec.outputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.outputs[i].trigger ? KX_VALUE_NONE : spec.outputs[i].type;
    signature.outputs.push_back(port);
  }
  signature.state_size = spec.state_size;
  signature.state_alignment = spec.state_alignment;
  signature.needs_tick = spec.needs_tick || spec.tick != nullptr;
  signature.breaks_trigger_cycles = spec.breaks_trigger_cycles;
  signature.ports_from_ir = spec.ports_from_ir;
  return signature;
}

}  // namespace

void RegisterB6UIFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6UISpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6UI_") + spec.type_id,
        MakeB6UISignature(*entry),
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
