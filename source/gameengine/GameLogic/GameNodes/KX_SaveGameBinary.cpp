/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file KX_SaveGameBinary.cpp
 *  \ingroup gamelogic
 */

#include "KX_SaveGameBinary.h"

#include <cstring>
#include <vector>

#include <zlib.h>

namespace KXSaveGameBinary {

namespace {

constexpr size_t kHeaderSize = 16;
constexpr size_t kMaxDepth = 64;
/** Refuse pathological compressed streams instead of exhausting memory. */
constexpr size_t kMaxInflatedBytes = 512u * 1024u * 1024u;

const char kMagic[4] = {'E', 'B', 'C', 'S'};

void AppendU32(std::string *out, uint32_t value)
{
  out->push_back(static_cast<char>((value >> 0) & 0xffu));
  out->push_back(static_cast<char>((value >> 8) & 0xffu));
  out->push_back(static_cast<char>((value >> 16) & 0xffu));
  out->push_back(static_cast<char>((value >> 24) & 0xffu));
}

void AppendU64(std::string *out, uint64_t value)
{
  AppendU32(out, static_cast<uint32_t>(value & 0xffffffffu));
  AppendU32(out, static_cast<uint32_t>((value >> 32) & 0xffffffffu));
}

void AppendString(std::string *out, const std::string &value)
{
  AppendU32(out, static_cast<uint32_t>(value.size()));
  out->append(value);
}

bool EncodeValue(const KXJson::Value &value, size_t depth, std::string *out)
{
  if (depth > kMaxDepth) {
    return false;
  }
  switch (value.type) {
    case KXJson::Type::Null:
      out->push_back('\0');
      return true;
    case KXJson::Type::Bool:
      out->push_back('\1');
      out->push_back(value.boolean ? '\1' : '\0');
      return true;
    case KXJson::Type::Number:
      out->push_back('\2');
      {
        uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value.number), "double is 8 bytes");
        std::memcpy(&bits, &value.number, sizeof(bits));
        AppendU64(out, bits);
      }
      return true;
    case KXJson::Type::String:
      out->push_back('\3');
      AppendString(out, value.string);
      return true;
    case KXJson::Type::Array:
      out->push_back('\4');
      AppendU32(out, static_cast<uint32_t>(value.array.size()));
      for (const KXJson::Value &element : value.array) {
        if (!EncodeValue(element, depth + 1, out)) {
          return false;
        }
      }
      return true;
    case KXJson::Type::Object:
      out->push_back('\5');
      AppendU32(out, static_cast<uint32_t>(value.object.size()));
      for (const auto &member : value.object) {
        AppendString(out, member.first);
        if (!EncodeValue(member.second, depth + 1, out)) {
          return false;
        }
      }
      return true;
  }
  return false;
}

struct Reader {
  const std::string &data;
  size_t pos = 0;

  bool ReadU32(uint32_t *out)
  {
    if (pos + 4 > data.size()) {
      return false;
    }
    *out = uint32_t(uint8_t(data[pos])) | (uint32_t(uint8_t(data[pos + 1])) << 8) |
           (uint32_t(uint8_t(data[pos + 2])) << 16) |
           (uint32_t(uint8_t(data[pos + 3])) << 24);
    pos += 4;
    return true;
  }

  bool ReadU64(uint64_t *out)
  {
    uint32_t low = 0;
    uint32_t high = 0;
    if (!ReadU32(&low) || !ReadU32(&high)) {
      return false;
    }
    *out = uint64_t(low) | (uint64_t(high) << 32);
    return true;
  }

  bool ReadString(std::string *out)
  {
    uint32_t length = 0;
    if (!ReadU32(&length)) {
      return false;
    }
    if (pos + length > data.size()) {
      return false;
    }
    out->assign(data, pos, length);
    pos += length;
    return true;
  }
};

bool DecodeValue(Reader *reader, size_t depth, KXJson::Value *out)
{
  if (depth > kMaxDepth || reader->pos >= reader->data.size()) {
    return false;
  }
  const char tag = reader->data[reader->pos++];
  switch (tag) {
    case '\0':
      *out = KXJson::Value::MakeNull();
      return true;
    case '\1': {
      if (reader->pos >= reader->data.size()) {
        return false;
      }
      const char raw = reader->data[reader->pos++];
      *out = KXJson::Value::MakeBool(raw != '\0');
      return true;
    }
    case '\2': {
      uint64_t bits = 0;
      if (!reader->ReadU64(&bits)) {
        return false;
      }
      double number = 0.0;
      std::memcpy(&number, &bits, sizeof(number));
      *out = KXJson::Value::MakeNumber(number);
      return true;
    }
    case '\3': {
      std::string text;
      if (!reader->ReadString(&text)) {
        return false;
      }
      *out = KXJson::Value::MakeString(std::move(text));
      return true;
    }
    case '\4': {
      uint32_t count = 0;
      if (!reader->ReadU32(&count)) {
        return false;
      }
      *out = KXJson::Value::MakeArray();
      out->array.resize(count);
      for (uint32_t index = 0; index < count; ++index) {
        if (!DecodeValue(reader, depth + 1, &out->array[index])) {
          return false;
        }
      }
      return true;
    }
    case '\5': {
      uint32_t count = 0;
      if (!reader->ReadU32(&count)) {
        return false;
      }
      *out = KXJson::Value::MakeObject();
      out->object.resize(count);
      for (uint32_t index = 0; index < count; ++index) {
        if (!reader->ReadString(&out->object[index].first)) {
          return false;
        }
        if (!DecodeValue(reader, depth + 1, &out->object[index].second)) {
          return false;
        }
      }
      return true;
    }
    default:
      return false;
  }
}

/* ------------------------------------------------------------------------- */
/* Payload compression (version 2)                                           */

bool DeflatePayload(const std::string &input, std::string *output, std::string *error)
{
  const uLong source_size = static_cast<uLong>(input.size());
  const uLong bound = compressBound(source_size);
  output->resize(static_cast<size_t>(bound));
  uLongf destination_size = bound;
  const int status = compress2(reinterpret_cast<Bytef *>(output->data()),
                               &destination_size,
                               reinterpret_cast<const Bytef *>(input.data()),
                               source_size,
                               Z_DEFAULT_COMPRESSION);
  if (status != Z_OK) {
    output->clear();
    if (error != nullptr) {
      *error = "zlib failed to compress the save payload";
    }
    return false;
  }
  output->resize(static_cast<size_t>(destination_size));
  return true;
}

bool InflatePayload(const std::string &input, std::string *output, std::string *error)
{
  z_stream stream = {};
  stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  if (inflateInit(&stream) != Z_OK) {
    if (error != nullptr) {
      *error = "zlib failed to initialise the save decompressor";
    }
    return false;
  }

  constexpr size_t kChunkSize = 64u * 1024u;
  output->clear();
  int status = Z_OK;
  bool limit_hit = false;
  while (status == Z_OK) {
    const size_t produced_from = output->size();
    if (produced_from >= kMaxInflatedBytes) {
      limit_hit = true;
      break;
    }
    output->resize(produced_from + kChunkSize);
    stream.next_out = reinterpret_cast<Bytef *>(output->data() + produced_from);
    stream.avail_out = static_cast<uInt>(kChunkSize);
    status = inflate(&stream, Z_NO_FLUSH);
    output->resize(produced_from + (kChunkSize - static_cast<size_t>(stream.avail_out)));
  }

  const bool complete = status == Z_STREAM_END && stream.avail_in == 0;
  inflateEnd(&stream);

  if (limit_hit) {
    output->clear();
    if (error != nullptr) {
      *error = "compressed save payload expands past the 512 MiB limit";
    }
    return false;
  }
  if (!complete) {
    output->clear();
    if (error != nullptr) {
      *error = "compressed save payload is corrupt or truncated";
    }
    return false;
  }
  return true;
}

}  // namespace

uint32_t Crc32(const void *data, size_t len)
{
  static uint32_t table[256];
  static bool initialized = false;
  if (!initialized) {
    for (uint32_t index = 0; index < 256; ++index) {
      uint32_t value = index;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
      }
      table[index] = value;
    }
    initialized = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  const unsigned char *bytes = static_cast<const unsigned char *>(data);
  for (size_t index = 0; index < len; ++index) {
    crc = table[(crc ^ bytes[index]) & 0xffu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

std::string Encode(const KXJson::Value &value, bool compress)
{
  std::string payload;
  if (!EncodeValue(value, 0, &payload)) {
    return std::string();
  }
  std::string stored = payload;
  uint32_t version = kVersion;
  if (compress) {
    std::string error;
    std::string deflated;
    if (!DeflatePayload(payload, &deflated, &error)) {
      return std::string();
    }
    stored.swap(deflated);
    version = kVersionZlib;
  }
  std::string blob;
  blob.reserve(kHeaderSize + stored.size());
  blob.append(kMagic, 4);
  AppendU32(&blob, version);
  AppendU32(&blob, static_cast<uint32_t>(stored.size()));
  AppendU32(&blob, Crc32(stored.data(), stored.size()));
  blob.append(stored);
  return blob;
}

bool Decode(const std::string &blob, KXJson::Value *out, std::string *error)
{
  if (out == nullptr) {
    return false;
  }
  if (blob.size() < kHeaderSize) {
    if (error != nullptr) {
      *error = "truncated save header";
    }
    return false;
  }
  if (std::memcmp(blob.data(), kMagic, 4) != 0) {
    if (error != nullptr) {
      *error = "not an EBC binary save";
    }
    return false;
  }
  Reader reader{blob, 4};
  uint32_t version = 0;
  uint32_t length = 0;
  uint32_t crc = 0;
  if (!reader.ReadU32(&version) || !reader.ReadU32(&length) || !reader.ReadU32(&crc)) {
    if (error != nullptr) {
      *error = "truncated save header";
    }
    return false;
  }
  if (version != kVersion && version != kVersionZlib) {
    if (error != nullptr) {
      *error = "unsupported save version " + std::to_string(version);
    }
    return false;
  }
  if (length > blob.size() - kHeaderSize ||
      reader.pos + length != blob.size()) {
    if (error != nullptr) {
      *error = "save payload length mismatch (truncated or trailing data)";
    }
    return false;
  }
  /* The checksum covers the stored bytes, so a damaged deflate stream is
   * rejected before it reaches zlib. */
  const std::string stored(blob, reader.pos, length);
  if (Crc32(stored.data(), stored.size()) != crc) {
    if (error != nullptr) {
      *error = "save checksum mismatch";
    }
    return false;
  }

  std::string payload;
  if (version == kVersionZlib) {
    if (!InflatePayload(stored, &payload, error)) {
      return false;
    }
  }
  else {
    payload = stored;
  }

  Reader payload_reader{payload, 0};
  KXJson::Value decoded;
  if (!DecodeValue(&payload_reader, 0, &decoded)) {
    if (error != nullptr) {
      *error = "malformed save payload";
    }
    return false;
  }
  if (payload_reader.pos != payload.size()) {
    if (error != nullptr) {
      *error = "trailing bytes in save payload";
    }
    return false;
  }
  *out = std::move(decoded);
  return true;
}

}  // namespace KXSaveGameBinary
