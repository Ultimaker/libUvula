#include "Vector2F.h"

#include <cmath>
#include <limits>

#include "Point2F.h"

Vector2F::Vector2F(const float x, const float y)
    : x_(x)
    , y_(y)
{
}

Vector2F::Vector2F(const Point2F& v1, const Point2F& v2)
    : x_(v2.x - v1.x)
    , y_(v2.y - v1.y)
{
}

float Vector2F::dot(const Vector2F& other) const
{
    return (x_ * other.x_) + (y_ * other.y_);
}
