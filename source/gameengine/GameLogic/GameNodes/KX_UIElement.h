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

/** \file KX_UIElement.h
 *  \ingroup gamelogic
 *
 *  One screen space UI element and the attribute vocabulary shared by the
 *  engine adapter and the headless mock.
 *
 *  Keeping the attribute application in one place means the mock records the
 *  exact same state transitions the engine performs, so a headless test is a
 *  faithful proxy for the real element.
 *
 *  Coordinates are pixels with the origin at the top left corner of the canvas.
 *  #position and #size are absolute for now; the renderer resolves anchors and
 *  relative sizing before drawing.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "KX_RuntimeValue.h"

/** Element kinds created by the B4.1 nodes. */
enum class KX_UIElementType : uint8_t {
  Canvas = 0,
  Label,
  Image,
  Button,
  Layout,
  Slider,
};

/** `layout_type` values shared by the editor table and the renderer. */
enum KX_UILayoutType : int {
  KX_UI_LAYOUT_SEQUENTIAL = 0,
  KX_UI_LAYOUT_BOX = 1,
  /** B6.6: children evenly spread on a circle (radius / starting_angle). */
  KX_UI_LAYOUT_POLAR = 2,
};

/** Frame events derived from the mouse state in BeginFrame. */
enum class KX_UIEvent : uint8_t {
  Click = 0,
  Hover,
  Release,
  Changed,
};

/** One UI element. Never owned by a node; handles index the adapter table. */
struct KX_UIElement {
  KX_UIElementType type = KX_UIElementType::Canvas;
  /** Adapter handle; -1 until inserted. */
  int handle = -1;
  /** Owning canvas handle, -1 for a canvas itself. */
  int canvas = -1;
  std::string text;
  /** Image datablock name stored by config (D3). */
  std::string image;
  /** Font datablock name stored by config (D3). */
  std::string font;
  /** Top left corner in canvas pixels. */
  float position[2] = {0.0f, 0.0f};
  /** Size in canvas pixels. */
  float size[2] = {0.0f, 0.0f};
  float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float text_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float hover_color[4] = {0.0f, 0.0f, 0.0f, 0.5f};
  float border_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float shadow_color[4] = {0.0f, 0.0f, 0.0f, 0.5f};
  float shadow_offset[2] = {1.0f, -1.0f};
  /** Text position inside the button, normalized 0..1. */
  float text_pos[2] = {0.5f, 0.5f};

  float angle = 0.0f;
  float font_size = 12.0f;
  float line_height = 1.5f;
  float border_width = 0.0f;
  float opacity = 1.0f;

  int halign = 0;
  int valign = 0;
  int text_halign = 0;
  int text_valign = 0;
  /** 0 left/top, 1 centre, 2 right/bottom. */
  int anchor_x = 0;
  int anchor_y = 0;
  int scale_mode = 0;
  int layout_type = 0;
  int orientation = 0;
  int spacing = 0;
  int rows = 1;
  int cols = 1;
  float starting_angle = 0.0f;
  float radius = 0.0f;

  bool rel_pos = false;
  bool rel_size = false;
  bool wrap = false;
  bool shadow = false;
  bool inverted = false;
  bool preserve_aspect = false;
  bool visible = true;
  /**
   * B6.4: clip this element and its descendants to its own rectangle.
   *
   * The renderer walks the `canvas` chain to the nearest clipping ancestor and
   * applies a GPU scissor to the element's resolved rectangle.
   */
  bool clip = false;
  /**
   * B6.4: skip layout and drawing while entirely outside the clipping ancestor
   * (list view virtualization). Hit testing is done by the node's arithmetic,
   * so the visible item indices do not depend on this flag.
   */
  bool virtualized = false;
  /**
   * B6.7: basic vertical text. One UTF-8 character per row with the row height
   * equal to the font size. Punctuation rotation, line spacing tuning and
   * mixed CJK/Latin rules are not implemented (this is a layout primitive,
   * not typesetting).
   */
  bool vertical = false;

  /* Slider state. */
  float value = 0.0f;
  int steps = 0;
  bool allow_bar_click = true;
  float bar_width = 0.5f;
  float bar_color[4] = {0.1f, 0.1f, 0.1f, 0.5f};
  float bar_hover_color[4] = {0.1f, 0.1f, 0.1f, 0.7f};
  float knob_size = 1.0f;
  float knob_color[4] = {0.7f, 0.8f, 1.0f, 1.0f};
  float knob_hover_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  std::string bar_texture;
  std::string knob_texture;
  bool dragging = false;

  /* Canvas sizing (D1/D2): design resolution and stretch/fit/fill. */
  float design_size[2] = {0.0f, 0.0f};

  /* Painter order; larger draws later and wins the hit test. */
  int z_order = 0;

  /** Bumped by every attribute write so the renderer can rebuild one batch. */
  uint32_t revision = 0;
};

/**
 * Private state of one B4.1 UI creator node.
 *
 * The runtime data buffer zeroes node state, so a "created" flag tracks the
 * first successful creation instead of relying on a negative handle sentinel.
 * P4.1 restores both fields when a graph is hot replaced, so the new creator
 * adopts the already mounted element instead of creating a duplicate.
 */
struct KX_UIHandleState {
  int handle = -1;
  bool created = false;
};

/** Slider creator state; the value is restored across a hot reload as well. */
struct KX_UISliderState {
  int handle = -1;
  float value = 0.0f;
  bool created = false;
};

/** The seven text attributes D1 commits to. Everything else is B5. */
inline bool KX_UIIsLabelTextAttribute(const std::string &key)
{
  return key == "font_size" || key == "color" || key == "halign" || key == "valign" ||
         key == "wrap" || key == "shadow" || key == "shadow_offset";
}

/**
 * Apply one typed attribute.
 *
 * Accepts both the native node port keys and the legacy `widget_attr` names
 * that SetUIWidgetAttr exports, so one vocabulary serves all nine nodes.
 * The return value tells the node whether the key is understood; a false
 * return is a configuration error, not a runtime failure.
 */
inline bool KX_UISetElementAttribute(KX_UIElement &element,
                                     const std::string &key,
                                     const KX_RuntimeValue &value)
{
  if (key == "text") {
    const char *text = value.AsString();
    if (text == nullptr) {
      return false;
    }
    element.text = text;
  }
  else if (key == "image" || key == "texture") {
    const char *image = value.AsString();
    if (image == nullptr) {
      return false;
    }
    element.image = image;
  }
  else if (key == "font") {
    const char *font = value.AsString();
    if (font == nullptr) {
      return false;
    }
    element.font = font;
  }
  else if (key == "position" || key == "pos" || key == "pivot") {
    element.position[0] = value.AsFloat();
    element.position[1] = value.type == KX_VALUE_VECTOR ? value.data.vec[1] : 0.0f;
  }
  else if (key == "size") {
    element.size[0] = value.AsFloat();
    element.size[1] = value.type == KX_VALUE_VECTOR ? value.data.vec[1] : 0.0f;
  }
  else if (key == "width") {
    element.size[0] = value.AsFloat();
  }
  else if (key == "height") {
    element.size[1] = value.AsFloat();
  }
  else if (key == "design_size") {
    element.design_size[0] = value.AsFloat();
    element.design_size[1] = value.type == KX_VALUE_VECTOR ? value.data.vec[1] : 0.0f;
  }
  else if (key == "shadow_offset") {
    element.shadow_offset[0] = value.AsFloat();
    element.shadow_offset[1] = value.type == KX_VALUE_VECTOR ? value.data.vec[1] : 0.0f;
  }
  else if (key == "text_pos") {
    element.text_pos[0] = value.AsFloat();
    element.text_pos[1] = value.type == KX_VALUE_VECTOR ? value.data.vec[1] : 0.0f;
  }
  else if (key == "color" || key == "bg_color") {
    value.ColorTo(element.color);
  }
  else if (key == "font_color") {
    value.ColorTo(element.text_color);
  }
  else if (key == "hover_color") {
    value.ColorTo(element.hover_color);
  }
  else if (key == "border_color") {
    value.ColorTo(element.border_color);
  }
  else if (key == "shadow_color") {
    value.ColorTo(element.shadow_color);
  }
  else if (key == "opacity") {
    element.opacity = value.AsFloat();
  }
  else if (key == "font_opacity") {
    element.text_color[3] = value.AsFloat();
  }
  else if (key == "angle") {
    element.angle = value.AsFloat();
  }
  else if (key == "font_size") {
    element.font_size = value.AsFloat();
  }
  else if (key == "line_height") {
    element.line_height = value.AsFloat();
  }
  else if (key == "border_width") {
    element.border_width = value.AsFloat();
  }
  else if (key == "halign") {
    element.halign = value.AsInt();
  }
  else if (key == "valign") {
    element.valign = value.AsInt();
  }
  else if (key == "text_halign") {
    element.text_halign = value.AsInt();
  }
  else if (key == "text_valign") {
    element.text_valign = value.AsInt();
  }
  else if (key == "anchor") {
    element.anchor_x = value.AsInt();
    element.anchor_y = value.AsInt();
  }
  else if (key == "anchor_x") {
    element.anchor_x = value.AsInt();
  }
  else if (key == "anchor_y") {
    element.anchor_y = value.AsInt();
  }
  else if (key == "scale_mode") {
    element.scale_mode = value.AsInt();
  }
  else if (key == "layout_type") {
    element.layout_type = value.AsInt();
  }
  else if (key == "orientation") {
    element.orientation = value.AsInt();
  }
  else if (key == "spacing") {
    element.spacing = value.AsInt();
  }
  else if (key == "rows") {
    element.rows = value.AsInt();
  }
  else if (key == "cols") {
    element.cols = value.AsInt();
  }
  else if (key == "starting_angle") {
    element.starting_angle = value.AsFloat();
  }
  else if (key == "radius") {
    element.radius = value.AsFloat();
  }
  else if (key == "wrap") {
    element.wrap = value.AsBool();
  }
  else if (key == "shadow") {
    element.shadow = value.AsBool();
  }
  else if (key == "visible" || key == "show") {
    element.visible = value.AsBool();
  }
  else if (key == "rel_pos") {
    element.rel_pos = value.AsBool();
  }
  else if (key == "rel_size") {
    element.rel_size = value.AsBool();
  }
  else if (key == "inverted") {
    element.inverted = value.AsBool();
  }
  else if (key == "preserve_aspect") {
    element.preserve_aspect = value.AsBool();
  }
  else if (key == "clip") {
    element.clip = value.AsBool();
  }
  else if (key == "virtualized") {
    element.virtualized = value.AsBool();
  }
  else if (key == "vertical") {
    element.vertical = value.AsBool();
  }
  else if (key == "tint") {
    value.ColorTo(element.text_color);
  }
  else if (key == "bar_width") {
    element.bar_width = value.AsFloat();
  }
  else if (key == "bar_color") {
    value.ColorTo(element.bar_color);
  }
  else if (key == "bar_hover_color") {
    value.ColorTo(element.bar_hover_color);
  }
  else if (key == "bar_texture") {
    const char *texture = value.AsString();
    if (texture == nullptr) {
      return false;
    }
    element.bar_texture = texture;
  }
  else if (key == "knob_size") {
    element.knob_size = value.AsFloat();
  }
  else if (key == "knob_color") {
    value.ColorTo(element.knob_color);
  }
  else if (key == "knob_hover_color") {
    value.ColorTo(element.knob_hover_color);
  }
  else if (key == "knob_texture") {
    const char *texture = value.AsString();
    if (texture == nullptr) {
      return false;
    }
    element.knob_texture = texture;
  }
  else if (key == "value") {
    element.value = value.AsFloat();
  }
  else if (key == "steps") {
    element.steps = value.AsInt();
  }
  else if (key == "allow_bar_click" || key == "bar_control") {
    element.allow_bar_click = value.AsBool();
  }
  else if (key == "use_clipping" || key == "icon" || key == "points") {
    /* Accepted for editor compatibility; drawing them is B5. */
  }
  else {
    return false;
  }
  ++element.revision;
  return true;
}

/** Axis aligned rectangle of an element, as {x, y, width, height}. */
inline void KX_UIElementRect(const KX_UIElement &element, float out[4])
{
  out[0] = element.position[0];
  out[1] = element.position[1];
  out[2] = element.size[0] < 0.0f ? -element.size[0] : element.size[0];
  out[3] = element.size[1] < 0.0f ? -element.size[1] : element.size[1];
}

/** Point in element test, inclusive on both borders. */
inline bool KX_UIElementContains(const KX_UIElement &element, float x, float y)
{
  if (!element.visible) {
    return false;
  }
  float rect[4];
  KX_UIElementRect(element, rect);
  return x >= rect[0] && x <= rect[0] + rect[2] && y >= rect[1] && y <= rect[1] + rect[3];
}

/**
 * Read one typed attribute with the same vocabulary as
 * #KX_UISetElementAttribute. Returns false for unknown keys.
 */
inline bool KX_UIGetElementAttribute(const KX_UIElement &element,
                                     const std::string &key,
                                     KX_RuntimeValue &out)
{
  if (key == "text") {
    out = KX_RuntimeValue::String(element.text.c_str());
  }
  else if (key == "image" || key == "texture") {
    out = KX_RuntimeValue::String(element.image.c_str());
  }
  else if (key == "font") {
    out = KX_RuntimeValue::String(element.font.c_str());
  }
  else if (key == "position" || key == "pos" || key == "pivot") {
    out = KX_RuntimeValue::Vector(element.position[0], element.position[1], 0.0f);
  }
  else if (key == "size") {
    out = KX_RuntimeValue::Vector(element.size[0], element.size[1], 0.0f);
  }
  else if (key == "width") {
    out = KX_RuntimeValue::Float(element.size[0]);
  }
  else if (key == "height") {
    out = KX_RuntimeValue::Float(element.size[1]);
  }
  else if (key == "shadow_offset") {
    out = KX_RuntimeValue::Vector(element.shadow_offset[0], element.shadow_offset[1], 0.0f);
  }
  else if (key == "text_pos") {
    out = KX_RuntimeValue::Vector(element.text_pos[0], element.text_pos[1], 0.0f);
  }
  else if (key == "color" || key == "bg_color") {
    out = KX_RuntimeValue::Color(
        element.color[0], element.color[1], element.color[2], element.color[3]);
  }
  else if (key == "font_color" || key == "tint") {
    out = KX_RuntimeValue::Color(element.text_color[0],
                                 element.text_color[1],
                                 element.text_color[2],
                                 element.text_color[3]);
  }
  else if (key == "hover_color") {
    out = KX_RuntimeValue::Color(
        element.hover_color[0], element.hover_color[1], element.hover_color[2], element.hover_color[3]);
  }
  else if (key == "border_color") {
    out = KX_RuntimeValue::Color(element.border_color[0],
                                 element.border_color[1],
                                 element.border_color[2],
                                 element.border_color[3]);
  }
  else if (key == "shadow_color") {
    out = KX_RuntimeValue::Color(element.shadow_color[0],
                                 element.shadow_color[1],
                                 element.shadow_color[2],
                                 element.shadow_color[3]);
  }
  else if (key == "opacity") {
    out = KX_RuntimeValue::Float(element.opacity);
  }
  else if (key == "font_opacity") {
    out = KX_RuntimeValue::Float(element.text_color[3]);
  }
  else if (key == "angle" || key == "starting_angle") {
    out = KX_RuntimeValue::Float(
        key == "angle" ? element.angle : element.starting_angle);
  }
  else if (key == "font_size") {
    out = KX_RuntimeValue::Float(element.font_size);
  }
  else if (key == "line_height") {
    out = KX_RuntimeValue::Float(element.line_height);
  }
  else if (key == "border_width") {
    out = KX_RuntimeValue::Float(element.border_width);
  }
  else if (key == "radius") {
    out = KX_RuntimeValue::Float(element.radius);
  }
  else if (key == "value") {
    out = KX_RuntimeValue::Float(element.value);
  }
  else if (key == "halign" || key == "valign" || key == "text_halign" ||
           key == "text_valign" || key == "scale_mode" || key == "layout_type" ||
           key == "orientation" || key == "spacing" || key == "rows" ||
           key == "cols" || key == "steps") {
    int result = 0;
    if (key == "halign") {
      result = element.halign;
    }
    else if (key == "valign") {
      result = element.valign;
    }
    else if (key == "text_halign") {
      result = element.text_halign;
    }
    else if (key == "text_valign") {
      result = element.text_valign;
    }
    else if (key == "scale_mode") {
      result = element.scale_mode;
    }
    else if (key == "layout_type") {
      result = element.layout_type;
    }
    else if (key == "orientation") {
      result = element.orientation;
    }
    else if (key == "spacing") {
      result = element.spacing;
    }
    else if (key == "rows") {
      result = element.rows;
    }
    else if (key == "cols") {
      result = element.cols;
    }
    else {
      result = element.steps;
    }
    out = KX_RuntimeValue::Int(result);
  }
  else if (key == "visible" || key == "show") {
    out = KX_RuntimeValue::Bool(element.visible);
  }
  else if (key == "wrap") {
    out = KX_RuntimeValue::Bool(element.wrap);
  }
  else if (key == "shadow") {
    out = KX_RuntimeValue::Bool(element.shadow);
  }
  else if (key == "use_clipping") {
    out = KX_RuntimeValue::Bool(element.clip);
  }
  else if (key == "virtualized") {
    out = KX_RuntimeValue::Bool(element.virtualized);
  }
  else if (key == "vertical") {
    out = KX_RuntimeValue::Bool(element.vertical);
  }
  else {
    return false;
  }
  return true;
}

/**
 * B6.7: write Polar child positions back before hit testing.
 *
 * The renderer used to place Polar children only while drawing, so a click
 * still tested the authored position and landed on the wrong child. Both
 * adapters run this at the start of a frame. The values are parent relative
 * and identical to what the renderer resolves for its Polar branch, so the
 * draw pass recomputes the same pixels and hit testing now agrees with it.
 *
 * Parents are created before their children, so a single forward pass also
 * handles a Polar layout nested inside another Polar layout.
 */
inline void KX_UILayoutPolarElements(std::vector<KX_UIElement> &elements)
{
  if (elements.empty()) {
    return;
  }
  std::map<int, KX_UIElement *> by_handle;
  std::map<int, int> child_total;
  for (KX_UIElement &element : elements) {
    by_handle[element.handle] = &element;
    if (element.canvas >= 0) {
      ++child_total[element.canvas];
    }
  }
  std::map<int, int> child_index;
  for (KX_UIElement &element : elements) {
    if (element.canvas < 0) {
      continue;
    }
    const auto parent_it = by_handle.find(element.canvas);
    if (parent_it == by_handle.end()) {
      continue;
    }
    const KX_UIElement &parent = *parent_it->second;
    if (parent.type != KX_UIElementType::Layout ||
        parent.layout_type != KX_UI_LAYOUT_POLAR) {
      continue;
    }
    const int slot = child_index[parent.handle]++;
    const int total = child_total[parent.handle] > 0 ? child_total[parent.handle] : 1;
    const float step = 360.0f / float(total);
    const float angle = (parent.starting_angle + step * float(slot)) * 3.14159265358979323846f /
                        180.0f;
    element.position[0] = parent.position[0] + parent.size[0] * 0.5f +
                          parent.radius * std::cos(angle);
    element.position[1] = parent.position[1] + parent.size[1] * 0.5f +
                          parent.radius * std::sin(angle);
  }
}
