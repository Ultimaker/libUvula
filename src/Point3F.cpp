#include "Point3F.h"


Point3F::Point3F(const float x, const float y, const float z)
    : x_(x)
    , y_(y)
    , z_(z)
{
}

Point3F& Point3F::operator/=(const float scale)
{
    x_ /= scale;
    y_ /= scale;
    z_ /= scale;

    return *this;
}
