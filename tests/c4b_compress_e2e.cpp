/*
 * B6.7 Phase C.4.2 - KX_SaveGameBinary payload compression E2E.
 *
 * Covers:
 *   1. compress=false / compress=true round-trip, decoded content byte-identical
 *   2. compressed container is smaller than the raw one for a large payload
 *   3. corrupted deflate stream (CRC fixed up so zlib itself fails) -> error, no crash
 *   4. legacy v1 (no compression flag) blob still decodes
 * Plus header invariants and explicit errors for truncated / bad-version blobs.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "KX_Json.h"
#include "KX_SaveGameBinary.h"

namespace {

int g_assertions = 0;
int g_failures = 0;

void Check(bool condition, const char *name, const std::string &detail = std::string())
{
  ++g_assertions;
  if (condition) {
    std::printf("PASS  %s\n", name);
    return;
  }
  ++g_failures;
    std::printf("FAIL  %s%s%s\n",
                name,
                detail.empty() ? "" : " : ",
                detail.empty() ? "" : detail.c_str());
}

uint32_t ReadU32(const std::string &blob, size_t offset)
{
  return uint32_t(uint8_t(blob[offset])) | (uint32_t(uint8_t(blob[offset + 1])) << 8) |
         (uint32_t(uint8_t(blob[offset + 2])) << 16) |
         (uint32_t(uint8_t(blob[offset + 3])) << 24);
}

void WriteU32(std::string *blob, size_t offset, uint32_t value)
{
  (*blob)[offset + 0] = char((value >> 0) & 0xffu);
  (*blob)[offset + 1] = char((value >> 8) & 0xffu);
  (*blob)[offset + 2] = char((value >> 16) & 0xffu);
  (*blob)[offset + 3] = char((value >> 24) & 0xffu);
}

KXJson::Value MakeLargeValue()
{
  KXJson::Value root = KXJson::Value::MakeObject();
  root.object.push_back({"version", KXJson::Value::MakeNumber(1)});

  KXJson::Value assets = KXJson::Value::MakeArray();
  for (int index = 0; index < 4000; ++index) {
    KXJson::Value asset = KXJson::Value::MakeObject();
    asset.object.push_back(
        {"name", KXJson::Value::MakeString("asset_" + std::to_string(index) +
                                          "_repeated_text_for_compression")});
    asset.object.push_back({"id", KXJson::Value::MakeNumber(index)});
    asset.object.push_back({"enabled", KXJson::Value::MakeBool(index % 2 == 0)});
    KXJson::Value values = KXJson::Value::MakeArray();
    for (int slot = 0; slot < 8; ++slot) {
      values.array.push_back(KXJson::Value::MakeNumber(slot * 0.125));
    }
    asset.object.push_back({"values", values});
    assets.array.push_back(asset);
  }
  root.object.push_back({"assets", assets});
  return root;
}

bool DecodeFailure(const std::string &blob, std::string *error)
{
  KXJson::Value value;
  const bool ok = KXSaveGameBinary::Decode(blob, &value, error);
  return !ok;
}

}  // namespace

int main()
{
  const KXJson::Value original = MakeLargeValue();

  /* --- 1/2: write both variants ----------------------------------------- */
  const std::string raw = KXSaveGameBinary::Encode(original, false);
  const std::string packed = KXSaveGameBinary::Encode(original, true);
  Check(!raw.empty() && !packed.empty(), "encode raw/compressed blobs");

  Check(raw.compare(0, 4, "EBCS") == 0 && packed.compare(0, 4, "EBCS") == 0,
        "both blobs keep the EBCS magic and 16-byte header");
  Check(ReadU32(raw, 4) == 1, "compress=false writes version 1 (legacy/raw)");
  Check(ReadU32(packed, 4) == 2, "compress=true writes version 2 (zlib)");
  Check(raw.size() == 16u + ReadU32(raw, 8), "raw length field matches header size");
  Check(packed.size() == 16u + ReadU32(packed, 8), "compressed length field matches header size");

  KXJson::Value decoded_raw;
  KXJson::Value decoded_packed;
  std::string error;
  const bool raw_ok = KXSaveGameBinary::Decode(raw, &decoded_raw, &error);
  Check(raw_ok, "decode raw blob", error);
  error.clear();
  const bool packed_ok = KXSaveGameBinary::Decode(packed, &decoded_packed, &error);
  Check(packed_ok, "decode compressed blob", error);

  /* Re-encoding the decoded trees yields identical bytes, i.e. the round-trip
   * preserves the payload exactly, not just "some" of it. */
  if (raw_ok && packed_ok) {
    const std::string raw_again = KXSaveGameBinary::Encode(decoded_raw, false);
    const std::string packed_again = KXSaveGameBinary::Encode(decoded_packed, false);
    Check(raw_again == packed_again, "decoded content is byte-identical across both variants");
    Check(raw_again == raw, "decoded raw content re-encodes to the original bytes");
  }

  Check(packed.size() < raw.size(), "compressed file is smaller than the raw file");
  std::printf("INFO  raw=%zu bytes compressed=%zu bytes ratio=%.2f%%\n",
              raw.size(),
              packed.size(),
              raw.empty() ? 0.0 : 100.0 * double(packed.size()) / double(raw.size()));

  /* Small payload still round-trips even when deflate would not help. */
  KXJson::Value small = KXJson::Value::MakeObject();
  small.object.push_back({"k", KXJson::Value::MakeString("v")});
  const std::string small_packed = KXSaveGameBinary::Encode(small, true);
  KXJson::Value small_decoded;
  error.clear();
  Check(!small_packed.empty() &&
            KXSaveGameBinary::Decode(small_packed, &small_decoded, &error) &&
            KXSaveGameBinary::Encode(small_decoded, false) == KXSaveGameBinary::Encode(small, false),
        "small compressed payload round-trips",
        error);

  /* --- 3: corrupt the deflate stream, keep CRC valid --------------------- */
  std::string damaged = packed;
  const size_t stored_begin = 16;
  const size_t stored_length = damaged.size() - stored_begin;
  damaged[stored_begin + stored_length / 2] = char(uint8_t(damaged[stored_begin + stored_length / 2]) ^ 0x5a);
  WriteU32(&damaged, 12, KXSaveGameBinary::Crc32(damaged.data() + stored_begin, stored_length));
  std::string damaged_error;
  Check(DecodeFailure(damaged, &damaged_error), "corrupted deflate stream is rejected");
  Check(!damaged_error.empty(), "corrupted deflate stream reports an explicit error", damaged_error);
  std::printf("INFO  corrupted-stream error: %s\n", damaged_error.c_str());

  /* CRC-only corruption (stream itself untouched) must fail earlier. */
  std::string crc_broken = packed;
  crc_broken[stored_begin] = char(uint8_t(crc_broken[stored_begin]) ^ 0x01);
  std::string crc_error;
  Check(DecodeFailure(crc_broken, &crc_error), "checksum mismatch on compressed payload is rejected");
  std::printf("INFO  crc-mismatch error: %s\n", crc_error.c_str());

  /* --- 4: legacy v1 (raw, no compression flag) still decodes ------------- */
  KXJson::Value legacy;
  error.clear();
  Check(KXSaveGameBinary::Decode(raw, &legacy, &error) && ReadU32(raw, 4) == 1,
        "legacy v1 blob (no compression flag) still decodes",
        error);

  /* Unknown version -> explicit error, not silent success. */
  std::string unknown = raw;
  WriteU32(&unknown, 4, 3);
  std::string unknown_error;
  Check(DecodeFailure(unknown, &unknown_error), "version 3 is rejected");
  Check(unknown_error.find("unsupported save version 3") != std::string::npos,
        "unknown version reports the version number",
        unknown_error);

  /* Truncated compressed payload -> explicit error. */
  std::string truncated = packed.substr(0, packed.size() - 8);
  std::string truncated_error;
  Check(DecodeFailure(truncated, &truncated_error), "truncated compressed blob is rejected");
  std::printf("INFO  truncated error: %s\n", truncated_error.c_str());

  /* Garbage inside a valid-length compressed payload. */
  std::string garbage = packed;
  for (size_t offset = stored_begin; offset < garbage.size(); ++offset) {
    garbage[offset] = '\0';
  }
  WriteU32(&garbage, 12, KXSaveGameBinary::Crc32(garbage.data() + stored_begin, stored_length));
  std::string garbage_error;
  Check(DecodeFailure(garbage, &garbage_error), "non-deflate payload bytes are rejected");
  std::printf("INFO  garbage error: %s\n", garbage_error.c_str());

  std::printf("RESULT assertions=%d failures=%d\n", g_assertions, g_failures);
  return g_failures == 0 ? 0 : 1;
}
