#include "Vector3F.h"

#include <cmath>
#include <limits>

#include "Point3F.h"

Vector3F::Vector3F(const float x, const float y, const float z)
    : x_(x)
    , y_(y)
    , z_(z)
{
}

Vector3F::Vector3F(const Point3F& v1, const Point3F& v2)
    : x_(v2.x() - v1.x())
    , y_(v2.y() - v1.y())
    , z_(v2.z() - v1.z())
{
}

float Vector3F::dot(const Vector3F& other) const
{
    return (x_ * other.x_) + (y_ * other.y_) + (z_ * other.z_);
}

Vector3F Vector3F::cross(const Vector3F& other) const
{
    return Vector3F((y_ * other.z_) - (z_ * other.y_), (z_ * other.x_) - (x_ * other.z_), (x_ * other.y_) - (y_ * other.x_));
}

Vector3F Vector3F::operator+(const Vector3F& other) const
{
    return Vector3F(x_ + other.x_, y_ + other.y_, z_ + other.z_);
}

void Vector3F::operator+=(const Vector3F& other)
{
    x_ += other.x_;
    y_ += other.y_;
    z_ += other.z_;
}

Vector3F Vector3F::operator*(const float factor) const
{
    return Vector3F(x_ * factor, y_ * factor, z_ * factor);
}

void Vector3F::operator*=(const float factor)
{
    x_ *= factor;
    y_ *= factor;
    z_ *= factor;
}

Vector3F Vector3F::operator/(const float factor) const
{
    return Vector3F(x_ / factor, y_ / factor, z_ / factor);
}

void Vector3F::operator/=(const float factor)
{
    x_ /= factor;
    y_ /= factor;
    z_ /= factor;
}

float Vector3F::lengthSquared() const
{
    return (x_ * x_) + (y_ * y_) + (z_ * z_);
}

float Vector3F::length() const
{
    return std::sqrt(lengthSquared());
}

bool Vector3F::normalize()
{
    const float actual_length = length();
    if (actual_length > std::numeric_limits<float>::epsilon()) [[likely]]
    {
        *this /= actual_length;
        return true;
    }

    x_ = 0.0;
    y_ = 0.0;
    z_ = 0.0;
    return false;
}

std::optional<Vector3F> Vector3F::normalized() const
{
    const float actual_length = length();
    if (actual_length > std::numeric_limits<float>::epsilon()) [[likely]]
    {
        return *this / actual_length;
    }

    return std::nullopt;
}
