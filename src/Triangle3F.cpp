#include "Triangle3F.h"

#include "geometry_utils.h"


std::optional<Vector3F> Triangle3F::normal() const
{
    return geometry_utils::triangleNormal((*this)[0], (*this)[1], (*this)[2]);
}