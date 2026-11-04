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

/** \file KX_UIRenderer.cpp
 *  \ingroup gamelogic
 */

#include "KX_UIRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "BLF_api.hh"
#include "BKE_global.hh"
#include "BKE_image_gpu.hh"
#include "BKE_lib_id.hh"
#include "DNA_image_types.h"
#include "GPU_batch.hh"
#include "GPU_batch_presets.hh"
#include "GPU_context.hh"
#include "GPU_immediate.hh"
#include "GPU_matrix.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"

using namespace blender;

namespace {

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

bool gpu_available()
{
  return GPU_context_active_get() != nullptr;
}

float clamp_alpha(float alpha, float opacity)
{
  const float result = alpha * opacity;
  return result < 0.0f ? 0.0f : (result > 1.0f ? 1.0f : result);
}

std::vector<std::string> split_lines(const std::string &text)
{
  std::vector<std::string> lines;
  if (text.empty()) {
    return lines;
  }
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find('\n', start);
    lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return lines;
}

std::vector<std::string> wrap_lines(int font, const std::string &text, float max_width)
{
  std::vector<std::string> lines;
  for (const std::string &paragraph : split_lines(text)) {
    if (max_width <= 0.0f) {
      lines.push_back(paragraph);
      continue;
    }
    std::string current;
    size_t cursor = 0;
    while (cursor < paragraph.size()) {
      const size_t space = paragraph.find(' ', cursor);
      const std::string word = paragraph.substr(
          cursor, space == std::string::npos ? std::string::npos : space - cursor);
      std::string candidate = current.empty() ? word : current + " " + word;
      if (BLF_width(font, candidate.c_str(), candidate.size()) <= max_width || current.empty()) {
        current = candidate;
      }
      else {
        lines.push_back(current);
        current = word;
      }
      if (space == std::string::npos) {
        break;
      }
      cursor = space + 1;
    }
    lines.push_back(current);
  }
  return lines;
}

}  // namespace

struct KX_UIRenderer::Impl {
  struct Entry {
    int handle = -1;
    uint32_t revision = 0xffffffffu;
    float rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<std::string> lines;
    float text_w = 0.0f;
    float text_h = 0.0f;
    bool layout_valid = false;
    bool text_valid = false;
    bool texture_valid = false;
    std::string texture_name;
  };

  std::vector<Entry> entries;
  std::map<int, size_t> by_handle;
  std::map<std::string, gpu::Texture *> textures;
  std::map<std::string, bool> reported_missing;
  KX_UIRenderStats stats;

  Entry &entry_for(const KX_UIElement &element, size_t index)
  {
    if (entries.size() <= index) {
      entries.resize(index + 1);
    }
    Entry &entry = entries[index];
    if (entry.handle != element.handle) {
      entry = Entry();
      entry.handle = element.handle;
      by_handle[element.handle] = index;
    }
    return entry;
  }

  const Entry *find_entry(int handle, size_t index_limit) const
  {
    const auto it = by_handle.find(handle);
    if (it == by_handle.end() || it->second >= index_limit) {
      return nullptr;
    }
    return &entries[it->second];
  }

  void free_textures()
  {
    for (auto &entry : textures) {
      if (entry.second != nullptr) {
        GPU_texture_free(entry.second);
      }
    }
    textures.clear();
  }

  gpu::Texture *texture_for(const std::string &name)
  {
    if (name.empty()) {
      return nullptr;
    }
    const auto it = textures.find(name);
    if (it != textures.end()) {
      return it->second;
    }
    Image *image = reinterpret_cast<Image *>(
        BKE_libblock_find_name(G_MAIN, ID_IM, name.c_str()));
    if (image == nullptr) {
      if (!reported_missing[name]) {
        std::fprintf(stderr, "[UI] image datablock '%s' not found; element skipped\n", name.c_str());
        reported_missing[name] = true;
      }
      textures[name] = nullptr;
      return nullptr;
    }
    gpu::Texture *texture = BKE_image_acquire_gpu_texture(image, nullptr);
    if (texture == nullptr) {
      if (!reported_missing[name]) {
        std::fprintf(stderr, "[UI] image datablock '%s' has no GPU texture\n", name.c_str());
        reported_missing[name] = true;
      }
    }
    textures[name] = texture;
    return texture;
  }
};

KX_UIRenderer::KX_UIRenderer() : impl_(std::make_unique<Impl>()) {}

KX_UIRenderer::~KX_UIRenderer()
{
  if (gpu_available()) {
    impl_->free_textures();
  }
}

void KX_UIRenderer::Invalidate()
{
  impl_->entries.clear();
  impl_->by_handle.clear();
}

const KX_UIRenderStats &KX_UIRenderer::Stats() const
{
  return impl_->stats;
}

void KX_UIRenderer::Draw(const std::vector<KX_UIElement> &elements,
                         int screen_w,
                         int screen_h,
                         bool use_gpu)
{
  const auto start = std::chrono::high_resolution_clock::now();
  Impl &state = *impl_;
  state.stats.elements = int(elements.size());
  int rebuilds = 0;

  if (elements.empty()) {
    state.stats.idle_ms = 0.0;
    return;
  }

  /* Resolve absolute rectangles. Elements are created before their children,
   * so a single forward pass with a parent lookup is enough. */
  std::map<int, Rect> rects;
  std::map<int, std::pair<float, float>> canvas_scale;
  for (size_t index = 0; index < elements.size(); ++index) {
    const KX_UIElement &element = elements[index];
    Impl::Entry &entry = state.entry_for(element, index);
    const bool dirty_layout = entry.revision != element.revision || !entry.layout_valid;
    if (dirty_layout) {
      ++rebuilds;
    }

    if (element.type == KX_UIElementType::Canvas) {
      Rect canvas_rect{0.0f, 0.0f, float(screen_w), float(screen_h)};
      float sx = 1.0f;
      float sy = 1.0f;
      if (element.design_size[0] > 0.0f && element.design_size[1] > 0.0f) {
        sx = float(screen_w) / element.design_size[0];
        sy = float(screen_h) / element.design_size[1];
        if (element.scale_mode == 1) {
          const float uniform = std::min(sx, sy);
          sx = sy = uniform;
        }
        else if (element.scale_mode == 2) {
          const float uniform = std::max(sx, sy);
          sx = sy = uniform;
        }
      }
      entry.rect[0] = canvas_rect.x;
      entry.rect[1] = canvas_rect.y;
      entry.rect[2] = canvas_rect.w;
      entry.rect[3] = canvas_rect.h;
      rects[element.handle] = canvas_rect;
      canvas_scale[element.handle] = {sx, sy};
      entry.layout_valid = true;
      entry.revision = element.revision;
      continue;
    }

    Rect parent{0.0f, 0.0f, float(screen_w), float(screen_h)};
    float sx = 1.0f;
    float sy = 1.0f;
    const auto parent_rect = rects.find(element.canvas);
    if (parent_rect != rects.end()) {
      parent = parent_rect->second;
    }
    const auto parent_scale = canvas_scale.find(element.canvas);
    if (parent_scale != canvas_scale.end()) {
      sx = parent_scale->second.first;
      sy = parent_scale->second.second;
    }

    float local_x = element.position[0];
    float local_y = element.position[1];
    float local_w = element.size[0];
    float local_h = element.size[1];
    if (element.rel_pos) {
      local_x *= parent.w;
      local_y *= parent.h;
    }
    if (element.rel_size) {
      local_w *= parent.w;
      local_h *= parent.h;
    }
    local_x *= sx;
    local_y *= sy;
    local_w *= sx;
    local_h *= sy;

    Rect resolved{parent.x + local_x, parent.y + local_y, local_w, local_h};
    resolved.x -= resolved.w * (float(element.anchor_x) * 0.5f);
    resolved.y -= resolved.h * (float(element.anchor_y) * 0.5f);
    entry.rect[0] = resolved.x;
    entry.rect[1] = resolved.y;
    entry.rect[2] = resolved.w;
    entry.rect[3] = resolved.h;
    rects[element.handle] = resolved;
    if (element.type == KX_UIElementType::Layout) {
      canvas_scale[element.handle] = {sx, sy};
    }

    /* Text layout is the expensive part; only rebuild it when the text,
     * font or box changed. */
    const bool text_dirty = entry.revision != element.revision || !entry.text_valid;
    if (text_dirty) {
      ++rebuilds;
      if (use_gpu && gpu_available() &&
          (element.type == KX_UIElementType::Label || element.type == KX_UIElementType::Button))
      {
        const int font = BLF_default();
        BLF_size(font, element.font_size * (element.font_size > 0.0f ? sy : 1.0f));
        entry.lines = element.wrap ? wrap_lines(font, element.text, resolved.w)
                                   : split_lines(element.text);
        float width = 0.0f;
        for (const std::string &line : entry.lines) {
          width = std::max(width, BLF_width(font, line.c_str(), line.size()));
        }
        entry.text_w = width;
        entry.text_h = float(entry.lines.size()) * (BLF_height(font, "Ay", 2) +
                                                    (element.line_height - 1.0f) * element.font_size);
        entry.text_valid = true;
      }
      else {
        entry.lines = split_lines(element.text);
        entry.text_w = resolved.w;
        entry.text_h = resolved.h;
        entry.text_valid = true;
      }
    }

    const bool texture_dirty = entry.revision != element.revision || !entry.texture_valid ||
                               entry.texture_name != element.image ||
                               (element.type == KX_UIElementType::Slider &&
                                entry.texture_name != element.bar_texture);
    if (texture_dirty) {
      entry.texture_name = element.image;
      entry.texture_valid = true;
    }

    entry.revision = element.revision;
    entry.layout_valid = true;
  }

  state.stats.full_rebuilds = rebuilds;
  state.stats.idle_rebuilds = rebuilds;
  const auto end = std::chrono::high_resolution_clock::now();
  state.stats.full_ms = std::chrono::duration<double, std::milli>(end - start).count();
  state.stats.idle_ms = state.stats.full_ms;

  if (!use_gpu || !gpu_available()) {
    return;
  }

  gpu::Batch *quad = GPU_batch_preset_quad();
  gpu::Shader *image_shader = GPU_shader_get_builtin_shader(GPU_SHADER_2D_IMAGE_RECT_COLOR);
  int image_color_loc = GPU_shader_get_builtin_uniform(image_shader, GPU_UNIFORM_COLOR);
  int image_rect_tex_loc = GPU_shader_get_uniform(image_shader, "rect_icon");
  int image_rect_geo_loc = GPU_shader_get_uniform(image_shader, "rect_geom");
  int image_sampler_loc = GPU_shader_get_sampler_binding(image_shader, "image");

  /* One level of clipping: everything is clipped to the first canvas. */
  bool clipped = false;
  for (const KX_UIElement &element : elements) {
    if (element.type == KX_UIElementType::Canvas) {
      const float *rect = state.entries[state.by_handle[element.handle]].rect;
      GPU_scissor_test(true);
      GPU_scissor(int(rect[0]), int(screen_h - rect[1] - rect[3]), int(rect[2]), int(rect[3]));
      clipped = true;
      break;
    }
  }

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", gpu::VertAttrType::SFLOAT_32_32);

  for (size_t index = 0; index < elements.size(); ++index) {
    const KX_UIElement &element = elements[index];
    if (!element.visible || element.type == KX_UIElementType::Canvas) {
      continue;
    }
    const Impl::Entry &entry = state.entries[index];
    const Rect rect{entry.rect[0], entry.rect[1], entry.rect[2], entry.rect[3]};

    GPU_matrix_push();
    if (element.angle != 0.0f) {
      GPU_matrix_translate_2f(rect.x + rect.w * 0.5f, screen_h - rect.y - rect.h * 0.5f);
      GPU_matrix_rotate_2d(element.angle);
      GPU_matrix_translate_2f(-(rect.x + rect.w * 0.5f), -(screen_h - rect.y - rect.h * 0.5f));
    }

    const auto draw_rect = [&](float x, float y, float w, float h, const float color[4]) {
      if (w <= 0.0f || h <= 0.0f || clamp_alpha(color[3], element.opacity) <= 0.0f) {
        return;
      }
      immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
      immUniformColor4f(color[0], color[1], color[2], clamp_alpha(color[3], element.opacity));
      immBegin(GPU_PRIM_TRI_FAN, 4);
      const float y0 = float(screen_h) - (y + h);
      const float y1 = float(screen_h) - y;
      immVertex2f(pos, x, y0);
      immVertex2f(pos, x + w, y0);
      immVertex2f(pos, x + w, y1);
      immVertex2f(pos, x, y1);
      immEnd();
      immUnbindProgram();
    };

    const auto draw_border = [&]() {
      if (element.border_width <= 0.0f) {
        return;
      }
      const float bw = element.border_width;
      draw_rect(rect.x, rect.y, rect.w, bw, element.border_color);
      draw_rect(rect.x, rect.y + rect.h - bw, rect.w, bw, element.border_color);
      draw_rect(rect.x, rect.y + bw, bw, rect.h - 2.0f * bw, element.border_color);
      draw_rect(rect.x + rect.w - bw, rect.y + bw, bw, rect.h - 2.0f * bw, element.border_color);
    };

    const auto draw_texture = [&](const std::string &name, float x, float y, float w, float h) {
      gpu::Texture *texture = state.texture_for(name);
      if (texture == nullptr || quad == nullptr) {
        return;
      }
      GPU_shader_bind(image_shader);
      GPU_texture_bind(texture, image_sampler_loc);
      const float rect_tex[4] = {0.0f, 0.0f, 1.0f, 1.0f};
      const float rect_geo[4] = {x, float(screen_h) - (y + h), x + w, float(screen_h) - y};
      GPU_shader_uniform_float_ex(image_shader, image_rect_tex_loc, 4, 1, rect_tex);
      GPU_shader_uniform_float_ex(image_shader, image_rect_geo_loc, 4, 1, rect_geo);
      GPU_shader_uniform_float_ex(image_shader, image_color_loc, 4, 1, element.text_color);
      GPU_batch_set_shader(quad, image_shader);
      GPU_batch_draw(quad);
      GPU_texture_unbind(texture);
    };

    const auto draw_text = [&](float x, float y, float w, float h, int halign, int valign) {
      if (entry.lines.empty()) {
        return;
      }
      const int font = BLF_default();
      BLF_size(font, element.font_size);
      BLF_color4fv(font, element.text_color);
      BLF_enable(font, BLF_CLIPPING);
      BLF_clipping(font, int(x), int(screen_h - (y + h)), int(x + w), int(screen_h - y));

      float block_h = 0.0f;
      std::vector<float> heights;
      for (const std::string &line : entry.lines) {
        const float line_h = BLF_height(font, line.c_str(), line.size());
        heights.push_back(line_h * element.line_height);
        block_h += heights.back();
      }
      float cursor_y = y;
      if (valign == 1) {
        cursor_y = y + (h - block_h) * 0.5f;
      }
      else if (valign == 2) {
        cursor_y = y + h - block_h;
      }
      for (size_t line_index = 0; line_index < entry.lines.size(); ++line_index) {
        const std::string &line = entry.lines[line_index];
        const float line_w = BLF_width(font, line.c_str(), line.size());
        float line_x = x;
        if (halign == 1) {
          line_x = x + (w - line_w) * 0.5f;
        }
        else if (halign == 2) {
          line_x = x + w - line_w;
        }
        const float baseline = screen_h - (cursor_y + heights[line_index] * 0.8f);
        BLF_position(font, line_x, baseline, 0.0f);
        BLF_draw(font, line.c_str(), line.size());
        cursor_y += heights[line_index];
      }
      BLF_disable(font, BLF_CLIPPING);
    };

    switch (element.type) {
      case KX_UIElementType::Label:
        draw_rect(rect.x, rect.y, rect.w, rect.h, element.color);
        draw_text(rect.x, rect.y, rect.w, rect.h, element.halign, element.valign);
        break;
      case KX_UIElementType::Image:
        draw_texture(element.image, rect.x, rect.y, rect.w, rect.h);
        break;
      case KX_UIElementType::Button:
        draw_rect(rect.x, rect.y, rect.w, rect.h, element.color);
        draw_border();
        draw_text(rect.x + rect.w * element.text_pos[0] - rect.w * 0.5f,
                  rect.y + rect.h * element.text_pos[1] - rect.h * 0.5f,
                  rect.w,
                  rect.h,
                  element.text_halign,
                  element.text_valign);
        break;
      case KX_UIElementType::Slider: {
        const float bar_h = rect.h * element.bar_width;
        const float bar_y = rect.y + (rect.h - bar_h) * 0.5f;
        draw_rect(rect.x, bar_y, rect.w, bar_h, element.bar_color);
        draw_border();
        const float knob_w = rect.h * element.knob_size;
        const float knob_x = rect.x + (rect.w - knob_w) * element.value;
        draw_rect(knob_x, rect.y, knob_w, rect.h, element.knob_color);
        draw_texture(element.bar_texture, rect.x, bar_y, rect.w, bar_h);
        draw_texture(element.knob_texture, knob_x, rect.y, knob_w, rect.h);
        break;
      }
      case KX_UIElementType::Layout:
        draw_rect(rect.x, rect.y, rect.w, rect.h, element.color);
        draw_border();
        break;
      default:
        break;
    }

    GPU_matrix_pop();
  }

  if (clipped) {
    GPU_scissor_test(false);
  }
  immUnbindProgram();
}

KX_UIRenderStats KX_UIRenderer::Bench(const std::string &log_path,
                                      int labels,
                                      int buttons,
                                      int images)
{
  std::vector<KX_UIElement> elements;
  KX_UIElement canvas;
  canvas.type = KX_UIElementType::Canvas;
  canvas.handle = 1;
  canvas.design_size[0] = 1920.0f;
  canvas.design_size[1] = 1080.0f;
  elements.push_back(canvas);

  int handle = 2;
  for (int i = 0; i < labels; ++i) {
    KX_UIElement element;
    element.type = KX_UIElementType::Label;
    element.handle = handle++;
    element.canvas = 1;
    element.text = "Score: 0";
    element.position[0] = float(i % 10) * 150.0f;
    element.position[1] = float(i / 10) * 24.0f;
    element.size[0] = 140.0f;
    element.size[1] = 20.0f;
    element.font_size = 16.0f;
    elements.push_back(element);
  }
  for (int i = 0; i < buttons; ++i) {
    KX_UIElement element;
    element.type = KX_UIElementType::Button;
    element.handle = handle++;
    element.canvas = 1;
    element.text = "Start";
    element.position[0] = float(i % 10) * 160.0f;
    element.position[1] = 300.0f + float(i / 10) * 40.0f;
    element.size[0] = 150.0f;
    element.size[1] = 32.0f;
    element.font_size = 16.0f;
    elements.push_back(element);
  }
  for (int i = 0; i < images; ++i) {
    KX_UIElement element;
    element.type = KX_UIElementType::Image;
    element.handle = handle++;
    element.canvas = 1;
    element.image = "bench_image";
    element.position[0] = 600.0f + float(i) * 64.0f;
    element.position[1] = 600.0f;
    element.size[0] = 64.0f;
    element.size[1] = 64.0f;
    elements.push_back(element);
  }

  KX_UIRenderStats result;
  result.elements = int(elements.size());

  /* Warm the cache, then measure the steady state with unchanged revisions. */
  KX_UIRenderer renderer;
  renderer.Draw(elements, 1920, 1080, false);
  const auto idle_start = std::chrono::high_resolution_clock::now();
  const int idle_pass = 0;
  (void)idle_pass;
  renderer.Draw(elements, 1920, 1080, false);
  const auto idle_end = std::chrono::high_resolution_clock::now();
  result.idle_ms = std::chrono::duration<double, std::milli>(idle_end - idle_start).count();
  result.idle_rebuilds = renderer.Stats().idle_rebuilds;

  /* Full update: every element revision changes, so every batch is rebuilt. */
  for (KX_UIElement &element : elements) {
    ++element.revision;
    if (element.type == KX_UIElementType::Label) {
      element.text = "Score: 42";
    }
  }
  const auto full_start = std::chrono::high_resolution_clock::now();
  renderer.Draw(elements, 1920, 1080, false);
  const auto full_end = std::chrono::high_resolution_clock::now();
  result.full_ms = std::chrono::duration<double, std::milli>(full_end - full_start).count();
  result.full_rebuilds = renderer.Stats().full_rebuilds;

  const bool idle_ok = result.idle_ms < 1.0;
  const bool full_ok = result.full_ms < 5.0;

  std::FILE *log = std::fopen(log_path.c_str(), "w");
  if (log != nullptr) {
    std::fprintf(log, "B4.1 KX_UIRenderer bench (software layout pass, gpu off)\n");
    std::fprintf(log, "scene: %d labels + %d buttons + %d images + canvas = %d elements\n",
                 labels, buttons, images, result.elements);
    std::fprintf(log, "idle: %.6f ms (rebuilds=%d) target < 1.0 ms : %s\n",
                 result.idle_ms, result.idle_rebuilds, idle_ok ? "PASS" : "FAIL");
    std::fprintf(log, "full: %.6f ms (rebuilds=%d) target < 5.0 ms : %s\n",
                 result.full_ms, result.full_rebuilds, full_ok ? "PASS" : "FAIL");
    if (!idle_ok || !full_ok) {
      std::fprintf(log,
                   "NOTE: virtual X build; GPU path disabled. Bottleneck is per element "
                   "string splitting and std::map parent lookups, not GPU batching.\n");
    }
    std::fclose(log);
  }
  return result;
}
