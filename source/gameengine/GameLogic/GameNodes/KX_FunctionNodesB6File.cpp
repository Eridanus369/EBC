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

/** \file KX_FunctionNodesB6File.cpp
 *  \ingroup gamelogic
 *
 *  B6.4 advanced file nodes: binary save games, slot listing, trivial
 *  encryption and line-oriented reads.
 *
 *  `EncryptData` is a XOR stream + Base64 *obfuscation* (only readable by a
 *  casual viewer, not a security primitive; documented in
 *  docs/B6_FILE_ADVANCED.md). There is no AES entry point in Blender's BLI, so
 *  `algorithm: aes` reports "AES 待接入" on stderr and takes the failure path
 *  instead of silently falling back to XOR.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "KX_B4AssetIO.h"
#include "KX_ExecutionContext.h"
#include "KX_FileAdapter.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_Json.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"
#include "KX_SaveGameBinary.h"
#include "KX_ValuePool.h"

#define B6FILE_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}
#define B6FILE_COUNTS(array) static_cast<uint32_t>(B6FILE_ARRAY_LEN(array))
#define B6FILE_ACTION(id, ins, outs, exec) \
  { id, ins, B6FILE_COUNTS(ins), outs, B6FILE_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, false, false, false }
#define B6FILE_VALUE_SOURCE(id, ins, outs, exec) \
  { id, ins, 0, outs, B6FILE_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, true, false, false }

namespace {

std::string B6FileInString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue &value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

void B6FileWarn(const std::string &node, const std::string &message)
{
  std::fprintf(stderr, "GameNode: %s: %s\n", node.c_str(), message.c_str());
}

/* ------------------------------------------------------------------------- */
/* Base64 (obfuscation container, not encryption)                            */

const char *kB64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string B6Base64Encode(const std::string &input)
{
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);
  size_t index = 0;
  while (index + 2 < input.size()) {
    const uint32_t triple = (uint32_t(uint8_t(input[index])) << 16) |
                            (uint32_t(uint8_t(input[index + 1])) << 8) |
                            uint32_t(uint8_t(input[index + 2]));
    out.push_back(kB64Alphabet[(triple >> 18) & 0x3f]);
    out.push_back(kB64Alphabet[(triple >> 12) & 0x3f]);
    out.push_back(kB64Alphabet[(triple >> 6) & 0x3f]);
    out.push_back(kB64Alphabet[triple & 0x3f]);
    index += 3;
  }
  const size_t remaining = input.size() - index;
  if (remaining == 1) {
    const uint32_t triple = uint32_t(uint8_t(input[index])) << 16;
    out.push_back(kB64Alphabet[(triple >> 18) & 0x3f]);
    out.push_back(kB64Alphabet[(triple >> 12) & 0x3f]);
    out.push_back('=');
    out.push_back('=');
  }
  else if (remaining == 2) {
    const uint32_t triple = (uint32_t(uint8_t(input[index])) << 16) |
                            (uint32_t(uint8_t(input[index + 1])) << 8);
    out.push_back(kB64Alphabet[(triple >> 18) & 0x3f]);
    out.push_back(kB64Alphabet[(triple >> 12) & 0x3f]);
    out.push_back(kB64Alphabet[(triple >> 6) & 0x3f]);
    out.push_back('=');
  }
  return out;
}

bool B6Base64Decode(const std::string &input, std::string *out)
{
  if (input.size() % 4 != 0) {
    return false;
  }
  out->clear();
  out->reserve((input.size() / 4) * 3);
  for (size_t index = 0; index < input.size(); index += 4) {
    uint32_t value = 0;
    int padding = 0;
    for (int offset = 0; offset < 4; ++offset) {
      const char symbol = input[index + offset];
      if (symbol == '=') {
        ++padding;
        value <<= 6;
        continue;
      }
      if (padding > 0) {
        return false;
      }
      const char *slot = std::strchr(kB64Alphabet, symbol);
      if (slot == nullptr) {
        return false;
      }
      value = (value << 6) | uint32_t(slot - kB64Alphabet);
    }
    if (padding > 2) {
      return false;
    }
    out->push_back(char((value >> 16) & 0xff));
    if (padding < 2) {
      out->push_back(char((value >> 8) & 0xff));
    }
    if (padding < 1) {
      out->push_back(char(value & 0xff));
    }
  }
  return true;
}

bool B6Xor(const std::string &input, const std::string &key, std::string *out)
{
  if (key.empty()) {
    return false;
  }
  out->resize(input.size());
  for (size_t index = 0; index < input.size(); ++index) {
    (*out)[index] = char(uint8_t(input[index]) ^ uint8_t(key[index % key.size()]));
  }
  return true;
}

/* ------------------------------------------------------------------------- */
/* SaveGameBinary                                                            */

const KX_FuncPort kSaveGameBinaryInputs[] = {
    TP("OnEnter"),
    FP("Slot", KX_VALUE_STRING),
    FP("Compress", KX_VALUE_BOOL),
};
const KX_FuncPort kSaveGameBinaryOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncSaveGameBinary(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string slot = B6FileInString(node, ctx, 1);
  const bool compress = node.InBool(ctx, 2);
  KXJson::Value root;
  std::string error;
  if (!B4SerializeAssetsValue(ctx, &root, &error)) {
    B6FileWarn("SaveGameBinary", error);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  const std::string blob = KXSaveGameBinary::Encode(root, compress);
  if (blob.empty()) {
    B6FileWarn("SaveGameBinary",
               "asset graph exceeds the binary depth limit or the payload could not be compressed");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  if (!ctx.File().SaveGameBinary(slot, blob)) {
    B6FileWarn("SaveGameBinary", "adapter failed to write slot '" + slot + "'");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* SaveGameSlots                                                             */

const KX_FuncPort kSaveGameSlotsOutputs[] = {
    FP("Slots", KX_VALUE_LIST),
    FP("Count", KX_VALUE_INT),
};

void FuncSaveGameSlots(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  std::vector<std::string> slots;
  const bool ok = ctx.File().ListSaveSlots(&slots);
  if (!ok) {
    B6FileWarn("SaveGameSlots", "adapter failed to list save slots");
    slots.clear();
  }
  KX_ValueList &list = ctx.Pool().AllocList(slots.size());
  for (const std::string &slot : slots) {
    list.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(slot)));
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&list));
  node.OutInt(ctx, 1, static_cast<int>(slots.size()));
}

/* ------------------------------------------------------------------------- */
/* EncryptData / DecryptData                                                 */

const KX_FuncPort kEncryptDataInputs[] = {
    TP("OnEnter"),
    FP("Data", KX_VALUE_STRING),
    FP("Key", KX_VALUE_STRING),
};
const KX_FuncPort kEncryptDataOutputs[] = {
    TP("OnDone"),
    FP("Encrypted", KX_VALUE_STRING),
};

const KX_FuncPort kDecryptDataInputs[] = {
    TP("OnEnter"),
    FP("Encrypted", KX_VALUE_STRING),
    FP("Key", KX_VALUE_STRING),
};
const KX_FuncPort kDecryptDataOutputs[] = {
    TP("OnDone"),
    FP("Data", KX_VALUE_STRING),
};

void FuncEncryptData(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string algorithm = node.ConfigString("algorithm");
  const std::string data = B6FileInString(node, ctx, 1);
  const std::string key = B6FileInString(node, ctx, 2);
  if (algorithm == "aes") {
    B6FileWarn("EncryptData", "AES 待接入 (no BLI AES entry point); refusing to fall back to XOR");
    return;
  }
  if (algorithm != "xor" && !algorithm.empty()) {
    B6FileWarn("EncryptData", "unknown algorithm '" + algorithm + "'");
    return;
  }
  std::string stream;
  if (!B6Xor(data, key, &stream)) {
    B6FileWarn("EncryptData", "key must not be empty");
    return;
  }
  node.OutString(ctx, 1, B6Base64Encode(stream));
  node.OutTrigger(ctx, 0, true);
}

void FuncDecryptData(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string algorithm = node.ConfigString("algorithm");
  const std::string encrypted = B6FileInString(node, ctx, 1);
  const std::string key = B6FileInString(node, ctx, 2);
  if (algorithm == "aes") {
    B6FileWarn("DecryptData", "AES 待接入 (no BLI AES entry point); refusing to fall back to XOR");
    return;
  }
  if (algorithm != "xor" && !algorithm.empty()) {
    B6FileWarn("DecryptData", "unknown algorithm '" + algorithm + "'");
    return;
  }
  std::string stream;
  if (!B6Base64Decode(encrypted, &stream)) {
    B6FileWarn("DecryptData", "input is not a valid Base64 payload");
    return;
  }
  std::string data;
  if (!B6Xor(stream, key, &data)) {
    B6FileWarn("DecryptData", "key must not be empty");
    return;
  }
  node.OutString(ctx, 1, data);
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* FileReadLines                                                             */

const KX_FuncPort kFileReadLinesInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
};
const KX_FuncPort kFileReadLinesOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
    FP("Lines", KX_VALUE_LIST),
};

bool B6PathEscapes(const std::string &path)
{
  if (path.empty()) {
    return true;
  }
  size_t start = 0;
  while (start <= path.size()) {
    const size_t slash = path.find('/', start);
    const std::string segment =
        slash == std::string::npos ? path.substr(start) : path.substr(start, slash - start);
    if (segment == "..") {
      return true;
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return false;
}

void FuncFileReadLines(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string path = B6FileInString(node, ctx, 1);
  if (B6PathEscapes(path)) {
    B6FileWarn("FileReadLines", "path '" + path + "' escapes the blend directory");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  std::string content;
  if (!ctx.File().ReadFile(path, false, &content)) {
    B6FileWarn("FileReadLines", "cannot read '" + path + "'");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  if (content.compare(0, 8, "\xEF\xBB\xBF") == 0) {
    content.erase(0, 3);
  }

  KX_ValueList &lines = ctx.Pool().AllocList();
  size_t start = 0;
  while (start <= content.size()) {
    const size_t newline = content.find('\n', start);
    std::string line = newline == std::string::npos ? content.substr(start)
                                                    : content.substr(start, newline - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    /* A trailing newline terminates the last line instead of adding a blank
     * element; blank lines inside the file are preserved. */
    const bool trailing = newline == std::string::npos;
    if (trailing && line.empty()) {
      break;
    }
    lines.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(line)));
    if (newline == std::string::npos) {
      break;
    }
    start = newline + 1;
  }

  node.Out(ctx, 2, KX_RuntimeValue::List(&lines));
  node.OutTrigger(ctx, 0, true);
  node.OutTrigger(ctx, 1, false);
}

/* ------------------------------------------------------------------------- */
/* B6.9 Fi-1: streaming read/write (appended)                                 */

const uint32_t kB6FileMaxChunkSize = 1024 * 1024;

bool B6FileValidateChunkSize(const char *name, int configured, uint32_t *out)
{
  if (configured <= 0 || static_cast<uint32_t>(configured) > kB6FileMaxChunkSize) {
    B6FileWarn(name,
               "chunk_size must be in [1, 1048576] bytes, got " + std::to_string(configured));
    return false;
  }
  *out = static_cast<uint32_t>(configured);
  return true;
}

const KX_FuncPort kFileReadChunkInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
    FP("Offset", KX_VALUE_INT),
};
const KX_FuncPort kFileReadChunkOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
    FP("Content", KX_VALUE_STRING),
    FP("Size", KX_VALUE_INT),
    FP("Eof", KX_VALUE_BOOL),
};

void FuncFileReadChunk(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  uint32_t chunk_size = 0;
  if (!B6FileValidateChunkSize("FileReadChunk", node.ConfigInt("chunk_size", 65536), &chunk_size)) {
    node.OutTrigger(ctx, 1, true);
    return;
  }
  const std::string path = B6FileInString(node, ctx, 1);
  const int offset = node.InInt(ctx, 2);
  if (path.empty()) {
    B6FileWarn("FileReadChunk", "path must not be empty");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  if (offset < 0) {
    B6FileWarn("FileReadChunk", "offset must be >= 0, got " + std::to_string(offset));
    node.OutTrigger(ctx, 1, true);
    return;
  }
  std::string content;
  bool eof = false;
  if (!ctx.File().ReadFileRange(path, static_cast<size_t>(offset), chunk_size, &content, &eof)) {
    B6FileWarn("FileReadChunk", "cannot read a chunk of '" + path + "' at offset " +
                                   std::to_string(offset));
    node.OutTrigger(ctx, 1, true);
    return;
  }
  node.OutString(ctx, 2, content);
  node.OutInt(ctx, 3, static_cast<int>(content.size()));
  node.OutBool(ctx, 4, eof);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kFileWriteChunkInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
    FP("Content", KX_VALUE_STRING),
};
const KX_FuncPort kFileWriteChunkOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncFileWriteChunk(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  uint32_t chunk_size = 0;
  if (!B6FileValidateChunkSize("FileWriteChunk", node.ConfigInt("chunk_size", 65536),
                               &chunk_size)) {
    node.OutTrigger(ctx, 1, true);
    return;
  }
  const std::string path = B6FileInString(node, ctx, 1);
  const std::string content = B6FileInString(node, ctx, 2);
  if (path.empty()) {
    B6FileWarn("FileWriteChunk", "path must not be empty");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  const bool append = node.ConfigBool("append", false);
  size_t cursor = 0;
  bool first = true;
  while (cursor < content.size() || first) {
    const size_t take = std::min<size_t>(chunk_size, content.size() - cursor);
    const bool truncate = !append && first;
    if (!ctx.File().WriteFileRange(path,
                                   content.substr(cursor, take),
                                   truncate)) {
      B6FileWarn("FileWriteChunk", "cannot write a chunk of '" + path + "' at offset " +
                                      std::to_string(cursor));
      node.OutTrigger(ctx, 1, true);
      return;
    }
    first = false;
    if (take == 0) {
      break;
    }
    cursor += take;
  }
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* B6.9 Fi-2: directory operations (appended)                                */

const KX_FuncPort kListDirectoryInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
};
const KX_FuncPort kListDirectoryOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
    FP("Entries", KX_VALUE_LIST),
    FP("Directories", KX_VALUE_LIST),
    FP("Count", KX_VALUE_INT),
};

void FuncListDirectory(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string path = B6FileInString(node, ctx, 1);
  std::vector<KX_DirectoryEntry> entries;
  std::string error;
  if (!ctx.File().ListDirectory(path, &entries, &error)) {
    B6FileWarn("ListDirectory",
               error.empty() ? "cannot list directory '" + path + "'" : error);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  KX_ValueList &names = ctx.Pool().AllocList();
  KX_ValueList &directories = ctx.Pool().AllocList();
  for (const KX_DirectoryEntry &entry : entries) {
    names.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(entry.name)));
    if (entry.directory) {
      directories.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(entry.name)));
    }
  }
  node.Out(ctx, 2, KX_RuntimeValue::List(&names));
  node.Out(ctx, 3, KX_RuntimeValue::List(&directories));
  node.OutInt(ctx, 4, static_cast<int>(entries.size()));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kCreateDirectoryInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
};
const KX_FuncPort kCreateDirectoryOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncCreateDirectory(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string path = B6FileInString(node, ctx, 1);
  const bool parents = node.ConfigBool("parents", true);
  std::string error;
  if (!ctx.File().CreateDirectory(path, parents, &error)) {
    B6FileWarn("CreateDirectory",
               error.empty() ? "cannot create directory '" + path + "'" : error);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kRemoveDirectoryInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
};
const KX_FuncPort kRemoveDirectoryOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncRemoveDirectory(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string path = B6FileInString(node, ctx, 1);
  const bool recursive = node.ConfigBool("recursive", false);
  std::string error;
  if (!ctx.File().RemoveDirectory(path, recursive, &error)) {
    B6FileWarn("RemoveDirectory",
               error.empty() ? "cannot remove directory '" + path + "'" : error);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */

const KX_FunctionSpec kB6FileSpecs[] = {
    B6FILE_ACTION("SaveGameBinary",
                  kSaveGameBinaryInputs,
                  kSaveGameBinaryOutputs,
                  FuncSaveGameBinary),
    B6FILE_VALUE_SOURCE("SaveGameSlots", nullptr, kSaveGameSlotsOutputs, FuncSaveGameSlots),
    B6FILE_ACTION("EncryptData", kEncryptDataInputs, kEncryptDataOutputs, FuncEncryptData),
    B6FILE_ACTION("DecryptData", kDecryptDataInputs, kDecryptDataOutputs, FuncDecryptData),
    B6FILE_ACTION("FileReadLines", kFileReadLinesInputs, kFileReadLinesOutputs, FuncFileReadLines),
};

/* B6.9 Fi-1/Fi-2 specs are registered separately so the B6.4 table above stays
 * untouched for other in-flight branches. */
const KX_FunctionSpec kB6FileStreamSpecs[] = {
    B6FILE_ACTION("FileReadChunk", kFileReadChunkInputs, kFileReadChunkOutputs, FuncFileReadChunk),
    B6FILE_ACTION("FileWriteChunk", kFileWriteChunkInputs, kFileWriteChunkOutputs, FuncFileWriteChunk),
    B6FILE_ACTION("ListDirectory", kListDirectoryInputs, kListDirectoryOutputs, FuncListDirectory),
    B6FILE_ACTION("CreateDirectory",
                  kCreateDirectoryInputs,
                  kCreateDirectoryOutputs,
                  FuncCreateDirectory),
    B6FILE_ACTION("RemoveDirectory",
                  kRemoveDirectoryInputs,
                  kRemoveDirectoryOutputs,
                  FuncRemoveDirectory),
};

KX_NodeSignature MakeB6FileSignature(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    port.kind = spec.inputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.inputs[i].trigger ? KX_VALUE_NONE : spec.inputs[i].type;
    signature.inputs.push_back(port);
  }
  for (uint32_t i = 0; i < spec.output_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.outputs[i].name;
    port.kind = spec.outputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.outputs[i].trigger ? KX_VALUE_NONE : spec.outputs[i].type;
    signature.outputs.push_back(port);
  }
  signature.state_size = spec.state_size;
  signature.state_alignment = spec.state_alignment;
  signature.needs_tick = spec.needs_tick || spec.tick != nullptr;
  signature.breaks_trigger_cycles = spec.breaks_trigger_cycles;
  signature.ports_from_ir = spec.ports_from_ir;
  return signature;
}

}  // namespace

void RegisterB6FileFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6FileSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6File_") + spec.type_id,
        MakeB6FileSignature(*entry),
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
  for (const KX_FunctionSpec &spec : kB6FileStreamSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6File_") + spec.type_id,
        MakeB6FileSignature(*entry),
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
