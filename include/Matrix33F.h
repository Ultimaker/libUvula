// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

struct Point2F;
struct Point3F;
class Vector3F;

class Matrix33F
{
public:
    explicit Matrix33F() = default;

    void transpose();

    [[nodiscard]] Point2F project(const Point3F& vertex) const;

    static Matrix33F makeOrthogonalBasis(const Vector3F& normal);

private:
    float values_[3][3];
};
