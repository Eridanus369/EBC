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

/** \file KX_ValuePool.cpp
 *  \ingroup gamelogic
 */

#include "KX_ValuePool.h"

void KX_ValuePool::ResetFrame()
{
  /* Flip to the other generation without moving any storage: the deque it
   * owned keeps its blocks, so the addresses handed out during the previous
   * frame stay valid. Clearing the new current generation releases the values
   * that have now survived two frames. */
  current_ = 1 - current_;
  Generation &generation = generations_[current_];
  generation.strings.clear();
  generation.lists.clear();
  generation.list_cursor = 0;
}

const char *KX_ValuePool::Intern(const std::string &text)
{
  Generation &generation = generations_[current_];
  generation.strings.push_back(text);
  return generation.strings.back().c_str();
}

const char *KX_ValuePool::Intern(const char *text)
{
  if (text == nullptr) {
    return Intern(std::string());
  }
  return Intern(std::string(text));
}

KX_ValueList &KX_ValuePool::AllocList(size_t reserve)
{
  Generation &generation = generations_[current_];
  if (reserve > kMaxListElements) {
    reserve = kMaxListElements;
  }

  if (generation.list_cursor < generation.lists.size()) {
    KX_ValueList &list = generation.lists[generation.list_cursor++];
    list.clear();
    if (reserve != 0) {
      list.reserve(reserve);
    }
    return list;
  }

  generation.lists.emplace_back();
  KX_ValueList &list = generation.lists.back();
  ++generation.list_cursor;
  if (reserve != 0) {
    list.reserve(reserve);
  }
  return list;
}

size_t KX_ValuePool::StringCount() const
{
  return generations_[current_].strings.size();
}

size_t KX_ValuePool::ListCount() const
{
  return generations_[current_].lists.size();
}
