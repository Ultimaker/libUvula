#include "Triangle3F.h"

#include "geometry_utils.h"


std::optional<Vector3F> Triangle3F::normal() const
{
    return geometry_utils::triangleNormal(std::get<0>(*this), std::get<1>(*this), std::get<2>(*this));
}