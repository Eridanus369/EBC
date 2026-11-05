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

/** \file KX_UIAdapterEngine.cpp
 *  \ingroup gamelogic
 */

#include "KX_UIAdapterEngine.h"

#include <cstdio>

#include "BLF_api.hh"
#include "KX_Scene.h"
#include "RAS_ICanvas.h"

namespace {

/* BLF state is process wide, so the guard is shared by every adapter. The
 * counter lets the unit test prove that a second label does not reload it. */
bool g_ui_font_stack_loaded = false;
int g_ui_font_stack_loads = 0;

}  // namespace

KX_UIAdapterEngine::KX_UIAdapterEngine(KX_Scene *scene) : scene_(scene) {}

KX_UIAdapterEngine::~KX_UIAdapterEngine() = default;

int KX_UIAdapterEngine::FontStackLoads() const
{
  return g_ui_font_stack_loads;
}

void KX_UIAdapterEngine::EnsureFontStack()
{
  if (g_ui_font_stack_loaded) {
    return;
  }
  /* Loads the default and mono fonts plus every datafiles font, including the
   * bundled Noto Sans CJK, which is what makes Chinese text render at all. */
  blender::BLF_load_font_stack();
  g_ui_font_stack_loaded = true;
  ++g_ui_font_stack_loads;
}

void KX_UIAdapterEngine::BeginFrame(float delta_time,
                                    float mouse_x,
                                    float mouse_y,
                                    bool mouse_down)
{
  (void)delta_time;
  mouse_x_ = mouse_x;
  mouse_y_ = mouse_y;
  mouse_down_ = mouse_down;
  /* B6.7: Polar children get their final coordinates before hit testing, so
   * clicks no longer land on the authored position. */
  KX_UILayoutPolarElements(elements_);
  UpdateEvents();
}

void KX_UIAdapterEngine::UpdateEvents()
{
  event_flags_.clear();
  hovered_handle_ = HitTestPoint(mouse_x_, mouse_y_);
  if (hovered_handle_ >= 0) {
    event_flags_[hovered_handle_] |= 1u << static_cast<uint8_t>(KX_UIEvent::Hover);
  }

  const bool pressed = mouse_down_ && !previous_mouse_down_;
  const bool released = !mouse_down_ && previous_mouse_down_;
  if (pressed) {
    pressed_handle_ = hovered_handle_;
    if (pressed_handle_ >= 0) {
      event_flags_[pressed_handle_] |= 1u << static_cast<uint8_t>(KX_UIEvent::Click);
    }
  }
  if (released && pressed_handle_ >= 0) {
    event_flags_[pressed_handle_] |= 1u << static_cast<uint8_t>(KX_UIEvent::Release);
    if (KX_UIElement *element = Find(pressed_handle_)) {
      element->dragging = false;
    }
    pressed_handle_ = -1;
  }

  if (mouse_down_ && pressed_handle_ >= 0) {
    KX_UIElement *element = Find(pressed_handle_);
    if (element != nullptr && element->type == KX_UIElementType::Slider) {
      float rect[4];
      KX_UIElementRect(*element, rect);
      const bool vertical = element->orientation != 0;
      float ratio = vertical ? (rect[3] > 0.0f ? (mouse_y_ - rect[1]) / rect[3] : 0.0f)
                             : (rect[2] > 0.0f ? (mouse_x_ - rect[0]) / rect[2] : 0.0f);
      ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
      ratio = vertical ? 1.0f - ratio : ratio;
      if (element->steps > 1) {
        const float step = 1.0f / float(element->steps - 1);
        ratio = float(int(ratio / step + 0.5f)) * step;
      }
      if (ratio != element->value) {
        element->value = ratio;
        ++element->revision;
        event_flags_[pressed_handle_] |= 1u << static_cast<uint8_t>(KX_UIEvent::Changed);
      }
      element->dragging = true;
    }
  }
  previous_mouse_down_ = mouse_down_;
}

void KX_UIAdapterEngine::EndFrame()
{
  DrawScreen(0, 0);
}

void KX_UIAdapterEngine::DrawScreen(int width, int height)
{
  if (scene_ != nullptr && (width <= 0 || height <= 0)) {
    RAS_ICanvas *canvas = scene_->GetCanvas();
    if (canvas != nullptr) {
      width = canvas->GetWidth();
      height = canvas->GetHeight();
    }
  }
  if (width <= 0 || height <= 0) {
    return;
  }
  screen_w_ = width;
  screen_h_ = height;
  renderer_.Draw(elements_, width, height, true);
}

int KX_UIAdapterEngine::CreateElement(KX_UIElementType type,
                                      int canvas,
                                      const std::string &text)
{
  const int handle = next_handle_++;
  KX_UIElement element;
  element.handle = handle;
  element.type = type;
  element.canvas = canvas;
  element.text = text;
  if (type != KX_UIElementType::Canvas) {
    element.size[0] = 100.0f;
    element.size[1] = 24.0f;
  }
  by_handle_[handle] = elements_.size();
  elements_.push_back(element);
  return handle;
}

int KX_UIAdapterEngine::CreateCanvas(const std::string &name)
{
  (void)name;
  return CreateElement(KX_UIElementType::Canvas, -1, std::string());
}

bool KX_UIAdapterEngine::LoadFont(const std::string &name, const std::string &path)
{
  /* The default stack stays untouched: BLF_load_unique gives the extra face
   * its own slot, so the single-shot EnsureFontStack guard keeps its meaning. */
  EnsureFontStack();
  return KX_UIRenderer::LoadFont(name, path);
}

int KX_UIAdapterEngine::CreateLabel(int canvas, const std::string &text)
{
  EnsureFontStack();
  return CreateElement(KX_UIElementType::Label, canvas, text);
}

int KX_UIAdapterEngine::CreateImage(int canvas, const std::string &image)
{
  const int handle = CreateElement(KX_UIElementType::Image, canvas, std::string());
  if (KX_UIElement *element = Find(handle)) {
    element->image = image;
  }
  return handle;
}

int KX_UIAdapterEngine::CreateButton(int canvas, const std::string &text)
{
  EnsureFontStack();
  return CreateElement(KX_UIElementType::Button, canvas, text);
}

int KX_UIAdapterEngine::CreateLayout(int canvas, int layout_type)
{
  const int handle = CreateElement(KX_UIElementType::Layout, canvas, std::string());
  if (KX_UIElement *element = Find(handle)) {
    element->layout_type = layout_type;
  }
  return handle;
}

int KX_UIAdapterEngine::CreateSlider(int canvas,
                                     float initial_value,
                                     int steps,
                                     bool allow_bar_click)
{
  EnsureFontStack();
  const int handle = CreateElement(KX_UIElementType::Slider, canvas, std::string());
  if (KX_UIElement *element = Find(handle)) {
    element->value = initial_value;
    element->steps = steps;
    element->allow_bar_click = allow_bar_click;
  }
  return handle;
}

bool KX_UIAdapterEngine::DestroyElement(int handle)
{
  KX_UIElement *element = Find(handle);
  if (element == nullptr) {
    return false;
  }
  element->visible = false;
  ++element->revision;
  return true;
}

bool KX_UIAdapterEngine::SetAttribute(int handle,
                                      const std::string &key,
                                      const KX_RuntimeValue &value)
{
  KX_UIElement *element = Find(handle);
  if (element == nullptr) {
    return false;
  }
  return KX_UISetElementAttribute(*element, key, value);
}

bool KX_UIAdapterEngine::GetText(int handle, std::string &out) const
{
  const KX_UIElement *element = Find(handle);
  if (element == nullptr) {
    return false;
  }
  out = element->text;
  return true;
}

bool KX_UIAdapterEngine::GetAttribute(int handle,
                                      const std::string &key,
                                      KX_RuntimeValue &out) const
{
  const KX_UIElement *element = Find(handle);
  if (element == nullptr) {
    return false;
  }
  return KX_UIGetElementAttribute(*element, key, out);
}

bool KX_UIAdapterEngine::Event(int handle, KX_UIEvent event) const
{
  const auto it = event_flags_.find(handle);
  if (it == event_flags_.end()) {
    return false;
  }
  return (it->second & (1u << static_cast<uint8_t>(event))) != 0;
}

float KX_UIAdapterEngine::SliderValue(int handle) const
{
  const KX_UIElement *element = Find(handle);
  return element != nullptr ? element->value : 0.0f;
}

bool KX_UIAdapterEngine::KnobPosition(int handle, float &x, float &y) const
{
  const KX_UIElement *element = Find(handle);
  if (element == nullptr || element->type != KX_UIElementType::Slider) {
    x = 0.0f;
    y = 0.0f;
    return false;
  }
  x = element->value;
  y = 0.0f;
  return true;
}

bool KX_UIAdapterEngine::MoveElement(int handle, int mode)
{
  if (Find(handle) == nullptr) {
    return false;
  }
  std::vector<std::pair<int, KX_UIElement>> ordered(elements_.size());
  for (const auto &entry : by_handle_) {
    if (entry.second < elements_.size()) {
      ordered[entry.second] = {entry.first, elements_[entry.second]};
    }
  }
  const size_t index = by_handle_[handle];
  const KX_UIElement moved = elements_[index];
  ordered.erase(ordered.begin() + index);
  size_t target = index;
  if (mode == 0) {
    target = ordered.size();
  }
  else if (mode == 1) {
    target = index + 1 < elements_.size() ? index + 1 : index;
  }
  else if (mode == 2) {
    target = index > 0 ? index - 1 : 0;
  }
  else {
    target = 0;
  }
  if (target > ordered.size()) {
    target = ordered.size();
  }
  ordered.insert(ordered.begin() + target, {handle, moved});
  elements_.clear();
  by_handle_.clear();
  for (size_t slot = 0; slot < ordered.size(); ++slot) {
    elements_.push_back(ordered[slot].second);
    by_handle_[ordered[slot].first] = slot;
  }
  for (KX_UIElement &element : elements_) {
    element.z_order = int(by_handle_[element.handle]);
  }
  return true;
}

bool KX_UIAdapterEngine::HitTest(int handle, float x, float y) const
{
  const KX_UIElement *element = Find(handle);
  return element != nullptr && KX_UIElementContains(*element, x, y);
}

int KX_UIAdapterEngine::HitTestPoint(float x, float y) const
{
  for (size_t index = elements_.size(); index > 0; --index) {
    const KX_UIElement &element = elements_[index - 1];
    if (element.type == KX_UIElementType::Canvas) {
      continue;
    }
    if (KX_UIElementContains(element, x, y)) {
      return element.handle;
    }
  }
  return -1;
}

KX_UIElement *KX_UIAdapterEngine::Find(int handle)
{
  const auto it = by_handle_.find(handle);
  return it != by_handle_.end() ? &elements_[it->second] : nullptr;
}

const KX_UIElement *KX_UIAdapterEngine::Find(int handle) const
{
  const auto it = by_handle_.find(handle);
  return it != by_handle_.end() ? &elements_[it->second] : nullptr;
}

std::unique_ptr<KX_UIAdapter> KX_CreateEngineUIAdapter(KX_Scene *scene)
{
  return std::make_unique<KX_UIAdapterEngine>(scene);
}
