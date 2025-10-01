#include "Matrix44F.h"

#include <cstring>

#include <spdlog/spdlog.h>


Matrix44F::Matrix44F(const float values[4][4])
{
    std::memcpy(values_, values, sizeof(values_));
}

Point3F Matrix44F::preMultiply(const Point3F& point) const
{
    return Point3F{ values_[0][0] * point.x() + values_[0][1] * point.y() + values_[0][2] * point.z() + values_[0][3],
                    values_[1][0] * point.x() + values_[1][1] * point.y() + values_[1][2] * point.z() + values_[1][3],
                    values_[2][0] * point.x() + values_[2][1] * point.y() + values_[2][2] * point.z() + values_[2][3] };
}