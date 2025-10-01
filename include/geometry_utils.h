// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <optional>

struct Point3F;
class Vector3F;

namespace geometry_utils
{

std::optional<Vector3F> triangleNormal(const Point3F& v1, const Point3F& v2, const Point3F& v3);

float deg2rad(float angle);

}; // namespace geometry_utils
