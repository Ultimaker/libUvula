// (c) 2025, UltiMaker -- see LICENCE for details

#pragma once

#include <cassert>
#include <cstdint>
#include <utility>

template<typename IndexType>
struct FaceIndex
{
    IndexType i1{ 0 };
    IndexType i2{ 0 };
    IndexType i3{ 0 };

    IndexType& at(const int index)
    {
        assert(index >= 0 && index < 3);
        switch (index)
        {
            case 0: return i1;
            case 1: return i2;
            default: return i3;
        }
    }

    inline bool operator==(const FaceIndex<IndexType>& other) const = default;
};

using Face = FaceIndex<uint32_t>;

using FaceSigned = FaceIndex<int32_t>;

namespace std
{
template<typename IndexType>
struct hash<FaceIndex<IndexType>>
{
    inline size_t operator()(const FaceIndex<IndexType>& face) const noexcept
    {
        return std::hash<IndexType>{}(face.i1) ^ (std::hash<IndexType>{}(face.i2) << 1) ^ (std::hash<IndexType>{}(face.i3) << 2);
    }
};
} // namespace std
