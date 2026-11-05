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

/** \file KX_SaveGameBinary.h
 *  \ingroup gamelogic
 *
 *  B6.4 binary save container.
 *
 *  The payload encodes the same asset/scene schema as the JSON save
 *  (`KXJson::Value`) but as tagged records. The container adds a magic, a
 *  version and a CRC32 so truncated, corrupted or future files are rejected
 *  instead of applied.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "KX_Json.h"

namespace KXSaveGameBinary {

/**
 * Container versions; the version field doubles as the payload compression
 * flag so the 16-byte header layout stays unchanged.
 *
 * - #kVersion (1): payload stored verbatim. This is both the legacy format and
 *   what `Encode(value, false)` writes, so old saves and new uncompressed ones
 *   are byte-identical.
 * - #kVersionZlib (2): payload is a zlib deflate stream. Written by
 *   `Encode(value, true)`.
 *
 * Readers accept both; any other value is rejected as unsupported.
 */
constexpr uint32_t kVersion = 1;
constexpr uint32_t kVersionZlib = 2;

/** Little-endian CRC32 (polynomial 0xEDB88320) over @p data. */
uint32_t Crc32(const void *data, size_t len);

/**
 * Encode @p value into a versioned binary blob.
 *
 * @param compress deflate the payload with zlib (version 2); when false the
 *                 payload is stored verbatim (version 1, the legacy format).
 * @return the blob; empty if the value exceeds the depth limit or the deflate
 *         step fails.
 */
std::string Encode(const KXJson::Value &value, bool compress = false);

/**
 * Decode a blob produced by #Encode.
 *
 * @param out   decoded value, only written on success
 * @param error human readable reason on failure
 */
bool Decode(const std::string &blob, KXJson::Value *out, std::string *error);

}  // namespace KXSaveGameBinary
