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

#include <cmath>
#include <cstdlib>
#include <string>

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

class NullNetworkAdapter final : public KX_NetworkAdapter {
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

KX_NetworkAdapter &KX_NullNetworkAdapter()
{
  static NullNetworkAdapter adapter;
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

  /* B6.7: keep the hit test on the same Polar coordinates the draw pass uses. */
  KX_UILayoutPolarElements(elements);

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

bool KX_MockUIAdapter::LoadFont(const std::string &name, const std::string &path)
{
  ++load_font_calls;
  if (name.empty() || path.empty()) {
    return false;
  }
  fonts[name] = path;
  return true;
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
  if (width > 0 && height > 0) {
    screen_w_ = width;
    screen_h_ = height;
  }
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

void KX_MockMessageAdapter::SendDelayed(const std::string &to,
                                         KX_GameObject *from,
                                         const std::string &subject,
                                         const std::string &body,
                                         float delay_seconds)
{
  if (!(delay_seconds > 0.0f)) {
    Send(to, from, subject, body);
    return;
  }
  DelayedMessage entry;
  entry.message.to = to;
  entry.message.from = from != nullptr ? from->GetName() : std::string();
  entry.message.subject = subject;
  entry.message.body = body;
  entry.from = from;
  entry.remaining = delay_seconds;
  delayed.push_back(entry);
}

void KX_MockMessageAdapter::TickDelayed(float delta_time)
{
  if (delta_time <= 0.0f) {
    return;
  }
  for (size_t index = 0; index < delayed.size();) {
    delayed[index].remaining -= delta_time;
    if (delayed[index].remaining > 0.0f) {
      ++index;
      continue;
    }
    const DelayedMessage entry = delayed[index];
    delayed.erase(delayed.begin() + static_cast<std::ptrdiff_t>(index));
    ++delayed_send_calls;
    Send(entry.message.to, entry.from, entry.message.subject, entry.message.body);
  }
}

void KX_MockMessageAdapter::Reset()
{
  dropped_delayed += static_cast<uint32_t>(delayed.size());
  delayed.clear();
  sent.clear();
  mailbox.clear();
  delivered.clear();
  send_calls = 0;
  receive_calls = 0;
  delayed_send_calls = 0;
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
  HandleSettings settings = global_settings;
  settings.volume = volume;
  handle_settings[handle] = settings;
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
  ReleaseSoundOwner(handle);
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
  attenuation_requests.clear();
  doppler_requests.clear();
  zone_requests.clear();
  bus_requests.clear();
  spatial_requests.clear();
  attenuation_calls = 0;
  doppler_calls = 0;
  zone_calls = 0;
  bus_calls = 0;
  spatial_calls = 0;
  handle_settings.clear();
  global_settings = HandleSettings();
  ClearSoundOwners();
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
  set->network = std::make_unique<NullNetworkAdapter>();
  set->physics_advanced = KX_CreateNullPhysicsAdvancedAdapter();
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

  /* B6.3: pump the non-blocking sockets once per logic frame. */
  if (network) {
    network->Tick();
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

/* ------------------------------------------------------------------------- */
/* B6.2 input extensions                                                      */
/* ------------------------------------------------------------------------- */

namespace {

std::string TrimSpaces(const std::string &text)
{
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
    ++begin;
  }
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool ParseIntStrict(const std::string &text, int &out)
{
  if (text.empty()) {
    return false;
  }
  int sign = 1;
  size_t i = 0;
  if (text[0] == '-') {
    sign = -1;
    i = 1;
  }
  else if (text[0] == '+') {
    i = 1;
  }
  if (i >= text.size()) {
    return false;
  }
  int value = 0;
  for (; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
    value = value * 10 + (text[i] - '0');
  }
  out = value * sign;
  return true;
}

/** Split "a:b[:c]" into up to three parts; the third may be empty. */
bool SplitOptionalThird(const std::string &text, std::string *parts)
{
  const size_t first = text.find(':');
  if (first == std::string::npos) {
    return false;
  }
  parts[0] = text.substr(0, first);
  const size_t second = text.find(':', first + 1);
  if (second == std::string::npos) {
    parts[1] = text.substr(first + 1);
    parts[2].clear();
  }
  else {
    parts[1] = text.substr(first + 1, second - first - 1);
    parts[2] = text.substr(second + 1);
  }
  return !parts[0].empty() && !parts[1].empty();
}

bool ParseFloatStrict(const std::string &text, float &out)
{
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const float value = std::strtof(text.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  out = value;
  return true;
}

float ClampUnit(float value)
{
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

/** Remap a stick pair after a radial deadzone; inside the zone both read 0. */
void ApplyRadialDeadzone(float &x, float &y, float deadzone)
{
  deadzone = ClampUnit(deadzone);
  if (deadzone <= 0.0f) {
    return;
  }
  float magnitude = std::sqrt(x * x + y * y);
  if (magnitude > 1.0f) {
    x /= magnitude;
    y /= magnitude;
    magnitude = 1.0f;
  }
  if (magnitude <= deadzone) {
    x = 0.0f;
    y = 0.0f;
    return;
  }
  const float scale = ((magnitude - deadzone) / (1.0f - deadzone)) / magnitude;
  x *= scale;
  y *= scale;
}

/** Remap a 0..1 scalar after a deadzone; inside the zone it reads 0. */
float ApplyScalarDeadzone(float value, float deadzone)
{
  value = ClampUnit(value);
  deadzone = ClampUnit(deadzone);
  if (value <= deadzone) {
    return 0.0f;
  }
  if (deadzone <= 0.0f) {
    return value;
  }
  return (value - deadzone) / (1.0f - deadzone);
}

}  // namespace

void KX_InputAdapter::SetInputMap(const std::string &name,
                                  const std::string &binding,
                                  int priority)
{
  std::map<std::string, int>::const_iterator it = input_map_priority_.find(name);
  if (it != input_map_priority_.end() && priority < it->second) {
    return;
  }
  input_map_[name] = binding;
  input_map_priority_[name] = priority;
}

bool KX_InputAdapter::ReadInputMap(const std::string &name, float &value, bool &down)
{
  value = 0.0f;
  down = false;
  std::map<std::string, std::string>::const_iterator it = input_map_.find(name);
  if (it == input_map_.end()) {
    return false;
  }
  const std::string &binding = it->second;
  const size_t colon = binding.find(':');
  if (colon == std::string::npos) {
    return false;
  }
  const std::string kind = binding.substr(0, colon);
  const std::string rest = binding.substr(colon + 1);

  if (kind == "key") {
    int code = 0;
    if (!ParseIntStrict(rest, code)) {
      return false;
    }
    down = KeyActive(code, KX_InputMode::Hold);
    value = down ? 1.0f : 0.0f;
    return true;
  }
  if (kind == "keycombo") {
    down = IsKeyComboDown(rest);
    value = down ? 1.0f : 0.0f;
    return true;
  }
  if (kind == "mouse") {
    int button = 0;
    if (!ParseIntStrict(rest, button)) {
      return false;
    }
    down = MouseButtonActive(button, KX_InputMode::Hold);
    value = down ? 1.0f : 0.0f;
    return true;
  }
  if (kind == "gamepad_button") {
    std::string parts[3];
    int pad = 0;
    int button = 0;
    float deadzone = 0.0f;
    if (!SplitOptionalThird(rest, parts) || !ParseIntStrict(parts[0], pad) ||
        !ParseIntStrict(parts[1], button) ||
        (!parts[2].empty() && !ParseFloatStrict(parts[2], deadzone)))
    {
      return false;
    }
    value = ApplyScalarDeadzone(GetGamepadButtonValue(pad, button), deadzone);
    down = value > 0.0f;
    return true;
  }
  if (kind == "gamepad_axis") {
    std::string parts[3];
    int pad = 0;
    int axis = 0;
    float deadzone = 0.0f;
    if (!SplitOptionalThird(rest, parts) || !ParseIntStrict(parts[0], pad) ||
        !ParseIntStrict(parts[1], axis) ||
        (!parts[2].empty() && !ParseFloatStrict(parts[2], deadzone)))
    {
      return false;
    }
    float x = 0.0f;
    float y = 0.0f;
    GetGamepadAxis(pad, axis, x, y);
    ApplyRadialDeadzone(x, y, deadzone);
    value = x;
    down = x != 0.0f || y != 0.0f;
    return true;
  }
  if (kind == "gamepad_trigger") {
    std::string parts[3];
    int pad = 0;
    int trigger = 0;
    float deadzone = 0.0f;
    if (!SplitOptionalThird(rest, parts) || !ParseIntStrict(parts[0], pad) ||
        !ParseIntStrict(parts[1], trigger) ||
        (!parts[2].empty() && !ParseFloatStrict(parts[2], deadzone)))
    {
      return false;
    }
    float amount = 0.0f;
    GetGamepadTrigger(pad, trigger, amount);
    value = ApplyScalarDeadzone(amount, deadzone);
    down = value > 0.0f;
    return true;
  }
  return false;
}

void KX_MockInputAdapter::SetGamepadButton(int pad, int button, bool down)
{
  gamepad_buttons[std::make_pair(pad, button)] = down;
}

void KX_MockInputAdapter::SetGamepadButtonValue(int pad, int button, float value)
{
  gamepad_button_values[std::make_pair(pad, button)] = value;
}

void KX_MockInputAdapter::SetGamepadCount(int count)
{
  gamepad_count = count < 0 ? 0 : count;
}

void KX_MockInputAdapter::SetGamepadAxis(int pad, int axis, float x, float y)
{
  gamepad_axes[std::make_pair(pad, axis)] = std::make_pair(x, y);
}

void KX_MockInputAdapter::SetGamepadTrigger(int pad, int trigger, float value)
{
  gamepad_triggers[std::make_pair(pad, trigger)] = value;
}

void KX_MockInputAdapter::SetTouch(int index, float x, float y, bool down, float pressure)
{
  if (index < 0) {
    return;
  }
  if (static_cast<size_t>(index) >= touches.size()) {
    touches.resize(static_cast<size_t>(index) + 1);
  }
  touches[static_cast<size_t>(index)].x = x;
  touches[static_cast<size_t>(index)].y = y;
  touches[static_cast<size_t>(index)].down = down;
  touches[static_cast<size_t>(index)].pressure = pressure;
}

void KX_MockInputAdapter::SetKeyDown(const std::string &key, bool down)
{
  pressed_keys[key] = down;
}

bool KX_MockInputAdapter::IsGamepadButtonDown(int pad, int button)
{
  std::map<std::pair<int, int>, bool>::const_iterator it =
      gamepad_buttons.find(std::make_pair(pad, button));
  return it != gamepad_buttons.end() && it->second;
}

float KX_MockInputAdapter::GetGamepadButtonValue(int pad, int button)
{
  std::map<std::pair<int, int>, float>::const_iterator it =
      gamepad_button_values.find(std::make_pair(pad, button));
  if (it != gamepad_button_values.end()) {
    return ClampUnit(it->second);
  }
  return IsGamepadButtonDown(pad, button) ? 1.0f : 0.0f;
}

int KX_MockInputAdapter::GetGamepadCount()
{
  if (gamepad_count > 0) {
    return gamepad_count;
  }
  int highest = -1;
  const auto note_pad = [&highest](const std::pair<int, int> &key) {
    if (key.first > highest) {
      highest = key.first;
    }
  };
  for (const auto &entry : gamepad_buttons) {
    note_pad(entry.first);
  }
  for (const auto &entry : gamepad_button_values) {
    note_pad(entry.first);
  }
  for (const auto &entry : gamepad_axes) {
    note_pad(entry.first);
  }
  for (const auto &entry : gamepad_triggers) {
    note_pad(entry.first);
  }
  return highest + 1;
}

void KX_MockInputAdapter::GetGamepadAxis(int pad, int axis, float &x, float &y)
{
  std::map<std::pair<int, int>, std::pair<float, float> >::const_iterator it =
      gamepad_axes.find(std::make_pair(pad, axis));
  if (it == gamepad_axes.end()) {
    x = 0.0f;
    y = 0.0f;
    return;
  }
  x = it->second.first;
  y = it->second.second;
}

void KX_MockInputAdapter::GetGamepadTrigger(int pad, int trigger, float &value)
{
  std::map<std::pair<int, int>, float>::const_iterator it =
      gamepad_triggers.find(std::make_pair(pad, trigger));
  value = it != gamepad_triggers.end() ? it->second : 0.0f;
}

void KX_MockInputAdapter::GetTouch(int index, float &x, float &y, bool &down)
{
  if (index < 0 || static_cast<size_t>(index) >= touches.size()) {
    x = 0.0f;
    y = 0.0f;
    down = false;
    return;
  }
  const TouchPoint &point = touches[static_cast<size_t>(index)];
  x = point.x;
  y = point.y;
  down = point.down;
}

int KX_MockInputAdapter::GetTouchCount()
{
  return static_cast<int>(touches.size());
}

float KX_MockInputAdapter::GetTouchPressure(int index)
{
  if (index < 0 || static_cast<size_t>(index) >= touches.size()) {
    return 0.0f;
  }
  return ClampUnit(touches[static_cast<size_t>(index)].pressure);
}

bool KX_MockInputAdapter::IsKeyComboDown(const std::string &keys)
{
  const std::string trimmed = TrimSpaces(keys);
  if (trimmed.empty()) {
    return false;
  }
  size_t start = 0;
  while (start <= trimmed.size()) {
    const size_t plus = trimmed.find('+', start);
    const std::string token = TrimSpaces(
        trimmed.substr(start, plus == std::string::npos ? std::string::npos : plus - start));
    if (token.empty()) {
      return false;
    }
    std::map<std::string, bool>::const_iterator it = pressed_keys.find(token);
    if (it == pressed_keys.end() || !it->second) {
      return false;
    }
    if (plus == std::string::npos) {
      break;
    }
    start = plus + 1;
  }
  return true;
}

bool KX_MockInputAdapter::VibrateGamepad(int pad, float intensity, float duration)
{
  ++vibrate_calls;
  vibration_requests.push_back(std::to_string(pad) + ":" + std::to_string(intensity) + ":" +
                               std::to_string(duration));
  return vibrate_result;
}

void KX_MockInputAdapter::Reset()
{
  gamepad_buttons.clear();
  gamepad_button_values.clear();
  gamepad_axes.clear();
  gamepad_triggers.clear();
  touches.clear();
  pressed_keys.clear();
  vibration_requests.clear();
  vibrate_calls = 0;
  vibrate_result = true;
  gamepad_count = 0;
  input_map_.clear();
  input_map_priority_.clear();
}

/* ------------------------------------------------------------------------- */
/* B6.2 audio recordings                                                      */
/* ------------------------------------------------------------------------- */

const KX_MockAudioAdapter::HandleSettings &KX_MockAudioAdapter::SettingsOf(
    uint64_t handle) const
{
  std::map<uint64_t, HandleSettings>::const_iterator found = handle_settings.find(handle);
  return found != handle_settings.end() ? found->second : global_settings;
}

void KX_MockAudioAdapter::SetAttenuation(KX_GameObject *object,
                                         int model,
                                         float reference_distance,
                                         float maximum_distance)
{
  ++attenuation_calls;
  AttenuationRecord record;
  record.object = object;
  record.model = model;
  record.reference_distance = reference_distance;
  record.maximum_distance = maximum_distance;
  attenuation_requests.push_back(record);

  if (object == nullptr) {
    global_settings.model = model;
    global_settings.reference_distance = reference_distance;
    global_settings.maximum_distance = maximum_distance;
    for (std::map<uint64_t, HandleSettings>::iterator it = handle_settings.begin();
         it != handle_settings.end(); ++it)
    {
      it->second.model = model;
      it->second.reference_distance = reference_distance;
      it->second.maximum_distance = maximum_distance;
    }
    return;
  }

  /* Object scoped: only the handles owned by this object change. */
  const std::vector<uint64_t> handles = HandlesFor(object);
  for (uint64_t handle : handles) {
    std::map<uint64_t, HandleSettings>::iterator it = handle_settings.find(handle);
    if (it == handle_settings.end()) {
      continue;
    }
    it->second.model = model;
    it->second.reference_distance = reference_distance;
    it->second.maximum_distance = maximum_distance;
  }
}

void KX_MockAudioAdapter::SetDopplerFactor(KX_GameObject *object, float factor)
{
  ++doppler_calls;
  DopplerRecord record;
  record.object = object;
  record.factor = factor;
  doppler_requests.push_back(record);

  if (object == nullptr) {
    global_settings.doppler = factor;
    for (std::map<uint64_t, HandleSettings>::iterator it = handle_settings.begin();
         it != handle_settings.end(); ++it)
    {
      it->second.doppler = factor;
    }
    return;
  }

  const std::vector<uint64_t> handles = HandlesFor(object);
  for (uint64_t handle : handles) {
    std::map<uint64_t, HandleSettings>::iterator it = handle_settings.find(handle);
    if (it != handle_settings.end()) {
      it->second.doppler = factor;
    }
  }
}

uint64_t KX_MockAudioAdapter::SetAudioZone(const std::string &zone_name,
                                           const std::string &sound,
                                           float volume)
{
  ++zone_calls;
  ZoneRecord record;
  record.zone = zone_name;
  record.sound = sound;
  record.volume = volume;
  zone_requests.push_back(record);
  return PlaySoundByName(sound, true, volume, 1.0f);
}

void KX_MockAudioAdapter::SetAudioBus(const std::string &bus, float volume)
{
  ++bus_calls;
  BusRecord record;
  record.bus = bus;
  record.volume = volume;
  bus_requests.push_back(record);
}

uint64_t KX_MockAudioAdapter::PlaySound3D(const std::string &sound,
                                          const float position[3],
                                          float volume,
                                          float pitch)
{
  ++spatial_calls;
  SpatialRecord record;
  record.sound = sound;
  for (int i = 0; i < 3; ++i) {
    record.position[i] = position != nullptr ? position[i] : 0.0f;
  }
  record.volume = volume;
  record.pitch = pitch;
  spatial_requests.push_back(record);
  return PlaySoundByName(sound, false, volume, pitch);
}

/* ------------------------------------------------------------------------- */
/* B6.3 mock network adapter                                                  */
/* ------------------------------------------------------------------------- */

void KX_MockNetworkAdapter::Reset()
{
  host_result = true;
  join_result = true;
  local_player = 0;
  player_count = 1;
  role = Role::None;
  connected = false;
  last_error.clear();
  host_called = false;
  join_called = false;
  leave_called = false;
  host_port = 0;
  host_max_players = 0;
  join_host.clear();
  join_port = 0;
  list_result = true;
  refresh_result = true;
  reconnect_result = true;
  room_name.clear();
  room_list.clear();
  list_called = false;
  refresh_called = false;
  list_host.clear();
  list_port = 0;
  reconnect_calls = 0;
  reconnect_attempts = 0;
  reconnect_max_attempts = 0;
  reconnect_backoff_ms = 0;
  reconnect_status.clear();
  sent.clear();
  events.clear();
  local_props.clear();
  remote_props.clear();
}

bool KX_MockNetworkAdapter::HostRoom(int port, int max_players, std::string *error)
{
  host_called = true;
  host_port = port;
  host_max_players = max_players;
  if (!host_result) {
    last_error = "mock host failure";
    if (error != nullptr) {
      *error = last_error;
    }
    return false;
  }
  role = Role::Host;
  connected = true;
  local_player = 0;
  player_count = 1;
  return true;
}

bool KX_MockNetworkAdapter::JoinRoom(const std::string &host, int port, std::string *error)
{
  join_called = true;
  join_host = host;
  join_port = port;
  if (!join_result) {
    last_error = "mock join failure";
    if (error != nullptr) {
      *error = last_error;
    }
    return false;
  }
  role = Role::Client;
  connected = true;
  player_count = 2;
  if (!events.empty() || local_player != 0) {
    return true;
  }
  KX_NetworkEvent event;
  event.type = KX_NetworkEvent::PlayerJoin;
  event.player_id = 0;
  events.push_back(event);
  event.type = KX_NetworkEvent::PlayerJoin;
  event.player_id = local_player;
  events.push_back(event);
  return true;
}

void KX_MockNetworkAdapter::LeaveRoom()
{
  leave_called = true;
  if (connected) {
    KX_NetworkEvent event;
    event.type = KX_NetworkEvent::PlayerLeave;
    event.player_id = local_player;
    events.push_back(event);
  }
  connected = false;
  role = Role::None;
  sent.clear();
  player_count = 0;
}

bool KX_MockNetworkAdapter::SendReliable(int target_id, const void *data, size_t len)
{
  if (!connected || data == nullptr) {
    return false;
  }
  SentPacket packet;
  packet.target = target_id;
  packet.reliable = true;
  packet.data.assign(static_cast<const char *>(data), len);
  sent.push_back(packet);
  return true;
}

bool KX_MockNetworkAdapter::SendUnreliable(int target_id, const void *data, size_t len)
{
  if (!connected || data == nullptr) {
    return false;
  }
  SentPacket packet;
  packet.target = target_id;
  packet.reliable = false;
  packet.data.assign(static_cast<const char *>(data), len);
  sent.push_back(packet);
  return true;
}

bool KX_MockNetworkAdapter::BroadcastReliable(const void *data, size_t len)
{
  if (!connected || data == nullptr) {
    return false;
  }
  SentPacket packet;
  packet.target = -1;
  packet.reliable = true;
  packet.broadcast = true;
  packet.data.assign(static_cast<const char *>(data), len);
  sent.push_back(packet);
  return true;
}

void KX_MockNetworkAdapter::SetSyncProperty(const std::string &key, const KX_RuntimeValue &value)
{
  local_props[key] = value;
  if (connected) {
    remote_props[local_player][key] = value;
  }
}

bool KX_MockNetworkAdapter::GetRemoteProperty(int player_id,
                                             const std::string &key,
                                             KX_RuntimeValue *out) const
{
  std::map<int, std::map<std::string, KX_RuntimeValue>>::const_iterator player =
      remote_props.find(player_id);
  if (player == remote_props.end()) {
    return false;
  }
  std::map<std::string, KX_RuntimeValue>::const_iterator property = player->second.find(key);
  if (property == player->second.end()) {
    return false;
  }
  if (out != nullptr) {
    *out = property->second;
  }
  return true;
}

bool KX_MockNetworkAdapter::PollEvent(KX_NetworkEvent &out)
{
  if (events.empty()) {
    return false;
  }
  out = events.front();
  events.erase(events.begin());
  return true;
}

void KX_MockNetworkAdapter::RequeueEvent(const KX_NetworkEvent &event)
{
  events.insert(events.begin(), event);
}

bool KX_MockNetworkAdapter::ListRooms(const std::string &host,
                                      int port,
                                      std::vector<KX_RoomInfo> &out,
                                      std::string *error)
{
  list_called = true;
  list_host = host;
  list_port = port;
  out = room_list;
  if (!list_result) {
    last_error = "mock list failure";
    if (error != nullptr) {
      *error = last_error;
    }
    return false;
  }
  return true;
}

bool KX_MockNetworkAdapter::RefreshRooms(const std::string &host,
                                         int port,
                                         std::vector<KX_RoomInfo> &out,
                                         std::string *error)
{
  refresh_called = true;
  list_host = host;
  list_port = port;
  if (!refresh_result) {
    out.clear();
    last_error = "mock refresh failure";
    if (error != nullptr) {
      *error = last_error;
    }
    return false;
  }
  return ListRooms(host, port, out, error);
}

bool KX_MockNetworkAdapter::Reconnect(int max_attempts,
                                      int backoff_ms,
                                      int *attempts,
                                      std::string *error)
{
  ++reconnect_calls;
  reconnect_max_attempts = max_attempts;
  reconnect_backoff_ms = backoff_ms;
  if (connected) {
    reconnect_attempts = 0;
    reconnect_status = "already connected";
    if (attempts != nullptr) {
      *attempts = 0;
    }
    return true;
  }
  if (!reconnect_result) {
    reconnect_attempts = max_attempts;
    reconnect_status = "mock reconnect failure";
    last_error = reconnect_status;
    if (attempts != nullptr) {
      *attempts = reconnect_attempts;
    }
    if (error != nullptr) {
      *error = last_error;
    }
    return false;
  }
  reconnect_attempts = 1;
  reconnect_status = "connected";
  connected = true;
  role = Role::Client;
  local_player = 1;
  player_count = 2;
  if (attempts != nullptr) {
    *attempts = reconnect_attempts;
  }
  return true;
}
