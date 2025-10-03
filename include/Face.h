// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <cstdint>

template<typename IndexType>
struct FaceIndex
{
    IndexType i1{ 0 };
    IndexType i2{ 0 };
    IndexType i3{ 0 };
};

using Face = FaceIndex<uint32_t>;

using FaceSigned = FaceIndex<int32_t>;