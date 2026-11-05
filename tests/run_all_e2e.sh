#!/usr/bin/env bash
# D.1 entry point: build and run every E2E batch through one manifest.
#
# All harnesses link the SAME GameNodes TU set derived from build.ninja
# (tests/harness_config.sh). A missing TU now fails at build time with an
# undefined-symbol diagnostic instead of crashing at runtime at address 0.
#
# Usage:
#   tests/run_all_e2e.sh                 # every batch
#   tests/run_all_e2e.sh --only b5|c32   # name/tag regex filter
#   tests/run_all_e2e.sh -j 4            # parallel builds
#   tests/run_all_e2e.sh --parallel-run  # parallel runs (per-batch logs)
#   tests/run_all_e2e.sh --list
#
# Exit code: 0 only when every batch built and every batch had 0 failures.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tests/harness_config.sh"

BINDIR="$ROOT/tests/.e2e_bin"
# Every run writes its logs to a fresh timestamped directory, so an earlier
# run's evidence is never truncated by a later one.
RUN_ID="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${E2E_OUTDIR:-$ROOT/tests/.e2e_out/run_$RUN_ID}"
JOBS="${E2E_JOBS:-2}"
ONLY=""
PARALLEL_RUN=0
LIST_ONLY=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    -j) JOBS="$2"; shift 2 ;;
    --only) ONLY="$2"; shift 2 ;;
    --parallel-run) PARALLEL_RUN=1; shift ;;
    --list) LIST_ONLY=1; shift ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

mkdir -p "$BINDIR" "$OUTDIR"

# kind|name|source|extra sources|env|tags
#   harness = build with the unified manifest, then run
#   builtin = already-built binary (tests/b5_rerun Makefile)
MANIFEST=(
  "builtin|b5_p1|.build/p1_harness||BGE_GAMENODE_INLINE=0|b5 regression"
  "builtin|b5_p2|.build/p2_harness||BGE_GAMENODE_INLINE=0|b5 regression"
  "builtin|b5_p3|.build/p3_harness||BGE_GAMENODE_INLINE=0|b5 regression"
  "builtin|b5_p3_input|.build/p3_input_harness||BGE_GAMENODE_INLINE=0|b5 regression"
  "harness|b2_b3_reg|tests/reg_b2_b3.cpp|||b2b3 regression"
  "harness|b4_reg|tests/reg_b4_e2e.cpp|||b4 regression"
  "harness|p12_core|tests/reg_core_p123_t12.cpp|||p1p2p3 regression"
  "harness|p41_rebuild|tests/p41_rebuild.cpp|||p4"
  "harness|p42_hot_reload|tests/p42_hot_reload.cpp|||p4"
  "harness|p43_trace|tests/p43_trace.cpp|||p4"
  "harness|p44_stability|tests/p44_stability.cpp|||p4 stability"
  "harness|b3_audio|tests/b3_audio_e2e.cpp|||b3"
  "harness|b61|tests/b61_e2e.cpp|||b6.1"
  "harness|b62|tests/b62_e2e.cpp|||b6.2"
  "harness|b63_network|tests/b63_network_e2e.cpp|||b6.3"
  "harness|b63_nodes|tests/b63_nodes_e2e.cpp|||b6.3"
  "harness|b64|tests/b64_e2e.cpp|||b6.4"
  "harness|b65a|tests/b65a_e2e.cpp|tests/b65a_reg_stubs.cpp||b6.5"
  "harness|b661|tests/b661_e2e.cpp|||b6.6"
  "harness|b661_query|tests/b661_query_e2e.cpp|||b6.6"
  "harness|b661_logicgate|tests/b661_logicgate.cpp|||b6.6"
  "harness|b662_jit|tests/b662_jit.cpp|||b6.6"
  "harness|b66_uncovered|tests/b66_uncovered_e2e.cpp|||b6.6 uncovered"
  "harness|c31_udp|tests/c31_udp_e2e.cpp|||c3.1"
  "harness|c32_rooms|tests/c32_rooms_e2e.cpp|||c3.2"
  "harness|c32_nodes|tests/c32_nodes_e2e.cpp|||c3.2 graph"
)

matches()
{
  if [ -z "$ONLY" ]; then
    return 0
  fi
  printf '%s\n' "$1" | grep -Eq "$ONLY"
}

# --- filter ----------------------------------------------------------------
names=()
kinds=()
srcs=()
extras=()
envs=()
tags=()
while IFS='|' read -r kind name src extra env tag; do
  [ -n "$kind" ] || continue
  if matches "$name" || matches "$tag"; then
    names+=("$name"); kinds+=("$kind"); srcs+=("$src")
    extras+=("$extra"); envs+=("$env"); tags+=("$tag")
  fi
done <<< "$(printf '%s\n' "${MANIFEST[@]}")"

if [ "$LIST_ONLY" -eq 1 ]; then
  for index in "${!names[@]}"; do
    printf '%-16s %-10s %s [%s]\n' "${names[$index]}" "${kinds[$index]}" \
      "${srcs[$index]}" "${tags[$index]}"
  done
  exit 0
fi

if [ "${#names[@]}" -eq 0 ]; then
  echo "no batch matched --only '$ONLY'" >&2
  exit 2
fi

# --- b5 builtin group needs the Makefile once ------------------------------
needs_b5=0
for kind in "${kinds[@]}"; do
  [ "$kind" = "builtin" ] && needs_b5=1
done
if [ "$needs_b5" -eq 1 ]; then
  echo "== building tests/b5_rerun (unified TU list) =="
  if ! make -C "$ROOT/tests/b5_rerun" -j"$JOBS" >>"$OUTDIR/b5_build.log" 2>&1; then
    echo "FAIL(build) tests/b5_rerun (see $OUTDIR/b5_build.log)" >&2
  fi
fi

# --- precompile the shared object set once --------------------------------
echo "== precompiling GameNodes objects =="
HARNESS_HARNESS_SRCS=()
if ! harness_compile_tus; then
  echo "FAIL: shared GameNodes objects did not compile" >&2
  exit 1
fi
echo "objects: $(harness_obj_dir)"

# --- build ----------------------------------------------------------------
build_one()
{
  local kind="$1" name="$2" src="$3" extra="$4"
  local out="$BINDIR/$name"
  local log="$OUTDIR/$name.build.log"
  if [ "$kind" = "builtin" ]; then
    [ -x "$ROOT/tests/b5_rerun/$src" ] && return 0
    return 1
  fi
  local -a extra_args=()
  local item
  for item in $extra; do
    extra_args+=("$item")
  done
  # Manifest rows may use repo-relative sources (tests/...), so resolve them
  # against ROOT instead of relying on the caller's cwd.
  case "$src" in
    /*) ;;
    *) src="$ROOT/$src" ;;
  esac
  HARNESS_HARNESS_SRCS=("$src" "${extra_args[@]}")
  {
    harness_compile_tus &&
    harness_build "$src" "$out" "${extra_args[@]}" &&
    harness_check_required_symbols "$out"
  } >"$log" 2>&1
}

build_fail=()
running=0
for index in "${!names[@]}"; do
  build_one "${kinds[$index]}" "${names[$index]}" "${srcs[$index]}" "${extras[$index]}" &
  running=$((running + 1))
  if [ "$running" -ge "$JOBS" ]; then
    wait -n || true
    running=$((running - 1))
  fi
done
wait
for index in "${!names[@]}"; do
  name="${names[$index]}"
  if [ "${kinds[$index]}" = "builtin" ]; then
    [ -x "$ROOT/tests/b5_rerun/${srcs[$index]}" ] || build_fail+=("$name")
  else
    [ -x "$BINDIR/$name" ] || build_fail+=("$name")
  fi
done

# --- run ------------------------------------------------------------------
run_one()
{
  local kind="$1" name="$2" src="$3" env="$4"
  local bin
  if [ "$kind" = "builtin" ]; then
    bin="$ROOT/tests/b5_rerun/$src"
  else
    bin="$BINDIR/$name"
  fi
  local log="$OUTDIR/$name.log"
  # The subshell keeps a signal-terminated harness from making bash print
  # "Segmentation fault (core dumped)" on the console; the rc is still recorded.
  (
    if [ -n "$env" ]; then
      env $env timeout 300 "$bin" >"$log" 2>&1
    else
      timeout 300 "$bin" >"$log" 2>&1
    fi
    printf '%d\n' "$?" >"$OUTDIR/$name.rc"
  ) 2>/dev/null
  return 0
}

running=0
for index in "${!names[@]}"; do
  name="${names[$index]}"
  skip=0
  for failed in "${build_fail[@]}"; do
    [ "$failed" = "$name" ] && skip=1
  done
  [ "$skip" -eq 1 ] && continue
  if [ "$PARALLEL_RUN" -eq 1 ]; then
    run_one "${kinds[$index]}" "$name" "${srcs[$index]}" "${envs[$index]}" &
    running=$((running + 1))
    if [ "$running" -ge "$JOBS" ]; then
      wait -n || true
      running=$((running - 1))
    fi
  else
    run_one "${kinds[$index]}" "$name" "${srcs[$index]}" "${envs[$index]}"
  fi
done
wait

# --- summary --------------------------------------------------------------
total_assert=0
total_fail=0
total_crash=0
run_fail=()
printf '\n%-18s %-10s %s\n' "BATCH" "ASSERTS" "FAILURES"
printf '%-18s %-10s %s\n' "-----" "-------" "--------"
for index in "${!names[@]}"; do
  name="${names[$index]}"
  skip=0
  for failed in "${build_fail[@]}"; do
    [ "$failed" = "$name" ] && skip=1
  done
  if [ "$skip" -eq 1 ]; then
    printf '%-18s %-10s %s\n' "$name" "BUILD-FAIL" "-"
    run_fail+=("$name")
    continue
  fi
  log="$OUTDIR/$name.log"
  rc="$(cat "$OUTDIR/$name.rc" 2>/dev/null || echo 0)"
  # A batch that died from a signal (rc>=128) or produced no PASS/FAIL at all
  # is a crash; it must never be summarised as "0 assertions, 0 failures".
  if harness_log_crashed "$log" "$rc"; then
    printf '%-18s %-10s %s\n' "$name" "HARNESS_CRASH" "rc=$rc"
    run_fail+=("$name")
    total_crash=$((total_crash + 1))
    continue
  fi
  summary="$(harness_summarize_log "$log")"
  asserts="${summary#assertions=}"; asserts="${asserts%% *}"
  fails="${summary##*failures=}"
  if [ "$rc" -ne 0 ] && [ "$fails" -eq 0 ]; then
    fails=1
  fi
  total_assert=$((total_assert + asserts))
  total_fail=$((total_fail + fails))
  printf '%-18s %-10s %s\n' "$name" "$asserts" "$fails"
  if [ "$fails" -gt 0 ] || [ "$rc" -ne 0 ]; then
    run_fail+=("$name")
  fi
done

printf '\nTOTAL assertions=%d failures=%d build_failures=%d crashes=%d\n' \
  "$total_assert" "$total_fail" "${#build_fail[@]}" "$total_crash"
echo "logs: $OUTDIR"
if [ "${#run_fail[@]}" -gt 0 ]; then
  printf 'FAILED: %s\n' "${run_fail[*]}"
  exit 1
fi
exit 0
