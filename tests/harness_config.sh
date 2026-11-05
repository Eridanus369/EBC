#!/usr/bin/env bash
# tests/harness_config.sh
#
# Single source of truth for every standalone E2E / stability / benchmark
# harness in this repo.
#
# The GameNodes translation unit list is DERIVED from build_linux/build.ninja
# (the same list the real build uses), so a newly added TU is picked up
# automatically. Harnesses only declare what they intentionally leave out:
#
#   1. automatic: any TU whose Register*GameNodes() entry point is stubbed by
#      the harness source itself (detected by scanning the harness source);
#   2. explicit: the static table below, for TUs that are excluded for
#      non-stub reasons (each row must state the reason and the owner).
#
# Never hand-write a source path list again: that is exactly what produced two
# runtime "call to address 0" crashes (missing KX_InlinePass.cpp).
#
# Usage:
#   source tests/harness_config.sh
#   tus=( $(harness_resolve_tus "$SRC" "$STUBS") )
#   harness_compile_tus                 # fills $HARNESS_OBJ_DIR
#   harness_build "$SRC" /tmp/foo       # link against the cached objects
#   harness_check_required_symbols /tmp/foo

set -o pipefail

HARNESS_ROOT="${HARNESS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
HARNESS_G="$HARNESS_ROOT/source/gameengine/GameLogic/GameNodes"
# Point this at another build directory (e.g. build_clean_verify) to derive the
# TU manifest and generated includes from that tree instead of build_linux.
HARNESS_NINJA="${HARNESS_NINJA:-$HARNESS_ROOT/build_linux/build.ninja}"
HARNESS_BUILD_DIR="$(dirname "$HARNESS_NINJA")"
HARNESS_CXX="${CXX:-/usr/bin/c++}"
HARNESS_JOBS="${HARNESS_JOBS:-2}"
HARNESS_OBJ_ROOT="${HARNESS_OBJ_ROOT:-$HARNESS_ROOT/tests/.harness_obj}"
# HARNESS_DROP_TU is a self-test hook: it removes one TU from the manifest and
# the build must then fail at link check time (see harness_check_required_symbols).
HARNESS_DROP_TU="${HARNESS_DROP_TU:-}"

# --- static exclusion table -------------------------------------------------
# TU|scope|reason|owner
#
# scope:
#   all                exclude for every harness
#   never              keep the row for the audit trail, exclude for nobody
#                      (used when a formerly wrong exclusion must stay visible)
#   only:<glob,...>    exclude only when one of the harness source basenames
#                      (without .cpp) matches a glob, e.g. only:b5_stubs,b61_*
#
# Only use this for exclusions that are NOT a harness stub. Every added row
# must explain why, and for only: rows name the harnesses that need it.
HARNESS_STATIC_EXCLUSIONS=(
  "KX_ExecutionContextPhysics.cpp|all|physics execution path is not exercised by the node harnesses; it pulls the physics bridge into otherwise socket/graph-only binaries|tests/harness_config.sh"
  "KX_FileAdapterEngine.cpp|all|real filesystem backend is replaced by KX_FileAdapter mocks in every harness|tests/harness_config.sh"
  "KX_GameAdaptersEngine.cpp|all|engine scene/UI backend bindings are only needed by the in-engine build, not by standalone harnesses|tests/harness_config.sh"
  "KX_PhysicsAdvancedAdapter.cpp|never|REGRESSION FIX 2026-11-05: b61/b64 nodes call CreateVehicle through this TU; the old 'always exclude' row left a null function pointer and SIGSEGV. Include it everywhere; no harness needs it excluded.|tests/harness_config.sh"
  "KX_PropertyAccessorEngine.cpp|all|engine property backend is replaced by harness mocks; RegisterBuiltinGameNodes only references it under KX_GAME_NODES_ENGINE|tests/harness_config.sh"
  "KX_SaveGameBinary.cpp|never|REGRESSION FIX 2026-11-05: b64 calls SaveGameBinary through this TU; the old 'always exclude' row left a null function pointer and SIGSEGV. Include it everywhere; no harness needs it excluded.|tests/harness_config.sh"
  "KX_UIAdapterEngine.cpp|all|engine UI backend is never bound by standalone harnesses (KX_UIRenderer.cpp is compiled directly instead)|tests/harness_config.sh"
)

# Symbols that must be defined in every linked harness. These are the cold
# path entry points that a missing TU turns into a 0-address call at runtime.
HARNESS_REQUIRED_SYMBOLS=(
  "KX_CompiledGraph::Compile"
  "KX_InlinePass::Run"
  "CompileGraph(GraphIR const&)"
  "KX_GraphInstance::Load"
)

# --- build flags ------------------------------------------------------------
HARNESS_INCLUDES=(
  -I"$HARNESS_G"
  -I"$HARNESS_ROOT/source/gameengine/GameLogic"
  -I"$HARNESS_ROOT/source/gameengine/Common"
  -I"$HARNESS_ROOT/source/gameengine/Converter"
  -I"$HARNESS_ROOT/source/gameengine/Device"
  -I"$HARNESS_ROOT/source/gameengine/Expressions"
  -I"$HARNESS_ROOT/source/gameengine/Ketsji"
  -I"$HARNESS_ROOT/source/gameengine/Ketsji/KXNetworkMessage"
  -I"$HARNESS_ROOT/source/gameengine/Physics/Common"
  -I"$HARNESS_ROOT/source/gameengine/Rasterizer"
  -I"$HARNESS_ROOT/source/gameengine/SceneGraph"
  -I"$HARNESS_ROOT/source/blender/blenkernel"
  -I"$HARNESS_ROOT/source/blender/gpu"
  -I"$HARNESS_ROOT/source/blender/blenfont"
  -I"$HARNESS_ROOT/source/blender/imbuf"
  -I"$HARNESS_ROOT/source/blender/blenloader"
  -I"$HARNESS_ROOT/source/blender/blenmath"
  -I"$HARNESS_ROOT/source/blender/python/generic"
  -I"$HARNESS_ROOT/intern/termcolor"
  -I"$HARNESS_ROOT/intern/ghost"
  -I"$HARNESS_ROOT/intern/profile"
  -I"$HARNESS_ROOT/intern/guardedalloc"
  -I"$HARNESS_ROOT/source/blender/makesrna"
  -I"$HARNESS_ROOT/source/blender/blenlib"
  -I"$HARNESS_ROOT/source/blender/makesdna"
  -I"$HARNESS_ROOT/source/blender/windowmanager"
  -I"$HARNESS_BUILD_DIR/source/blender/makesdna/intern"
  -isystem /usr/local/include/python3.13
  -isystem "$HARNESS_ROOT/intern/moto/include"
  -isystem "$HARNESS_ROOT/extern/recastnavigation/Recast/Include"
  -isystem "$HARNESS_ROOT/extern/recastnavigation/Detour/Include"
  -isystem "$HARNESS_ROOT/extern/audaspace/include"
  -isystem "$HARNESS_BUILD_DIR/extern/audaspace"
  -isystem "$HARNESS_ROOT/extern/audaspace/bindings"
  -isystem "$HARNESS_ROOT/lib/tbb/include"
  -isystem "$HARNESS_ROOT/extern/json/include"
)

HARNESS_DEFINES=(
  -DFMT_SHARED
  -DHAVE_EXECINFO_H
  -DNDEBUG
  -DWITH_ASSERT_ABORT
  -DWITH_AUDASPACE
  -DWITH_PYTHON
  -DWITH_SDL
  -DWITH_TBB
  -D_FILE_OFFSET_BITS=64
  -D_LARGEFILE64_SOURCE
  -D_LARGEFILE_SOURCE
  -D__LITTLE_ENDIAN__
)

HARNESS_CODEGEN=(
  -funsigned-char
  -fno-strict-aliasing
  -ffp-contract=off
  -std=c++20
  -march=x86-64-v2
)

HARNESS_CXXFLAGS=(
  "${HARNESS_INCLUDES[@]}"
  "${HARNESS_DEFINES[@]}"
  "${HARNESS_CODEGEN[@]}"
  ${HARNESS_OPT:--O1}
)
# ASan harness builds use the same flags plus -fsanitize=address and -g.
HARNESS_ASAN_FLAGS=("${HARNESS_CXXFLAGS[@]}" -fsanitize=address -fno-omit-frame-pointer -g)
HARNESS_LDFLAGS=(-Wl,--unresolved-symbols=ignore-all -lpthread -ldl -lm)
HARNESS_ASAN_LDFLAGS=(-fsanitize=address "${HARNESS_LDFLAGS[@]}")

# --- manifest ---------------------------------------------------------------
harness_ninja_tus()
{
  if [ ! -f "$HARNESS_NINJA" ]; then
    printf 'harness_config: missing %s\n' "$HARNESS_NINJA" >&2
    return 1
  fi
  grep -o 'GameNodes/[A-Za-z0-9_]*\.cpp' "$HARNESS_NINJA" | sed 's#.*/##' | sort -u
}

# Map Register*GameNodes() definitions to the TU that owns them.
_harness_registration_map()
{
  # Key the cache on the ninja file contents so switching HARNESS_NINJA between
  # build trees (build_linux / build_clean_verify) cannot reuse a stale map.
  local key cache
  key="$(md5sum "$HARNESS_NINJA" | cut -c1-8)"
  cache="$HARNESS_OBJ_ROOT/registration_map_$key.txt"
  if [ -f "$cache" ] && [ "$cache" -nt "$HARNESS_NINJA" ]; then
    cat "$cache"
    return 0
  fi
  mkdir -p "$HARNESS_OBJ_ROOT"
  local tu fn
  : > "${cache}.tmp"
  for tu in $(harness_ninja_tus); do
    grep -o 'void Register[A-Za-z0-9_]*GameNodes' "$HARNESS_G/$tu" 2>/dev/null |
      sed 's/void //' |
      while read -r fn; do
        printf '%s|%s\n' "$fn" "$tu" >> "${cache}.tmp"
      done
  done
  sort -u "${cache}.tmp" > "$cache"
  rm -f "${cache}.tmp"
  cat "$cache"
}

# Names stubbed by the given harness sources (empty in two two-line braces).
harness_stubbed_registrations()
{
  local src
  for src in "$@"; do
    [ -f "$src" ] || continue
    # Stub bodies may be written on one line or across three lines, so match
    # the whole definition and require an empty body ({} with only whitespace).
    perl -0777 -ne 'while (/void (Register[A-Za-z0-9_]*GameNodes)\s*\(\s*\)\s*\{\s*\}/g) { print "$1\n" }' \
      "$src" 2>/dev/null
  done | sort -u
}

# True when the given harness source is covered by an "only:" scope.
_harness_scope_matches()
{
  local scope="$1"
  shift
  local glob base
  local -a globs
  IFS=',' read -r -a globs <<< "${scope#only:}"
  for glob in "${globs[@]}"; do
    [ -n "$glob" ] || continue
    for base in "$@"; do
      [ -n "$base" ] || continue
      # shellcheck disable=SC2053
      if [[ "$base" == $glob ]]; then
        return 0
      fi
    done
  done
  return 1
}

# Print TU|reason|owner rows for the exclusions that apply to this harness.
# shellcheck disable=SC2207
harness_exclusions_for()
{
  local map fn tu
  map="$(_harness_registration_map)"
  while read -r fn; do
    [ -n "$fn" ] || continue
    while IFS='|' read -r _mapfn map_tu; do
      if [ "$_mapfn" = "$fn" ]; then
        printf '%s|%s is stubbed by the harness|harness source\n' "$map_tu" "$fn"
      fi
    done <<< "$map"
  done < <(harness_stubbed_registrations "$@")

  # Harness identities for scoped rows: source basenames without extension.
  local -a harness_names=()
  local src
  for src in "$@"; do
    [ -n "$src" ] || continue
    harness_names+=("$(basename "${src%.*}")")
  done

  local row tu scope reason owner
  for row in "${HARNESS_STATIC_EXCLUSIONS[@]}"; do
    IFS='|' read -r tu scope reason owner <<< "$row"
    case "$scope" in
      all)
        printf '%s|%s|%s\n' "$tu" "$reason" "$owner"
        ;;
      never)
        continue
        ;;
      only:*)
        if _harness_scope_matches "$scope" "${harness_names[@]}"; then
          printf '%s|%s|%s\n' "$tu" "$reason" "$owner"
        fi
        ;;
      *)
        printf 'harness_config: bad static exclusion scope "%s" for %s\n' "$scope" "$tu" >&2
        ;;
    esac
  done
}

# Resolve the TU set for one harness: ninja list minus stub/static exclusions.
harness_resolve_tus()
{
  local excluded
  excluded="$(
    {
      harness_exclusions_for "$@" | cut -d'|' -f1
    } | sort -u
  )"
  local tu
  for tu in $(harness_ninja_tus); do
    [ "$tu" = "$HARNESS_DROP_TU" ] && continue
    if grep -qxF "$tu" <<< "$excluded"; then
      continue
    fi
    printf '%s\n' "$tu"
  done
}

# --- object cache -----------------------------------------------------------
harness_config_stamp()
{
  printf '%s\n' "${HARNESS_CXXFLAGS[@]}" | md5sum | cut -c1-8
}

harness_obj_dir()
{
  printf '%s/%s\n' "$HARNESS_OBJ_ROOT" "$(harness_config_stamp)"
}

# Compile every TU into the shared object directory. Incremental: an object is
# rebuilt only when its source, a header, or this config is newer.
harness_compile_tus()
{
  local dir
  dir="$(harness_obj_dir)"
  mkdir -p "$dir"
  local tus
  tus="$(harness_resolve_tus "${HARNESS_HARNESS_SRCS[@]}")"
  local running=0 status=0 tu obj log
  local -a pids=()
  while read -r tu; do
    [ -n "$tu" ] || continue
    obj="$dir/${tu%.cpp}.o"
    if [ -f "$obj" ] && [ "$obj" -nt "$HARNESS_G/$tu" ] &&
       [ "$obj" -nt "$HARNESS_ROOT/tests/harness_config.sh" ] &&
       [ -z "$(find "$HARNESS_G" -maxdepth 1 -name '*.h' -newer "$obj" -print -quit 2>/dev/null)" ]; then
      continue
    fi
    log="$dir/${tu%.cpp}.log"
    (
      if ! "$HARNESS_CXX" "${HARNESS_CXXFLAGS[@]}" -c "$HARNESS_G/$tu" -o "$obj" >"$log" 2>&1; then
        printf 'harness_config: failed to compile %s\n' "$tu" >&2
        sed -n '1,20p' "$log" >&2
        exit 1
      fi
    ) &
    pids+=("$!")
    running=$((running + 1))
    if [ "$running" -ge "$HARNESS_JOBS" ]; then
      wait -n || status=1
      running=$((running - 1))
    fi
  done <<< "$tus"
  local pid
  for pid in "${pids[@]}"; do
    wait "$pid" || status=1
  done
  return "$status"
}

# Link one harness source against the cached objects.
harness_build()
{
  local src="$1"
  local out="$2"
  shift 2
  local -a objs=()
  local dir tu
  dir="$(harness_obj_dir)"
  while read -r tu; do
    [ -n "$tu" ] || continue
    objs+=("$dir/${tu%.cpp}.o")
  done <<< "$(harness_resolve_tus "$src" "$@")"
  "$HARNESS_CXX" "${HARNESS_CXXFLAGS[@]}" "$src" "${objs[@]}" \
    "${HARNESS_LDFLAGS[@]}" -o "$out"
}

# Fail at BUILD time (not runtime) when a critical TU was left out.
harness_check_required_symbols()
{
  local bin="$1"
  shift
  local symbols=("${HARNESS_REQUIRED_SYMBOLS[@]}")
  [ "$#" -gt 0 ] && symbols=("$@")
  local sym failed=0 defined
  defined="$("${NM:-nm}" -C "$bin" 2>/dev/null | grep ' T ')"
  for sym in "${symbols[@]}"; do
    if ! grep -qF " T $sym" <<< "$defined"; then
      printf 'harness_config: undefined required symbol: %s\n' "$sym" >&2
      failed=1
    fi
  done
  return "$failed"
}

# Wait (do not delete) while another line holds the ninja lock.
harness_wait_ninja_lock()
{
  local wait_s="${1:-120}"
  local elapsed=0
  local build_dir="$HARNESS_BUILD_DIR"
  local build_name
  build_name="$(basename "$build_dir")"
  while pgrep -f "ninja -C [^ ]*$build_name" >/dev/null 2>&1 ||
        [ -f "$build_dir/.ninja_lock" ]; do
    if [ "$elapsed" -ge "$wait_s" ]; then
      printf 'harness_config: ninja lock still held after %ss\n' "$wait_s" >&2
      return 1
    fi
    sleep 2
    elapsed=$((elapsed + 2))
  done
  return 0
}

# --- result parsing ---------------------------------------------------------
# Assertion-count rule (authoritative, applies to every caller):
#   1. If the log declares a count ("RESULT/INFO assertions=N" or
#      "N assertions"), that declaration wins.
#   2. Otherwise count lines starting with "PASS " or "FAIL ", excluding:
#        - the summary line "PASS total failures=...";
#        - manually printed aggregate PASS lines (HARNESS_AGGREGATE_PASS_PATTERNS),
#          which do not go through the harness Check() and therefore are not
#          assertions (reg_b4_e2e.cpp:522, p44_stability.cpp:215).
#   3. Never use a bare `grep -c '^PASS'`.
# History: the old E2E total 737 was really 735 (b62, b65a summary lines);
# b4 counted 46 instead of 45 and p44 36 instead of 35 for the same reason.
# Root fix (owned by each harness): emit "RESULT assertions=N failures=M" and
# prefix aggregate lines with "INFO", then the patterns below can be deleted.
HARNESS_AGGREGATE_PASS_PATTERNS=(
  '^PASS [0-9]+ rebuilds kept'
)

# Reads a harness log and echoes "assertions=<n> failures=<n>".
harness_summarize_log()
{
  local log="$1"
  local failures assertions
  # Styles: "RESULT failures=N", "P1: 76 assertions, 2 failures", "PASS/FAIL".
  failures="$(grep -oE 'failures=[0-9]+' "$log" | tail -n 1 | cut -d= -f2)"
  if [ -z "$failures" ]; then
    failures="$(grep -oE '[0-9]+ failures' "$log" | tail -n 1 | grep -oE '[0-9]+')"
  fi
  if [ -z "$failures" ]; then
    failures="$(grep -c '^FAIL' "$log" || true)"
  fi
  assertions="$(grep -oE 'assertions=[0-9]+' "$log" | tail -n 1 | cut -d= -f2)"
  if [ -z "$assertions" ]; then
    assertions="$(grep -oE '[0-9]+ assertions' "$log" | tail -n 1 | grep -oE '[0-9]+')"
  fi
  if [ -z "$assertions" ]; then
    local -a exclude=( -e '^PASS total failures=' )
    local pat
    for pat in "${HARNESS_AGGREGATE_PASS_PATTERNS[@]}"; do
      exclude+=(-e "$pat")
    done
    assertions="$(grep -E '^(PASS|FAIL) ' "$log" |
      grep -vE "${exclude[@]}" | grep -c . || true)"
  fi
  printf 'assertions=%s failures=%s\n' "${assertions:-0}" "${failures:-0}"
}

# A batch that died from a signal, or printed no assertion output at all, must
# be treated as a crash rather than as a green run with zero assertions.
# Both result styles count as "output": PASS/FAIL lines and
# "P1: 76 assertions, 0 failures" / "RESULT failures=N".
harness_log_crashed()
{
  local log="$1"
  local rc="${2:-0}"
  [ "$rc" -ge 128 ] && return 0
  [ ! -f "$log" ] && return 0
  if grep -qE '^(PASS|FAIL)' "$log"; then
    return 1
  fi
  if grep -qE '[0-9]+ assertions|failures=[0-9]+' "$log"; then
    return 1
  fi
  return 0
}
