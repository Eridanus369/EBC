#!/usr/bin/env bash
# B6.6 task 1: one click rerun of the four rebuilt B5 bottom-layer harnesses.
#
# Builds the standalone harnesses (test code only; game engine sources are
# linked unmodified), regenerates the P3 scene manifest with the real editor
# exporter, then runs P1, P2, P3 and P3.5. The combined log is written to a
# timestamped /tmp/b661_b5_rerun_<YYYYmmdd-HHMMSS>.log.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TEST_DIR="$ROOT/tests/b5_rerun"
BUILD_DIR="$TEST_DIR/.build"
# Evidence logs are never reused: every run gets a unique timestamped path,
# and an existing file is refused instead of silently truncated.
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG=${B5_RERUN_LOG:-/tmp/b661_b5_rerun_$RUN_ID.log}
BUILD_LOG=/tmp/b661_b5_rerun_build_$RUN_ID.log
MANIFEST="$TEST_DIR/out/p3_manifest.json"

mkdir -p "$(dirname "$LOG")"
if ! ( set -C; : > "$LOG" ) 2>/dev/null; then
  echo "run_b5_rerun: REFUSE existing log: $LOG" >&2
  exit 1
fi
if [ -e "$BUILD_LOG" ]; then
  echo "run_b5_rerun: REFUSE existing build log: $BUILD_LOG" >&2
  exit 1
fi

log() {
  printf '%s\n' "$*" | tee -a "$LOG"
}

failures=0

log "=== B6.6 / task 1: B5 bottom layers rerun ==="
log "project: $ROOT"
log "date:    $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "note:    reconstructed harnesses, acceptance = behaviour not regressed"
log ""

# ---------------------------------------------------------------- build
log "--- build ---"
if make -f "$TEST_DIR/Makefile" -j"$(nproc)" all > "$BUILD_LOG" 2>&1; then
  log "build: OK ($BUILD_LOG)"
else
  log "build: FAILED, see $BUILD_LOG"
  tail -20 "$BUILD_LOG" | tee -a "$LOG"
  exit 1
fi

# ------------------------------------------------------- editor export (P3)
log ""
log "--- editor export (P3 input) ---"
if python3 "$TEST_DIR/p3_export.py" >> "$LOG" 2>&1; then
  log "editor export: OK (mock bpy data, real ir_export.py)"
else
  log "editor export: FAILED (mock bpy data, real ir_export.py)"
  failures=$((failures + 1))
fi

# ------------------------------------------------------------ harness runs
run_case() {
  local label=$1
  local binary=$2
  local extra=${3:-}
  local out="$BUILD_DIR/$binary.$RUN_ID.out"
  local status

  log ""
  log "--- $label ---"
  if [ -n "$extra" ]; then
    "$BUILD_DIR/$binary" "$extra" > "$out" 2>&1
  else
    "$BUILD_DIR/$binary" > "$out" 2>&1
  fi
  status=$?

  cat "$out" | tee -a "$LOG"
  if [ $status -eq 0 ] && grep -q "PASS" "$out"; then
    local counts
    counts=$(grep -o '[0-9]* assertions, [0-9]* failures' "$out" | tail -1)
    log "[$label] 重建 vs 历史一致 (${counts:-assertions unavailable})"
  else
    log "[$label] 重建 vs 历史不一致 (exit=$status)"
    failures=$((failures + 1))
  fi
}

run_case "P1 runtime"       p1_harness
run_case "P2 asset"         p2_harness
run_case "P3 pipeline"      p3_harness "$MANIFEST"
run_case "P3.5 input/physics" p3_input_harness

# ---------------------------------------------------------------- summary
log ""
if [ "$failures" -eq 0 ]; then
  log "SUMMARY: 4/4 PASS"
  log "log: $LOG"
  exit 0
fi

log "SUMMARY: $((4 - failures))/4 PASS, $failures failure(s)"
log "log: $LOG"
exit 1
