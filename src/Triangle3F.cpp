#include "Triangle3F.h"

#include "geometry_utils.h"


Triangle3F::Triangle3F(const Point3F& p1, const Point3F& p2, const Point3F& p3)
    : p1_(p1)
    , p2_(p2)
    , p3_(p3)
{
}

std::optional<Vector3F> Triangle3F::normal() const
{
    return geometry_utils::triangleNormal(p1_, p2_, p3_);
}