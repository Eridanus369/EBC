/* P-1 E2E: runtime Recast bake (B6.9).
 *
 * Builds the same geometry programmatically and asserts that the agent size
 * actually changes the baked navmesh:
 *   - 0.8 m doorway wall: agent_radius 0.3 / 0.6 -> passable vs blocked,
 *     plus a smaller walkable area for the larger radius;
 *   - 1.5 m clearance under a slab: agent_height 1.0 / 2.0 -> passable vs
 *     blocked, plus a different polygon count.
 *
 * Validation level: BAKE PIPELINE (`KX_NavMeshBuilder::Bake` ->
 * `dtCreateNavMeshData` -> `dtStatNavMesh::init`/findPath). This is one layer
 * below the node -> engine path (`BuildNavMesh` node -> adapter ->
 * `KX_NavMeshObject::BuildNavMeshRuntime`), which needs a converted scene and
 * a navmesh object and cannot be driven from a headless harness. The engine
 * side of that path is documented in docs/B6_PHYSICS_ADVANCED.md §3.4.
 */
#include "../source/gameengine/Ketsji/KX_NavMeshBuilder.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;
int assertions = 0;

void Check(bool ok, const char *what, double actual, double expected)
{
  ++assertions;
  if (!ok) {
    ++failures;
  }
  std::printf("%s %s (actual=%.4f expected=%.4f)\n", ok ? "PASS" : "FAIL", what, actual, expected);
}

/* Triangle soup in the Y-up convention the bake expects. */
struct Soup {
  std::vector<float> verts;
  std::vector<int> tris;

  int V(float x, float y, float z)
  {
    const int index = (int)verts.size() / 3;
    verts.push_back(x);
    verts.push_back(y);
    verts.push_back(z);
    return index;
  }

  /* Quad with the winding given by the caller (used for the upward floor). */
  void Quad(float ax,
            float ay,
            float az,
            float bx,
            float by,
            float bz,
            float cx,
            float cy,
            float cz,
            float dx,
            float dy,
            float dz)
  {
    const int a = V(ax, ay, az), b = V(bx, by, bz), c = V(cx, cy, cz), d = V(dx, dy, dz);
    tris.push_back(a);
    tris.push_back(b);
    tris.push_back(c);
    tris.push_back(a);
    tris.push_back(c);
    tris.push_back(d);
  }

  /* Axis-aligned box; the +Y face is upward-facing so its top is walkable. */
  void Box(float x0, float y0, float z0, float x1, float y1, float z1)
  {
    Quad(x0, y1, z0, x0, y1, z1, x1, y1, z1, x1, y1, z0);
    Quad(x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1);
    Quad(x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0);
    Quad(x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1);
    Quad(x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0);
    Quad(x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1);
  }
};

/* 10x10 floor with a 2 m wall at x=5, split around a 0.8 m doorway. */
Soup DoorwayScene()
{
  Soup s;
  s.Quad(0, 0, 0, 0, 0, 10, 10, 0, 10, 10, 0, 0);
  s.Box(4.9f, 0.0f, 0.0f, 5.1f, 2.0f, 4.6f);
  s.Box(4.9f, 0.0f, 5.4f, 5.1f, 2.0f, 10.0f);
  return s;
}

/* 10x10 floor with a 0.1 m slab at y=1.5 spanning the middle strip. */
Soup OverhangScene()
{
  Soup s;
  s.Quad(0, 0, 0, 0, 0, 10, 10, 0, 10, 10, 0, 0);
  s.Box(4.4f, 1.5f, 0.0f, 5.6f, 1.6f, 10.0f);
  return s;
}

bool BakeAndInit(const Soup &s,
                 float radius,
                 float height,
                 KX_NavMeshBuilder::Result *result,
                 dtStatNavMesh *mesh,
                 std::string *error)
{
  KX_NavMeshBuilder::Params params;
  params.agent_radius = radius;
  params.agent_height = height;
  params.cell_size = 0.1f;
  params.cell_height = 0.2f;
  if (!KX_NavMeshBuilder::Bake(s.verts.data(),
                               (int)s.verts.size() / 3,
                               s.tris.data(),
                               (int)s.tris.size() / 3,
                               params,
                               result,
                               error)) {
    return false;
  }
  unsigned char *data = nullptr;
  int size = 0;
  if (!KX_NavMeshBuilder::BuildStatNavMeshData(*result, &data, &size) || data == nullptr) {
    *error = "Detour packing failed";
    return false;
  }
  if (!mesh->init(data, size, true)) {
    delete[] data;
    *error = "Detour init rejected the baked data";
    return false;
  }
  return true;
}

struct Probe {
  bool start_found = false;
  bool end_found = false;
  bool reaches = false;
  int polys = 0;
};

/* `findPath` returns the start polygon only (1) when the goal is unreachable,
 * so "reaches" means the last path entry is the end polygon (or both probes
 * landed on the same polygon). */
Probe WalkPath(dtStatNavMesh &mesh, const float from[3], const float to[3])
{
  Probe probe;
  const float extents[3] = {0.2f, 0.5f, 0.2f};
  const dtStatPolyRef start = mesh.findNearestPoly(from, extents);
  const dtStatPolyRef end = mesh.findNearestPoly(to, extents);
  probe.start_found = start != 0;
  probe.end_found = end != 0;
  if (!probe.start_found || !probe.end_found) {
    return probe;
  }
  if (start == end) {
    probe.polys = 1;
    probe.reaches = true;
    return probe;
  }
  std::vector<dtStatPolyRef> polys(64);
  probe.polys = mesh.findPath(start, end, from, to, polys.data(), 64);
  probe.reaches = probe.polys >= 2 && polys[probe.polys - 1] == end;
  return probe;
}

double WalkableArea(const KX_NavMeshBuilder::Result &result)
{
  double area = 0.0;
  for (int p = 0; p < result.npolys; p++) {
    const unsigned short *poly = &result.polys[(size_t)p * result.nvp * 2];
    int count = 0;
    while (count < result.nvp && poly[count] != 0xffff) {
      count++;
    }
    for (int i = 0; i < count; i++) {
      const unsigned short *v0 = &result.verts[(size_t)poly[i] * 3];
      const unsigned short *v1 = &result.verts[(size_t)poly[(i + 1) % count] * 3];
      const double x0 = result.bmin[0] + v0[0] * result.cs;
      const double z0 = result.bmin[2] + v0[2] * result.cs;
      const double x1 = result.bmin[0] + v1[0] * result.cs;
      const double z1 = result.bmin[2] + v1[2] * result.cs;
      area += x0 * z1 - x1 * z0;
    }
  }
  return std::fabs(area) * 0.5;
}

void TestAgentRadius()
{
  Soup scene = DoorwayScene();
  const float from[3] = {1.0f, 0.1f, 1.0f};
  const float to[3] = {9.0f, 0.1f, 9.0f};
  std::string error;

  KX_NavMeshBuilder::Result small;
  dtStatNavMesh small_mesh;
  const bool small_ok = BakeAndInit(scene, 0.3f, 2.0f, &small, &small_mesh, &error);
  Check(small_ok, "bake succeeds for agent_radius=0.3", small_ok ? 1.0 : 0.0, 1.0);
  if (!small_ok) {
    std::printf("ERROR %s\n", error.c_str());
    return;
  }
  const double small_area = WalkableArea(small);
  const Probe small_probe = WalkPath(small_mesh, from, to);
  Check(small_area > 0.0, "agent_radius=0.3 yields a walkable area", small_area, 0.0);
  Check(small_probe.reaches,
        "agent_radius=0.3 keeps the 0.8 m doorway passable",
        small_probe.polys,
        (double)small_probe.polys);

  KX_NavMeshBuilder::Result big;
  dtStatNavMesh big_mesh;
  error.clear();
  const bool big_ok = BakeAndInit(scene, 0.6f, 2.0f, &big, &big_mesh, &error);
  Check(big_ok, "bake succeeds for agent_radius=0.6", big_ok ? 1.0 : 0.0, 1.0);
  if (!big_ok) {
    std::printf("ERROR %s\n", error.c_str());
    return;
  }
  const double big_area = WalkableArea(big);
  const Probe big_probe = WalkPath(big_mesh, from, to);
  Check(big_area < small_area,
        "agent_radius=0.6 shrinks the walkable area",
        big_area,
        small_area);
  Check(small_area - big_area > 5.0,
        "agent_radius=0.6 removes a meaningful walkable band",
        small_area - big_area,
        5.0);
  Check(!big_probe.reaches,
        "agent_radius=0.6 closes the 0.8 m doorway",
        big_probe.polys,
        (double)big_probe.polys);
}

void TestAgentHeight()
{
  Soup scene = OverhangScene();
  const float from[3] = {1.0f, 0.1f, 5.0f};
  const float to[3] = {9.0f, 0.1f, 5.0f};
  std::string error;

  KX_NavMeshBuilder::Result short_agent;
  dtStatNavMesh short_mesh;
  const bool short_ok = BakeAndInit(scene, 0.6f, 1.0f, &short_agent, &short_mesh, &error);
  Check(short_ok, "bake succeeds for agent_height=1.0", short_ok ? 1.0 : 0.0, 1.0);
  if (!short_ok) {
    std::printf("ERROR %s\n", error.c_str());
    return;
  }
  const Probe short_probe = WalkPath(short_mesh, from, to);
  Check(short_probe.reaches,
        "agent_height=1.0 walks under the 1.5 m slab",
        short_probe.polys,
        (double)short_probe.polys);

  KX_NavMeshBuilder::Result tall_agent;
  dtStatNavMesh tall_mesh;
  error.clear();
  const bool tall_ok = BakeAndInit(scene, 0.6f, 2.0f, &tall_agent, &tall_mesh, &error);
  Check(tall_ok, "bake succeeds for agent_height=2.0", tall_ok ? 1.0 : 0.0, 1.0);
  if (!tall_ok) {
    std::printf("ERROR %s\n", error.c_str());
    return;
  }
  Check(tall_agent.npolys != short_agent.npolys,
        "agent_height=2.0 changes the walkable polygon count",
        (double)tall_agent.npolys,
        (double)short_agent.npolys);
  const Probe tall_probe = WalkPath(tall_mesh, from, to);
  Check(!tall_probe.reaches,
        "agent_height=2.0 cannot fit under the 1.5 m slab",
        tall_probe.polys,
        (double)tall_probe.polys);
}

}  // namespace

int main()
{
  std::printf("INFO runtime Recast bake harness (bake pipeline layer)\n");
  TestAgentRadius();
  TestAgentHeight();
  std::printf("RESULT assertions=%d failures=%d\n", assertions, failures);
  return failures == 0 ? 0 : 1;
}
