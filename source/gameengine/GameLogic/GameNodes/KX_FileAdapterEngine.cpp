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
