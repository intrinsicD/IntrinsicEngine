#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <glm/glm.hpp>

import Geometry.IntersectionClassification;
import Geometry.RobustPredicates;

namespace
{
    namespace IX = Geometry::Intersection;
    namespace RP = Geometry::RobustPredicates;
}

// -----------------------------------------------------------------------------
// Enum stability — pin numeric values so persisted diagnostics stay readable.
// -----------------------------------------------------------------------------

TEST(IntersectionClassificationEnums, KindValuesArePinned)
{
    EXPECT_EQ(static_cast<std::uint8_t>(IX::Kind::None), 0u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::Kind::Proper), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::Kind::Touching), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::Kind::Overlap), 3u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::Kind::Coplanar), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::Kind::Coincident), 5u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::Kind::DegenerateInput), 6u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::Kind::Uncertain), 7u);
}

TEST(IntersectionClassificationEnums, SegmentFeatureValuesArePinned)
{
    EXPECT_EQ(static_cast<std::uint8_t>(IX::SegmentFeature::None), 0u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::SegmentFeature::Start), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::SegmentFeature::End), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::SegmentFeature::Interior), 3u);
}

TEST(IntersectionClassificationEnums, RayFeatureValuesArePinned)
{
    EXPECT_EQ(static_cast<std::uint8_t>(IX::RayFeature::None), 0u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::RayFeature::Origin), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::RayFeature::Interior), 2u);
}

TEST(IntersectionClassificationEnums, TriangleFeatureValuesArePinned)
{
    EXPECT_EQ(static_cast<std::uint8_t>(IX::TriangleFeature::None), 0u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::TriangleFeature::VertexA), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::TriangleFeature::VertexB), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::TriangleFeature::VertexC), 3u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::TriangleFeature::EdgeAB), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::TriangleFeature::EdgeBC), 5u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::TriangleFeature::EdgeCA), 6u);
    EXPECT_EQ(static_cast<std::uint8_t>(IX::TriangleFeature::Interior), 7u);
}

// -----------------------------------------------------------------------------
// Default construction — every record defaults to Uncertain with unspecified
// scalar fields so callers cannot accidentally consume an unwritten record.
// -----------------------------------------------------------------------------

TEST(IntersectionClassificationDefaults, AllRecordsDefaultToUncertain)
{
    IX::SegmentSegmentResult ss{};
    IX::SegmentTriangleResult st{};
    IX::RayTriangleResult rt{};
    IX::TriangleTriangleResult tt{};
    IX::PointTriangleResult pt{};

    EXPECT_EQ(ss.Kind, IX::Kind::Uncertain);
    EXPECT_EQ(st.Kind, IX::Kind::Uncertain);
    EXPECT_EQ(rt.Kind, IX::Kind::Uncertain);
    EXPECT_EQ(tt.Kind, IX::Kind::Uncertain);
    EXPECT_EQ(pt.Kind, IX::Kind::Uncertain);
}

TEST(IntersectionClassificationDefaults, ScalarFieldsAreUnspecified)
{
    IX::SegmentSegmentResult ss{};
    EXPECT_TRUE(std::isnan(ss.ParamA));
    EXPECT_TRUE(std::isnan(ss.ParamB));
    EXPECT_TRUE(std::isnan(ss.OverlapStart));
    EXPECT_TRUE(std::isnan(ss.OverlapEnd));

    IX::SegmentTriangleResult st{};
    EXPECT_TRUE(std::isnan(st.SegmentParam));
    EXPECT_TRUE(std::isnan(st.WA));
    EXPECT_TRUE(std::isnan(st.WB));
    EXPECT_TRUE(std::isnan(st.WC));
    EXPECT_TRUE(std::isnan(st.OverlapStart));
    EXPECT_TRUE(std::isnan(st.OverlapEnd));

    IX::RayTriangleResult rt{};
    EXPECT_TRUE(std::isnan(rt.RayParam));
    EXPECT_TRUE(std::isnan(rt.WA));
    EXPECT_TRUE(std::isnan(rt.WB));
    EXPECT_TRUE(std::isnan(rt.WC));

    IX::PointTriangleResult pt{};
    EXPECT_TRUE(std::isnan(pt.SignedPlaneDistance));
    EXPECT_TRUE(std::isnan(pt.WA));
    EXPECT_TRUE(std::isnan(pt.WB));
    EXPECT_TRUE(std::isnan(pt.WC));
}

TEST(IntersectionClassificationDefaults, FeatureFieldsAreNone)
{
    IX::SegmentSegmentResult ss{};
    EXPECT_EQ(ss.OnA, IX::SegmentFeature::None);
    EXPECT_EQ(ss.OnB, IX::SegmentFeature::None);

    IX::SegmentTriangleResult st{};
    EXPECT_EQ(st.OnSegment, IX::SegmentFeature::None);
    EXPECT_EQ(st.OnTriangle, IX::TriangleFeature::None);

    IX::RayTriangleResult rt{};
    EXPECT_EQ(rt.OnRay, IX::RayFeature::None);
    EXPECT_EQ(rt.OnTriangle, IX::TriangleFeature::None);

    IX::TriangleTriangleResult tt{};
    EXPECT_EQ(tt.OnA, IX::TriangleFeature::None);
    EXPECT_EQ(tt.OnB, IX::TriangleFeature::None);
    EXPECT_FALSE(tt.IsCoplanar);

    IX::PointTriangleResult pt{};
    EXPECT_EQ(pt.Feature, IX::TriangleFeature::None);
    EXPECT_EQ(pt.PlaneSide, RP::Sign::Zero);
    EXPECT_EQ(pt.PlaneSideCertainty, RP::Certainty::Uncertain);
}

// -----------------------------------------------------------------------------
// Record trivial-construction guarantees — these records are POD-like data
// envelopes; preserving trivial copyability protects downstream callers
// (benchmarks, serialization, etc.) against accidental ABI churn.
// -----------------------------------------------------------------------------

TEST(IntersectionClassificationRecords, RecordsAreTriviallyCopyable)
{
    EXPECT_TRUE(std::is_trivially_copyable_v<IX::SegmentSegmentResult>);
    EXPECT_TRUE(std::is_trivially_copyable_v<IX::SegmentTriangleResult>);
    EXPECT_TRUE(std::is_trivially_copyable_v<IX::RayTriangleResult>);
    EXPECT_TRUE(std::is_trivially_copyable_v<IX::TriangleTriangleResult>);
    EXPECT_TRUE(std::is_trivially_copyable_v<IX::PointTriangleResult>);
}

// -----------------------------------------------------------------------------
// HasIntersection / IsAmbiguous helpers.
// -----------------------------------------------------------------------------

TEST(IntersectionClassificationHelpers, HasIntersectionMatchesContract)
{
    EXPECT_TRUE(IX::HasIntersection(IX::Kind::Proper));
    EXPECT_TRUE(IX::HasIntersection(IX::Kind::Touching));
    EXPECT_TRUE(IX::HasIntersection(IX::Kind::Overlap));
    EXPECT_TRUE(IX::HasIntersection(IX::Kind::Coplanar));
    EXPECT_TRUE(IX::HasIntersection(IX::Kind::Coincident));

    EXPECT_FALSE(IX::HasIntersection(IX::Kind::None));
    EXPECT_FALSE(IX::HasIntersection(IX::Kind::DegenerateInput));
    EXPECT_FALSE(IX::HasIntersection(IX::Kind::Uncertain));
}

TEST(IntersectionClassificationHelpers, IsAmbiguousIsOnlyTrueForUncertain)
{
    EXPECT_TRUE(IX::IsAmbiguous(IX::Kind::Uncertain));
    EXPECT_FALSE(IX::IsAmbiguous(IX::Kind::None));
    EXPECT_FALSE(IX::IsAmbiguous(IX::Kind::Proper));
    EXPECT_FALSE(IX::IsAmbiguous(IX::Kind::Touching));
    EXPECT_FALSE(IX::IsAmbiguous(IX::Kind::Overlap));
    EXPECT_FALSE(IX::IsAmbiguous(IX::Kind::Coplanar));
    EXPECT_FALSE(IX::IsAmbiguous(IX::Kind::Coincident));
    EXPECT_FALSE(IX::IsAmbiguous(IX::Kind::DegenerateInput));
}

// -----------------------------------------------------------------------------
// TriangleFeatureFromBarycentric — bridges RobustPredicates → intersection
// records without re-encoding the mapping at every callsite.
// -----------------------------------------------------------------------------

TEST(IntersectionClassificationHelpers, TriangleFeatureFromBarycentricMapsVerticesAndEdges)
{
    using Reg = RP::BarycentricRegion;
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::VertexA), IX::TriangleFeature::VertexA);
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::VertexB), IX::TriangleFeature::VertexB);
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::VertexC), IX::TriangleFeature::VertexC);
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::EdgeAB), IX::TriangleFeature::EdgeAB);
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::EdgeBC), IX::TriangleFeature::EdgeBC);
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::EdgeCA), IX::TriangleFeature::EdgeCA);
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::Interior), IX::TriangleFeature::Interior);
}

TEST(IntersectionClassificationHelpers, TriangleFeatureFromBarycentricMapsOutOfTriangleToNone)
{
    using Reg = RP::BarycentricRegion;
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::Outside), IX::TriangleFeature::None);
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::Degenerate), IX::TriangleFeature::None);
    EXPECT_EQ(IX::TriangleFeatureFromBarycentric(Reg::Uncertain), IX::TriangleFeature::None);
}

// -----------------------------------------------------------------------------
// Caller-construction smoke: records can be populated from a hand-rolled
// computation and round-trip through the helpers without losing diagnostics.
// This exercises the records as a usable target for future Slice 3 callsite
// adoption.
// -----------------------------------------------------------------------------

TEST(IntersectionClassificationCallerUse, ProperSegmentSegmentRoundTrip)
{
    IX::SegmentSegmentResult r{};
    r.Kind = IX::Kind::Proper;
    r.OnA = IX::SegmentFeature::Interior;
    r.OnB = IX::SegmentFeature::Interior;
    r.ParamA = 0.5;
    r.ParamB = 0.25;
    r.Point = glm::vec3{0.5f, 0.5f, 0.0f};

    EXPECT_TRUE(IX::HasIntersection(r.Kind));
    EXPECT_FALSE(IX::IsAmbiguous(r.Kind));
    EXPECT_DOUBLE_EQ(r.ParamA, 0.5);
    EXPECT_DOUBLE_EQ(r.ParamB, 0.25);
    EXPECT_FLOAT_EQ(r.Point.x, 0.5f);
    EXPECT_TRUE(std::isnan(r.OverlapStart));
    EXPECT_TRUE(std::isnan(r.OverlapEnd));
}

TEST(IntersectionClassificationCallerUse, UncertainPointTrianglePropagatesPredicateDiagnostics)
{
    IX::PointTriangleResult r{};
    r.PlaneSide = RP::Sign::Positive;
    r.PlaneSideCertainty = RP::Certainty::Uncertain;
    r.SignedPlaneDistance = 1.0e-30;

    EXPECT_EQ(r.Kind, IX::Kind::Uncertain);
    EXPECT_EQ(r.Feature, IX::TriangleFeature::None);
    EXPECT_EQ(r.PlaneSide, RP::Sign::Positive);
    EXPECT_EQ(r.PlaneSideCertainty, RP::Certainty::Uncertain);
    EXPECT_TRUE(IX::IsAmbiguous(r.Kind));
}

