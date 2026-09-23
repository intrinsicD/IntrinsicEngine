// Dumps native atlas results through the public API for offline matched comparisons.
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

import Geometry.UvAtlas;
import Geometry.MeshSoup;
import Geometry.Properties;

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "Usage: IntrinsicUvAtlasMeshDiagnostic input.json output.json xatlas|fast-staged\n";
    return 2;
  }
  const std::string method{argv[3]};
  if (method != "xatlas" && method != "fast-staged") return 2;
  std::ifstream input{argv[1]};
  const auto source = nlohmann::json::parse(input, nullptr, false);
  if (source.is_discarded() || !source.contains("vertices") || !source.contains("faces")) return 2;
  Geometry::MeshSoup::IndexedMesh mesh;
  for (const auto& p : source["vertices"])
    (void)mesh.AddVertex({p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>()});
  for (const auto& f : source["faces"])
    (void)mesh.AddTriangle(f.at(0).get<std::uint32_t>(), f.at(1).get<std::uint32_t>(), f.at(2).get<std::uint32_t>());
  Geometry::UvAtlas::UvAtlasOptions options;
  options.ForceRegenerate = true;
  options.PreserveValidAuthoredUvs = false;
  options.UseAuthoredUvsAsChartHints = false;
  options.AllowXAtlasFallback = false;
  options.Method = method == "xatlas" ? Geometry::UvAtlas::UvAtlasMethod::XAtlas
                                      : Geometry::UvAtlas::UvAtlasMethod::FastStaged;
  options.BackendName = method;
  options.Resolution = source.value("resolution", 1024u);
  options.Padding = source.value("padding", 2u);
  const std::string objective = source.value("distortion", method == "xatlas" ? "angle" : "both");
  const auto distortion = Geometry::UvAtlas::ParseUvAtlasDistortion(objective);
  if (!distortion) return 2;
  options.Distortion = *distortion;
  options.MaxConformalDistortion = source.value("max_conformal_distortion", 10.0);
  options.MaxAreaDistortion = source.value("max_area_distortion", 10.0);
  options.MaxCharts = source.value("max_charts", 16384u);
  options.MaxIterations = source.value("max_iterations", 40u);
  const auto regions = source.value("face_regions", std::vector<std::uint32_t>{});
  auto atlasInput = Geometry::UvAtlas::BorrowInput(mesh);
  atlasInput.FaceRegions = regions;
  const auto start = std::chrono::steady_clock::now();
  const auto atlas = Geometry::UvAtlas::ResolveUvAtlas(atlasInput, options);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  nlohmann::json result{
      {"schema", "intrinsic.native-atlas.diagnostic.v1"}, {"claim_eligible", false},
      {"requested_method", method}, {"actual_method", Geometry::UvAtlas::ToString(atlas.Diagnostics.ActualMethod)},
      {"status", Geometry::UvAtlas::ToString(atlas.Status)}, {"used_fallback", atlas.Diagnostics.UsedFallback},
      {"runtime_seconds", seconds}, {"chart_count", atlas.Diagnostics.ChartCount},
      {"atlas_width", atlas.Diagnostics.AtlasWidth}, {"atlas_height", atlas.Diagnostics.AtlasHeight},
      {"source_vertices", atlas.SourceVertexForOutputVertex},
      {"source_faces", atlas.SourceFaceForOutputFace}, {"face_charts", atlas.OutputFaceChart},
      {"resolution", options.Resolution}, {"padding", options.Padding},
      {"requested_distortion", objective},
      {"actual_distortion", Geometry::UvAtlas::ToString(atlas.Diagnostics.ActualDistortion)},
      {"diagnostic", atlas.Diagnostics.BackendDetail},
      {"fallback_reason", atlas.Diagnostics.FallbackReason},
      {"source_corner_uvs", nlohmann::json::array()},
      {"source_face_charts", atlas.SourceFaceChart},
      {"source_face_regions", atlas.SourceFaceRegionComponent},
      {"validation", {
          {"evaluated", atlas.Diagnostics.Validation.Evaluated},
          {"passed", atlas.Diagnostics.Validation.Passed()},
          {"overlap_pairs", atlas.Diagnostics.Validation.Overlaps.OverlapPairCount},
          {"non_positive_faces", atlas.Diagnostics.Validation.NonPositiveOrientationCount},
          {"region_crossings", atlas.Diagnostics.Validation.RegionCrossingChartCount},
          {"max_conformal_distortion", atlas.Diagnostics.Validation.MaxConformalDistortion},
          {"mean_conformal_distortion", atlas.Diagnostics.Validation.MeanConformalDistortion},
          {"max_area_distortion", atlas.Diagnostics.Validation.MaxAreaDistortion},
          {"mean_area_distortion", atlas.Diagnostics.Validation.MeanAreaDistortion},
          {"underresolved_charts", atlas.Diagnostics.Validation.UnderResolvedChartCount},
          {"charts_without_texel_centers", atlas.Diagnostics.Validation.ChartsWithoutTexelCenterCount},
          {"subtexel_faces", atlas.Diagnostics.Validation.SubTexelFaceCount},
          {"texels_per_unit", atlas.Diagnostics.Validation.TexelsPerUnit},
      }}};
  for (const auto uv : atlas.SourceCornerUvs) result["source_corner_uvs"].push_back({uv.x, uv.y});
  const auto output = Geometry::UvAtlas::BorrowInput(atlas.OutputMesh);
  const auto uvs = atlas.OutputMesh.GetVertexProperty<glm::vec2>("v:texcoord");
  for (const auto& f : output.Faces) result["faces"].push_back(f.Indices);
  if (uvs.IsValid())
    for (const auto uv : uvs.Vector()) result["uvs"].push_back({uv.x, uv.y});
  std::ofstream destination{argv[2]}; destination << result.dump() << '\n';
  std::cout << result["status"] << ' ' << result["chart_count"] << ' ' << seconds << "s\n";
  return atlas.Succeeded() && destination.good() ? 0 : 1;
}
