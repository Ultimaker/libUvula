// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <array>
#include <cstdint>

template<typename IndexType>
using FaceIndex = std::array<IndexType, 3>;

using Face = FaceIndex<uint32_t>;

using FaceSigned = FaceIndex<int32_t>;
