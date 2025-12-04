// tests/Test_GraphicsGeometry.cpp
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <vector>

import Runtime.Graphics.Geometry;
// Mock Device would be needed for a full GPU test,
// but here we verify logic correctness via unit tests on CpuData

TEST(Geometry, CpuDataToRequest)
{
    using namespace Runtime::Graphics;

    GeometryCpuData cpu;
    cpu.Positions = {{0,0,0}, {1,1,1}};
    cpu.Normals = {{0,1,0}, {0,1,0}};
    cpu.Aux = {{0,0,0,0}, {1,1,0,0}};
    cpu.Indices = {0, 1};

    auto req = cpu.ToUploadRequest();

    EXPECT_EQ(req.Positions.size(), 2);
    EXPECT_EQ(req.Positions[1].x, 1.0f);
    EXPECT_EQ(req.Normals.size(), 2);
    EXPECT_EQ(req.Aux.size(), 2);
    EXPECT_EQ(req.Indices.size(), 2);
}

// Full GPU test requires Vulkan Context, handled in Integration tests.