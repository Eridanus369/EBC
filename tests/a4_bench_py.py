# A.4 pure-Python reference path.
#
# Equivalent 100-node graph (1 OnUpdate + 2 ConstFloat + 96 FloatMath + 1 Sink)
# evaluated by a plain Python interpreter loop inside the clean Blender binary:
#   build_clean_verify/bin/blender -b --python /tmp/a4_bench_py.py
#
# This is the "pure Python" comparison point. The legacy in-engine Python node
# executor (uplogic/bge_gamenodes) is not installed, so it cannot be launched.
import os
import time

NODE_COUNT = 100
MATH_NODES = 96
FRAMES_PER_ROUND = 1000
ROUNDS = 21

build_id = os.environ.get("A4_BUILD_ID", "unset")
path_label = os.environ.get("A4_PATH_LABEL", "python_equiv")


def build_graph():
    """Return ops in topological order: (kind, lhs, rhs)."""
    ops = []
    ops.append(("update", 0, 0))    # node 0: OnUpdate
    ops.append(("const", 1.0, 0))   # node 1
    ops.append(("const", 2.0, 0))   # node 2
    # Node index layout mirrors the C++ benchmark: 3..98 are FloatMath.
    # The first math node reads the two constants; later ones read the previous
    # result plus the constant 2.  Operation alternates Add / Mul.
    prev = None
    for i in range(MATH_NODES):
        if i == 0:
            ops.append(("math", 0, 1))
        else:
            ops.append(("math", prev, 1))
        prev = len(ops) - 1
    ops.append(("sink", prev, 0))   # node 99
    return ops


def main():
    ops = build_graph()
    assert len(ops) == NODE_COUNT, len(ops)

    values = [0.0] * NODE_COUNT
    # OnUpdate node 0 writes a trigger; represented as a flag.
    trigger = False
    sink_count = 0

    def eval_frame():
        nonlocal trigger, sink_count
        trigger = True
        for index, (kind, lhs, rhs) in enumerate(ops):
            if kind == "update":
                continue
            if kind == "const":
                values[index] = lhs
            elif kind == "math":
                a = values[lhs]
                b = values[rhs]
                # Mirrors the C++ alternation: even math index Add, odd Mul.
                math_index = index - 3
                values[index] = (a + b) if (math_index % 2 == 0) else (a * b)
            else:
                if trigger:
                    sink_count += 1

    for _ in range(200):
        eval_frame()

    round_ns = []
    for _ in range(ROUNDS):
        start = time.perf_counter_ns()
        for _ in range(FRAMES_PER_ROUND):
            eval_frame()
        end = time.perf_counter_ns()
        round_ns.append((end - start) / FRAMES_PER_ROUND)

    round_ns.sort()
    median = round_ns[len(round_ns) // 2]
    mean = sum(round_ns) / len(round_ns)
    print(
        "BENCH path=%s frames=%d rounds=%d nodes=%d median_ns_per_frame=%.1f "
        "mean_ns_per_frame=%.1f min_ns_per_frame=%.1f build_id=%s"
        % (path_label, FRAMES_PER_ROUND, ROUNDS, NODE_COUNT,
           median, mean, round_ns[0], build_id)
    )


main()
