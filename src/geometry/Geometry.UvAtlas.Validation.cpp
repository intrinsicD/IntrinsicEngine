module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Geometry.UvAtlas.Validation;

import Geometry.AABB;
import Geometry.BVH;
import Geometry.MeshSoup;
import Geometry.UvAtlas.Types;

namespace Geometry::UvAtlas {
namespace {
constexpr double kRelativeDegenerateEpsilon = 1.0e-12;
// Relative slack for float round-off on bounds that are met exactly by
// construction (for example an isometric single triangle at bound 1).
constexpr double kLimitTolerance = 1.0e-6;

[[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

// True when a pixel center (i + 0.5, j + 0.5) inside the width x height
// image lies strictly inside the positively oriented pixel-space triangle.
// Centers exactly on an edge are not counted, so the answer never claims a
// center that a top-left-rule rasterizer could leave uncovered. Work is
// bounded by the triangle's row span clipped to the image.
[[nodiscard]] bool CoversPixelCenter(const std::array<glm::dvec2, 3u> &w,
                                     const double width, const double height) {
  const double yMin = std::min({w[0].y, w[1].y, w[2].y});
  const double yMax = std::max({w[0].y, w[1].y, w[2].y});
  const auto strictlyInside = [&w](const glm::dvec2 c) {
    for (std::size_t k = 0u; k < 3u; ++k) {
      const glm::dvec2 a = w[k];
      const glm::dvec2 b = w[(k + 1u) % 3u];
      if (!((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0.0)) {
        return false;
      }
    }
    return true;
  };
  // Row j has center j + 0.5; start at the first center above yMin.
  const double rowBegin = std::max(0.0, std::floor(yMin - 0.5) + 1.0);
  const double rowEnd = std::min(height, std::ceil(yMax - 0.5));
  for (double row = rowBegin; row < rowEnd; row += 1.0) {
    const double y = row + 0.5;
    double left = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    for (std::size_t k = 0u; k < 3u; ++k) {
      const glm::dvec2 a = w[k];
      const glm::dvec2 b = w[(k + 1u) % 3u];
      if ((a.y < y) != (b.y < y)) {
        const double x = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
        left = std::min(left, x);
        right = std::max(right, x);
      }
    }
    // First center right of `left`, plus one more against round-off.
    const double column = std::max(0.0, std::floor(left - 0.5) + 1.0);
    for (double c = column; c < std::min(column + 2.0, width); c += 1.0) {
      if (c + 0.5 >= right) {
        break;
      }
      if (strictlyInside(glm::dvec2{c + 0.5, y})) {
        return true;
      }
    }
  }
  return false;
}

// Knuth two-sum: a + b == sum + error exactly under IEEE round-to-nearest.
void TwoSum(const double a, const double b, double &sum,
            double &error) noexcept {
  sum = a + b;
  const double bVirtual = sum - a;
  const double aVirtual = sum - bVirtual;
  error = (a - aVirtual) + (b - bVirtual);
}

// Two triangles with positive orientation have disjoint open interiors iff
// the supporting line of one of their six edges weakly separates them (the
// edge normals are the face normals of the Minkowski difference).
[[nodiscard]] bool EdgeSeparates(const std::array<glm::vec2, 3u> &owner,
                                 const std::array<glm::vec2, 3u> &other) {
  for (std::size_t edge = 0u; edge < 3u; ++edge) {
    const glm::vec2 p = owner[edge];
    const glm::vec2 q = owner[(edge + 1u) % 3u];
    bool allOutside = true;
    for (const glm::vec2 point : other) {
      if (ExactUvOrientation(p, q, point) > 0) {
        allOutside = false;
        break;
      }
    }
    if (allOutside) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool InteriorsOverlap(const std::array<glm::vec2, 3u> &a,
                                    const std::array<glm::vec2, 3u> &b) {
  return !EdgeSeparates(a, b) && !EdgeSeparates(b, a);
}

struct SingularValues {
  double Max{0.0};
  double Min{0.0};
};

// Closed-form singular values of a 2x2 matrix [[a, b], [c, d]].
[[nodiscard]] SingularValues Singular2x2(const double a, const double b,
                                         const double c,
                                         const double d) noexcept {
  const double e = 0.5 * (a + d);
  const double f = 0.5 * (a - d);
  const double g = 0.5 * (c + b);
  const double h = 0.5 * (c - b);
  const double q = std::sqrt(e * e + h * h);
  const double r = std::sqrt(f * f + g * g);
  return SingularValues{.Max = q + r, .Min = std::abs(q - r)};
}

// Union-find over source faces, used for region components.
struct DisjointSet {
  std::vector<std::uint32_t> Parent{};

  explicit DisjointSet(const std::size_t count) : Parent(count) {
    for (std::size_t i = 0u; i < count; ++i) {
      Parent[i] = static_cast<std::uint32_t>(i);
    }
  }

  [[nodiscard]] std::uint32_t Find(std::uint32_t value) {
    while (Parent[value] != value) {
      Parent[value] = Parent[Parent[value]];
      value = Parent[value];
    }
    return value;
  }

  void Unite(const std::uint32_t a, const std::uint32_t b) {
    const std::uint32_t rootA = Find(a);
    const std::uint32_t rootB = Find(b);
    if (rootA != rootB) {
      Parent[std::max(rootA, rootB)] = std::min(rootA, rootB);
    }
  }
};

void Fail(UvAtlasValidationReport &report, const UvAtlasStatus status,
          std::string detail) {
  // Keep the first failure of the most severe class.
  const auto rank = [](const UvAtlasStatus value) {
    switch (value) {
    case UvAtlasStatus::ValidationFailed:
      return 3;
    case UvAtlasStatus::QualityLimitNotMet:
      return 2;
    case UvAtlasStatus::UnderResolved:
      return 1;
    default:
      return 0;
    }
  };
  if (report.Status == UvAtlasStatus::Success ||
      rank(status) > rank(report.Status)) {
    report.Status = status;
    report.Detail = std::move(detail);
  }
}
} // namespace

int ExactUvOrientation(const glm::vec2 a, const glm::vec2 b,
                       const glm::vec2 c) noexcept {
  // (b-a) x (c-a) expanded into six float products, each exact in double.
  const std::array<double, 6u> terms{
      static_cast<double>(b.x) * static_cast<double>(c.y),
      -(static_cast<double>(b.x) * static_cast<double>(a.y)),
      -(static_cast<double>(a.x) * static_cast<double>(c.y)),
      -(static_cast<double>(b.y) * static_cast<double>(c.x)),
      static_cast<double>(a.x) * static_cast<double>(b.y),
      static_cast<double>(c.x) * static_cast<double>(a.y),
  };
  // Shewchuk grow-expansion: components stay nonoverlapping and ordered by
  // magnitude, so the most significant nonzero component carries the sign.
  std::array<double, 6u> expansion{};
  std::size_t length = 0u;
  for (const double term : terms) {
    double carry = term;
    for (std::size_t i = 0u; i < length; ++i) {
      double sum = 0.0;
      double error = 0.0;
      TwoSum(carry, expansion[i], sum, error);
      expansion[i] = error;
      carry = sum;
    }
    expansion[length++] = carry;
  }
  for (std::size_t i = length; i > 0u; --i) {
    if (expansion[i - 1u] > 0.0) {
      return 1;
    }
    if (expansion[i - 1u] < 0.0) {
      return -1;
    }
  }
  return 0;
}

bool IsDegenerateAtlasTriangle(const glm::vec3 p0, const glm::vec3 p1,
                               const glm::vec3 p2) noexcept {
  if (!IsFinite(p0) || !IsFinite(p1) || !IsFinite(p2)) {
    return true;
  }
  const glm::dvec3 a{p0};
  const glm::dvec3 b{p1};
  const glm::dvec3 c{p2};
  const double doubleArea = glm::length(glm::cross(b - a, c - a));
  const double longestSq =
      std::max({glm::dot(b - a, b - a), glm::dot(c - b, c - b),
                glm::dot(a - c, a - c)});
  return !(longestSq > 0.0) ||
         !(doubleArea > kRelativeDegenerateEpsilon * longestSq);
}

UvFaceDistortion MeasureUvFaceDistortion(const glm::dvec3 p0,
                                         const glm::dvec3 p1,
                                         const glm::dvec3 p2,
                                         const glm::dvec2 w0,
                                         const glm::dvec2 w1,
                                         const glm::dvec2 w2) noexcept {
  UvFaceDistortion metric{};
  const glm::dvec3 e1 = p1 - p0;
  const glm::dvec3 e2 = p2 - p0;
  const glm::dvec3 normal = glm::cross(e1, e2);
  const double doubleArea = glm::length(normal);
  const double e1Length = glm::length(e1);
  if (!(doubleArea > 0.0) || !(e1Length > 0.0)) {
    return metric;
  }
  const glm::dvec3 xAxis = e1 / e1Length;
  const glm::dvec3 yAxis = glm::cross(normal / doubleArea, xAxis);
  // Local reference triangle (0,0), (x1,0), (x2,y2) with y2 > 0.
  const double x1 = e1Length;
  const double x2 = glm::dot(e2, xAxis);
  const double y2 = glm::dot(e2, yAxis);
  if (!(y2 > 0.0)) {
    return metric;
  }
  const glm::dvec2 d1 = w1 - w0;
  const glm::dvec2 d2 = w2 - w0;
  // J = [d1 d2] * inverse([[x1, x2], [0, y2]]).
  const double j00 = d1.x / x1;
  const double j10 = d1.y / x1;
  const double j01 = (d2.x - j00 * x2) / y2;
  const double j11 = (d2.y - j10 * x2) / y2;
  const SingularValues sigma = Singular2x2(j00, j01, j10, j11);
  metric.SurfaceArea = 0.5 * doubleArea;
  metric.UvArea = 0.5 * (d1.x * d2.y - d1.y * d2.x);
  metric.SigmaMax = sigma.Max;
  metric.SigmaMin = sigma.Min;
  metric.Valid = std::isfinite(sigma.Max) && std::isfinite(sigma.Min) &&
                 sigma.Min > 0.0 && metric.UvArea > 0.0 &&
                 std::isfinite(metric.UvArea);
  return metric;
}

UvTriangleOverlapReport
FindUvTriangleOverlaps(const std::span<const glm::vec2> corners,
                       const std::size_t maxReportedOverlaps) {
  UvTriangleOverlapReport report{};
  report.TriangleCount = corners.size() / 3u;
  if (report.TriangleCount < 2u) {
    return report;
  }

  std::vector<std::uint8_t> usable(report.TriangleCount, 0u);
  std::vector<AABB> bounds(report.TriangleCount);
  for (std::size_t t = 0u; t < report.TriangleCount; ++t) {
    const glm::vec2 a = corners[3u * t];
    const glm::vec2 b = corners[3u * t + 1u];
    const glm::vec2 c = corners[3u * t + 2u];
    if (!IsFinite(a) || !IsFinite(b) || !IsFinite(c) ||
        ExactUvOrientation(a, b, c) <= 0) {
      // Unusable triangles get an empty point box far from any query.
      bounds[t].Min = bounds[t].Max = glm::vec3{0.0f, 0.0f, 1.0f};
      continue;
    }
    usable[t] = 1u;
    const glm::vec2 lo = glm::min(a, glm::min(b, c));
    const glm::vec2 hi = glm::max(a, glm::max(b, c));
    bounds[t].Min = glm::vec3{lo, 0.0f};
    bounds[t].Max = glm::vec3{hi, 0.0f};
  }

  BVH bvh;
  if (!bvh.Build(std::span<const AABB>{bounds})) {
    return report;
  }

  std::vector<BVH::ElementIndex> candidates;
  for (std::size_t t = 0u; t < report.TriangleCount; ++t) {
    if (usable[t] == 0u) {
      continue;
    }
    bvh.QueryAABB(bounds[t], candidates);
    const std::array<glm::vec2, 3u> a{corners[3u * t], corners[3u * t + 1u],
                                      corners[3u * t + 2u]};
    for (const BVH::ElementIndex other : candidates) {
      if (other <= t || usable[other] == 0u) {
        continue;
      }
      ++report.CandidatePairCount;
      const std::array<glm::vec2, 3u> b{corners[3u * other],
                                        corners[3u * other + 1u],
                                        corners[3u * other + 2u]};
      if (!InteriorsOverlap(a, b)) {
        continue;
      }
      if (report.OverlapPairCount == 0u) {
        report.FirstOverlapA = static_cast<std::uint32_t>(t);
        report.FirstOverlapB = other;
      }
      ++report.OverlapPairCount;
      if (report.OverlapPairCount >= std::max<std::size_t>(
                                         1u, maxReportedOverlaps)) {
        report.OverlapCountSaturated = true;
        return report;
      }
    }
  }
  return report;
}

UvAtlasValidationReport
ValidateUvAtlasCorners(const UvAtlasValidationInput &input,
                       const UvAtlasValidationOptions &options) {
  UvAtlasValidationReport report{};
  report.Evaluated = true;
  report.Status = UvAtlasStatus::Success;
  report.FaceCount = input.Faces.size();
  const std::size_t faceCount = input.Faces.size();

  if (faceCount == 0u) {
    Fail(report, UvAtlasStatus::ValidationFailed, "atlas has no faces");
    return report;
  }
  if (input.CornerUvs.size() != faceCount * 3u) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         "corner UV count does not equal three per source face");
    return report;
  }
  if (!input.FaceRegions.empty() && input.FaceRegions.size() != faceCount) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         "region label count does not equal source face count");
    return report;
  }
  if (!input.FaceCharts.empty() && input.FaceCharts.size() != faceCount) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         "chart id count does not equal source face count");
    return report;
  }
  if (!std::isfinite(options.MaxConformalDistortion) ||
      !std::isfinite(options.MaxAreaDistortion) ||
      options.MaxConformalDistortion < 1.0 || options.MaxAreaDistortion < 1.0) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         "distortion limits must be finite and at least 1");
    return report;
  }

  // Source faces and UV finiteness/bounds/orientation.
  std::vector<std::uint8_t> faceUsable(faceCount, 0u);
  for (std::size_t f = 0u; f < faceCount; ++f) {
    const MeshSoup::PolygonFace &face = input.Faces[f];
    bool sourceValid = face.Indices.size() == 3u;
    if (sourceValid) {
      for (const MeshSoup::Index index : face.Indices) {
        sourceValid = sourceValid && index < input.Positions.size();
      }
    }
    if (sourceValid) {
      sourceValid = !IsDegenerateAtlasTriangle(
          input.Positions[face.Indices[0]], input.Positions[face.Indices[1]],
          input.Positions[face.Indices[2]]);
    }
    if (!sourceValid) {
      ++report.InvalidSourceFaceCount;
    }

    bool uvValid = true;
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const glm::vec2 uv = input.CornerUvs[3u * f + corner];
      if (!IsFinite(uv)) {
        ++report.NonFiniteUvCount;
        uvValid = false;
      } else if (uv.x < 0.0f || uv.y < 0.0f || uv.x > 1.0f || uv.y > 1.0f) {
        ++report.OutOfBoundsUvCount;
      }
    }
    const int orientation =
        uvValid ? ExactUvOrientation(input.CornerUvs[3u * f],
                                     input.CornerUvs[3u * f + 1u],
                                     input.CornerUvs[3u * f + 2u])
                : 1;
    if (orientation <= 0) {
      if (report.NonPositiveOrientationCount == 0u) {
        report.FirstNonPositiveFace = static_cast<std::uint32_t>(f);
      }
      ++report.NonPositiveOrientationCount;
      ++(orientation == 0 ? report.CollapsedUvTriangleCount
                          : report.FlippedUvTriangleCount);
      uvValid = false;
    }
    faceUsable[f] = sourceValid && uvValid ? 1u : 0u;
  }
  if (report.InvalidSourceFaceCount > 0u) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         std::to_string(report.InvalidSourceFaceCount) +
             " source faces are not valid non-degenerate triangles");
  }
  if (report.NonFiniteUvCount > 0u) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         std::to_string(report.NonFiniteUvCount) + " corner UVs are not finite");
  }
  if (report.OutOfBoundsUvCount > 0u) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         std::to_string(report.OutOfBoundsUvCount) +
             " corner UVs lie outside [0,1]^2");
  }
  if (report.NonPositiveOrientationCount > 0u) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         std::to_string(report.FlippedUvTriangleCount) +
             " UV triangles are flipped and " +
             std::to_string(report.CollapsedUvTriangleCount) +
             " collapse to zero float area (first face " +
             std::to_string(report.FirstNonPositiveFace) + ")");
  }

  // Global overlap across every chart.
  report.Overlaps =
      FindUvTriangleOverlaps(input.CornerUvs, options.MaxReportedOverlaps);
  if (report.Overlaps.OverlapPairCount > 0u) {
    Fail(report, UvAtlasStatus::ValidationFailed,
         std::string{report.Overlaps.OverlapCountSaturated ? "at least " : ""} +
             std::to_string(report.Overlaps.OverlapPairCount) +
             " UV triangle pairs overlap (first faces " +
             std::to_string(report.Overlaps.FirstOverlapA) + " and " +
             std::to_string(report.Overlaps.FirstOverlapB) + ")");
  }

  // Region components: faces sharing a source edge and a label.
  const auto labelOf = [&input](const std::size_t face) -> std::uint32_t {
    return input.FaceRegions.empty() ? 0u : input.FaceRegions[face];
  };
  DisjointSet regions(faceCount);
  {
    std::unordered_map<std::uint64_t, std::uint32_t> firstFaceByEdge;
    firstFaceByEdge.reserve(faceCount * 3u);
    for (std::size_t f = 0u; f < faceCount; ++f) {
      const MeshSoup::PolygonFace &face = input.Faces[f];
      if (face.Indices.size() != 3u) {
        continue;
      }
      for (std::size_t corner = 0u; corner < 3u; ++corner) {
        const std::uint32_t a = face.Indices[corner];
        const std::uint32_t b = face.Indices[(corner + 1u) % 3u];
        const std::uint64_t key =
            (static_cast<std::uint64_t>(std::min(a, b)) << 32u) |
            static_cast<std::uint64_t>(std::max(a, b));
        const auto [it, inserted] =
            firstFaceByEdge.try_emplace(key, static_cast<std::uint32_t>(f));
        if (!inserted && labelOf(it->second) == labelOf(f)) {
          regions.Unite(it->second, static_cast<std::uint32_t>(f));
        }
      }
    }
  }
  {
    std::vector<std::uint32_t> labels;
    labels.reserve(faceCount);
    std::size_t components = 0u;
    for (std::size_t f = 0u; f < faceCount; ++f) {
      labels.push_back(labelOf(f));
      components += regions.Find(static_cast<std::uint32_t>(f)) == f ? 1u : 0u;
    }
    std::sort(labels.begin(), labels.end());
    report.RegionLabelCount = static_cast<std::size_t>(
        std::unique(labels.begin(), labels.end()) - labels.begin());
    report.RegionComponentCount = components;
  }

  std::unordered_map<std::uint32_t, std::uint32_t> chartSlot;
  std::vector<std::uint32_t> chartComponent;
  std::vector<std::uint8_t> chartCrosses;
  std::vector<double> chartPixelArea;
  if (!input.FaceCharts.empty()) {
    for (std::size_t f = 0u; f < faceCount; ++f) {
      const auto [it, inserted] = chartSlot.try_emplace(
          input.FaceCharts[f], static_cast<std::uint32_t>(chartComponent.size()));
      const std::uint32_t component = regions.Find(static_cast<std::uint32_t>(f));
      if (inserted) {
        chartComponent.push_back(component);
        chartCrosses.push_back(0u);
        chartPixelArea.push_back(0.0);
      } else if (chartComponent[it->second] != component) {
        chartCrosses[it->second] = 1u;
      }
    }
    report.ChartCount = chartComponent.size();
    report.RegionCrossingChartCount = static_cast<std::size_t>(
        std::count(chartCrosses.begin(), chartCrosses.end(), 1u));
    if (report.RegionCrossingChartCount > 0u) {
      Fail(report, UvAtlasStatus::ValidationFailed,
           std::to_string(report.RegionCrossingChartCount) +
               " charts cross a region label or region component boundary");
    }
  }

  // Density-normalized distortion in pixel space.
  const bool pixelSpace = options.AtlasWidth > 0u && options.AtlasHeight > 0u;
  const double width = pixelSpace ? static_cast<double>(options.AtlasWidth) : 1.0;
  const double height =
      pixelSpace ? static_cast<double>(options.AtlasHeight) : 1.0;
  std::vector<UvFaceDistortion> metrics(faceCount);
  std::vector<std::array<glm::dvec2, 3u>> pixelCorners(faceCount);
  double totalSurface = 0.0;
  double totalPixel = 0.0;
  for (std::size_t f = 0u; f < faceCount; ++f) {
    if (faceUsable[f] == 0u) {
      continue;
    }
    const MeshSoup::PolygonFace &face = input.Faces[f];
    std::array<glm::dvec3, 3u> p{};
    std::array<glm::dvec2, 3u> &w = pixelCorners[f];
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      p[corner] = glm::dvec3{input.Positions[face.Indices[corner]]};
      const glm::vec2 uv = input.CornerUvs[3u * f + corner];
      w[corner] = glm::dvec2{static_cast<double>(uv.x) * width,
                             static_cast<double>(uv.y) * height};
    }
    metrics[f] = MeasureUvFaceDistortion(p[0], p[1], p[2], w[0], w[1], w[2]);
    if (!metrics[f].Valid) {
      continue;
    }
    totalSurface += metrics[f].SurfaceArea;
    totalPixel += metrics[f].UvArea;
  }

  if (totalSurface > 0.0 && totalPixel > 0.0) {
    const double density = std::sqrt(totalPixel / totalSurface);
    report.TexelsPerUnit = pixelSpace ? density : 0.0;
    double conformalSum = 0.0;
    double areaSum = 0.0;
    double weightSum = 0.0;
    for (std::size_t f = 0u; f < faceCount; ++f) {
      const UvFaceDistortion &metric = metrics[f];
      if (!metric.Valid) {
        continue;
      }
      const double conformal = metric.SigmaMax / metric.SigmaMin;
      const double areaRatio =
          (metric.SigmaMax / density) * (metric.SigmaMin / density);
      const double areaDistortion = std::max(areaRatio, 1.0 / areaRatio);
      conformalSum += metric.SurfaceArea * conformal;
      areaSum += metric.SurfaceArea * areaDistortion;
      weightSum += metric.SurfaceArea;
      if (conformal > report.MaxConformalDistortion) {
        report.MaxConformalDistortion = conformal;
        report.WorstConformalFace = static_cast<std::uint32_t>(f);
      }
      if (areaDistortion > report.MaxAreaDistortion) {
        report.MaxAreaDistortion = areaDistortion;
        report.WorstAreaFace = static_cast<std::uint32_t>(f);
      }
      if (conformal > options.MaxConformalDistortion * (1.0 + kLimitTolerance)) {
        ++report.ConformalLimitViolationCount;
      }
      if (areaDistortion > options.MaxAreaDistortion * (1.0 + kLimitTolerance)) {
        ++report.AreaLimitViolationCount;
      }
      if (pixelSpace && metric.UvArea < 1.0) {
        ++report.SubTexelFaceCount;
      }
      if (!chartSlot.empty()) {
        chartPixelArea[chartSlot.at(input.FaceCharts[f])] += metric.UvArea;
      }
    }
    if (weightSum > 0.0) {
      report.MeanConformalDistortion = conformalSum / weightSum;
      report.MeanAreaDistortion = areaSum / weightSum;
    }
  }
  if (report.ConformalLimitViolationCount > 0u) {
    Fail(report, UvAtlasStatus::QualityLimitNotMet,
         std::to_string(report.ConformalLimitViolationCount) +
             " faces exceed the conformal distortion limit (max " +
             std::to_string(report.MaxConformalDistortion) + " at face " +
             std::to_string(report.WorstConformalFace) + ")");
  }
  if (report.AreaLimitViolationCount > 0u) {
    Fail(report, UvAtlasStatus::QualityLimitNotMet,
         std::to_string(report.AreaLimitViolationCount) +
             " faces exceed the density-normalized area distortion limit (max " +
             std::to_string(report.MaxAreaDistortion) + " at face " +
             std::to_string(report.WorstAreaFace) + ")");
  }

  if (pixelSpace && !chartPixelArea.empty()) {
    report.MinChartTexelArea = std::numeric_limits<double>::max();
    for (const double area : chartPixelArea) {
      report.MinChartTexelArea = std::min(report.MinChartTexelArea, area);
      if (area < options.MinChartTexelArea) {
        ++report.UnderResolvedChartCount;
      }
    }
    if (report.UnderResolvedChartCount > 0u) {
      Fail(report, UvAtlasStatus::UnderResolved,
           std::to_string(report.UnderResolvedChartCount) +
               " charts cover less than " +
               std::to_string(options.MinChartTexelArea) +
               " texels; increase the atlas resolution");
    }

    // Enough area does not guarantee a sampled texel: a thin chart can fall
    // between pixel-center rows or diagonals and bake to nothing.
    std::vector<std::uint8_t> chartHasCenter(chartPixelArea.size(), 0u);
    for (std::size_t f = 0u; f < faceCount; ++f) {
      const std::uint32_t slot = chartSlot.at(input.FaceCharts[f]);
      if (metrics[f].Valid && chartHasCenter[slot] == 0u &&
          CoversPixelCenter(pixelCorners[f], width, height)) {
        chartHasCenter[slot] = 1u;
      }
    }
    report.ChartsWithoutTexelCenterCount = static_cast<std::size_t>(
        std::count(chartHasCenter.begin(), chartHasCenter.end(), 0u));
    if (report.ChartsWithoutTexelCenterCount > 0u) {
      Fail(report, UvAtlasStatus::UnderResolved,
           std::to_string(report.ChartsWithoutTexelCenterCount) +
               " charts contain no texel center; increase the atlas "
               "resolution");
    }
  }
  return report;
}
} // namespace Geometry::UvAtlas
