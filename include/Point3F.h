// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <bit>
#include <memory>

class Point3F
{
public:
    constexpr Point3F() noexcept = default;

    explicit Point3F(const float x, const float y, const float z);

    [[nodiscard]] constexpr float x() const noexcept
    {
        return x_;
    }

    [[nodiscard]] constexpr float y() const noexcept
    {
        return y_;
    }

    [[nodiscard]] constexpr float z() const noexcept
    {
        return z_;
    }

    Point3F& operator/=(const float scale);

    friend constexpr bool operator<(const Point3F& lhs, const Point3F& rhs) noexcept
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

    constexpr bool operator==(const Point3F& other) const noexcept = default;

    // NOTE: _Exact_ hash, don't use when comparison requires near-equal.
    [[nodiscard]] size_t hash_() const noexcept
    {
        size_t seed = std::hash<float>{}(x_);
        seed ^= std::hash<float>{}(y_) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<float>{}(z_) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }

private:
    float x_{ 0.0f };
    float y_{ 0.0f };
    float z_{ 0.0f };
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
