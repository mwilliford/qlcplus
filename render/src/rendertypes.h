#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qlcrender {

struct FixtureSceneGraph;

struct RenderFixture
{
    uint32_t id;
    int fixtureType;      // QLCFixtureDef::FixtureType enum value
    float transform[16];  // 4x4 column-major (from RigidTransform::to_4x4_column_major)
    float color[4];       // RGBA
    std::string name;     // fixture display name for labels
    const FixtureSceneGraph *sceneGraph = nullptr;  // null = legacy single-mesh path
};

struct RenderEllipsoid
{
    uint32_t fixtureId;
    float center[3];
    float semiAxes[3];    // a, b, c in meters (converted from cm)
    float rotation[9];    // 3x3 row-major eigenvector matrix
    float color[4];       // quality-based RGBA
};

struct RenderLine
{
    float start[3];
    float end[3];
    float color[4];
};

struct RenderPlane
{
    float normal[3];
    float distance;       // from origin, in meters
    float color[4];
    float extent;         // visual size in meters
};

struct RenderTruss
{
    float start[3];
    float end[3];
    float color[4] = {0.8f, 0.8f, 0.2f, 1.0f};  // yellow
};

} // namespace qlcrender
