#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

import Geometry;

namespace
{
	std::vector<glm::vec3> MakeExactSphereSamples(const glm::vec3& center, float radius)
	{
		return {
			center + glm::vec3{ radius, 0.0f, 0.0f},
			center + glm::vec3{-radius, 0.0f, 0.0f},
			center + glm::vec3{0.0f,  radius, 0.0f},
			center + glm::vec3{0.0f, -radius, 0.0f},
			center + glm::vec3{0.0f, 0.0f,  radius},
			center + glm::vec3{0.0f, 0.0f, -radius},
		};
	}

	float DistanceToCenter(const Geometry::Sphere& sphere, const glm::vec3& point)
	{
		return glm::distance(point, sphere.Center);
	}
}

TEST(SphereFitting, ReturnsNulloptForEmptyInput)
{
	const std::vector<glm::vec3> points;
	EXPECT_FALSE(Geometry::ToSphere(points).has_value());
}

TEST(SphereFitting, ReturnsNulloptForOnlyNonFiniteSamples)
{
	const std::vector<glm::vec3> points = {
		{std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
		{0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f},
	};

	EXPECT_FALSE(Geometry::ToSphere(points).has_value());
}

TEST(SphereFitting, FitsDiameterSphereForTwoPoints)
{
	const std::array<glm::vec3, 2> points = {
		glm::vec3{-1.0f, 0.5f, 2.0f},
		glm::vec3{ 3.0f, 2.5f, 6.0f},
	};

	const auto sphere = Geometry::ToSphere(points);
	ASSERT_TRUE(sphere.has_value());

	EXPECT_NEAR(sphere->Center.x, 1.0f, 1e-5f);
	EXPECT_NEAR(sphere->Center.y, 1.5f, 1e-5f);
	EXPECT_NEAR(sphere->Center.z, 4.0f, 1e-5f);
	EXPECT_NEAR(sphere->Radius, 3.0f, 1e-5f);
}

TEST(SphereFitting, FitsExactSphereFromAxisSamples)
{
	const glm::vec3 center{1.5f, -2.0f, 0.75f};
	const float radius = 3.25f;
	const auto points = MakeExactSphereSamples(center, radius);

	Geometry::FittingParams params;
	params.Method = Geometry::FittingParams::FittingMethod::LeastSquares;

	const auto sphere = Geometry::ToSphere(points, params);
	ASSERT_TRUE(sphere.has_value());

	EXPECT_NEAR(sphere->Center.x, center.x, 1e-4f);
	EXPECT_NEAR(sphere->Center.y, center.y, 1e-4f);
	EXPECT_NEAR(sphere->Center.z, center.z, 1e-4f);
	EXPECT_NEAR(sphere->Radius, radius, 1e-4f);
}

TEST(SphereFitting, LeastSquaresRecoversNoisySphere)
{
	const glm::vec3 center{4.0f, -3.0f, 1.25f};
	const float radius = 2.5f;

	const std::array<glm::vec3, 12> directions = {
		glm::vec3{ 1.0f,  0.0f,  0.0f},
		glm::vec3{-1.0f,  0.0f,  0.0f},
		glm::vec3{ 0.0f,  1.0f,  0.0f},
		glm::vec3{ 0.0f, -1.0f,  0.0f},
		glm::vec3{ 0.0f,  0.0f,  1.0f},
		glm::vec3{ 0.0f,  0.0f, -1.0f},
		glm::normalize(glm::vec3{ 1.0f,  1.0f,  1.0f}),
		glm::normalize(glm::vec3{-1.0f,  1.0f,  1.0f}),
		glm::normalize(glm::vec3{ 1.0f, -1.0f,  1.0f}),
		glm::normalize(glm::vec3{ 1.0f,  1.0f, -1.0f}),
		glm::normalize(glm::vec3{-1.0f, -1.0f,  1.0f}),
		glm::normalize(glm::vec3{ 1.0f, -1.0f, -1.0f}),
	};

	std::vector<glm::vec3> samples;
	samples.reserve(directions.size());
	for (std::size_t i = 0; i < directions.size(); ++i)
	{
		const float radialNoise = (static_cast<float>(i % 5) - 2.0f) * 0.0125f;
		const glm::vec3 offset = glm::vec3{
			0.01f * (static_cast<float>(i % 3) - 1.0f),
			0.008f * (static_cast<float>(i % 4) - 1.5f),
			-0.006f * static_cast<float>((i % 2) ? 1.0f : -1.0f),
		};
		samples.push_back(center + directions[i] * (radius + radialNoise) + offset);
	}

	Geometry::FittingParams params;
	params.Method = Geometry::FittingParams::FittingMethod::LeastSquares;

	const auto sphere = Geometry::ToSphere(samples, params);
	ASSERT_TRUE(sphere.has_value());

	EXPECT_NEAR(sphere->Center.x, center.x, 5.0e-2f);
	EXPECT_NEAR(sphere->Center.y, center.y, 5.0e-2f);
	EXPECT_NEAR(sphere->Center.z, center.z, 5.0e-2f);
	EXPECT_NEAR(sphere->Radius, radius, 5.0e-2f);
}

TEST(SphereFitting, BoundingSphereContainsAllSamples)
{
	const std::vector<glm::vec3> samples = {
		{-1.0f, -2.0f,  0.5f},
		{ 4.0f,  1.5f, -2.0f},
		{ 2.5f,  3.0f,  4.0f},
		{-0.5f,  2.0f,  5.5f},
		{ 6.0f, -1.0f,  1.0f},
	};

	Geometry::FittingParams params;
	params.Method = Geometry::FittingParams::FittingMethod::Bounding;

	const auto sphere = Geometry::ToSphere(samples, params);
	ASSERT_TRUE(sphere.has_value());

	for (const glm::vec3& p : samples)
	{
		EXPECT_LE(DistanceToCenter(*sphere, p), sphere->Radius + 1.0e-5f);
	}
}

TEST(SphereFitting, EnforceContainmentInflatesLeastSquaresFit)
{
	std::vector<glm::vec3> samples = MakeExactSphereSamples(glm::vec3{0.0f, 0.0f, 0.0f}, 2.0f);
	samples.emplace_back(3.5f, -0.5f, 0.0f);

	Geometry::FittingParams params;
	params.Method = Geometry::FittingParams::FittingMethod::LeastSquares;
	params.EnforceContainment = true;
	params.ContainmentSlack = 1.0e-5f;

	const auto sphere = Geometry::ToSphere(samples, params);
	ASSERT_TRUE(sphere.has_value());

	for (const glm::vec3& p : samples)
	{
		EXPECT_LE(DistanceToCenter(*sphere, p), sphere->Radius + 1.0e-5f);
	}
}

