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

/** \file KX_ValuePool.h
 *  \ingroup gamelogic
 *
 *  Per frame storage for the non scalar runtime value types.
 *
 *  Strings are interned in a deque so their addresses stay stable while the
 *  generation that produced them is alive; lists are recycled from a deque as
 *  well. #KX_ValuePool::ResetFrame keeps the previous generation allocated, so a
 *  value produced in frame N stays readable during frame N+1 and is reclaimed at
 *  the start of frame N+2. Values which must outlive that window (asset fields,
 *  node state) copy the payload instead of storing the pooled pointer.
 */

#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "KX_RuntimeValue.h"

/** A list value stored inside the execution context pool. */
using KX_ValueList = std::vector<KX_RuntimeValue>;

class KX_ValuePool {
 public:
  /** Hard cap per list, prevents a runaway mesh node from exhausting memory. */
  static constexpr size_t kMaxListElements = 4096;

  /**
   * Retire the current generation.
   *
   * The generation written during the frame that just ended is kept alive for
   * the new frame, the generation that has now survived two frames is cleared.
   * Clearing keeps the deque blocks allocated, so the next frames reuse them.
   */
  void ResetFrame();

  /** Return a stable pointer to @a text for the current frame. */
  const char *Intern(const std::string &text);
  const char *Intern(const char *text);

  /** Allocate (or recycle) a list for the current frame. */
  KX_ValueList &AllocList(size_t reserve = 0);

  size_t StringCount() const;
  size_t ListCount() const;

 private:
  struct Generation {
    std::deque<std::string> strings;
    std::deque<KX_ValueList> lists;
    size_t list_cursor = 0;
  };

  /* [current] receives this frame's values, [1 - current] keeps the previous
   * frame's values alive until the next ResetFrame. */
  Generation generations_[2];
  size_t current_ = 0;
};
