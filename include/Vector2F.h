// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

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

private:
    float x_{ 0.0 };
    float y_{ 0.0 };
};
