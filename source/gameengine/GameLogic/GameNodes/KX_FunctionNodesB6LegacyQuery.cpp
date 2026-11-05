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

/** \file KX_FunctionNodesB6LegacyQuery.cpp
 *  \ingroup gamelogic
 *
 *  B6.6 legacy screen query nodes.
 *
 *  WorldPosition (screen pixel + depth -> world point) and Touch (screen pixel
 *  -> first hit + overlapping objects) are screen space queries that the legacy
 *  editor exposes. They are not physics contact sensors: WorldPosition never
 *  touches the physics world and Touch starts from a single ray hit instead of
 *  the contact stream. Both therefore live on KX_SceneAdapter and stay separate
 *  from the Collision node, whose port contract is unchanged.
 */

#include "KX_FunctionNode.h"

#include <cstdint>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_NodeRegistry.h"
#include "KX_ValuePool.h"

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}

namespace {

/* ------------------------------------------------------------------------- */
/* WorldPositionLegacy                                                        */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kWorldPositionLegacyInputs[] = {
    TP("OnEnter"),
    FP("ScreenX", KX_VALUE_FLOAT),
    FP("ScreenY", KX_VALUE_FLOAT),
    FP("Depth", KX_VALUE_FLOAT),
};
const KX_FuncPort kWorldPositionLegacyOutputs[] = {
    TP("OnDone"),
    FP("WorldPos", KX_VALUE_VECTOR),
};

void FuncWorldPositionLegacy(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const float screen_x = node.InFloat(ctx, 1);
  const float screen_y = node.InFloat(ctx, 2);
  const float depth = node.InFloat(ctx, 3);

  float position[3] = {0.0f, 0.0f, 0.0f};
  /* A failed query still completes the action: the legacy node has no failure
   * socket, so the graph must not stall. The vector degrades to the origin. */
  ctx.SceneQuery().RayCastFromScreenAnyDepth(screen_x, screen_y, depth, position);
  node.OutVector(ctx, 1, position[0], position[1], position[2]);
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* TouchLegacy                                                                */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kTouchLegacyInputs[] = {
    TP("OnEnter"),
    FP("ScreenX", KX_VALUE_FLOAT),
    FP("ScreenY", KX_VALUE_FLOAT),
};
const KX_FuncPort kTouchLegacyOutputs[] = {
    TP("OnDone"),
    FP("Target", KX_VALUE_OBJECT),
    FP("Objects", KX_VALUE_LIST),
    FP("Point", KX_VALUE_VECTOR),
    FP("Normal", KX_VALUE_VECTOR),
};

struct TouchOverlapCollector {
  std::vector<KX_GameObject *> objects;
  float point[3] = {0.0f, 0.0f, 0.0f};
  float normal[3] = {0.0f, 0.0f, 0.0f};
};

void CollectTouchOverlap(const KX_OverlapHit &hit, void *user)
{
  TouchOverlapCollector *collector = static_cast<TouchOverlapCollector *>(user);
  if (collector == nullptr || hit.object == nullptr) {
    return;
  }
  /* The scene adapter reports the ray hit object first, so the first callback
   * seeds Point/Normal with the exact surface contact. */
  if (collector->objects.empty()) {
    collector->point[0] = hit.point[0];
    collector->point[1] = hit.point[1];
    collector->point[2] = hit.point[2];
    collector->normal[0] = hit.normal[0];
    collector->normal[1] = hit.normal[1];
    collector->normal[2] = hit.normal[2];
  }
  collector->objects.push_back(hit.object);
}

void FuncTouchLegacy(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const float screen_x = node.InFloat(ctx, 1);
  const float screen_y = node.InFloat(ctx, 2);
  const float threshold = node.ConfigFloat("overlap_threshold", 0.1f);

  /* RayCastFromScreen takes normalized 0..1 coordinates with the origin at the
   * bottom left; the node exposes pixels like the legacy editor. */
  float width = ctx.SceneQuery().ScreenWidth();
  float height = ctx.SceneQuery().ScreenHeight();
  if (width <= 0.0f) {
    width = 1920.0f;
  }
  if (height <= 0.0f) {
    height = 1080.0f;
  }
  const float nx = screen_x / width;
  const float ny = screen_y / height;

  KX_RayHit ray_hit;
  const bool hit = ctx.SceneQuery().RayCastFromScreen(nx, ny, nullptr, ray_hit);

  TouchOverlapCollector collector;
  if (hit) {
    ctx.SceneQuery().OverlapQuery(ray_hit, threshold, CollectTouchOverlap, &collector);
  }

  KX_ValueList &list = ctx.Pool().AllocList(collector.objects.size());
  for (KX_GameObject *object : collector.objects) {
    list.push_back(KX_RuntimeValue::Object(object));
  }

  node.OutObject(ctx, 1, hit ? ray_hit.object : nullptr);
  node.Out(ctx, 2, KX_RuntimeValue::List(&list));
  node.OutVector(ctx, 3, collector.point[0], collector.point[1], collector.point[2]);
  node.OutVector(ctx, 4, collector.normal[0], collector.normal[1], collector.normal[2]);
  /* Empty space still completes the trigger: OnDone means "the query ran",
   * not "something was touched". */
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Table                                                                     */
/* ------------------------------------------------------------------------- */

#define PORT_COUNT(array) static_cast<uint32_t>(sizeof(array) / sizeof(array[0]))

#define B6LQ_ACTION(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

const KX_FunctionSpec kB6LegacyQuerySpecs[] = {
    B6LQ_ACTION("WorldPositionLegacy",
                kWorldPositionLegacyInputs,
                kWorldPositionLegacyOutputs,
                FuncWorldPositionLegacy),
    B6LQ_ACTION("TouchLegacy",
                kTouchLegacyInputs,
                kTouchLegacyOutputs,
                FuncTouchLegacy),
};

#undef PORT_COUNT

KX_NodeSignature MakeB6LegacyQuerySignature(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    if (spec.inputs[i].trigger) {
      port.kind = IRPortKind::Trigger;
      port.value_type = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      port.value_type = spec.inputs[i].type;
    }
    signature.inputs.push_back(port);
  }
  for (uint32_t i = 0; i < spec.output_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.outputs[i].name;
    if (spec.outputs[i].trigger) {
      port.kind = IRPortKind::Trigger;
      port.value_type = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      port.value_type = spec.outputs[i].type;
    }
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

void RegisterB6LegacyQueryFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6LegacyQuerySpecs) {
    const KX_FunctionSpec *entry = &spec;
    const KX_NodeSignature signature = MakeB6LegacyQuerySignature(spec);
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6LQ_") + spec.type_id,
        signature,
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
