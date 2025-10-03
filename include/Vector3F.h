// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <optional>

class Point3F;

class Vector3F
{
public:
    explicit Vector3F() = default;

    Vector3F(const float x, const float y, const float z);

    Vector3F(const Point3F& v1, const Point3F& v2);

    float x() const
    {
        return x_;
    }

    float y() const
    {
        return y_;
    }

    float z() const
    {
        return z_;
    }

    [[nodiscard]] float dot(const Vector3F& other) const;

    [[nodiscard]] Vector3F cross(const Vector3F& other) const;

    Vector3F operator+(const Vector3F& other) const;

    void operator+=(const Vector3F& other);

    Vector3F operator*(const float factor) const;

    void operator*=(const float factor);

    Vector3F operator/(const float factor) const;

    void operator/=(const float factor);

    [[nodiscard]] float lengthSquared() const;

    [[nodiscard]] float length() const;

    bool normalize();

    [[nodiscard]] std::optional<Vector3F> normalized() const;

private:
    float x_{ 0.0 };
    float y_{ 0.0 };
    float z_{ 0.0 };
};
