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

/** \file KX_DebugTrace.h
 *  \ingroup gamelogic
 *
 *  Opt in per frame trace of the nodes a graph activated.
 *
 *  One line per frame is appended to `/tmp/p4_debug_trace.log`:
 *
 *      frame 12 activated: [Source, Branch, OnTrue, Delay, Sink]
 *
 *  Because it is off by default and only records indices (never values), it is
 *  safe to keep compiled into release builds. Enable it with the environment
 *  variable `BGE_GAMENODE_DEBUG=1` before the player starts, or through
 *  #SetEnabled from an engine preference toggle.
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct KX_ExecutionPlan;

class KX_DebugTrace {
 public:
  /** Preference switch; the environment variable is still honoured. */
  static void SetEnabled(bool enabled);
  static bool Enabled();

  /** Override the trace file, tests point it at a scratch path. */
  static void SetLogPath(const std::string &path);
  static std::string LogPath();

  /**
   * Editor overlay snapshot, written at most every 100 ms.
   *
   * The file is the same disk channel the hot reload uses: the editor owns the
   * path (`<blend_dir>/.debug_trace.json`) and only reads it, the player writes
   * it. It contains the node names, the frame number and how many frames each
   * node was activated since the previous snapshot. An empty path disables it.
   */
  static void SetSnapshotPath(const std::string &path);
  static std::string SnapshotPath();

  /** Start a frame; #node_count sizes the duplicate filter. */
  static void BeginFrame(uint64_t frame, size_t node_count);
  /** Record a node that ran this frame; only the first hit is kept. */
  static void RecordActivated(uint32_t node_index);
  /** Write the frame line and drop the per frame buffer. */
  static void EndFrame(const KX_ExecutionPlan &plan);

 private:
  static bool ResolveEnabled();
  static std::FILE *LogFile();
};
