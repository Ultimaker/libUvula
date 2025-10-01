// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once
#include "Point3F.h"

class Matrix44F
{
public:
    explicit Matrix44F() = default;

    explicit Matrix44F(const float values[4][4]);

    Point3F preMultiply(const Point3F& point) const;

private:
    float values_[4][4];
};
