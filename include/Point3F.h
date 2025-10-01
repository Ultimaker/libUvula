// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

class Matrix44F;

class Point3F
{
public:
    explicit Point3F(const float x, const float y, const float z);

    [[nodiscard]] float x() const
    {
        return x_;
    }

    [[nodiscard]] float y() const
    {
        return y_;
    }

    [[nodiscard]] float z() const
    {
        return z_;
    }

    Point3F& operator/=(const float scale);

    friend bool operator<(const Point3F& lhs, const Point3F& rhs)
    {
        if (lhs.x_ != rhs.x_)
        {
            return lhs.x_ < rhs.x_;
        }

        if (lhs.y_ != rhs.y_)
        {
            return lhs.y_ < rhs.y_;
        }

        return lhs.z_ < rhs.z_;
    }

private:
    float x_{ 0.0 };
    float y_{ 0.0 };
    float z_{ 0.0 };
};
