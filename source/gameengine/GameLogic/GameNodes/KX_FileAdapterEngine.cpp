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

/** \file KX_FileAdapterEngine.cpp
 *  \ingroup gamelogic
 *
 *  Filesystem backed B4 file adapter.
 *
 *  Save slots resolve to `<blend_dir>/save/<slot>.json`. Slot names are
 *  validated before they become part of a path: `..`, `/`, `\`, control
 *  characters and empty names are rejected. Generic file paths resolve
 *  relative to the blend directory unless they are already absolute.
 */

#include "KX_FileAdapter.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <system_error>
#include <utility>

#include "KX_Globals.h"

namespace fs = std::filesystem;

namespace {

bool ReadWholeFile(const std::string &path, std::string *out)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }
  out->assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  return stream.good() || stream.eof();
}

bool WriteWholeFile(const std::string &path, const void *data, size_t len)
{
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  if (data != nullptr && len > 0) {
    stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
  }
  stream.flush();
  return stream.good();
}

}  // namespace

KX_FileAdapterEngine::KX_FileAdapterEngine(std::string blend_dir)
    : blend_dir_(std::move(blend_dir))
{
}

bool KX_FileAdapterEngine::SanitizeSlot(const std::string &slot,
                                        std::string *sanitized,
                                        std::string *error)
{
  std::string value = slot;
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  value = value.substr(begin, end - begin);

  if (value.empty()) {
    if (error != nullptr) {
      *error = "save slot is empty";
    }
    return false;
  }
  if (value == "." || value.find("..") != std::string::npos) {
    if (error != nullptr) {
      *error = "save slot must not contain '..': '" + slot + "'";
    }
    return false;
  }
  if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos) {
    if (error != nullptr) {
      *error = "save slot must not contain path separators: '" + slot + "'";
    }
    return false;
  }
  for (char c : value) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
      if (error != nullptr) {
        *error = "save slot contains control characters";
      }
      return false;
    }
  }
  if (sanitized != nullptr) {
    *sanitized = value;
  }
  return true;
}

std::string KX_FileAdapterEngine::ResolvePath(const std::string &path) const
{
  if (path.empty()) {
    return std::string();
  }
  fs::path input(path);
  if (input.is_absolute() || blend_dir_.empty()) {
    return input.string();
  }
  return (fs::path(blend_dir_) / input).string();
}

bool KX_FileAdapterEngine::EnsureParentDirectory(const std::string &path) const
{
  const fs::path parent = fs::path(path).parent_path();
  if (parent.empty()) {
    return true;
  }
  std::error_code ec;
  fs::create_directories(parent, ec);
  return !ec;
}

bool KX_FileAdapterEngine::SaveGame(const std::string &slot, const std::string &json_data)
{
  std::string safe_slot;
  std::string error;
  if (!SanitizeSlot(slot, &safe_slot, &error)) {
    std::fprintf(stderr, "GameNode: SaveGame: %s\n", error.c_str());
    return false;
  }
  if (blend_dir_.empty()) {
    std::fprintf(stderr, "GameNode: SaveGame: blend directory is unknown\n");
    return false;
  }
  const fs::path save_dir = fs::path(blend_dir_) / "save";
  std::error_code ec;
  fs::create_directories(save_dir, ec);
  if (ec) {
    std::fprintf(stderr,
                 "GameNode: SaveGame: cannot create '%s': %s\n",
                 save_dir.string().c_str(),
                 ec.message().c_str());
    return false;
  }
  const fs::path target = save_dir / (safe_slot + ".json");
  if (!WriteWholeFile(target.string(), json_data.data(), json_data.size())) {
    std::fprintf(stderr,
                 "GameNode: SaveGame: cannot write '%s'\n",
                 target.string().c_str());
    return false;
  }
  return true;
}

bool KX_FileAdapterEngine::LoadGame(const std::string &slot, std::string *json_out)
{
  std::string safe_slot;
  std::string error;
  if (!SanitizeSlot(slot, &safe_slot, &error)) {
    std::fprintf(stderr, "GameNode: LoadGame: %s\n", error.c_str());
    return false;
  }
  if (json_out == nullptr) {
    return false;
  }
  const fs::path target = fs::path(blend_dir_) / "save" / (safe_slot + ".json");
  if (!ReadWholeFile(target.string(), json_out)) {
    std::fprintf(stderr,
                 "GameNode: LoadGame: cannot read '%s'\n",
                 target.string().c_str());
    return false;
  }
  return true;
}

bool KX_FileAdapterEngine::SaveGameBinary(const std::string &slot, const std::string &data)
{
  std::string safe_slot;
  std::string error;
  if (!SanitizeSlot(slot, &safe_slot, &error)) {
    std::fprintf(stderr, "GameNode: SaveGameBinary: %s\n", error.c_str());
    return false;
  }
  if (blend_dir_.empty()) {
    std::fprintf(stderr, "GameNode: SaveGameBinary: blend directory is unknown\n");
    return false;
  }
  const fs::path save_dir = fs::path(blend_dir_) / "save";
  std::error_code ec;
  fs::create_directories(save_dir, ec);
  if (ec) {
    std::fprintf(stderr,
                 "GameNode: SaveGameBinary: cannot create '%s': %s\n",
                 save_dir.string().c_str(),
                 ec.message().c_str());
    return false;
  }
  const fs::path target = save_dir / (safe_slot + ".bin");
  if (!WriteWholeFile(target.string(), data.data(), data.size())) {
    std::fprintf(stderr,
                 "GameNode: SaveGameBinary: cannot write '%s'\n",
                 target.string().c_str());
    return false;
  }
  return true;
}

bool KX_FileAdapterEngine::LoadGameBinary(const std::string &slot, std::string *out)
{
  std::string safe_slot;
  std::string error;
  if (!SanitizeSlot(slot, &safe_slot, &error)) {
    std::fprintf(stderr, "GameNode: LoadGameBinary: %s\n", error.c_str());
    return false;
  }
  if (out == nullptr) {
    return false;
  }
  const fs::path target = fs::path(blend_dir_) / "save" / (safe_slot + ".bin");
  if (!ReadWholeFile(target.string(), out)) {
    std::fprintf(stderr,
                 "GameNode: LoadGameBinary: cannot read '%s'\n",
                 target.string().c_str());
    return false;
  }
  return true;
}

bool KX_FileAdapterEngine::ListSaveSlots(std::vector<std::string> *slots)
{
  if (slots == nullptr || blend_dir_.empty()) {
    return false;
  }
  const fs::path save_dir = fs::path(blend_dir_) / "save";
  std::error_code ec;
  if (!fs::is_directory(save_dir, ec) || ec) {
    *slots = {};
    return true;
  }
  std::vector<std::string> names;
  for (const auto &entry : fs::directory_iterator(save_dir, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const std::string ext = entry.path().extension().string();
    if (ext == ".json" || ext == ".bin") {
      names.push_back(entry.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  *slots = std::move(names);
  return true;
}

bool KX_FileAdapterEngine::ReadFile(const std::string &path, bool binary, std::string *out)
{
  (void)binary;
  if (out == nullptr) {
    return false;
  }
  const std::string resolved = ResolvePath(path);
  if (resolved.empty()) {
    std::fprintf(stderr, "GameNode: ReadFile: empty path\n");
    return false;
  }
  if (!ReadWholeFile(resolved, out)) {
    std::fprintf(stderr, "GameNode: ReadFile: cannot read '%s'\n", resolved.c_str());
    return false;
  }
  return true;
}

bool KX_FileAdapterEngine::WriteFile(const std::string &path,
                                     bool binary,
                                     const void *data,
                                     size_t len)
{
  (void)binary;
  const std::string resolved = ResolvePath(path);
  if (resolved.empty()) {
    std::fprintf(stderr, "GameNode: WriteFile: empty path\n");
    return false;
  }
  if (!EnsureParentDirectory(resolved)) {
    std::fprintf(stderr,
                 "GameNode: WriteFile: cannot create parent directory for '%s'\n",
                 resolved.c_str());
    return false;
  }
  if (!WriteWholeFile(resolved, data, len)) {
    std::fprintf(stderr, "GameNode: WriteFile: cannot write '%s'\n", resolved.c_str());
    return false;
  }
  return true;
}

bool KX_FileAdapterEngine::FileExists(const std::string &path)
{
  const std::string resolved = ResolvePath(path);
  if (resolved.empty()) {
    return false;
  }
  std::error_code ec;
  return fs::exists(fs::path(resolved), ec) && !ec;
}

bool KX_FileAdapterEngine::DeleteFile(const std::string &path)
{
  const std::string resolved = ResolvePath(path);
  if (resolved.empty()) {
    return false;
  }
  std::error_code ec;
  const bool removed = fs::remove(fs::path(resolved), ec);
  if (ec) {
    std::fprintf(stderr,
                 "GameNode: DeleteFile: cannot remove '%s': %s\n",
                 resolved.c_str(),
                 ec.message().c_str());
    return false;
  }
  return removed;
}

/* ------------------------------------------------------------------------- */
/* B6.9 Fi-1/Fi-2: streaming IO and directory operations (appended).         */

bool KX_FileAdapterEngine::ReadFileRange(const std::string &path,
                                         size_t offset,
                                         size_t length,
                                         std::string *out,
                                         bool *eof)
{
  if (out == nullptr) {
    return false;
  }
  const std::string resolved = ResolvePath(path);
  if (resolved.empty()) {
    return false;
  }
  std::error_code ec;
  const size_t size = fs::file_size(fs::path(resolved), ec);
  if (ec) {
    std::fprintf(stderr,
                 "GameNode: ReadFileChunk: cannot stat '%s': %s\n",
                 resolved.c_str(),
                 ec.message().c_str());
    return false;
  }
  if (offset > size) {
    std::fprintf(stderr,
                 "GameNode: ReadFileChunk: offset %zu is past the end of '%s' (%zu bytes)\n",
                 offset,
                 resolved.c_str(),
                 size);
    return false;
  }
  const size_t take = std::min(length, size - offset);
  std::ifstream stream(resolved, std::ios::binary);
  if (!stream) {
    std::fprintf(stderr, "GameNode: ReadFileChunk: cannot read '%s'\n", resolved.c_str());
    return false;
  }
  stream.seekg(static_cast<std::streamoff>(offset));
  out->assign(take, '\0');
  if (take > 0) {
    stream.read(out->data(), static_cast<std::streamsize>(take));
    if (static_cast<size_t>(stream.gcount()) != take) {
      std::fprintf(stderr, "GameNode: ReadFileChunk: short read on '%s'\n", resolved.c_str());
      return false;
    }
  }
  if (eof != nullptr) {
    *eof = offset + take >= size;
  }
  return true;
}

bool KX_FileAdapterEngine::WriteFileRange(const std::string &path,
                                          const std::string &data,
                                          bool truncate)
{
  const std::string resolved = ResolvePath(path);
  if (resolved.empty()) {
    return false;
  }
  if (!EnsureParentDirectory(resolved)) {
    std::fprintf(stderr, "GameNode: WriteFileChunk: cannot create the parent of '%s'\n",
                 resolved.c_str());
    return false;
  }
  std::ofstream stream(resolved,
                       std::ios::binary | (truncate ? std::ios::trunc : std::ios::app));
  if (!stream) {
    std::fprintf(stderr, "GameNode: WriteFileChunk: cannot open '%s'\n", resolved.c_str());
    return false;
  }
  if (!data.empty()) {
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  stream.flush();
  return stream.good();
}

bool KX_FileAdapterEngine::ListDirectory(const std::string &path,
                                         std::vector<KX_DirectoryEntry> *entries,
                                         std::string *error)
{
  std::string safe;
  if (!KX_FileAdapterDirectoryPathIsSafe(path, true, &safe, error)) {
    return false;
  }
  if (entries == nullptr) {
    return false;
  }
  const std::string resolved = safe.empty() ? blend_dir_ : ResolvePath(safe);
  std::error_code ec;
  if (!fs::is_directory(fs::path(resolved), ec)) {
    if (error != nullptr) {
      *error = "not a directory: '" + resolved + "'";
    }
    return false;
  }
  std::vector<KX_DirectoryEntry> collected;
  fs::directory_iterator iterator(fs::path(resolved), ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot list '" + resolved + "': " + ec.message();
    }
    return false;
  }
  for (const fs::directory_entry &entry : iterator) {
    KX_DirectoryEntry child;
    child.name = entry.path().filename().string();
    std::error_code type_ec;
    child.directory = entry.is_directory(type_ec);
    collected.push_back(child);
  }
  std::sort(collected.begin(), collected.end(), [](const KX_DirectoryEntry &a, const KX_DirectoryEntry &b) {
    return a.name < b.name;
  });
  *entries = std::move(collected);
  return true;
}

bool KX_FileAdapterEngine::CreateDirectory(const std::string &path,
                                           bool parents,
                                           std::string *error)
{
  std::string safe;
  if (!KX_FileAdapterDirectoryPathIsSafe(path, false, &safe, error)) {
    return false;
  }
  const std::string resolved = ResolvePath(safe);
  std::error_code ec;
  if (parents) {
    fs::create_directories(fs::path(resolved), ec);
  }
  else {
    fs::create_directory(fs::path(resolved), ec);
  }
  if (ec) {
    if (error != nullptr) {
      *error = "cannot create '" + resolved + "': " + ec.message();
    }
    return false;
  }
  return true;
}

bool KX_FileAdapterEngine::RemoveDirectory(const std::string &path,
                                           bool recursive,
                                           std::string *error)
{
  std::string safe;
  if (!KX_FileAdapterDirectoryPathIsSafe(path, false, &safe, error)) {
    return false;
  }
  const std::string resolved = ResolvePath(safe);
  std::error_code ec;
  if (!fs::is_directory(fs::path(resolved), ec)) {
    if (error != nullptr) {
      *error = "not a directory: '" + resolved + "'";
    }
    return false;
  }
  bool non_empty = false;
  fs::directory_iterator iterator(fs::path(resolved), ec);
  if (ec) {
    if (error != nullptr) {
      *error = "cannot list '" + resolved + "': " + ec.message();
    }
    return false;
  }
  for (const fs::directory_entry &entry : iterator) {
    (void)entry;
    non_empty = true;
    break;
  }
  if (non_empty && !recursive) {
    if (error != nullptr) {
      *error = "directory is not empty: '" + resolved + "' (recursive=false)";
    }
    return false;
  }
  if (recursive) {
    fs::remove_all(fs::path(resolved), ec);
  }
  else {
    fs::remove(fs::path(resolved), ec);
  }
  if (ec) {
    if (error != nullptr) {
      *error = "cannot remove '" + resolved + "': " + ec.message();
    }
    return false;
  }
  return true;
}

std::unique_ptr<KX_FileAdapter> KX_CreateEngineFileAdapter(KX_Scene *scene)
{
  (void)scene;
  std::string blend_path = KX_GetMainPath();
  std::string blend_dir;
  if (!blend_path.empty()) {
    blend_dir = fs::path(blend_path).parent_path().string();
  }
  return std::unique_ptr<KX_FileAdapter>(new KX_FileAdapterEngine(std::move(blend_dir)));
}
