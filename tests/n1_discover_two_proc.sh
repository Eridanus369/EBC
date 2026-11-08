#!/bin/bash
# B6.9-N N-1 two-process LAN discovery E2E runner.
#
# Builds tests/n1_discover_two_proc.cpp against the shared GameNodes TU list and
# launches TWO independent processes: one room (host), one discovering client.
# The client checks broadcast discovery, endpoint de-duplication, the
# "never answer another room's reply" rule, and room expiry after the host
# stops answering.
#
# Evidence log: /tmp/n1_discover_two_proc_<YYYYmmdd-HHMMSS>.log, created
# exclusively (an existing file is refused), then chmod 444. Existing logs are
# never overwritten.

set -u

ROOT=/home/eridanusepsilon/Code/EBC/EBC5.3
source "$ROOT/tests/harness_config.sh"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="${N1_TWO_PROC_DIR:-/tmp/n1_discover_two_proc_$RUN_ID}"
LOG="${N1_TWO_PROC_LOG:-/tmp/n1_discover_two_proc_$RUN_ID.log}"
PORT="${N1_TWO_PROC_PORT:-46312}"

if ! ( set -C; : > "$LOG" ) 2>/dev/null; then
  echo "n1_discover_two_proc: REFUSE existing log: $LOG" >&2
  exit 1
fi
if [ -e "$RUN_DIR" ]; then
  echo "n1_discover_two_proc: REFUSE existing run dir: $RUN_DIR" >&2
  exit 1
fi
mkdir -p "$RUN_DIR/shared"

SRC="$ROOT/tests/n1_discover_two_proc.cpp"
BIN="$RUN_DIR/n1_discover_two_proc"

{
  echo "# B6.9-N N-1 two-process LAN discovery E2E"
  echo "# date: $(date -Is)"
  echo "# port: $PORT"
  echo "# loadavg at start: $(cat /proc/loadavg)"
} >> "$LOG"

echo "== build ==" | tee -a "$LOG"
HARNESS_HARNESS_SRCS=("$SRC")
if ! harness_compile_tus || ! harness_build "$SRC" "$BIN" ||
   ! harness_check_required_symbols "$BIN"; then
  echo "FAIL: could not build $SRC" | tee -a "$LOG"
  chmod 444 "$LOG"
  printf 'LOG %s\n' "$LOG"
  exit 1
fi
echo "BUILD_OK $BIN" | tee -a "$LOG"

echo "== run (two processes) ==" | tee -a "$LOG"
"$BIN" host "$PORT" "$RUN_DIR/shared" > "$RUN_DIR/host.log" 2>&1 &
HOST_PID=$!
"$BIN" client "$PORT" "$RUN_DIR/shared" > "$RUN_DIR/client.log" 2>&1 &
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

verdict="PASS"
if [ "$HOST_RC" -ne 0 ] || [ "$CLIENT_RC" -ne 0 ]; then
  verdict="FAIL"
fi
echo "RESULT two_process_pids=$HOST_PID,$CLIENT_PID host_rc=$HOST_RC client_rc=$CLIENT_RC verdict=$verdict" | tee -a "$LOG"
echo "# loadavg at end: $(cat /proc/loadavg)" >> "$LOG"

chmod 444 "$LOG"
printf 'LOG %s bytes=%s sha256=%s\n' "$LOG" "$(stat -c %s "$LOG")" "$(sha256sum "$LOG" | cut -d' ' -f1)"

[ "$verdict" = "PASS" ]
