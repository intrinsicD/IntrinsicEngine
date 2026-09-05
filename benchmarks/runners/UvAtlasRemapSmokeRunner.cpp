// Measures FastStaged atlas allocation traffic through its public CPU entry point.
// The isolated executable keeps allocation interception out of other tests.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

import Geometry.UvAtlas;
import Geometry.MeshSoup;
import Geometry.Properties;

namespace {
thread_local bool g_CountAllocations = false;
thread_local std::uint64_t g_AllocatedBytes = 0u;
constexpr std::uint32_t kCharts = 512u;
constexpr std::uint32_t kVertices = 65536u;
constexpr std::uint64_t kAllocationBudget = 64u * 1024u * 1024u;
constexpr std::size_t kMeasuredRuns = 3u;
using Json = nlohmann::ordered_json;
}

void* operator new(std::size_t size) {
  if (void* memory = std::malloc(size == 0u ? 1u : size)) {
    if (g_CountAllocations) g_AllocatedBytes += size;
    return memory;
  }
  std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  void* memory = std::malloc(size == 0u ? 1u : size);
  if (memory && g_CountAllocations) g_AllocatedBytes += size;
  return memory;
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
  return ::operator new(size, tag);
}
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
Geometry::MeshSoup::IndexedMesh MakeMesh() {
  Geometry::MeshSoup::IndexedMesh mesh;
  for (std::uint32_t corner = 0u; corner < 3u; ++corner) {
    for (std::uint32_t chart = 0u; chart < kCharts; ++chart) {
      (void)mesh.AddVertex({static_cast<float>(2u * chart + (corner == 1u)),
                           corner == 2u ? 1.0f : 0.0f, 0.0f});
    }
  }
  for (std::uint32_t i = kCharts * 3u; i < kVertices; ++i)
    (void)mesh.AddVertex({0.0f, 0.0f, 0.0f});
  for (std::uint32_t chart = 0u; chart < kCharts; ++chart)
    (void)mesh.AddTriangle(chart + 2u * kCharts, chart, chart + kCharts);
  auto ids = mesh.GetOrAddVertexProperty<std::uint32_t>("v:source_id");
  for (std::uint32_t i = 0u; i < kVertices; ++i) ids.Vector()[i] = i;
  return mesh;
}

Geometry::MeshSoup::IndexedMesh MakeFixture(
    std::span<const glm::vec3> positions,
    std::span<const std::array<std::uint32_t, 3>> faces) {
  Geometry::MeshSoup::IndexedMesh mesh;
  for (const auto position : positions) (void)mesh.AddVertex(position);
  for (const auto face : faces) (void)mesh.AddTriangle(face[0], face[1], face[2]);
  auto ids = mesh.GetOrAddVertexProperty<std::uint32_t>("v:source_id");
  for (std::uint32_t i = 0u; i < mesh.VertexCount(); ++i) ids.Vector()[i] = i;
  return mesh;
}

std::array<Geometry::MeshSoup::IndexedMesh, 3> ExistingFixtures() {
  const glm::vec3 squarePositions[]{{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}};
  const std::array<std::uint32_t, 3> squareFaces[]{{0,1,2}, {0,2,3}};
  const glm::vec3 cubePositions[]{
      {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
      {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1}};
  const std::array<std::uint32_t, 3> cubeFaces[]{
      {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7}, {0,4,7}, {0,7,3},
      {1,2,6}, {1,6,5}, {0,1,5}, {0,5,4}, {2,3,7}, {2,7,6}};
  const glm::vec3 nonmanifoldPositions[]{
      {0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}, {0,-1,0}};
  const std::array<std::uint32_t, 3> nonmanifoldFaces[]{
      {0,1,2}, {1,0,3}, {0,1,4}};
  return {MakeFixture(squarePositions, squareFaces),
          MakeFixture(cubePositions, cubeFaces),
          MakeFixture(nonmanifoldPositions, nonmanifoldFaces)};
}

template<class Quality>
Json QualityRecord(const Quality& q) {
  Json result = Json::object();
  result["Status"] = q.Status;
  result["VertexStorageCount"] = q.VertexStorageCount;
  result["FaceStorageCount"] = q.FaceStorageCount;
  result["LiveFaceCount"] = q.LiveFaceCount;
  result["EvaluatedFaceCount"] = q.EvaluatedFaceCount;
  result["SkippedFaceCount"] = q.SkippedFaceCount;
  result["DeletedFaceCount"] = q.DeletedFaceCount;
  result["NonTriangleFaceCount"] = q.NonTriangleFaceCount;
  result["DegeneratePositionFaceCount"] = q.DegeneratePositionFaceCount;
  result["DegenerateUvFaceCount"] = q.DegenerateUvFaceCount;
  result["NonFinitePositionFaceCount"] = q.NonFinitePositionFaceCount;
  result["NonFiniteUvFaceCount"] = q.NonFiniteUvFaceCount;
  result["FlippedElementCount"] = q.FlippedElementCount;
  result["MeanConformalDistortion"] = q.MeanConformalDistortion;
  result["MaxConformalDistortion"] = q.MaxConformalDistortion;
  result["MeanConformalError"] = q.MeanConformalError;
  result["RootMeanSquareConformalError"] = q.RootMeanSquareConformalError;
  result["MaxConformalError"] = q.MaxConformalError;
  result["FaceConformalDistortion"] = q.FaceConformalDistortion;
  result["MeanAreaRatio"] = q.MeanAreaRatio;
  result["MinAreaRatio"] = q.MinAreaRatio;
  result["MaxAreaRatio"] = q.MaxAreaRatio;
  result["MeanAreaDistortion"] = q.MeanAreaDistortion;
  result["MaxAreaDistortion"] = q.MaxAreaDistortion;
  result["MeanAreaError"] = q.MeanAreaError;
  result["MaxAreaError"] = q.MaxAreaError;
  result["MeanSymmetricDirichletEnergy"] = q.MeanSymmetricDirichletEnergy;
  result["MaxSymmetricDirichletEnergy"] = q.MaxSymmetricDirichletEnergy;
  result["MeanSymmetricDirichletExcess"] = q.MeanSymmetricDirichletExcess;
  result["MaxSymmetricDirichletExcess"] = q.MaxSymmetricDirichletExcess;
  result["MeanStretch"] = q.MeanStretch;
  result["MaxStretch"] = q.MaxStretch;
  result["MeanStretchError"] = q.MeanStretchError;
  result["MaxStretchError"] = q.MaxStretchError;
  result["BoundaryLoopCount"] = q.BoundaryLoopCount;
  result["BoundaryEdgeCount"] = q.BoundaryEdgeCount;
  result["SkippedBoundaryEdgeCount"] = q.SkippedBoundaryEdgeCount;
  result["MeanBoundaryLengthRatio"] = q.MeanBoundaryLengthRatio;
  result["MinBoundaryLengthRatio"] = q.MinBoundaryLengthRatio;
  result["MaxBoundaryLengthRatio"] = q.MaxBoundaryLengthRatio;
  result["MeanBoundaryLengthDistortion"] = q.MeanBoundaryLengthDistortion;
  result["MaxBoundaryLengthDistortion"] = q.MaxBoundaryLengthDistortion;
  result["SeamDiscontinuityCount"] = q.SeamDiscontinuityCount;
  result["MeanSeamDiscontinuity"] = q.MeanSeamDiscontinuity;
  result["MaxSeamDiscontinuity"] = q.MaxSeamDiscontinuity;
  return result;
}

Json Snapshot(const Geometry::UvAtlas::UvAtlasResult& atlas) {
  Json result{
      {"status", atlas.Status}, {"provenance", atlas.Provenance},
      {"source_vertices", atlas.SourceVertexForOutputVertex},
      {"source_faces", atlas.SourceFaceForOutputFace},
      {"face_charts", atlas.OutputFaceChart}};
  const auto& d = atlas.Diagnostics;
  Json diagnostics = Json::object();
  diagnostics["Status"] = d.Status;
  diagnostics["Provenance"] = d.Provenance;
  diagnostics["RequestedMethod"] = d.RequestedMethod;
  diagnostics["ActualMethod"] = d.ActualMethod;
  diagnostics["BackendName"] = d.BackendName;
  diagnostics["BackendDetail"] = d.BackendDetail;
  diagnostics["UsedFallback"] = d.UsedFallback;
  diagnostics["FallbackReason"] = d.FallbackReason;
  diagnostics["InputVertexCount"] = d.InputVertexCount;
  diagnostics["InputFaceCount"] = d.InputFaceCount;
  diagnostics["OutputVertexCount"] = d.OutputVertexCount;
  diagnostics["OutputFaceCount"] = d.OutputFaceCount;
  diagnostics["NonTriangleFaceCount"] = d.NonTriangleFaceCount;
  diagnostics["OutOfRangeIndexCount"] = d.OutOfRangeIndexCount;
  diagnostics["NonFinitePositionCount"] = d.NonFinitePositionCount;
  diagnostics["NonFiniteAuthoredUvCount"] = d.NonFiniteAuthoredUvCount;
  diagnostics["DegenerateFaceCount"] = d.DegenerateFaceCount;
  diagnostics["PreservedAuthoredUvCount"] = d.PreservedAuthoredUvCount;
  diagnostics["CopiedVertexPropertyCount"] = d.CopiedVertexPropertyCount;
  diagnostics["SkippedVertexPropertyCount"] = d.SkippedVertexPropertyCount;
  diagnostics["PropertyXrefOutOfRangeCount"] = d.PropertyXrefOutOfRangeCount;
  diagnostics["ChartCount"] = d.ChartCount;
  diagnostics["SeamCutCount"] = d.SeamCutCount;
  diagnostics["BoundarySeamCount"] = d.BoundarySeamCount;
  diagnostics["AtlasWidth"] = d.AtlasWidth;
  diagnostics["AtlasHeight"] = d.AtlasHeight;
  diagnostics["AtlasCount"] = d.AtlasCount;
  diagnostics["TexelsPerUnit"] = d.TexelsPerUnit;
  diagnostics["uv_min"] = {d.NormalizedUvMin.x, d.NormalizedUvMin.y};
  diagnostics["uv_max"] = {d.NormalizedUvMax.x, d.NormalizedUvMax.y};
  diagnostics["quality"] = QualityRecord(d.Quality);
  result["diagnostics"] = std::move(diagnostics);
  for (const auto& c : atlas.Charts) {
    result["charts"].push_back({
        c.ChartId, c.SourceFaceStart, c.SourceFaceCount, c.OutputFaceStart,
        c.OutputFaceCount, c.OutputVertexStart, c.OutputVertexCount,
        c.UvMin.x, c.UvMin.y, c.UvMax.x, c.UvMax.y,
        c.ParameterizationBackend, QualityRecord(c.Quality)});
  }
  for (const auto& s : atlas.SeamCuts)
    result["seams"].push_back({s.SourceVertexA, s.SourceVertexB,
        s.SourceFaceA, s.SourceFaceB, s.ChartA, s.ChartB, s.Boundary});
  const auto input = Geometry::UvAtlas::BorrowInput(atlas.OutputMesh);
  const auto ids = atlas.OutputMesh.GetVertexProperty<std::uint32_t>("v:source_id");
  for (const auto& p : input.Positions)
    result["positions"].push_back({p.x, p.y, p.z});
  for (const auto& f : input.Faces) result["faces"].push_back(f.Indices);
  const auto uvs = atlas.OutputMesh.GetVertexProperty<glm::vec2>("v:texcoord");
  if (uvs.IsValid())
    for (const auto uv : uvs.Vector()) result["uvs"].push_back({uv.x, uv.y});
  if (ids.IsValid()) result["copied_source_ids"] = ids.Vector();
  return result;
}

bool Valid(const Geometry::UvAtlas::UvAtlasResult& atlas) {
  const auto uvs = atlas.OutputMesh.GetVertexProperty<glm::vec2>("v:texcoord");
  if (!atlas.Succeeded() || atlas.Diagnostics.UsedFallback ||
      atlas.Diagnostics.ActualMethod != Geometry::UvAtlas::UvAtlasMethod::FastStaged ||
      atlas.OutputMesh.FaceCount() != kCharts ||
      atlas.OutputMesh.VertexCount() != 3u * kCharts ||
      atlas.Diagnostics.ChartCount != kCharts ||
      !uvs.IsValid() || uvs.Vector().size() != 3u * kCharts)
    return false;
  for (const auto uv : uvs.Vector())
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y) ||
        uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
      return false;
  return true;
}
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: IntrinsicUvAtlasRemapSmoke <result.json>\n";
    return 2;
  }
  const std::filesystem::path output{argv[1]};
  if (!output.parent_path().empty())
    std::filesystem::create_directories(output.parent_path());
  auto mesh = MakeMesh();
  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.AllowXAtlasFallback = false;
  options.Resolution = 1024u;
  Json reference;
  std::vector<double> samples;
  std::vector<std::uint64_t> allocationSamples;
  bool valid = true;
  for (std::size_t run = 0u; run <= kMeasuredRuns; ++run) {
    g_AllocatedBytes = 0u;
    g_CountAllocations = true;
    const auto begin = std::chrono::steady_clock::now();
    const auto atlas = Geometry::UvAtlas::ResolveUvAtlas(
        Geometry::UvAtlas::BorrowInput(mesh), options);
    const auto end = std::chrono::steady_clock::now();
    g_CountAllocations = false;
    const auto allocated = g_AllocatedBytes;
    valid = Valid(atlas) && valid;
    Json snapshot = Snapshot(atlas);
    if (run == 0u) reference = std::move(snapshot);
    else {
      valid = (snapshot == reference) && valid;
      samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
      allocationSamples.push_back(allocated);
    }
  }
  Json fixtureSnapshots = Json::array();
  for (const auto& fixture : ExistingFixtures()) {
    const auto first = Geometry::UvAtlas::ResolveUvAtlas(
        Geometry::UvAtlas::BorrowInput(fixture), options);
    const auto second = Geometry::UvAtlas::ResolveUvAtlas(
        Geometry::UvAtlas::BorrowInput(fixture), options);
    valid = first.Succeeded() && second.Succeeded() &&
            Snapshot(first) == Snapshot(second) && valid;
    fixtureSnapshots.push_back(Snapshot(first));
  }
  auto sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  const auto maxBytes = *std::max_element(allocationSamples.begin(), allocationSamples.end());
  const bool passed = valid && maxBytes <= kAllocationBudget;
  Json result{
      {"benchmark_id", "geometry.uv_atlas.fast_staged_remap.smoke"},
      {"method", "geometry.uv_atlas.fast_staged_remap"},
      {"backend", "cpu_optimized"},
      {"dataset", "builtin.uv_atlas_sparse_disconnected_triangles_v1"},
      {"commit", "local-dev"},
      {"metrics", {{"runtime_ms", sorted[kMeasuredRuns / 2u]},
                   {"quality_error_l2", valid ? 0.0 : 1.0}}},
      {"diagnostics", {
          {"runner", "IntrinsicUvAtlasRemapSmoke"},
          {"product_evidence_owner", "BENCH-001"},
          {"timed_scope", "resident_input_uv_enrichment_only"},
          {"adoption_claim", false}, {"warmup_iterations", 1},
          {"measured_iterations", kMeasuredRuns},
          {"runtime_samples_ms", samples},
          {"unaligned_new_bytes_samples", allocationSamples},
          {"unaligned_new_bytes_budget", kAllocationBudget},
          {"allocation_budget_passed", maxBytes <= kAllocationBudget},
          {"allocation_scope", "synchronous ResolveUvAtlas only; excludes fixture, aligned/native allocation, snapshot and output IO"},
          {"input_vertex_count", kVertices}, {"input_face_count", kCharts},
          {"output_vertex_count", reference["diagnostics"]["OutputVertexCount"]},
          {"output_face_count", reference["diagnostics"]["OutputFaceCount"]},
          {"requested_backend", "fast-staged"},
          {"actual_backend", reference["diagnostics"]["BackendName"]},
          {"fallback_observed", reference["diagnostics"]["UsedFallback"]},
          {"finite_complete_deterministic", valid},
          {"atlas_snapshot", output.filename().string() + ".snapshot"}
      }},
      {"status", passed ? "passed" : "failed"}};
  std::ofstream file(output);
  file << result.dump(2) << '\n';
  std::ofstream snapshotFile(output.string() + ".snapshot");
  snapshotFile << Json{{"many_chart", reference}, {"square_cube_nonmanifold", fixtureSnapshots}}.dump(2) << '\n';
  std::cout << "atlas allocation bytes: " << maxBytes
            << "; budget: " << kAllocationBudget
            << "; valid: " << valid << '\n';
  return passed && file.good() && snapshotFile.good() ? 0 : 1;
}
