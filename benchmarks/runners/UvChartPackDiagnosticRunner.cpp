// Packs precomputed UV charts without running segmentation or parameterization.
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <nlohmann/json.hpp>
#include <xatlas.h>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::ifstream input{argv[1]};
  const auto source = nlohmann::json::parse(input, nullptr, false);
  if (source.is_discarded()) return 2;
  const auto uvs = source.at("uvs").get<std::vector<std::array<float, 2>>>();
  const auto indices = source.at("indices").get<std::vector<std::uint32_t>>();
  const auto materials = source.at("materials").get<std::vector<std::uint32_t>>();
  if (indices.size() % 3 || materials.size() != indices.size()/3) return 2;
  const auto start = std::chrono::steady_clock::now();
  auto* atlas = xatlas::Create();
  if (!atlas) return 1;
  xatlas::UvMeshDecl mesh;
  mesh.vertexUvData = uvs.data(); mesh.vertexCount = static_cast<std::uint32_t>(uvs.size());
  mesh.vertexStride = sizeof(uvs[0]); mesh.indexData = indices.data();
  mesh.indexCount = static_cast<std::uint32_t>(indices.size()); mesh.indexFormat = xatlas::IndexFormat::UInt32;
  mesh.faceMaterialData = materials.data();
  if (xatlas::AddUvMesh(atlas, mesh) != xatlas::AddMeshError::Success) {
    xatlas::Destroy(atlas); return 1;
  }
  xatlas::PackOptions options;
  options.resolution = 1024; options.padding = 2;
  // For UvMesh input this identifies supplied UV components; there is no 3D
  // embedding to re-segment or parameterize. The caller verifies chart identity.
  xatlas::ComputeCharts(atlas);
  xatlas::PackCharts(atlas, options);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  if (atlas->meshCount != 1 || atlas->atlasCount != 1 || atlas->width == 0 || atlas->height == 0) {
    xatlas::Destroy(atlas); return 1;
  }
  const auto& output = atlas->meshes[0];
  nlohmann::json result{{"chart_count", atlas->chartCount}, {"runtime_seconds", seconds},
      {"width", atlas->width}, {"height", atlas->height}, {"raster_utilization", atlas->utilization[0]},
      {"implementation", "xatlas_pack_existing_uv_mesh"}, {"resolution", 1024}, {"padding", 2}};
  for (std::uint32_t i = 0; i < output.vertexCount; ++i) {
    result["uvs"].push_back({output.vertexArray[i].uv[0], output.vertexArray[i].uv[1]});
    result["source_vertices"].push_back(output.vertexArray[i].xref);
  }
  for (std::uint32_t i = 0; i < output.indexCount; ++i) result["indices"].push_back(output.indexArray[i]);
  xatlas::Destroy(atlas);
  std::ofstream destination{argv[2]}; destination << result.dump() << '\n';
  return destination.good() ? 0 : 1;
}
