#include "Triangle3F.h"


Triangle3F::Triangle3F(const Point3F& p1, const Point3F& p2, const Point3F& p3)
    : p1_(p1)
    , p2_(p2)
    , p3_(p3)
{
}

Vector3F Triangle3F::normal() const
{
    return Vector3F(p1_, p2_).cross(Vector3F(p1_, p3_));
}