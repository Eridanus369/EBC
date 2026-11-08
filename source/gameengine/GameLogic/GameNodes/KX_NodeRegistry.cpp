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

/** \file KX_NodeRegistry.cpp
 *  \ingroup gamelogic
 */

#include "KX_NodeRegistry.h"

#include <cassert>

KX_NodeRegistry &KX_NodeRegistry::Get()
{
  static KX_NodeRegistry instance;
  return instance;
}

KX_NodeTypeId KX_NodeRegistry::RegisterType(const std::string &editor_type_id,
                                            const std::string &runtime_type_id,
                                            KX_NodeSignature signature,
                                            Factory factory)
{
  /* Registration time sanity check of the port order contract. The compiler
   * still compares every IR port against this signature. */
  assert(signature.inputs.size() <= KX_MAX_NODE_INPUTS);
  assert(signature.outputs.size() <= KX_MAX_NODE_OUTPUTS);
  for (const KX_NodePortSignature &port : signature.inputs) {
    (void)port;
    assert(port.kind == IRPortKind::Trigger
               ? port.value_type == KX_VALUE_NONE
               : (port.dynamic_type || port.value_type != KX_VALUE_NONE));
  }
  for (const KX_NodePortSignature &port : signature.outputs) {
    (void)port;
    assert(port.kind == IRPortKind::Trigger
               ? port.value_type == KX_VALUE_NONE
               : (port.dynamic_type || port.value_type != KX_VALUE_NONE));
  }

  const auto editor_it = editor_type_ids_.find(editor_type_id);
  if (editor_it != editor_type_ids_.end()) {
    return editor_it->second;
  }
  const auto runtime_it = runtime_type_ids_.find(runtime_type_id);
  if (runtime_it != runtime_type_ids_.end()) {
    return runtime_it->second;
  }

  entries_.push_back({editor_type_id, runtime_type_id, std::move(signature), std::move(factory)});
  const KX_NodeTypeId type_id = KX_NodeTypeId(entries_.size());
  editor_type_ids_[editor_type_id] = type_id;
  runtime_type_ids_[runtime_type_id] = type_id;
  return type_id;
}

KX_NodeTypeId KX_NodeRegistry::FindByEditorType(const std::string &editor_type_id) const
{
  const auto it = editor_type_ids_.find(editor_type_id);
  return it == editor_type_ids_.end() ? KX_INVALID_NODE_TYPE_ID : it->second;
}

KX_NodeTypeId KX_NodeRegistry::FindByRuntimeType(const std::string &runtime_type_id) const
{
  const auto it = runtime_type_ids_.find(runtime_type_id);
  return it == runtime_type_ids_.end() ? KX_INVALID_NODE_TYPE_ID : it->second;
}

const char *KX_NodeRegistry::EditorTypeId(KX_NodeTypeId type_id) const
{
  if (type_id == KX_INVALID_NODE_TYPE_ID || type_id > entries_.size()) {
    return nullptr;
  }
  return entries_[type_id - 1].editor_type_id.c_str();
}

const char *KX_NodeRegistry::RuntimeTypeId(KX_NodeTypeId type_id) const
{
  if (type_id == KX_INVALID_NODE_TYPE_ID || type_id > entries_.size()) {
    return nullptr;
  }
  return entries_[type_id - 1].runtime_type_id.c_str();
}

const KX_NodeSignature *KX_NodeRegistry::GetSignature(KX_NodeTypeId type_id) const
{
  if (type_id == KX_INVALID_NODE_TYPE_ID || type_id > entries_.size()) {
    return nullptr;
  }
  return &entries_[type_id - 1].signature;
}

std::unique_ptr<KX_GameNodeRuntime> KX_NodeRegistry::Create(KX_NodeTypeId type_id) const
{
  if (type_id == KX_INVALID_NODE_TYPE_ID || type_id > entries_.size()) {
    return nullptr;
  }
  const Entry &entry = entries_[type_id - 1];
  if (!entry.factory) {
    return nullptr;
  }
  std::unique_ptr<KX_GameNodeRuntime> node = entry.factory();
  if (node) {
    node->SetTypeId(type_id);
  }
  return node;
}

std::unique_ptr<KX_GameNodeRuntime> KX_NodeRegistry::Create(const std::string &type_id) const
{
  KX_NodeTypeId resolved = FindByEditorType(type_id);
  if (resolved == KX_INVALID_NODE_TYPE_ID) {
    resolved = FindByRuntimeType(type_id);
  }
  return Create(resolved);
}

size_t KX_NodeRegistry::TypeCount() const
{
  return entries_.size();
}

void KX_NodeRegistry::Clear()
{
  entries_.clear();
  editor_type_ids_.clear();
  runtime_type_ids_.clear();
}
