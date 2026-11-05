/* ***** BEGIN GPL LICENSE BLOCK *****
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

/** \file KX_FunctionNodesB6Net.cpp
 *  \ingroup gamelogic
 *
 *  B6.3 LAN network nodes: room hosting/joining, message send, event polling
 *  and property synchronisation. OnNetworkEvent is a trigger root that polls
 *  the adapter from Tick and expands one pending trigger per event, so it never
 *  blocks the frame.
 */

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}
#define B6_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
#define B6_COUNTS(array) static_cast<uint32_t>(B6_ARRAY_LEN(array))
#define B6_ACTION(id, ins, outs, exec) \
  { id, ins, B6_COUNTS(ins), outs, B6_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, false, false, false }
#define B6_TICK_SEQUENCE(id, ins, outs, tick, pending, element, finish, state_type) \
  { id, ins, B6_COUNTS(ins), outs, B6_COUNTS(outs), nullptr, nullptr, tick, \
    static_cast<uint32_t>(sizeof(state_type)), static_cast<uint32_t>(alignof(state_type)), \
    false, true, false, pending, element, finish }
#define B6_PORTFLEX(id, ins, outs, exec) \
  { id, ins, B6_COUNTS(ins), outs, B6_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, false, false, false, nullptr, nullptr, nullptr, true }
/* Zero-input event source: the port contract scanner reads arg1 as the input
 * array and arg2 as the output array, so both stay positional here. */
#define B6_EVENT_SOURCE(id, ins, outs, tick, pending, element, finish, state_type) \
  { id, ins, 0, outs, B6_COUNTS(outs), nullptr, nullptr, tick, \
    static_cast<uint32_t>(sizeof(state_type)), static_cast<uint32_t>(alignof(state_type)), \
    false, true, false, pending, element, finish }

namespace {

std::string B6NetInString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

/* ------------------------------------------------------------------------- */
/* Room management                                                            */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kHostRoomInputs[] = {
    TP("OnEnter"),
    FP("Port", KX_VALUE_INT),
    FP("MaxPlayers", KX_VALUE_INT),
};
const KX_FuncPort kHostRoomOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncHostRoom(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  /* The room name is what ListRooms/RefreshRooms advertise; an empty config
   * keeps the adapter's default ("room"). */
  const std::string room_name = node.ConfigString("room_name");
  if (!room_name.empty()) {
    ctx.Network().SetRoomName(room_name);
  }
  std::string error;
  const bool ok = ctx.Network().HostRoom(node.InInt(ctx, 1), node.InInt(ctx, 2), &error);
  if (!ok) {
    std::fprintf(stderr, "GameNode: HostRoom failed: %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, ok);
  node.OutTrigger(ctx, 1, !ok);
}

const KX_FuncPort kJoinRoomInputs[] = {
    TP("OnEnter"),
    FP("Host", KX_VALUE_STRING),
    FP("Port", KX_VALUE_INT),
};
const KX_FuncPort kJoinRoomOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
    FP("LocalPlayerId", KX_VALUE_INT),
};

void FuncJoinRoom(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string host = B6NetInString(node, ctx, 1);
  std::string error;
  const bool ok = ctx.Network().JoinRoom(host, node.InInt(ctx, 2), &error);
  node.OutInt(ctx, 2, ok ? ctx.Network().LocalPlayerId() : -1);
  if (!ok) {
    std::fprintf(stderr, "GameNode: JoinRoom(%s) failed: %s\n", host.c_str(), error.c_str());
  }
  node.OutTrigger(ctx, 0, ok);
  node.OutTrigger(ctx, 1, !ok);
}

const KX_FuncPort kLeaveRoomInputs[] = {
    TP("OnEnter"),
};
const KX_FuncPort kLeaveRoomOutputs[] = {
    TP("OnDone"),
};

void FuncLeaveRoom(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Network().LeaveRoom();
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* OnNetworkEvent                                                             */
/* ------------------------------------------------------------------------- */

struct B6NetworkEventState {
  std::vector<KX_NetworkEvent> *events = nullptr;
};

const KX_FuncPort kOnNetworkEventOutputs[] = {
    TP("OnPlayerJoin"),
    TP("OnPlayerLeave"),
    TP("OnMessage"),
    TP("OnDisconnect"),
    FP("PlayerId", KX_VALUE_INT),
    FP("Data", KX_VALUE_STRING),
};

int B6NetworkEventTriggerFor(KX_NetworkEvent::Type type)
{
  switch (type) {
    case KX_NetworkEvent::PlayerJoin:
      return 0;
    case KX_NetworkEvent::PlayerLeave:
      return 1;
    case KX_NetworkEvent::Message:
      return 2;
    case KX_NetworkEvent::Disconnect:
      return 3;
    default:
      return -1;
  }
}

bool B6NetworkFilterMatches(int filter, KX_NetworkEvent::Type type)
{
  /* Editor enum: 0 any, 1 join, 2 leave, 3 message, 4 disconnect. */
  switch (filter) {
    case 1:
      return type == KX_NetworkEvent::PlayerJoin;
    case 2:
      return type == KX_NetworkEvent::PlayerLeave;
    case 3:
      return type == KX_NetworkEvent::Message;
    case 4:
      return type == KX_NetworkEvent::Disconnect;
    case 0:
    default:
      return true;
  }
}

void FuncOnNetworkEventTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  B6NetworkEventState *state = node.StateBlock<B6NetworkEventState>(ctx);
  if (state->events != nullptr) {
    delete state->events;
    state->events = nullptr;
  }
  state->events = new std::vector<KX_NetworkEvent>();

  const int filter = node.ConfigInt("event_type", 0);
  KX_NetworkEvent event;
  /* Bounded drain: a runaway peer cannot monopolise one frame. */
  for (int guard = 0; guard < 64 && ctx.Network().PollEvent(event); ++guard) {
    if (B6NetworkFilterMatches(filter, event.type)) {
      state->events->push_back(event);
    }
  }
}

uint32_t FuncOnNetworkEventPending(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6NetworkEventState *state = node.StateBlock<B6NetworkEventState>(ctx);
  return state->events != nullptr ? static_cast<uint32_t>(state->events->size()) : 0;
}

void FuncOnNetworkEventElement(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  B6NetworkEventState *state = node.StateBlock<B6NetworkEventState>(ctx);
  if (state->events == nullptr || index >= state->events->size()) {
    return;
  }
  const KX_NetworkEvent &event = (*state->events)[index];
  node.OutInt(ctx, 4, event.player_id);
  node.OutString(ctx, 5, event.data);
  for (int trigger = 0; trigger < 4; ++trigger) {
    node.OutTrigger(ctx, uint32_t(trigger), false);
  }
  const int trigger = B6NetworkEventTriggerFor(event.type);
  if (trigger >= 0) {
    node.OutTrigger(ctx, uint32_t(trigger), true);
  }
}

void FuncOnNetworkEventFinish(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6NetworkEventState *state = node.StateBlock<B6NetworkEventState>(ctx);
  if (state->events != nullptr) {
    delete state->events;
    state->events = nullptr;
  }
}

/* ------------------------------------------------------------------------- */
/* Sending                                                                    */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kSendNetworkMessageInputs[] = {
    TP("OnEnter"),
    FP("TargetId", KX_VALUE_INT),
    FP("Data", KX_VALUE_STRING),
};
const KX_FuncPort kSendNetworkMessageOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncSendNetworkMessage(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string data = B6NetInString(node, ctx, 2);
  const int target = node.InInt(ctx, 1);
  const bool reliable = node.ConfigBool("reliable", true);
  const bool ok = reliable ? ctx.Network().SendReliable(target, data.data(), data.size())
                           : ctx.Network().SendUnreliable(target, data.data(), data.size());
  if (!ok) {
    std::fprintf(stderr,
                 "GameNode: SendNetworkMessage to %d failed: %s\n",
                 target,
                 ctx.Network().LastError().c_str());
  }
  node.OutTrigger(ctx, 0, ok);
  node.OutTrigger(ctx, 1, !ok);
}

const KX_FuncPort kBroadcastMessageInputs[] = {
    TP("OnEnter"),
    FP("Data", KX_VALUE_STRING),
};
const KX_FuncPort kBroadcastMessageOutputs[] = {
    TP("OnSuccess"),
};

void FuncBroadcastMessage(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string data = B6NetInString(node, ctx, 1);
  const bool ok = ctx.Network().BroadcastReliable(data.data(), data.size());
  node.OutTrigger(ctx, 0, ok);
}

/* ------------------------------------------------------------------------- */
/* Property synchronisation                                                   */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kSyncPropertyInputs[] = {
    TP("OnEnter"),
    FP("Key", KX_VALUE_STRING),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kSyncPropertyOutputs[] = {
    TP("OnDone"),
};

void FuncSyncProperty(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string key = B6NetInString(node, ctx, 1);
  if (key.empty()) {
    std::fprintf(stderr, "GameNode: SyncProperty requires a non-empty key\n");
    node.OutTrigger(ctx, 0, true);
    return;
  }
  ctx.Network().SetSyncProperty(key, node.In(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kGetRemotePropertyInputs[] = {
    TP("OnEnter"),
    FP("PlayerId", KX_VALUE_INT),
    FP("Key", KX_VALUE_STRING),
};
const KX_FuncPort kGetRemotePropertyOutputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncGetRemoteProperty(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const int player_id = node.InInt(ctx, 1);
  const std::string key = B6NetInString(node, ctx, 2);
  KX_RuntimeValue value;
  const bool ok = ctx.Network().GetRemoteProperty(player_id, key, &value);
  if (ok) {
    node.Out(ctx, 0, value);
  }
  else {
    node.OutFloat(ctx, 0, 0.0f);
  }
  node.OutTrigger(ctx, 1, ok);
  node.OutTrigger(ctx, 2, !ok);
}

/* ------------------------------------------------------------------------- */
/* Room table and reconnect (B6.7 C.3.2)                                      */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kListRoomsInputs[] = {
    TP("OnEnter"),
    FP("Host", KX_VALUE_STRING),
    FP("Port", KX_VALUE_INT),
};
const KX_FuncPort kListRoomsOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
    FP("Rooms", KX_VALUE_LIST),
    FP("Count", KX_VALUE_INT),
    FP("Error", KX_VALUE_STRING),
};

void B6NetWriteRooms(KX_FunctionNode &node,
                     KX_ExecutionContext &ctx,
                     uint32_t list_port,
                     uint32_t count_port,
                     const std::vector<KX_RoomInfo> &rooms)
{
  KX_ValueList &list = ctx.Pool().AllocList(rooms.size());
  for (const KX_RoomInfo &room : rooms) {
    list.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(room.name)));
  }
  node.Out(ctx, list_port, KX_RuntimeValue::List(&list));
  node.OutInt(ctx, count_port, int(rooms.size()));
}

void B6NetQueryRooms(KX_FunctionNode &node, KX_ExecutionContext &ctx, bool refresh)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::vector<KX_RoomInfo> rooms;
  std::string error;
  const std::string host = B6NetInString(node, ctx, 1);
  const int port = node.InInt(ctx, 2);
  const bool ok = refresh ? ctx.Network().RefreshRooms(host, port, rooms, &error)
                          : ctx.Network().ListRooms(host, port, rooms, &error);
  if (!ok) {
    std::fprintf(stderr, "GameNode: %s(%s:%d) failed: %s\n",
                 refresh ? "RefreshRooms" : "ListRooms",
                 host.c_str(),
                 port,
                 error.c_str());
  }
  B6NetWriteRooms(node, ctx, 2, 3, rooms);
  node.OutString(ctx, 4, ok ? std::string() : error);
  node.OutTrigger(ctx, 0, ok);
  node.OutTrigger(ctx, 1, !ok);
}

void FuncListRooms(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6NetQueryRooms(node, ctx, false);
}

void FuncRefreshRooms(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6NetQueryRooms(node, ctx, true);
}

const KX_FuncPort kReconnectInputs[] = {
    TP("OnEnter"),
};
const KX_FuncPort kReconnectOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
    FP("Attempts", KX_VALUE_INT),
    FP("Status", KX_VALUE_STRING),
};

void FuncReconnect(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const int max_attempts = node.ConfigInt("max_attempts", 3);
  const int backoff_ms = node.ConfigInt("backoff_ms", 250);
  int attempts = 0;
  std::string error;
  const bool ok =
      ctx.Network().Reconnect(max_attempts, backoff_ms, &attempts, &error);
  std::string status = ok ? std::string("connected") : error;
  if (!ok) {
    std::fprintf(stderr, "GameNode: Reconnect failed after %d attempts: %s\n",
                 attempts,
                 status.c_str());
  }
  node.OutInt(ctx, 2, attempts);
  node.OutString(ctx, 3, status);
  node.OutTrigger(ctx, 0, ok);
  node.OutTrigger(ctx, 1, !ok);
}

const KX_FunctionSpec kB6NetSpecs[] = {
    B6_ACTION("HostRoom", kHostRoomInputs, kHostRoomOutputs, FuncHostRoom),
    B6_ACTION("JoinRoom", kJoinRoomInputs, kJoinRoomOutputs, FuncJoinRoom),
    B6_ACTION("LeaveRoom", kLeaveRoomInputs, kLeaveRoomOutputs, FuncLeaveRoom),
    B6_EVENT_SOURCE("OnNetworkEvent",
                    nullptr,
                    kOnNetworkEventOutputs,
                    FuncOnNetworkEventTick,
                    FuncOnNetworkEventPending,
                    FuncOnNetworkEventElement,
                    FuncOnNetworkEventFinish,
                    B6NetworkEventState),
    B6_ACTION("SendNetworkMessage",
              kSendNetworkMessageInputs,
              kSendNetworkMessageOutputs,
              FuncSendNetworkMessage),
    B6_ACTION("BroadcastMessage", kBroadcastMessageInputs, kBroadcastMessageOutputs, FuncBroadcastMessage),
    B6_PORTFLEX("SyncProperty", kSyncPropertyInputs, kSyncPropertyOutputs, FuncSyncProperty),
    B6_PORTFLEX("GetRemoteProperty",
                kGetRemotePropertyInputs,
                kGetRemotePropertyOutputs,
                FuncGetRemoteProperty),
    /* B6.7 C.3.2: no discovery protocol, the caller types the host address. */
    B6_ACTION("ListRooms", kListRoomsInputs, kListRoomsOutputs, FuncListRooms),
    B6_ACTION("RefreshRooms", kListRoomsInputs, kListRoomsOutputs, FuncRefreshRooms),
    B6_ACTION("Reconnect", kReconnectInputs, kReconnectOutputs, FuncReconnect),
};

KX_NodeSignature MakeB6NetSignature(const KX_FunctionSpec &spec)
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

void RegisterB6NetworkFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6NetSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6Net_") + spec.type_id,
        MakeB6NetSignature(*entry),
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
