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

/** \file KX_FileAdapter.h
 *  \ingroup gamelogic
 *
 *  B4 file domain adapter.
 *
 *  Save games and generic file IO never touch the filesystem from node code.
 *  Nodes talk to #KX_FileAdapter, so tests use #KX_MockFileAdapter and the
 *  engine build uses #KX_FileAdapterEngine rooted at the blend directory.
 *
 *  Every entry point returns bool. A rejected path, a missing file, a write
 *  failure or a malformed save slot is reported as false; callers turn that
 *  into the node's OnFail trigger and a stderr diagnostic.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class KX_Scene;
struct KX_DirectoryEntry;

/** Save game and generic file IO service used by the B4 file nodes. */
class KX_FileAdapter {
 public:
  virtual ~KX_FileAdapter() = default;

  /** Write a save game blob. @p json_data is the serialized scene state. */
  virtual bool SaveGame(const std::string &slot, const std::string &json_data)
  {
    (void)slot;
    (void)json_data;
    return false;
  }

  /** Read a save game blob into @p json_out. */
  virtual bool LoadGame(const std::string &slot, std::string *json_out)
  {
    (void)slot;
    (void)json_out;
    return false;
  }

  /**
   * B6.4: write a binary save game blob (versioned header, CRC32, same field
   * schema as the JSON save). Used by the SaveGameBinary node.
   */
  virtual bool SaveGameBinary(const std::string &slot, const std::string &data)
  {
    (void)slot;
    (void)data;
    return false;
  }

  /** B6.4: read the raw bytes of a binary save slot. */
  virtual bool LoadGameBinary(const std::string &slot, std::string *out)
  {
    (void)slot;
    (void)out;
    return false;
  }

  /** B6.4: list available save slot names (JSON and binary), sorted, unique. */
  virtual bool ListSaveSlots(std::vector<std::string> *slots)
  {
    (void)slots;
    return false;
  }

  /**
   * Read a file. Binary content is returned as raw bytes in @p out; the
   * @p binary flag only documents the caller's intent, both modes preserve bytes.
   */
  virtual bool ReadFile(const std::string &path, bool binary, std::string *out)
  {
    (void)path;
    (void)binary;
    (void)out;
    return false;
  }

  /** Write @p len bytes from @p data to @p path. */
  virtual bool WriteFile(const std::string &path,
                         bool binary,
                         const void *data,
                         size_t len)
  {
    (void)path;
    (void)binary;
    (void)data;
    (void)len;
    return false;
  }

  virtual bool FileExists(const std::string &path)
  {
    (void)path;
    return false;
  }

  virtual bool DeleteFile(const std::string &path)
  {
    (void)path;
    return false;
  }

  /* ---------------------------------------------------------------------- */
  /* B6.9 Fi-1/Fi-2: streaming IO and directory operations (appended).       */
  /* ---------------------------------------------------------------------- */

  /**
   * Read a byte range. @p out receives `[offset, offset + length)` clamped to
   * the file end. @p eof is true when the range reached the end of the file.
   *
   * `offset == size` is valid and returns an empty chunk with `eof=true`;
   * `offset > size` is rejected so a corrupt cursor cannot silently wrap.
   */
  virtual bool ReadFileRange(const std::string &path,
                             size_t offset,
                             size_t length,
                             std::string *out,
                             bool *eof)
  {
    (void)path;
    (void)offset;
    (void)length;
    (void)out;
    (void)eof;
    return false;
  }

  /**
   * Write one chunk. `truncate=true` replaces the file, false appends.
   * Parent directories are created. There is no inter-process locking: two
   * independent writers appending the same file interleave chunks in call
   * order, and a truncating call discards what was written before it.
   */
  virtual bool WriteFileRange(const std::string &path, const std::string &data, bool truncate)
  {
    (void)path;
    (void)data;
    (void)truncate;
    return false;
  }

  /**
   * List the entries of a directory. Names are sorted ascending; each entry
   * reports whether it is a directory. Only direct children are returned.
   */
  virtual bool ListDirectory(const std::string &path,
                             std::vector<KX_DirectoryEntry> *entries,
                             std::string *error)
  {
    (void)path;
    (void)entries;
    (void)error;
    return false;
  }

  /** Create a directory; @p parents also creates missing ancestors. */
  virtual bool CreateDirectory(const std::string &path, bool parents, std::string *error)
  {
    (void)path;
    (void)parents;
    (void)error;
    return false;
  }

  /**
   * Remove a directory. `recursive=false` refuses a non-empty directory;
   * `recursive=true` removes its whole subtree first. The root is never
   * removable (empty path is rejected).
   */
  virtual bool RemoveDirectory(const std::string &path, bool recursive, std::string *error)
  {
    (void)path;
    (void)recursive;
    (void)error;
    return false;
  }
};

/** One direct child of a directory, returned by #KX_FileAdapter::ListDirectory. */
struct KX_DirectoryEntry {
  std::string name;
  bool directory = false;
};

/**
 * Validate a directory path. Directory operations are rooted at the blend
 * directory: absolute paths, `..` and `.` components, backslashes and control
 * characters are rejected. An empty path means "the blend directory root" and
 * is only accepted when @p allow_root is true.
 */
bool KX_FileAdapterDirectoryPathIsSafe(const std::string &path,
                                       bool allow_root,
                                       std::string *sanitized,
                                       std::string *error = nullptr);

/**
 * In-memory adapter used by unit tests and the B4 E2E probe.
 *
 * `files` is the complete store, `result` is the injectable return value for
 * every operation, and the call counters record how often each entry point was
 * reached. Save slots use the key `save:<slot>`; generic files use the path as-is.
 */
class KX_MockFileAdapter : public KX_FileAdapter {
 public:
  std::unordered_map<std::string, std::string> files;
  /** Return value of every operation. Set to false to simulate IO failure. */
  bool result = true;

  size_t save_calls = 0;
  size_t load_calls = 0;
  size_t savebin_calls = 0;
  size_t loadbin_calls = 0;
  size_t list_slots_calls = 0;
  size_t read_calls = 0;
  size_t write_calls = 0;
  size_t exists_calls = 0;
  size_t delete_calls = 0;
  size_t read_range_calls = 0;
  size_t write_range_calls = 0;
  size_t list_directory_calls = 0;
  size_t create_directory_calls = 0;
  size_t remove_directory_calls = 0;

  /** Directories tracked by the mock; the root is implicit (`""`). */
  std::unordered_set<std::string> directories;

  bool SaveGame(const std::string &slot, const std::string &json_data) override;
  bool LoadGame(const std::string &slot, std::string *json_out) override;
  bool SaveGameBinary(const std::string &slot, const std::string &data) override;
  bool LoadGameBinary(const std::string &slot, std::string *out) override;
  bool ListSaveSlots(std::vector<std::string> *slots) override;
  bool ReadFile(const std::string &path, bool binary, std::string *out) override;
  bool WriteFile(const std::string &path,
                 bool binary,
                 const void *data,
                 size_t len) override;
  bool FileExists(const std::string &path) override;
  bool DeleteFile(const std::string &path) override;
  bool ReadFileRange(const std::string &path,
                     size_t offset,
                     size_t length,
                     std::string *out,
                     bool *eof) override;
  bool WriteFileRange(const std::string &path, const std::string &data, bool truncate) override;
  bool ListDirectory(const std::string &path,
                     std::vector<KX_DirectoryEntry> *entries,
                     std::string *error) override;
  bool CreateDirectory(const std::string &path, bool parents, std::string *error) override;
  bool RemoveDirectory(const std::string &path, bool recursive, std::string *error) override;

  /** Clear every stored file, reset the counters and restore `result = true`. */
  void Reset();
};

/** Key used by #KX_MockFileAdapter for a save slot. */
std::string KX_MockFileKey(const std::string &slot);

/** Key used by #KX_MockFileAdapter for a binary save slot. */
std::string KX_MockBinaryFileKey(const std::string &slot);

/** Filesystem adapter rooted at @p blend_dir. Save slots live in `<blend_dir>/save/`. */
class KX_FileAdapterEngine : public KX_FileAdapter {
 public:
  explicit KX_FileAdapterEngine(std::string blend_dir);

  bool SaveGame(const std::string &slot, const std::string &json_data) override;
  bool LoadGame(const std::string &slot, std::string *json_out) override;
  bool SaveGameBinary(const std::string &slot, const std::string &data) override;
  bool LoadGameBinary(const std::string &slot, std::string *out) override;
  bool ListSaveSlots(std::vector<std::string> *slots) override;
  bool ReadFile(const std::string &path, bool binary, std::string *out) override;
  bool WriteFile(const std::string &path,
                 bool binary,
                 const void *data,
                 size_t len) override;
  bool FileExists(const std::string &path) override;
  bool DeleteFile(const std::string &path) override;
  bool ReadFileRange(const std::string &path,
                     size_t offset,
                     size_t length,
                     std::string *out,
                     bool *eof) override;
  bool WriteFileRange(const std::string &path, const std::string &data, bool truncate) override;
  bool ListDirectory(const std::string &path,
                     std::vector<KX_DirectoryEntry> *entries,
                     std::string *error) override;
  bool CreateDirectory(const std::string &path, bool parents, std::string *error) override;
  bool RemoveDirectory(const std::string &path, bool recursive, std::string *error) override;

  /** Blend directory this adapter resolves relative paths against. */
  const std::string &BlendDir() const
  {
    return blend_dir_;
  }

  /**
   * Validate one save slot. Rejects empty names, `..`, `/` and `\`, and control
   * characters. Returns false and fills @p error when the slot is unsafe.
   */
  static bool SanitizeSlot(const std::string &slot, std::string *sanitized, std::string *error);

 private:
  std::string ResolvePath(const std::string &path) const;
  bool EnsureParentDirectory(const std::string &path) const;

  std::string blend_dir_;
};

/** Engine adapter factory. Uses the blend directory of the running file. */
std::unique_ptr<KX_FileAdapter> KX_CreateEngineFileAdapter(KX_Scene *scene);
/** Neutral adapter used by tests and headless builds. */
std::unique_ptr<KX_FileAdapter> KX_CreateNullFileAdapter();
/** Shared neutral instance, mirrors the other KX_Null*Adapt*r accessors. */
KX_FileAdapter &KX_NullFileAdapter();
