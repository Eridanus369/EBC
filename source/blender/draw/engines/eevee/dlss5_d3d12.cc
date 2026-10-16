/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Platform abstraction for the DLSS5 NR runtime.
 *
 * - Windows: nvngx_dlssnr.dll via D3D12 (not implemented in this build).
 * - Linux: frames are sent to a persistent opendlss-rs server over a Unix
 *   socket at /tmp/opendlss-nr.sock. The server is built and started
 *   separately (see opendlss-rs). When the socket is missing the session
 *   reports unavailable and EEVEE falls back to the native Film output.
 */

#include "dlss5_d3d12.hh"

#ifdef _WIN32

#include "BLI_string.hh"
#include "MEM_guardedalloc.h"

namespace blender::eevee {

struct Dlss5D3D12Session::Impl {
  char status_[128] = "Unavailable";
  double gpu_time_ms_ = -1.0;
};

Dlss5D3D12Session::Dlss5D3D12Session() {
  impl_ = new Impl();
  STRNCPY(impl_->status_, "DLSS5: Windows runtime not yet linked");
}
Dlss5D3D12Session::~Dlss5D3D12Session() { delete impl_; }

bool Dlss5D3D12Session::ensure_resources(int2, int2, int2, const Dlss5NRSettings &, bool, bool) { return false; }
bool Dlss5D3D12Session::warmup() { return false; }
void Dlss5D3D12Session::retry_initialization() {}
bool Dlss5D3D12Session::copy_inputs_and_evaluate(const Dlss5D3D12Frame &, bool, bool) { return false; }
bool Dlss5D3D12Session::wait_for_output() { return false; }
void Dlss5D3D12Session::reset() {}
gpu::Texture *Dlss5D3D12Session::color_texture() const { return nullptr; }
gpu::Texture *Dlss5D3D12Session::depth_texture() const { return nullptr; }
gpu::Texture *Dlss5D3D12Session::velocity_texture() const { return nullptr; }
gpu::Texture *Dlss5D3D12Session::output_texture() const { return nullptr; }
double Dlss5D3D12Session::gpu_time_ms() const { return impl_ ? impl_->gpu_time_ms_ : -1.0; }
bool Dlss5D3D12Session::available() const { return false; }
const char *Dlss5D3D12Session::status() const { return impl_ ? impl_->status_ : ""; }

}  // namespace blender::eevee

#else /* !_WIN32 */

#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "BLI_string.hh"
#include "MEM_guardedalloc.h"

#include "GPU_state.hh"
#include "GPU_texture.hh"

namespace blender::eevee {

namespace {

constexpr const char *k_sock_path = "/tmp/opendlss-nr.sock";
constexpr uint32_t k_magic = 0x4F4E4C52u;

struct RequestHeader {
  uint32_t magic;
  uint32_t width;
  uint32_t height;
  uint32_t seed;
  uint32_t auto_mask;
  float local_tone;
  float local_structure;
  float skin_structure;
  float style;
  uint32_t rgb_len;
};

struct ResponseHeader {
  uint32_t magic;
  uint32_t status;
  uint32_t width;
  uint32_t height;
  uint32_t rgb_len;
};

bool write_all(int fd, const void *buf, size_t len)
{
  const uint8_t *p = static_cast<const uint8_t *>(buf);
  while (len > 0) {
    ssize_t n = ::write(fd, p, len);
    if (n <= 0) {
      return false;
    }
    p += size_t(n);
    len -= size_t(n);
  }
  return true;
}

bool read_all(int fd, void *buf, size_t len)
{
  uint8_t *p = static_cast<uint8_t *>(buf);
  while (len > 0) {
    ssize_t n = ::read(fd, p, len);
    if (n <= 0) {
      return false;
    }
    p += size_t(n);
    len -= size_t(n);
  }
  return true;
}

}  // namespace

struct Dlss5D3D12Session::Impl {
  char status_[192] = "DLSS5: NR server not running (start opendlss-server)";
  double gpu_time_ms_ = -1.0;

  int sock_fd = -1;
  Dlss5NRSettings settings;
  int2 in_extent = int2(0);
  int2 out_extent = int2(0);

  gpu::Texture *color_tx = nullptr;
  gpu::Texture *depth_tx = nullptr;
  gpu::Texture *velocity_tx = nullptr;
  gpu::Texture *output_tx = nullptr;

  std::vector<uint8_t> rgb_in;
  std::vector<float> rgba_out;
};

Dlss5D3D12Session::Dlss5D3D12Session()
{
  impl_ = new Impl();
}

Dlss5D3D12Session::~Dlss5D3D12Session()
{
  if (impl_ != nullptr) {
    if (impl_->sock_fd >= 0) {
      ::close(impl_->sock_fd);
    }
    if (impl_->color_tx != nullptr) {
      GPU_texture_free(impl_->color_tx);
    }
    if (impl_->depth_tx != nullptr) {
      GPU_texture_free(impl_->depth_tx);
    }
    if (impl_->velocity_tx != nullptr) {
      GPU_texture_free(impl_->velocity_tx);
    }
    if (impl_->output_tx != nullptr) {
      GPU_texture_free(impl_->output_tx);
    }
    delete impl_;
  }
}

bool Dlss5D3D12Session::ensure_resources(int2 input_extent,
                                         int2 output_extent,
                                         int2 /*guide_extent*/,
                                         const Dlss5NRSettings &settings,
                                         bool /*color_is_scene_linear*/,
                                         bool /*depth_is_reverse_z*/)
{
  Impl &im = *impl_;

  if (im.sock_fd < 0) {
    im.sock_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (im.sock_fd < 0) {
      STRNCPY(im.status_, "DLSS5: socket() failed");
      return false;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, k_sock_path, sizeof(addr.sun_path) - 1);
    if (::connect(im.sock_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
      ::close(im.sock_fd);
      im.sock_fd = -1;
      STRNCPY(im.status_, "DLSS5: opendlss-server not running (start opendlss-server)");
      return false;
    }
  }

  if (im.color_tx == nullptr || im.in_extent != input_extent) {
    if (im.color_tx != nullptr) {
      GPU_texture_free(im.color_tx);
      GPU_texture_free(im.depth_tx);
      GPU_texture_free(im.velocity_tx);
      im.color_tx = nullptr;
      im.depth_tx = nullptr;
      im.velocity_tx = nullptr;
    }
    im.color_tx = GPU_texture_create_2d("dlss5_nr_color",
                                        input_extent.x,
                                        input_extent.y,
                                        1,
                                        gpu::TextureFormat::SFLOAT_16_16_16_16,
                                        GPU_TEXTURE_USAGE_SHADER_READ |
                                            GPU_TEXTURE_USAGE_ATTACHMENT,
                                        nullptr);
    im.depth_tx = GPU_texture_create_2d("dlss5_nr_depth",
                                        input_extent.x,
                                        input_extent.y,
                                        1,
                                        gpu::TextureFormat::SFLOAT_16_16_16_16,
                                        GPU_TEXTURE_USAGE_SHADER_READ |
                                            GPU_TEXTURE_USAGE_ATTACHMENT,
                                        nullptr);
    im.velocity_tx = GPU_texture_create_2d("dlss5_nr_velocity",
                                           input_extent.x,
                                           input_extent.y,
                                           1,
                                           gpu::TextureFormat::SFLOAT_16_16,
                                           GPU_TEXTURE_USAGE_SHADER_READ |
                                               GPU_TEXTURE_USAGE_ATTACHMENT,
                                           nullptr);
  }

  if (im.output_tx == nullptr || im.out_extent != output_extent) {
    if (im.output_tx != nullptr) {
      GPU_texture_free(im.output_tx);
      im.output_tx = nullptr;
    }
    im.output_tx = GPU_texture_create_2d("dlss5_nr_output",
                                         output_extent.x,
                                         output_extent.y,
                                         1,
                                         gpu::TextureFormat::SFLOAT_16_16_16_16,
                                         GPU_TEXTURE_USAGE_SHADER_READ |
                                             GPU_TEXTURE_USAGE_ATTACHMENT,
                                         nullptr);
  }

  if (im.color_tx == nullptr || im.output_tx == nullptr) {
    STRNCPY(im.status_, "DLSS5: texture allocation failed");
    return false;
  }

  im.in_extent = input_extent;
  im.out_extent = output_extent;
  im.settings = settings;
  STRNCPY(im.status_, "DLSS5: NR server connected");
  return true;
}

bool Dlss5D3D12Session::warmup()
{
  return impl_ != nullptr && impl_->sock_fd >= 0;
}

void Dlss5D3D12Session::retry_initialization()
{
  if (impl_ != nullptr && impl_->sock_fd >= 0) {
    ::close(impl_->sock_fd);
    impl_->sock_fd = -1;
  }
}

void Dlss5D3D12Session::reset() {}

bool Dlss5D3D12Session::copy_inputs_and_evaluate(const Dlss5D3D12Frame &frame,
                                                 bool /*copy_color*/,
                                                 bool /*copy_velocity*/)
{
  Impl &im = *impl_;
  if (im.sock_fd < 0) {
    return false;
  }
  if (frame.color == nullptr || im.color_tx == nullptr || im.output_tx == nullptr) {
    return false;
  }

  const int in_w = frame.input_extent.x;
  const int in_h = frame.input_extent.y;
  if (in_w <= 0 || in_h <= 0) {
    return false;
  }

  /* Read the prepared display color from our intermediate texture. */
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
  float *src = static_cast<float *>(GPU_texture_read(im.color_tx, GPU_DATA_FLOAT, 0));
  if (src == nullptr) {
    STRNCPY(im.status_, "DLSS5: color readback failed");
    return false;
  }

  const size_t in_pixels = size_t(in_w) * size_t(in_h);
  im.rgb_in.resize(in_pixels * 3);
  for (size_t i = 0; i < in_pixels; i++) {
    for (int c = 0; c < 3; c++) {
      float v = src[i * 4 + size_t(c)];
      v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
      im.rgb_in[i * 3 + size_t(c)] = uint8_t(v * 255.0f + 0.5f);
    }
  }
  MEM_delete(static_cast<float *>(src));

  RequestHeader req{};
  req.magic = k_magic;
  req.width = uint32_t(in_w);
  req.height = uint32_t(in_h);
  req.seed = 0;
  req.auto_mask = im.settings.use_auto_mask ? 1u : 0u;
  req.local_tone = im.settings.local_tone_strength;
  req.local_structure = im.settings.local_structure_strength;
  req.skin_structure = im.settings.skin_structure_strength;
  req.style = float(im.settings.style);
  req.rgb_len = uint32_t(im.rgb_in.size());

  if (!write_all(im.sock_fd, &req, sizeof(req)) ||
      !write_all(im.sock_fd, im.rgb_in.data(), im.rgb_in.size()))
  {
    STRNCPY(im.status_, "DLSS5: send failed");
    ::close(im.sock_fd);
    im.sock_fd = -1;
    return false;
  }

  ResponseHeader resp{};
  if (!read_all(im.sock_fd, &resp, sizeof(resp))) {
    STRNCPY(im.status_, "DLSS5: recv header failed");
    ::close(im.sock_fd);
    im.sock_fd = -1;
    return false;
  }
  if (resp.magic != k_magic || resp.status != 0) {
    STRNCPY(im.status_, "DLSS5: server returned error");
    return false;
  }
  im.rgb_in.resize(resp.rgb_len);
  if (!read_all(im.sock_fd, im.rgb_in.data(), resp.rgb_len)) {
    STRNCPY(im.status_, "DLSS5: recv payload failed");
    ::close(im.sock_fd);
    im.sock_fd = -1;
    return false;
  }

  const int out_w = int(resp.width);
  const int out_h = int(resp.height);
  im.rgba_out.resize(size_t(out_w) * size_t(out_h) * 4);
  const size_t out_pixels = size_t(out_w) * size_t(out_h);
  for (size_t i = 0; i < out_pixels; i++) {
    for (int c = 0; c < 3; c++) {
      im.rgba_out[i * 4 + size_t(c)] = float(im.rgb_in[i * 3 + size_t(c)]) / 255.0f;
    }
    im.rgba_out[i * 4 + 3] = 1.0f;
  }

  GPU_texture_update(im.output_tx, GPU_DATA_FLOAT, im.rgba_out.data());
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
  STRNCPY(im.status_, "DLSS5: NR active");
  return true;
}

bool Dlss5D3D12Session::wait_for_output()
{
  return impl_ != nullptr && impl_->output_tx != nullptr;
}

gpu::Texture *Dlss5D3D12Session::color_texture() const
{
  return impl_ != nullptr ? impl_->color_tx : nullptr;
}

gpu::Texture *Dlss5D3D12Session::depth_texture() const
{
  return impl_ != nullptr ? impl_->depth_tx : nullptr;
}

gpu::Texture *Dlss5D3D12Session::velocity_texture() const
{
  return impl_ != nullptr ? impl_->velocity_tx : nullptr;
}

gpu::Texture *Dlss5D3D12Session::output_texture() const
{
  return impl_ != nullptr ? impl_->output_tx : nullptr;
}

double Dlss5D3D12Session::gpu_time_ms() const
{
  return impl_ != nullptr ? impl_->gpu_time_ms_ : -1.0;
}

bool Dlss5D3D12Session::available() const
{
  return impl_ != nullptr && impl_->sock_fd >= 0;
}

const char *Dlss5D3D12Session::status() const
{
  return impl_ != nullptr ? impl_->status_ : "Dlss5D3D12Session uninitialized";
}

}  // namespace blender::eevee

#endif /* !_WIN32 */
