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

/** \file KX_DebugTrace.cpp
 *  \ingroup gamelogic
 */

#include "KX_DebugTrace.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

#include "KX_ExecutionPlan.h"

namespace {

const char *kDefaultLogPath = "/tmp/p4_debug_trace.log";
constexpr auto kSnapshotInterval = std::chrono::milliseconds(100);

bool g_preference_enabled = false;
bool g_environment_checked = false;
bool g_environment_enabled = false;

std::string g_log_path = kDefaultLogPath;
std::string g_open_path;
std::FILE *g_log_file = nullptr;

uint64_t g_frame = 0;
std::vector<uint32_t> g_activated;
std::vector<uint8_t> g_seen;

std::string g_snapshot_path;
std::vector<uint32_t> g_snapshot_counts;
std::chrono::steady_clock::time_point g_last_snapshot;

/* Defined next to the snapshot writer below; declared here because the JSON
 * snapshot helper that precedes it also builds node names. */
std::string NodeLabel(const KX_ExecutionPlan &plan, uint32_t node_index);

std::string JsonEscape(const std::string &text)
{
  std::string result;
  result.reserve(text.size() + 2);
  for (const char character : text) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  return result;
}

void WriteSnapshot(const KX_ExecutionPlan &plan)
{
  if (g_snapshot_path.empty()) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (g_last_snapshot.time_since_epoch().count() != 0 &&
      now - g_last_snapshot < kSnapshotInterval)
  {
    return;
  }
  g_last_snapshot = now;

  std::FILE *file = std::fopen(g_snapshot_path.c_str(), "w");
  if (file == nullptr) {
    return;
  }
  std::string json = "{\"frame\": " + std::to_string(g_frame) + ", \"nodes\": [";
  bool first = true;
  for (size_t index = 0; index < g_snapshot_counts.size(); ++index) {
    const uint32_t activations = g_snapshot_counts[index];
    if (activations == 0) {
      continue;
    }
    if (!first) {
      json += ", ";
    }
    first = false;
    json += "{\"name\": \"" + JsonEscape(NodeLabel(plan, static_cast<uint32_t>(index))) +
            "\", \"frame\": " + std::to_string(g_frame) +
            ", \"activations\": " + std::to_string(activations) + "}";
  }
  json += "]}";
  std::fprintf(file, "%s\n", json.c_str());
  std::fclose(file);
  g_snapshot_counts.assign(g_snapshot_counts.size(), 0);
}

bool ReadEnvironmentFlag()
{
  if (!g_environment_checked) {
    g_environment_checked = true;
    const char *value = std::getenv("BGE_GAMENODE_DEBUG");
    g_environment_enabled =
        value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }
  return g_environment_enabled;
}

std::string NodeLabel(const KX_ExecutionPlan &plan, uint32_t node_index)
{
  if (node_index < plan.debug_names.size() && !plan.debug_names[node_index].empty()) {
    return plan.debug_names[node_index];
  }
  return std::string("#") + std::to_string(node_index);
}

}  // namespace

void KX_DebugTrace::SetEnabled(bool enabled)
{
  g_preference_enabled = enabled;
}

bool KX_DebugTrace::Enabled()
{
  return g_preference_enabled || ReadEnvironmentFlag();
}

void KX_DebugTrace::SetLogPath(const std::string &path)
{
  g_log_path = path.empty() ? std::string(kDefaultLogPath) : path;
}

std::string KX_DebugTrace::LogPath()
{
  return g_log_path;
}

void KX_DebugTrace::SetSnapshotPath(const std::string &path)
{
  if (path == g_snapshot_path) {
    return;
  }
  g_snapshot_path = path;
  g_snapshot_counts.clear();
  g_last_snapshot = std::chrono::steady_clock::time_point();
}

std::string KX_DebugTrace::SnapshotPath()
{
  return g_snapshot_path;
}

std::FILE *KX_DebugTrace::LogFile()
{
  if (g_log_file != nullptr && g_open_path == g_log_path) {
    return g_log_file;
  }
  if (g_log_file != nullptr) {
    std::fclose(g_log_file);
    g_log_file = nullptr;
  }
  g_log_file = std::fopen(g_log_path.c_str(), "a");
  g_open_path = g_log_path;
  return g_log_file;
}

void KX_DebugTrace::BeginFrame(uint64_t frame, size_t node_count)
{
  if (!Enabled()) {
    return;
  }
  g_frame = frame;
  g_activated.clear();
  g_seen.assign(node_count, 0);
}

void KX_DebugTrace::RecordActivated(uint32_t node_index)
{
  if (!Enabled()) {
    return;
  }
  if (node_index >= g_seen.size()) {
    g_seen.resize(node_index + 1, 0);
  }
  if (node_index >= g_snapshot_counts.size()) {
    g_snapshot_counts.resize(node_index + 1, 0);
  }
  g_snapshot_counts[node_index] += 1;
  if (g_seen[node_index] != 0) {
    return;
  }
  g_seen[node_index] = 1;
  g_activated.push_back(node_index);
}

void KX_DebugTrace::EndFrame(const KX_ExecutionPlan &plan)
{
  if (!Enabled()) {
    return;
  }

  std::string line = "frame " + std::to_string(g_frame) + " activated: [";
  for (size_t index = 0; index < g_activated.size(); ++index) {
    if (index != 0) {
      line += ", ";
    }
    line += NodeLabel(plan, g_activated[index]);
  }
  line += "]";

  if (std::FILE *file = LogFile()) {
    std::fprintf(file, "%s\n", line.c_str());
    std::fflush(file);
  }
  std::fprintf(stderr, "%s\n", line.c_str());

  WriteSnapshot(plan);

  g_activated.clear();
  g_seen.clear();
}
