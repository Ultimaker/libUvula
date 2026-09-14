// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <array>

#include "Point3F.h"
#include "Vector3F.h"

class Triangle3F : public std::array<Point3F, 3>
{
public:
    /*! @return The unitary normal of this triangle, or nullopt if it is degenerate */
    [[nodiscard]] std::optional<Vector3F> normal() const;
};
