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

/** \file KX_FileAdapter.cpp
 *  \ingroup gamelogic
 *
 *  In-memory and neutral file adapters. The filesystem implementation lives in
 *  KX_FileAdapterEngine.cpp so the node runtime test probe does not pull in
 *  Ketsji or the blend file globals.
 */

#include "KX_FileAdapter.h"

#include <utility>

namespace {

class NullFileAdapter final : public KX_FileAdapter {
};

NullFileAdapter &NullFileInstance()
{
  static NullFileAdapter instance;
  return instance;
}

}  // namespace

std::string KX_MockFileKey(const std::string &slot)
{
  return std::string("save:") + slot;
}

bool KX_MockFileAdapter::SaveGame(const std::string &slot, const std::string &json_data)
{
  ++save_calls;
  if (!result) {
    return false;
  }
  files[KX_MockFileKey(slot)] = json_data;
  return true;
}

bool KX_MockFileAdapter::LoadGame(const std::string &slot, std::string *json_out)
{
  ++load_calls;
  if (!result || json_out == nullptr) {
    return false;
  }
  const auto found = files.find(KX_MockFileKey(slot));
  if (found == files.end()) {
    return false;
  }
  *json_out = found->second;
  return true;
}

bool KX_MockFileAdapter::ReadFile(const std::string &path, bool binary, std::string *out)
{
  (void)binary;
  ++read_calls;
  if (!result || out == nullptr) {
    return false;
  }
  const auto found = files.find(path);
  if (found == files.end()) {
    return false;
  }
  *out = found->second;
  return true;
}

bool KX_MockFileAdapter::WriteFile(const std::string &path,
                                   bool binary,
                                   const void *data,
                                   size_t len)
{
  (void)binary;
  ++write_calls;
  if (!result) {
    return false;
  }
  if (data != nullptr && len > 0) {
    files[path] = std::string(static_cast<const char *>(data), len);
  }
  else {
    files[path] = std::string();
  }
  return true;
}

bool KX_MockFileAdapter::FileExists(const std::string &path)
{
  ++exists_calls;
  if (!result) {
    return false;
  }
  return files.find(path) != files.end();
}

bool KX_MockFileAdapter::DeleteFile(const std::string &path)
{
  ++delete_calls;
  if (!result) {
    return false;
  }
  return files.erase(path) > 0;
}

void KX_MockFileAdapter::Reset()
{
  files.clear();
  result = true;
  save_calls = 0;
  load_calls = 0;
  read_calls = 0;
  write_calls = 0;
  exists_calls = 0;
  delete_calls = 0;
}

std::unique_ptr<KX_FileAdapter> KX_CreateNullFileAdapter()
{
  return std::unique_ptr<KX_FileAdapter>(new NullFileAdapter());
}

KX_FileAdapter &KX_NullFileAdapter()
{
  return NullFileInstance();
}
