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

#include <algorithm>
#include <cctype>
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

bool KX_FileAdapterDirectoryPathIsSafe(const std::string &path,
                                       bool allow_root,
                                       std::string *sanitized,
                                       std::string *error)
{
  auto fail = [error](const std::string &message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };

  if (path.empty()) {
    if (!allow_root) {
      return fail("directory path is empty");
    }
    if (sanitized != nullptr) {
      sanitized->clear();
    }
    return true;
  }
  if (path.front() == '/' || path.back() == '/') {
    return fail("directory path must be relative and must not end with '/': '" + path + "'");
  }
  for (char c : path) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
      return fail("directory path contains control characters");
    }
    if (c == '\\' || c == ':') {
      return fail("directory path must not contain '\\' or ':': '" + path + "'");
    }
  }
  size_t cursor = 0;
  while (cursor <= path.size()) {
    const size_t slash = path.find('/', cursor);
    const std::string part = path.substr(cursor, slash == std::string::npos ? std::string::npos :
                                                                           slash - cursor);
    if (part.empty()) {
      return fail("directory path contains an empty component: '" + path + "'");
    }
    if (part == "." || part == "..") {
      return fail("directory path must not contain '.' or '..': '" + path + "'");
    }
    if (slash == std::string::npos) {
      break;
    }
    cursor = slash + 1;
  }
  if (sanitized != nullptr) {
    *sanitized = path;
  }
  return true;
}

std::string KX_MockFileKey(const std::string &slot)
{
  return std::string("save:") + slot;
}

std::string KX_MockBinaryFileKey(const std::string &slot)
{
  return std::string("savebin:") + slot;
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

bool KX_MockFileAdapter::SaveGameBinary(const std::string &slot, const std::string &data)
{
  ++savebin_calls;
  if (!result) {
    return false;
  }
  files[KX_MockBinaryFileKey(slot)] = data;
  return true;
}

bool KX_MockFileAdapter::LoadGameBinary(const std::string &slot, std::string *out)
{
  ++loadbin_calls;
  if (!result || out == nullptr) {
    return false;
  }
  const auto found = files.find(KX_MockBinaryFileKey(slot));
  if (found == files.end()) {
    return false;
  }
  *out = found->second;
  return true;
}

bool KX_MockFileAdapter::ListSaveSlots(std::vector<std::string> *slots)
{
  ++list_slots_calls;
  if (!result || slots == nullptr) {
    return false;
  }
  static const std::string kJson = "save:";
  static const std::string kBinary = "savebin:";
  std::vector<std::string> names;
  for (const auto &entry : files) {
    if (entry.first.compare(0, kJson.size(), kJson) == 0) {
      names.push_back(entry.first.substr(kJson.size()));
    }
    else if (entry.first.compare(0, kBinary.size(), kBinary) == 0) {
      names.push_back(entry.first.substr(kBinary.size()));
    }
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  *slots = std::move(names);
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

/* ------------------------------------------------------------------------- */
/* B6.9 Fi-1/Fi-2: mock streaming IO and directory operations (appended).    */

bool KX_MockFileAdapter::ReadFileRange(const std::string &path,
                                       size_t offset,
                                       size_t length,
                                       std::string *out,
                                       bool *eof)
{
  ++read_range_calls;
  if (!result || out == nullptr) {
    return false;
  }
  const auto found = files.find(path);
  if (found == files.end() || offset > found->second.size()) {
    return false;
  }
  const size_t available = found->second.size() - offset;
  const size_t take = std::min(length, available);
  *out = found->second.substr(offset, take);
  if (eof != nullptr) {
    *eof = offset + take >= found->second.size();
  }
  return true;
}

bool KX_MockFileAdapter::WriteFileRange(const std::string &path,
                                        const std::string &data,
                                        bool truncate)
{
  ++write_range_calls;
  if (!result) {
    return false;
  }
  if (truncate) {
    files[path] = data;
  }
  else {
    files[path] += data;
  }
  return true;
}

bool KX_MockFileAdapter::ListDirectory(const std::string &path,
                                       std::vector<KX_DirectoryEntry> *entries,
                                       std::string *error)
{
  ++list_directory_calls;
  std::string validated;
  if (!KX_FileAdapterDirectoryPathIsSafe(path, true, &validated)) {
    return false;
  }
  if (!result || entries == nullptr) {
    return false;
  }
  if (!path.empty() && directories.find(path) == directories.end()) {
    if (error != nullptr) {
      *error = "not a directory: '" + path + "'";
    }
    return false;
  }

  const std::string prefix = path.empty() ? std::string() : path + "/";
  std::vector<KX_DirectoryEntry> collected;
  for (const auto &entry : files) {
    if (entry.first.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }
    const std::string rest = entry.first.substr(prefix.size());
    const size_t slash = rest.find('/');
    KX_DirectoryEntry child;
    child.name = rest.substr(0, slash);
    child.directory = slash != std::string::npos;
    if (!child.name.empty()) {
      collected.push_back(child);
    }
  }
  for (const std::string &directory : directories) {
    if (directory.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }
    const std::string rest = directory.substr(prefix.size());
    if (rest.empty() || rest.find('/') != std::string::npos) {
      continue;
    }
    KX_DirectoryEntry child;
    child.name = rest;
    child.directory = true;
    collected.push_back(child);
  }
  std::sort(collected.begin(), collected.end(), [](const KX_DirectoryEntry &a, const KX_DirectoryEntry &b) {
    return a.name < b.name;
  });
  collected.erase(std::unique(collected.begin(),
                              collected.end(),
                              [](const KX_DirectoryEntry &a, const KX_DirectoryEntry &b) {
                                return a.name == b.name;
                              }),
                  collected.end());
  *entries = std::move(collected);
  return true;
}

bool KX_MockFileAdapter::CreateDirectory(const std::string &path,
                                         bool parents,
                                         std::string *error)
{
  ++create_directory_calls;
  std::string validated;
  if (!KX_FileAdapterDirectoryPathIsSafe(path, false, &validated)) {
    return false;
  }
  if (!result) {
    return false;
  }
  if (parents) {
    size_t cursor = 0;
    while (cursor != std::string::npos && cursor < validated.size()) {
      const size_t slash = validated.find('/', cursor);
      const std::string part = validated.substr(0, slash);
      if (!part.empty()) {
        directories.insert(part);
      }
      cursor = slash == std::string::npos ? std::string::npos : slash + 1;
    }
    directories.insert(validated);
    return true;
  }
  const size_t slash = validated.rfind('/');
  if (slash != std::string::npos && directories.find(validated.substr(0, slash)) == directories.end()) {
    if (error != nullptr) {
      *error = "parent directory does not exist";
    }
    return false;
  }
  directories.insert(validated);
  return true;
}

bool KX_MockFileAdapter::RemoveDirectory(const std::string &path,
                                         bool recursive,
                                         std::string *error)
{
  ++remove_directory_calls;
  std::string validated;
  if (!KX_FileAdapterDirectoryPathIsSafe(path, false, &validated)) {
    return false;
  }
  if (!result) {
    return false;
  }
  if (directories.find(validated) == directories.end()) {
    if (error != nullptr) {
      *error = "not a directory: '" + validated + "'";
    }
    return false;
  }
  const std::string prefix = validated + "/";
  bool non_empty = false;
  for (const auto &entry : files) {
    if (entry.first.compare(0, prefix.size(), prefix) == 0) {
      non_empty = true;
      break;
    }
  }
  if (!non_empty) {
    for (const std::string &directory : directories) {
      if (directory.compare(0, prefix.size(), prefix) == 0) {
        non_empty = true;
        break;
      }
    }
  }
  if (non_empty && !recursive) {
    if (error != nullptr) {
      *error = "directory is not empty: '" + validated + "' (recursive=false)";
    }
    return false;
  }
  if (recursive) {
    for (auto it = files.begin(); it != files.end();) {
      if (it->first.compare(0, prefix.size(), prefix) == 0) {
        it = files.erase(it);
      }
      else {
        ++it;
      }
    }
    for (auto it = directories.begin(); it != directories.end();) {
      if ((*it).compare(0, prefix.size(), prefix) == 0) {
        it = directories.erase(it);
      }
      else {
        ++it;
      }
    }
  }
  directories.erase(validated);
  return true;
}

void KX_MockFileAdapter::Reset()
{
  files.clear();
  directories.clear();
  result = true;
  save_calls = 0;
  load_calls = 0;
  savebin_calls = 0;
  loadbin_calls = 0;
  list_slots_calls = 0;
  read_calls = 0;
  write_calls = 0;
  exists_calls = 0;
  delete_calls = 0;
  read_range_calls = 0;
  write_range_calls = 0;
  list_directory_calls = 0;
  create_directory_calls = 0;
  remove_directory_calls = 0;
}

std::unique_ptr<KX_FileAdapter> KX_CreateNullFileAdapter()
{
  return std::unique_ptr<KX_FileAdapter>(new NullFileAdapter());
}

KX_FileAdapter &KX_NullFileAdapter()
{
  return NullFileInstance();
}
