// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <optional>

struct Point2F;

class Vector2F
{
public:
    explicit Vector2F() = default;

    Vector2F(const float x, const float y);

    Vector2F(const Point2F& v1, const Point2F& v2);

    float x() const
    {
        return x_;
    }

    float y() const
    {
        return y_;
    }

    [[nodiscard]] float dot(const Vector2F& other) const;

    // [[nodiscard]] Vector3F cross(const Vector3F& other) const;
    //
    // Vector3F operator+(const Vector3F& other) const;
    //
    // void operator+=(const Vector3F& other);
    //
    // Vector3F operator*(const float factor) const;
    //
    // void operator*=(const float factor);
    //
    // Vector3F operator/(const float factor) const;
    //
    // void operator/=(const float factor);
    //
    // [[nodiscard]] float lengthSquared() const;
    //
    // [[nodiscard]] float length() const;
    //
    // bool normalize();
    //
    // [[nodiscard]] std::optional<Vector2F> normalized() const;

private:
    float x_{ 0.0 };
    float y_{ 0.0 };
};
