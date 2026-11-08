/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 * \brief Runtime state for C++ physics node simulations.
 *
 * The simulation world is stepped by a Scene-level depsgraph operation
 * (`OperationCode::PHYS_SIM`). The step is deduplicated by the dependency
 * graph ctime, so redraws, parameter edits and renders at the same frame do
 * not advance the simulation again.
 *
 * `PhysNodesModifier` is a pure sink: it never steps the world, it only reads
 * the frame state produced here and writes it back to its mesh.
 */

#pragma once

#include "BLI_math_vector.hh"
#include "BLI_span.hh"
#include "BLI_string_ref.hh"

#include <string>
#include <vector>

namespace blender {

struct Depsgraph;

namespace bke::physnodes {

/**
 * Particle set produced by the liquid backend (B17). This is the C++ side of
 * the data abstraction layer: it mirrors the numpy arrays the Python reference
 * already computes (`positions` Nx3, `radii` N) so an output node can consume
 * them without inventing a second data model.
 */
struct ParticleSet {
  std::vector<float3> positions;
  std::vector<float> radii;
  /** True when the set holds at least one finite particle. */
  bool valid() const;
};

/**
 * Regular density field produced by the smoke backend (B18).
 * Values are x-major: `index = (z * resolution.y + y) * resolution.x + x`.
 * This is the transfer format a Volume/VDB output would consume.
 */
struct DensityField {
  int3 resolution = int3(0, 0, 0);
  float3 origin = float3(0.0f, 0.0f, 0.0f);
  float3 voxel_size = float3(1.0f, 1.0f, 1.0f);
  std::vector<float> values;

  bool valid() const;
  int voxel_count() const;
};

/**
 * Output layer status. Empty string means "not requested yet"; any other
 * value is the explicit reason an output could not be produced. The C++ code
 * never fabricates placeholder geometry or volume data.
 */
struct OutputStatus {
  std::string particle_mesh;
  std::string particle_volume;
  std::string volume_output;
};

/**
 * Explicit, user-visible reasons for the output nodes that have no native
 * producer yet. Nodes and modifier RNA read the same strings so the UI cannot
 * drift from what the kernel actually knows.
 */
namespace output_reasons {

inline constexpr StringRefNull particle_mesh =
    "No particle data: connect an Object with a particle system, a point cloud "
    "or mesh vertices to 'Particle Source'. The sink converts the evaluated "
    "positions into a ParticleSet and runs the B17.3 narrow-band extractor.";

inline constexpr StringRefNull particle_volume =
    "No particle data: connect an Object with a particle system, a point cloud "
    "or mesh vertices to 'Particle Source'. The sink splats them into a dense "
    "field for the Volume Output node.";

inline constexpr StringRefNull volume_output =
    "No density field in this tree: add a Particle to Volume node with a valid "
    "particle source, or publish a smoke density grid through the C++ sink.";

}  // namespace output_reasons

/** Per-tree simulation state visible to sink modifiers. */
struct FrameState {
  /** Number of integration steps performed since the last reset. */
  int step_count = 0;
  /** Number of times the Scene level operation evaluated this world. */
  int eval_count = 0;
  /** Dependency graph time of the last evaluated frame. */
  float last_ctime = 0.0f;
  /** World space offset applied by sink modifiers for this frame. */
  float3 offset = float3(0.0f, 0.0f, 0.0f);
  /** False until the first frame of a scene has been initialized. */
  bool initialized = false;
  /** True when the last evaluation found the referenced node tree. */
  bool tree_found = false;

  /* --- Step 2 diagnostics: assembled body registry ------------------------ */

  /** Number of rigid bodies assembled from the tree (all kinds). */
  int body_count = 0;
  int active_count = 0;
  int static_count = 0;
  int passive_count = 0;
  /** Uids added / removed by the last collection membership diff. */
  int collection_added = 0;
  int collection_removed = 0;
  /** Newline separated, sorted body uids (`<node>` or `<node>/<object>`). */
  std::string uid_dump;

  /* --- Passive (kinematic) body state ------------------------------------ */

  /**
   * Inverse mass of the last assembled passive body. Always zero: passive
   * bodies are kinematic and never receive a reaction impulse.
   */
  float passive_inv_mass = 0.0f;
  /** Pose injected from the driver object's evaluated world matrix. */
  float3 passive_pose = float3(0.0f, 0.0f, 0.0f);

  /* --- Collider mesh source (modifier stack policy) ---------------------- */

  /** Volume of the mesh the sink modifier saw, in object local space. */
  float collider_volume = 0.0f;
  /**
   * Where that mesh came from:
   * -1 = not measured, 0 = original mesh (no geometry nodes modifier before
   * the physics modifier), 1 = evaluated mesh (geometry nodes modifier ran
   * before the physics modifier).
   */
  int collider_source = -1;
  /** Index of the PhysNodes modifier in the object's modifier stack. */
  int collider_stack_index = -1;

  /* --- Step 3 data layer: particle / density payload --------------------- */

  /** Number of particles currently available for the output nodes. */
  int particle_count = 0;
  /** Number of voxels in the dense field (0 when there is no field). */
  int density_voxel_count = 0;
  /** Particle payload consumed by the output nodes on the last evaluation. */
  ParticleSet particles;
  /**
   * Dense density field produced by Particle to Volume (splatting) or supplied
   * by a smoke producer through the C++ sink. Consumed by Volume Output.
   */
  DensityField density;
  /** Explicit output status; see OutputStatus. */
  OutputStatus output;
};

/**
 * Step all simulation worlds referenced by \a tree_names for the current
 * dependency graph time.
 *
 * Only one step is performed when ctime advances by exactly one frame.
 * Evaluating the graph again at the same ctime (redraw, parameter change,
 * render) is a no-op. Evaluations on inactive dependency graphs are ignored.
 */
void sim_eval(Depsgraph *depsgraph, Span<std::string> tree_names);

/** Read the current frame state of one tree. Returns false if unknown. */
bool frame_state(StringRefNull tree_name, FrameState *r_state);

/**
 * Body count of the most recent evaluation that found its node tree.
 * Diagnostic only: sinks use it to report the last real assembly when the
 * referenced tree no longer resolves. Never used for simulation decisions.
 */
int last_assembled_body_count();

/**
 * Displacement of the active body bound to \a object_name in \a tree_name.
 * Returns false when the object is not simulated by that world, which the
 * sink modifier treats as "leave the mesh untouched" (never a guessed offset).
 */
bool object_offset(StringRefNull tree_name, StringRefNull object_name, float3 *r_offset);

/**
 * Publish the collider mesh measured by a sink modifier.
 *
 * The measurement happens inside the modifier, where the mesh at the
 * modifier's stack position is available without creating a depsgraph cycle.
 * \a source is 0 when the mesh is the original object mesh (no geometry nodes
 * modifier before the physics modifier) and 1 when preceding geometry nodes
 * modifiers already ran on it.
 */
void note_collider(StringRefNull tree_name, float volume, int source, int stack_index);

/** Drop all runtime state (used when a scene is reloaded or reset). */
void reset_runtime();

/**
 * Probe the C physics kernel ABI by loading the shared library at \a path
 * and resolving the rigid body entry points. Result is cached per path.
 */
bool probe_kernel(StringRefNull path);

/** Last kernel probe error message, empty on success. */
StringRefNull kernel_error();

}  // namespace bke::physnodes

}  // namespace blender
