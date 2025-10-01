#include "geometry_utils.h"

#include <numbers>

#include "Vector3F.h"


namespace geometry_utils
{

std::optional<Vector3F> triangleNormal(const Point3F& v1, const Point3F& v2, const Point3F& v3)
{
    return Vector3F(v1, v2).cross(Vector3F(v1, v3)).normalized();
}

float deg2rad(float angle)
{
    return angle * std::numbers::pi / 180.0;
}

}; // namespace geometry_utils
