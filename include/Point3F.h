// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <bit>
#include <memory>

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

    inline bool operator==(const Point3F& other) const = default;

    // NOTE: _Exact_ hash, don't use when comparison requires near-equal.
    inline size_t hash_() const noexcept
    {
        return std::hash<float>{}(x_) ^ (std::hash<float>{}(y_) << 1) ^ (std::hash<float>{}(z_) << 2);
    }

private:
    float x_{ 0.0 };
    float y_{ 0.0 };
    float z_{ 0.0 };
};

namespace std
{
template<>
struct hash<Point3F>
{
    inline size_t operator()(const Point3F& pt) const noexcept
    {
        return pt.hash_();
    }
};
} // namespace std
