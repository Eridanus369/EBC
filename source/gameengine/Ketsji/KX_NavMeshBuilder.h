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
 * ***** END GPL LICENSE BLOCK *****
 */

#pragma once

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "../../../extern/recastnavigation/recast-capi.h"
#include "DetourStatNavMesh.h"
#include "DetourStatNavMeshBuilder.h"

/**
 * Runtime Recast bake: triangle soup -> recastPolyMesh/Detail -> the byte blob
 * consumed by `dtStatNavMesh::init()`.
 *
 * This is the player-side counterpart of
 * `source/blender/editors/mesh/mesh_navmesh.cc:buildNavMesh()`, which cannot
 * run outside the editor because it needs `bContext` and `ReportList`.
 * Everything here is plain C++ over the public `recast-capi.h` surface, so
 * both the engine and a test harness can call it.
 *
 * Coordinate convention: geometry must be Y-up (x/z horizontal, y vertical),
 * the convention the baked navmesh stores. `KX_NavMeshObject` converts the
 * RAS mesh to this convention before calling, and `FindPath` converts back.
 */
namespace KX_NavMeshBuilder {

struct Params {
  float agent_radius = 0.6f;
  float agent_height = 2.0f;
  float agent_max_climb = 0.5f;
  float agent_max_slope_deg = 45.0f;
  float cell_size = 0.2f;
  float cell_height = 0.2f;
  float edge_max_error = 1.3f;
  float edge_max_len = 12.0f;
  float detail_sample_dist = 6.0f;
  float detail_sample_max_error = 1.0f;
  float region_min_size = 3.0f;
  float region_merge_size = 20.0f;
  int verts_per_poly = 6;
};

struct Result {
  int nverts = 0;
  int npolys = 0;
  int nvp = 0;
  float bmin[3] = {0.0f, 0.0f, 0.0f};
  float bmax[3] = {0.0f, 0.0f, 0.0f};
  float cs = 0.0f;
  float ch = 0.0f;
  /* Quantized vertices (x/z in cs, y in ch). */
  std::vector<unsigned short> verts;
  /* 2*nvp per polygon: vertex indices, then neighbour indices (0xffff = none). */
  std::vector<unsigned short> polys;
  /* Detail mesh in the 16-bit layout `dtCreateNavMeshData()` expects. */
  std::vector<float> dverts;
  std::vector<unsigned char> dtris;
  std::vector<unsigned short> dmeshes;

  bool empty() const
  {
    return nverts <= 0 || npolys <= 0;
  }
};

inline bool Bake(const float *verts,
                 int nverts,
                 const int *tris,
                 int ntris,
                 const Params &params,
                 Result *out,
                 std::string *error)
{
  *out = Result();

  if (verts == nullptr || tris == nullptr || nverts <= 0 || ntris <= 0) {
    *error = "no geometry to bake";
    return false;
  }
  if (!(params.agent_radius > 0.0f) || !(params.agent_height > 0.0f)) {
    *error = "agent radius and height must be positive";
    return false;
  }
  if (!(params.cell_size > 0.0f) || !(params.cell_height > 0.0f)) {
    *error = "cell size and cell height must be positive";
    return false;
  }

  float bmin[3], bmax[3];
  recast_calcBounds(verts, nverts, bmin, bmax);

  int walkableHeight = (int)ceilf(params.agent_height / params.cell_height);
  int walkableClimb = (int)floorf(params.agent_max_climb / params.cell_height);
  int walkableRadius = (int)ceilf(params.agent_radius / params.cell_size);
  int minRegionArea = (int)(params.region_min_size * params.region_min_size);
  int mergeRegionArea = (int)(params.region_merge_size * params.region_merge_size);
  int maxEdgeLen = (int)(params.edge_max_len / params.cell_size);
  float detailSampleDist = params.detail_sample_dist < 0.9f ?
                               0.0f :
                               params.cell_size * params.detail_sample_dist;
  float detailSampleMaxError = params.cell_height * params.detail_sample_max_error;

  int width = 0, height = 0;
  recast_calcGridSize(bmin, bmax, params.cell_size, &width, &height);
  if (width <= 0 || height <= 0) {
    *error = "geometry has zero width or height";
    return false;
  }

  struct recast_heightfield *solid = recast_newHeightfield();
  if (solid == nullptr ||
      !recast_createHeightfield(solid,
                                width,
                                height,
                                bmin,
                                bmax,
                                params.cell_size,
                                params.cell_height)) {
    if (solid != nullptr) {
      recast_destroyHeightfield(solid);
    }
    *error = "failed to create heightfield";
    return false;
  }

  std::vector<unsigned char> triflags((size_t)ntris, 0);
  recast_markWalkableTriangles(
      params.agent_max_slope_deg, verts, nverts, tris, ntris, triflags.data());
  recast_rasterizeTriangles(
      verts, nverts, tris, triflags.data(), ntris, solid, 1);

  recast_filterLowHangingWalkableObstacles(walkableClimb, solid);
  recast_filterLedgeSpans(walkableHeight, walkableClimb, solid);
  recast_filterWalkableLowHeightSpans(walkableHeight, solid);

  struct recast_compactHeightfield *chf = recast_newCompactHeightfield();
  bool ok = chf != nullptr &&
            recast_buildCompactHeightfield(walkableHeight, walkableClimb, solid, chf);
  recast_destroyHeightfield(solid);
  solid = nullptr;
  if (!ok) {
    if (chf != nullptr) {
      recast_destroyCompactHeightfield(chf);
    }
    *error = "failed to build compact heightfield (no walkable surface)";
    return false;
  }

  if (!recast_erodeWalkableArea(walkableRadius, chf)) {
    recast_destroyCompactHeightfield(chf);
    *error = "failed to erode walkable area";
    return false;
  }
  if (!recast_buildDistanceField(chf) ||
      !recast_buildRegions(chf, 0, minRegionArea, mergeRegionArea)) {
    recast_destroyCompactHeightfield(chf);
    *error = "failed to partition walkable surface into regions";
    return false;
  }

  struct recast_contourSet *cset = recast_newContourSet();
  ok = cset != nullptr &&
       recast_buildContours(
           chf, params.edge_max_error, maxEdgeLen, cset, RECAST_CONTOUR_TESS_WALL_EDGES);
  if (!ok) {
    if (cset != nullptr) {
      recast_destroyContourSet(cset);
    }
    recast_destroyCompactHeightfield(chf);
    *error = "failed to build contours";
    return false;
  }

  struct recast_polyMesh *pmesh = recast_newPolyMesh();
  ok = pmesh != nullptr && recast_buildPolyMesh(cset, params.verts_per_poly, pmesh);
  if (!ok) {
    if (pmesh != nullptr) {
      recast_destroyPolyMesh(pmesh);
    }
    recast_destroyContourSet(cset);
    recast_destroyCompactHeightfield(chf);
    *error = "failed to build poly mesh";
    return false;
  }

  struct recast_polyMeshDetail *dmesh = recast_newPolyMeshDetail();
  ok = dmesh != nullptr &&
       recast_buildPolyMeshDetail(
           pmesh, chf, detailSampleDist, detailSampleMaxError, dmesh);
  if (!ok) {
    if (dmesh != nullptr) {
      recast_destroyPolyMeshDetail(dmesh);
    }
    recast_destroyPolyMesh(pmesh);
    recast_destroyContourSet(cset);
    recast_destroyCompactHeightfield(chf);
    *error = "failed to build poly mesh detail";
    return false;
  }

  /* Copy out of the recast structs: they are destroyed right after this. */
  int pnverts = 0, pnpolys = 0, pnvp = 0;
  unsigned short *pverts = recast_polyMeshGetVerts(pmesh, &pnverts);
  unsigned short *ppolys = recast_polyMeshGetPolys(pmesh, &pnpolys, &pnvp);
  float cell[2] = {params.cell_size, params.cell_height};
  recast_polyMeshGetCell(pmesh, &cell[0], &cell[1]);
  recast_polyMeshGetBoundbox(pmesh, out->bmin, out->bmax);

  int dnverts = 0, dntris = 0, dnmeshes = 0;
  float *dverts = recast_polyMeshDetailGetVerts(dmesh, &dnverts);
  unsigned char *dtris = recast_polyMeshDetailGetTris(dmesh, &dntris);
  unsigned int *dmeshes = recast_polyMeshDetailGetMeshes(dmesh, &dnmeshes);

  if (pverts == nullptr || ppolys == nullptr || pnvp <= 0 || pnvp > 12 || pnpolys <= 0 ||
      pnverts <= 0 || dverts == nullptr || dtris == nullptr || dmeshes == nullptr ||
      dnmeshes != pnpolys) {
    recast_destroyPolyMeshDetail(dmesh);
    recast_destroyPolyMesh(pmesh);
    recast_destroyContourSet(cset);
    recast_destroyCompactHeightfield(chf);
    *error = "recast produced an unusable poly mesh";
    return false;
  }

  out->nverts = pnverts;
  out->npolys = pnpolys;
  out->nvp = pnvp;
  out->cs = cell[0];
  out->ch = cell[1];
  out->verts.assign(pverts, pverts + (size_t)pnverts * 3);
  out->polys.assign(ppolys, ppolys + (size_t)pnpolys * pnvp * 2);
  out->dverts.assign(dverts, dverts + (size_t)dnverts * 3);
  out->dtris.assign(dtris, dtris + (size_t)dntris * 4);
  out->dmeshes.resize((size_t)pnpolys * 4);
  for (int i = 0; i < pnpolys; i++) {
    for (int j = 0; j < 4; j++) {
      unsigned int v = dmeshes[i * 4 + j];
      if (v > 0xffffu) {
        recast_destroyPolyMeshDetail(dmesh);
        recast_destroyPolyMesh(pmesh);
        recast_destroyContourSet(cset);
        recast_destroyCompactHeightfield(chf);
        *out = Result();
        *error = "detail mesh exceeds the 16-bit Detour layout";
        return false;
      }
      out->dmeshes[(size_t)i * 4 + j] = (unsigned short)v;
    }
  }

  recast_destroyPolyMeshDetail(dmesh);
  recast_destroyPolyMesh(pmesh);
  recast_destroyContourSet(cset);
  recast_destroyCompactHeightfield(chf);
  return true;
}

/**
 * Pack the bake into the navigation memory blob `dtStatNavMesh::init()` takes
 * ownership of. Uses Detour's own builder (the same code path the editor uses
 * through `createBVTree`), so no second serialization format is introduced.
 */
inline bool BuildStatNavMeshData(const Result &result, unsigned char **out_data, int *out_size)
{
  *out_data = nullptr;
  *out_size = 0;
  if (result.empty() || result.dmeshes.empty() || result.dverts.empty() ||
      result.dtris.empty()) {
    return false;
  }
  return dtCreateNavMeshData(result.verts.data(),
                             result.nverts,
                             result.polys.data(),
                             result.npolys,
                             result.nvp,
                             result.bmin,
                             result.bmax,
                             result.cs,
                             result.ch,
                             result.dmeshes.data(),
                             result.dverts.data(),
                             (int)result.dverts.size() / 3,
                             result.dtris.data(),
                             (int)result.dtris.size() / 4,
                             out_data,
                             out_size);
}

}  // namespace KX_NavMeshBuilder
