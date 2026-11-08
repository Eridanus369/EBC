#!/bin/bash
# B6.9-Ms Ms-1 two-process RPC E2E runner.
#
# Builds tests/b69_rpc_two_proc.cpp against the shared GameNodes TU list and
# launches TWO independent processes. The client runs the real SendRPC node
# (plus two SendNetworkMessage nodes) over real non-blocking sockets; the host
# runs the real OnRPC node followed by OnNetworkEvent. The client taps
# BroadcastReliable so the EBCRPC1 envelope layout and byte lengths are
# asserted on the wire, while the host asserts the cross-process restored
# subject/payload/sender and that plain (non-RPC) events survive OnRPC's
# reverse requeue.
#
# Evidence log: /tmp/b69_rpc_two_proc_<YYYYmmdd-HHMMSS>.log, created
# exclusively (an existing file is refused), then chmod 444.

set -u

ROOT=/home/eridanusepsilon/Code/EBC/EBC5.3
source "$ROOT/tests/harness_config.sh"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="${B69_RPC_TWO_PROC_DIR:-/tmp/b69_rpc_two_proc_$RUN_ID}"
LOG="${B69_RPC_TWO_PROC_LOG:-/tmp/b69_rpc_two_proc_$RUN_ID.log}"
PORT="${B69_RPC_TWO_PROC_PORT:-46901}"

if ! ( set -C; : > "$LOG" ) 2>/dev/null; then
  echo "b69_rpc_two_proc: REFUSE existing log: $LOG" >&2
  exit 1
fi
if [ -e "$RUN_DIR" ]; then
  echo "b69_rpc_two_proc: REFUSE existing run dir: $RUN_DIR" >&2
  exit 1
fi
mkdir -p "$RUN_DIR"

SRC="$ROOT/tests/b69_rpc_two_proc.cpp"
BIN="$RUN_DIR/b69_rpc_two_proc"

{
  echo "# B6.9-Ms Ms-1 two-process RPC E2E"
  echo "# date: $(date -Is)"
  echo "# port: $PORT"
  echo "# loadavg at start: $(cat /proc/loadavg)"
} >> "$LOG"

echo "== build ==" | tee -a "$LOG"
HARNESS_HARNESS_SRCS=("$SRC")
if ! harness_compile_tus || ! harness_build "$SRC" "$BIN" ||
   ! harness_check_required_symbols "$BIN"; then
  echo "FAIL: could not build $SRC" | tee -a "$LOG"
  echo "RESULT assertions=1 failures=1" | tee -a "$LOG"
  chmod 444 "$LOG"
  printf 'LOG %s\n' "$LOG"
  exit 1
fi
echo "BUILD_OK $BIN" | tee -a "$LOG"

echo "== run (two processes) ==" | tee -a "$LOG"
"$BIN" host "$PORT" "$RUN_DIR" > "$RUN_DIR/host.log" 2>&1 &
HOST_PID=$!
"$BIN" client "$PORT" "$RUN_DIR" > "$RUN_DIR/client.log" 2>&1 &
CLIENT_PID=$!

wait "$HOST_PID"; HOST_RC=$?
wait "$CLIENT_PID"; CLIENT_RC=$?

{
  echo "HOST_PID=$HOST_PID HOST_RC=$HOST_RC"
  echo "CLIENT_PID=$CLIENT_PID CLIENT_RC=$CLIENT_RC"
  echo "--- host.log ---"
  cat "$RUN_DIR/host.log"
  echo "--- client.log ---"
  cat "$RUN_DIR/client.log"
} >> "$LOG"

echo "HOST_PID=$HOST_PID HOST_RC=$HOST_RC" | tee -a "$LOG"
echo "CLIENT_PID=$CLIENT_PID CLIENT_RC=$CLIENT_RC" | tee -a "$LOG"
grep -E '^(PASS|FAIL|INFO|RESULT)' "$RUN_DIR/host.log" | tee -a "$LOG"
grep -E '^(PASS|FAIL|INFO|RESULT)' "$RUN_DIR/client.log" | tee -a "$LOG"

host_assertions="$(grep -oE 'assertions=[0-9]+' "$RUN_DIR/host.log" | tail -n 1 | cut -d= -f2)"
host_failures="$(grep -oE 'failures=[0-9]+' "$RUN_DIR/host.log" | tail -n 1 | cut -d= -f2)"
client_assertions="$(grep -oE 'assertions=[0-9]+' "$RUN_DIR/client.log" | tail -n 1 | cut -d= -f2)"
client_failures="$(grep -oE 'failures=[0-9]+' "$RUN_DIR/client.log" | tail -n 1 | cut -d= -f2)"
host_assertions="${host_assertions:-0}"
client_assertions="${client_assertions:-0}"
host_failures="${host_failures:-1}"
client_failures="${client_failures:-1}"

verdict="PASS"
if [ "$HOST_RC" -ne 0 ] || [ "$CLIENT_RC" -ne 0 ] ||
   [ "$host_failures" -ne 0 ] || [ "$client_failures" -ne 0 ]; then
  verdict="FAIL"
fi

# +3 shell-level assertions: build succeeded, both processes exited 0, verdict.
shell_failures=0
[ "$verdict" = "PASS" ] || shell_failures=1
assertions=$((host_assertions + client_assertions + 3))
failures=$((host_failures + client_failures + shell_failures))

echo "RESULT two_process_pids=$HOST_PID,$CLIENT_PID host_rc=$HOST_RC client_rc=$CLIENT_RC verdict=$verdict" | tee -a "$LOG"
echo "RESULT assertions=$assertions failures=$failures" | tee -a "$LOG"
echo "# loadavg at end: $(cat /proc/loadavg)" >> "$LOG"

chmod 444 "$LOG"
printf 'LOG %s bytes=%s sha256=%s\n' "$LOG" "$(stat -c %s "$LOG")" "$(sha256sum "$LOG" | cut -d' ' -f1)"

[ "$verdict" = "PASS" ]
