// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include "Point3F.h"
#include "Vector3F.h"

class Triangle3F
{
public:
    explicit Triangle3F(const Point3F& p1, const Point3F& p2, const Point3F& p3);

    [[nodiscard]] const Point3F& p1() const
    {
        return p1_;
    };

    [[nodiscard]] const Point3F& p2() const
    {
        return p2_;
    }

    [[nodiscard]] const Point3F& p3() const
    {
        return p3_;
    }

    [[nodiscard]] Vector3F normal() const;

private:
    Point3F p1_;
    Point3F p2_;
    Point3F p3_;
};
