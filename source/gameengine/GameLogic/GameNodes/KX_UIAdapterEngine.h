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

/** \file KX_UIAdapterEngine.h
 *  \ingroup gamelogic
 *
 *  Engine backed implementation of #KX_UIAdapter.
 *
 *  Element records live here; drawing is delegated to the renderer. The BLF
 *  font stack is loaded lazily on the first label, because the player never
 *  calls BLF_load_font_stack() itself and without it CJK text has no glyphs
 *  (blf_font_default.cc:95-100, GP_ghost.cpp:1045).
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "KX_GameAdapters.h"
#include "KX_UIRenderer.h"

class KX_Scene;

class KX_UIAdapterEngine : public KX_UIAdapter {
 public:
  explicit KX_UIAdapterEngine(KX_Scene *scene);
  ~KX_UIAdapterEngine() override;

  void BeginFrame(float delta_time, float mouse_x, float mouse_y, bool mouse_down) override;
  void EndFrame() override;
  int CreateCanvas(const std::string &name) override;
  int CreateLabel(int canvas, const std::string &text) override;
  int CreateImage(int canvas, const std::string &image) override;
  int CreateButton(int canvas, const std::string &text) override;
  int CreateLayout(int canvas, int layout_type) override;
  int CreateSlider(int canvas, float initial_value, int steps, bool allow_bar_click) override;
  bool DestroyElement(int handle) override;
  bool SetAttribute(int handle, const std::string &key, const KX_RuntimeValue &value) override;
  bool GetText(int handle, std::string &out) const override;
  bool GetAttribute(int handle, const std::string &key, KX_RuntimeValue &out) const override;
  bool Event(int handle, KX_UIEvent event) const override;
  float SliderValue(int handle) const override;
  bool KnobPosition(int handle, float &x, float &y) const override;
  bool MoveElement(int handle, int mode) override;
  bool HitTest(int handle, float x, float y) const override;
  int HitTestPoint(float x, float y) const override;
  void DrawScreen(int width, int height) override;

  /** How many times BLF_load_font_stack() actually ran; test hook for idempotency. */
  int FontStackLoads() const;
  size_t ElementCount() const
  {
    return elements_.size();
  }
  bool MouseDown() const
  {
    return mouse_down_;
  }

  KX_UIElement *Find(int handle);
  const KX_UIElement *Find(int handle) const;

  /** Renderer used by DrawScreen; exposed for the bench harness. */
  KX_UIRenderer &Renderer()
  {
    return renderer_;
  }

 private:
  int CreateElement(KX_UIElementType type, int canvas, const std::string &text);
  void EnsureFontStack();
  void UpdateEvents();

  KX_Scene *scene_ = nullptr;
  std::vector<KX_UIElement> elements_;
  std::map<int, size_t> by_handle_;
  int next_handle_ = 1;
  float mouse_x_ = 0.0f;
  float mouse_y_ = 0.0f;
  bool mouse_down_ = false;
  bool previous_mouse_down_ = false;
  int pressed_handle_ = -1;
  int hovered_handle_ = -1;
  std::map<int, uint8_t> event_flags_;
  KX_UIRenderer renderer_;
};

/** Engine UI adapter factory, keeps the blenfont includes out of the game logic glue. */
std::unique_ptr<KX_UIAdapter> KX_CreateEngineUIAdapter(KX_Scene *scene);
