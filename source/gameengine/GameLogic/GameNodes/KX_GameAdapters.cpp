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

/** \file KX_GameAdapters.cpp
 *  \ingroup gamelogic
 *
 *  Neutral adapter instances and the adapter set frame hooks.
 */

#include "KX_GameAdapters.h"

#include "KX_GameObject.h"

namespace {

class NullInputAdapter final : public KX_InputAdapter {
};

class NullSceneAdapter final : public KX_SceneAdapter {
};

class NullPhysicsAdapter final : public KX_PhysicsAdapter {
};

class NullObjectAdapter final : public KX_ObjectAdapter {
};

class NullAnimationAdapter final : public KX_AnimationAdapter {
};

class NullMessageAdapter final : public KX_MessageAdapter {
};

class NullAudioAdapter final : public KX_AudioAdapter {
};

class NullSceneControlAdapter final : public KX_SceneControlAdapter {
};

class NullUIAdapter final : public KX_UIAdapter {
};

}  // namespace

KX_InputAdapter &KX_NullInputAdapter()
{
  static NullInputAdapter adapter;
  return adapter;
}

KX_SceneAdapter &KX_NullSceneAdapter()
{
  static NullSceneAdapter adapter;
  return adapter;
}

KX_PhysicsAdapter &KX_NullPhysicsAdapter()
{
  static NullPhysicsAdapter adapter;
  return adapter;
}

KX_ObjectAdapter &KX_NullObjectAdapter()
{
  static NullObjectAdapter adapter;
  return adapter;
}

KX_AnimationAdapter &KX_NullAnimationAdapter()
{
  static NullAnimationAdapter adapter;
  return adapter;
}

KX_MessageAdapter &KX_NullMessageAdapter()
{
  static NullMessageAdapter adapter;
  return adapter;
}

KX_AudioAdapter &KX_NullAudioAdapter()
{
  static NullAudioAdapter adapter;
  return adapter;
}

KX_SceneControlAdapter &KX_NullSceneControlAdapter()
{
  static NullSceneControlAdapter adapter;
  return adapter;
}

KX_UIAdapter &KX_NullUIAdapter()
{
  static NullUIAdapter adapter;
  return adapter;
}

void KX_MockUIAdapter::BeginFrame(float delta_time, float mouse_x, float mouse_y, bool mouse_down)
{
  ++begin_frame_calls;
  last_delta_time = delta_time;
  last_mouse_x = mouse_x;
  last_mouse_y = mouse_y;
  previous_mouse_down = last_mouse_down;
  last_mouse_down = mouse_down;
  event_flags.clear();

  hovered_handle = HitTestPoint(mouse_x, mouse_y);
  if (hovered_handle >= 0) {
    event_flags[hovered_handle] |= 1u << static_cast<uint8_t>(KX_UIEvent::Hover);
  }

  if (mouse_down && !previous_mouse_down) {
    pressed_handle = hovered_handle;
    if (pressed_handle >= 0) {
      event_flags[pressed_handle] |= 1u << static_cast<uint8_t>(KX_UIEvent::Click);
    }
  }

  if (!mouse_down && previous_mouse_down && pressed_handle >= 0) {
    event_flags[pressed_handle] |= 1u << static_cast<uint8_t>(KX_UIEvent::Release);
    if (!mouse_down) {
      KX_UIElement *element = Find(pressed_handle);
      if (element != nullptr) {
        element->dragging = false;
      }
    }
    pressed_handle = -1;
  }

  /* Dragging a slider updates its value from the cursor position. */
  if (mouse_down && pressed_handle >= 0) {
    KX_UIElement *element = Find(pressed_handle);
    if (element != nullptr && element->type == KX_UIElementType::Slider) {
      float rect[4];
      KX_UIElementRect(*element, rect);
      const bool vertical = element->orientation != 0;
      float ratio = vertical ? (rect[3] > 0.0f ? (mouse_y - rect[1]) / rect[3] : 0.0f)
                             : (rect[2] > 0.0f ? (mouse_x - rect[0]) / rect[2] : 0.0f);
      ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
      ratio = vertical ? 1.0f - ratio : ratio;
      if (element->steps > 1) {
        const float step = 1.0f / float(element->steps - 1);
        ratio = (float(int(ratio / step + 0.5f)) * step);
      }
      if (ratio != element->value) {
        element->value = ratio;
        ++element->revision;
        event_flags[pressed_handle] |= 1u << static_cast<uint8_t>(KX_UIEvent::Changed);
      }
      element->dragging = true;
    }
  }
}

void KX_MockUIAdapter::EndFrame()
{
  ++end_frame_calls;
  for (const KX_UIElement &element : elements) {
    if (element.visible && element.type != KX_UIElementType::Canvas) {
      ++draw_calls;
    }
  }
}

int KX_MockUIAdapter::CreateElement(KX_UIElementType type, int canvas, const std::string &text)
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
  by_handle[handle] = elements.size();
  elements.push_back(element);
  return handle;
}

int KX_MockUIAdapter::CreateCanvas(const std::string &name)
{
  ++create_canvas_calls;
  canvas_names.push_back(name);
  return CreateElement(KX_UIElementType::Canvas, -1, std::string());
}

int KX_MockUIAdapter::CreateLabel(int canvas, const std::string &text)
{
  ++create_label_calls;
  return CreateElement(KX_UIElementType::Label, canvas, text);
}

int KX_MockUIAdapter::CreateImage(int canvas, const std::string &image)
{
  ++create_image_calls;
  image_names.push_back(image);
  const int handle = CreateElement(KX_UIElementType::Image, canvas, std::string());
  if (KX_UIElement *element = Find(handle)) {
    element->image = image;
  }
  return handle;
}

int KX_MockUIAdapter::CreateButton(int canvas, const std::string &text)
{
  ++create_button_calls;
  return CreateElement(KX_UIElementType::Button, canvas, text);
}

int KX_MockUIAdapter::CreateLayout(int canvas, int layout_type)
{
  ++create_layout_calls;
  const int handle = CreateElement(KX_UIElementType::Layout, canvas, std::string());
  if (KX_UIElement *element = Find(handle)) {
    element->layout_type = layout_type;
  }
  return handle;
}

int KX_MockUIAdapter::CreateSlider(int canvas,
                                   float initial_value,
                                   int steps,
                                   bool allow_bar_click)
{
  ++create_slider_calls;
  const int handle = CreateElement(KX_UIElementType::Slider, canvas, std::string());
  if (KX_UIElement *element = Find(handle)) {
    element->value = initial_value;
    element->steps = steps;
    element->allow_bar_click = allow_bar_click;
  }
  return handle;
}

bool KX_MockUIAdapter::DestroyElement(int handle)
{
  ++destroy_calls;
  KX_UIElement *element = Find(handle);
  if (element == nullptr) {
    return false;
  }
  element->visible = false;
  return true;
}

bool KX_MockUIAdapter::SetAttribute(int handle,
                                    const std::string &key,
                                    const KX_RuntimeValue &value)
{
  ++set_attribute_calls;
  KX_UIElement *element = Find(handle);
  bool accepted = false;
  if (element != nullptr) {
    accepted = KX_UISetElementAttribute(*element, key, value);
  }

  AttributeCall call;
  call.handle = handle;
  call.key = key;
  call.value = value;
  call.accepted = accepted;
  attribute_calls.push_back(call);
  return accepted;
}

bool KX_MockUIAdapter::GetText(int handle, std::string &out) const
{
  const KX_UIElement *element = Find(handle);
  if (element == nullptr) {
    return false;
  }
  out = element->text;
  return true;
}

bool KX_MockUIAdapter::GetAttribute(int handle,
                                    const std::string &key,
                                    KX_RuntimeValue &out) const
{
  ++get_attribute_calls;
  const KX_UIElement *element = Find(handle);
  if (element == nullptr) {
    return false;
  }
  return KX_UIGetElementAttribute(*element, key, out);
}

bool KX_MockUIAdapter::Event(int handle, KX_UIEvent event) const
{
  ++event_calls;
  const auto it = event_flags.find(handle);
  if (it == event_flags.end()) {
    return false;
  }
  return (it->second & (1u << static_cast<uint8_t>(event))) != 0;
}

float KX_MockUIAdapter::SliderValue(int handle) const
{
  const KX_UIElement *element = Find(handle);
  return element != nullptr ? element->value : 0.0f;
}

bool KX_MockUIAdapter::KnobPosition(int handle, float &x, float &y) const
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

bool KX_MockUIAdapter::MoveElement(int handle, int mode)
{
  ++move_calls;
  KX_UIElement *element = Find(handle);
  if (element == nullptr) {
    return false;
  }
  /* The adapter table is painter ordered, so moving in the table is the same
   * as moving in the hierarchy. Handles are rebuilt from the old order. */
  std::vector<std::pair<int, KX_UIElement>> ordered(elements.size());
  for (const auto &entry : by_handle) {
    if (entry.second < elements.size()) {
      ordered[entry.second] = {entry.first, elements[entry.second]};
    }
  }
  size_t index = by_handle[handle];
  ordered.erase(ordered.begin() + index);
  size_t target = index;
  if (mode == 0) {
    target = ordered.size();
  }
  else if (mode == 1) {
    target = index + 1 < elements.size() ? index + 1 : index;
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
  ordered.insert(ordered.begin() + target, {handle, *element});

  elements.clear();
  by_handle.clear();
  for (size_t slot = 0; slot < ordered.size(); ++slot) {
    elements.push_back(ordered[slot].second);
    by_handle[ordered[slot].first] = slot;
  }
  return true;
}

void KX_MockUIAdapter::DrawScreen(int width, int height)
{
  (void)width;
  (void)height;
  EndFrame();
}

bool KX_MockUIAdapter::HitTest(int handle, float x, float y) const
{
  ++hit_test_calls;
  const KX_UIElement *element = Find(handle);
  const bool result = element != nullptr && KX_UIElementContains(*element, x, y);
  HitCall call;
  call.handle = handle;
  call.x = x;
  call.y = y;
  call.result = result;
  hit_calls.push_back(call);
  return result;
}

int KX_MockUIAdapter::HitTestPoint(float x, float y) const
{
  ++hit_point_calls;
  /* Last created wins, matching the painter's order of the renderer. */
  for (size_t index = elements.size(); index > 0; --index) {
    const KX_UIElement &element = elements[index - 1];
    if (element.type == KX_UIElementType::Canvas) {
      continue;
    }
    if (KX_UIElementContains(element, x, y)) {
      for (const auto &entry : by_handle) {
        if (entry.second == index - 1) {
          return entry.first;
        }
      }
    }
  }
  return -1;
}

KX_UIElement *KX_MockUIAdapter::Find(int handle)
{
  const auto it = by_handle.find(handle);
  return it != by_handle.end() ? &elements[it->second] : nullptr;
}

const KX_UIElement *KX_MockUIAdapter::Find(int handle) const
{
  const auto it = by_handle.find(handle);
  return it != by_handle.end() ? &elements[it->second] : nullptr;
}

void KX_MockUIAdapter::Reset()
{
  elements.clear();
  by_handle.clear();
  attribute_calls.clear();
  hit_calls.clear();
  canvas_names.clear();
  image_names.clear();
  event_flags.clear();
  begin_frame_calls = 0;
  end_frame_calls = 0;
  draw_calls = 0;
  create_canvas_calls = 0;
  create_label_calls = 0;
  create_image_calls = 0;
  create_button_calls = 0;
  create_layout_calls = 0;
  create_slider_calls = 0;
  set_attribute_calls = 0;
  get_attribute_calls = 0;
  event_calls = 0;
  move_calls = 0;
  hit_test_calls = 0;
  hit_point_calls = 0;
  destroy_calls = 0;
  last_delta_time = 0.0f;
  last_mouse_x = 0.0f;
  last_mouse_y = 0.0f;
  last_mouse_down = false;
  previous_mouse_down = false;
  pressed_handle = -1;
  hovered_handle = -1;
  next_handle_ = 1;
}

void KX_MockMessageAdapter::Send(const std::string &to,
                                 KX_GameObject *from,
                                 const std::string &subject,
                                 const std::string &body)
{
  KX_Message message;
  message.to = to;
  message.from = from != nullptr ? from->GetName() : std::string();
  message.subject = subject;
  message.body = body;
  ++send_calls;
  sent.push_back(message);
  mailbox.push_back(message);
}

std::vector<KX_Message> KX_MockMessageAdapter::Receive(const std::string &to,
                                                       const std::string &subject)
{
  ++receive_calls;
  std::vector<KX_Message> result;
  for (const KX_Message &message : mailbox) {
    if (!message.to.empty() && message.to != to) {
      continue;
    }
    if (!subject.empty() && message.subject != subject) {
      continue;
    }
    result.push_back(message);
  }
  delivered.insert(delivered.end(), result.begin(), result.end());
  return result;
}

void KX_MockMessageAdapter::Reset()
{
  sent.clear();
  mailbox.clear();
  delivered.clear();
  send_calls = 0;
  receive_calls = 0;
}

uint64_t KX_MockAudioAdapter::PlaySound(const std::string &filepath,
                                        bool loop,
                                        float volume,
                                        float pitch)
{
  ++play_calls;
  Entry entry;
  entry.filepath = filepath;
  entry.loop = loop;
  entry.volume = volume;
  entry.pitch = pitch;
  entry.playing = true;
  entry.finished = false;
  const uint64_t handle = next_handle++;
  entries[handle] = entry;
  return handle;
}

uint64_t KX_MockAudioAdapter::PlaySoundByName(const std::string &name,
                                              bool loop,
                                              float volume,
                                              float pitch)
{
  ++play_by_name_calls;
  by_name_requests.push_back(name);
  if (name.empty()) {
    missing_names.push_back(name);
    return 0;
  }
  return PlaySound(name, loop, volume, pitch);
}

void KX_MockAudioAdapter::Set3DListener(const float position[3], const float orientation[3])
{
  ++listener_calls;
  for (int i = 0; i < 3; ++i) {
    listener_position[i] = position != nullptr ? position[i] : 0.0f;
    listener_orientation[i] = orientation != nullptr ? orientation[i] : 0.0f;
  }
}

void KX_MockAudioAdapter::StopSound(uint64_t handle)
{
  ++stop_calls;
  std::map<uint64_t, Entry>::iterator it = entries.find(handle);
  if (it == entries.end()) {
    return;
  }
  it->second.playing = false;
  it->second.finished = true;
}

void KX_MockAudioAdapter::SetVolume(uint64_t handle, float volume)
{
  ++volume_calls;
  std::map<uint64_t, Entry>::iterator it = entries.find(handle);
  if (it != entries.end()) {
    it->second.volume = volume;
  }
}

void KX_MockAudioAdapter::SetPitch(uint64_t handle, float pitch)
{
  ++pitch_calls;
  std::map<uint64_t, Entry>::iterator it = entries.find(handle);
  if (it != entries.end()) {
    it->second.pitch = pitch;
  }
}

bool KX_MockAudioAdapter::IsPlaying(uint64_t handle)
{
  std::map<uint64_t, Entry>::iterator it = entries.find(handle);
  return it != entries.end() && it->second.playing;
}

bool KX_MockAudioAdapter::IsFinished(uint64_t handle)
{
  std::map<uint64_t, Entry>::iterator it = entries.find(handle);
  return it == entries.end() || it->second.finished;
}

void KX_MockAudioAdapter::Reset()
{
  next_handle = 1;
  entries.clear();
  by_name_requests.clear();
  missing_names.clear();
  listener_position[0] = listener_position[1] = listener_position[2] = 0.0f;
  listener_orientation[0] = listener_orientation[1] = listener_orientation[2] = 0.0f;
  play_calls = 0;
  play_by_name_calls = 0;
  listener_calls = 0;
  stop_calls = 0;
  volume_calls = 0;
  pitch_calls = 0;
}

bool KX_MockSceneControlAdapter::SuspendScene()
{
  ++suspend_calls;
  if (!suspend_result) {
    return false;
  }
  suspended = true;
  return true;
}

bool KX_MockSceneControlAdapter::ResumeScene()
{
  ++resume_calls;
  if (!resume_result) {
    return false;
  }
  suspended = false;
  return true;
}

bool KX_MockSceneControlAdapter::IsSuspended() const
{
  return suspended;
}

KX_GameObject *KX_MockSceneControlAdapter::AddObject(const std::string &name)
{
  ++add_calls;
  add_requests.push_back(name);
  return next_add_result;
}

void KX_MockSceneControlAdapter::Reset()
{
  suspended = false;
  suspend_result = true;
  resume_result = true;
  add_requests.clear();
  next_add_result = nullptr;
  suspend_calls = 0;
  resume_calls = 0;
  add_calls = 0;
}

std::unique_ptr<KX_AdapterSet> KX_CreateNullAdapters()
{
  std::unique_ptr<KX_AdapterSet> set = std::make_unique<KX_AdapterSet>();
  set->input = std::make_unique<NullInputAdapter>();
  set->scene = std::make_unique<NullSceneAdapter>();
  set->physics = std::make_unique<NullPhysicsAdapter>();
  set->objects = std::make_unique<NullObjectAdapter>();
  set->animation = std::make_unique<NullAnimationAdapter>();
  set->message = std::make_unique<NullMessageAdapter>();
  set->audio = std::make_unique<NullAudioAdapter>();
  set->scene_control = std::make_unique<NullSceneControlAdapter>();
  set->ui = std::make_unique<NullUIAdapter>();
  set->file = KX_CreateNullFileAdapter();
  return set;
}

void KX_AdapterSet::BeginFrame(float delta_time)
{
  if (input) {
    input->BeginFrame(delta_time);
  }

  /* The UI frame needs the mouse state, so it starts after the input adapter
   * computed this frame's edges. */
  if (ui) {
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool mouse_down = false;
    if (input) {
      input->MousePosition(mouse_x, mouse_y);
      mouse_down = input->MouseButtonActive(0, KX_InputMode::Hold);
    }
    ui->BeginFrame(delta_time, mouse_x, mouse_y, mouse_down);
  }
}

void KX_AdapterSet::EndFrame()
{
  if (input) {
    input->EndFrame();
  }
}

void KX_AdapterSet::DrawUI()
{
  if (ui) {
    ui->EndFrame();
  }
}
