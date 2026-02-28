/*
MIT License

Copyright (c) 2018-2020 Jonathan Young

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
/*
thekla_atlas
https://github.com/Thekla/thekla_atlas
MIT License
Copyright (c) 2013 Thekla, Inc
Copyright NVIDIA Corporation 2006 -- Ignacio Castano <icastano@nvidia.com>

Fast-BVH
https://github.com/brandonpelfrey/Fast-BVH
MIT License
Copyright (c) 2012 Brandon Pelfrey
*/
#include "xatlas.h"
#ifndef XATLAS_C_API
#define XATLAS_C_API 0
#endif
#if XATLAS_C_API
#include "xatlas_c.h"
#endif
#include <assert.h>
#include <atomic>
#include <condition_variable>
#include <float.h> // FLT_MAX
#include <limits.h>
#include <math.h>
#include <mutex>
#include <thread>
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef XA_DEBUG
#ifdef NDEBUG
#define XA_DEBUG 0
#else
#define XA_DEBUG 1
#endif
#endif

#ifndef XA_MULTITHREADED
#ifdef __EMSCRIPTEN__
#define XA_MULTITHREADED 0  // Disable threading for Emscripten/WASM
#else
#define XA_MULTITHREADED 1
#endif
#endif

#define XA_STR(x) #x
#define XA_XSTR(x) XA_STR(x)

#ifndef XA_ASSERT
#define XA_ASSERT(exp) \
    if (! (exp)) \
    { \
        XA_PRINT_WARNING("\rASSERT: %s %s %d\n", XA_XSTR(exp), __FILE__, __LINE__); \
    }
#endif

#ifndef XA_DEBUG_ASSERT
#define XA_DEBUG_ASSERT(exp) assert(exp)
#endif

#ifndef XA_PRINT
#define XA_PRINT(...) \
    if (xatlas::internal::s_print && xatlas::internal::s_printVerbose) \
        xatlas::internal::s_print(__VA_ARGS__);
#endif

#ifndef XA_PRINT_WARNING
#define XA_PRINT_WARNING(...) \
    if (xatlas::internal::s_print) \
        xatlas::internal::s_print(__VA_ARGS__);
#endif

#define XA_ALLOC(type) (type*)internal::Realloc(nullptr, sizeof(type), __FILE__, __LINE__)
#define XA_ALLOC_ARRAY(type, num) (type*)internal::Realloc(nullptr, sizeof(type) * (num), __FILE__, __LINE__)
#define XA_REALLOC(ptr, type, num) (type*)internal::Realloc(ptr, sizeof(type) * (num), __FILE__, __LINE__)
#define XA_REALLOC_SIZE(ptr, size) (uint8_t*)internal::Realloc(ptr, size, __FILE__, __LINE__)
#define XA_FREE(ptr) internal::Realloc(ptr, 0, __FILE__, __LINE__)
#define XA_NEW(type) new (XA_ALLOC(type)) type()
#define XA_NEW_ARGS(type, ...) new (XA_ALLOC(type)) type(__VA_ARGS__)

#ifdef _MSC_VER
#define XA_INLINE __forceinline
#else
#define XA_INLINE inline
#endif

#if defined(__clang__) || defined(__GNUC__)
#define XA_NODISCARD [[nodiscard]]
#elif defined(_MSC_VER)
#define XA_NODISCARD _Check_return_
#else
#define XA_NODISCARD
#endif

#define XA_UNUSED(a) ((void)(a))

#ifdef _MSC_VER
#define XA_FOPEN(_file, _filename, _mode) \
    { \
        if (fopen_s(&_file, _filename, _mode) != 0) \
            _file = NULL; \
    }
#define XA_SPRINTF(_buffer, _size, _format, ...) sprintf_s(_buffer, _size, _format, __VA_ARGS__)
#else
#define XA_FOPEN(_file, _filename, _mode) _file = fopen(_filename, _mode)
#define XA_SPRINTF(_buffer, _size, _format, ...) sprintf(_buffer, _format, __VA_ARGS__)
#endif

namespace xatlas
{
namespace internal
{

// Custom memory allocation.
typedef void* (*ReallocFunc)(void*, size_t);
typedef void (*FreeFunc)(void*);

// Custom print function.
typedef int (*PrintFunc)(const char*, ...);

static ReallocFunc s_realloc = realloc;
static FreeFunc s_free = free;
static PrintFunc s_print = printf;
static bool s_printVerbose = false;

static void* Realloc(void* ptr, size_t size, const char* /*file*/, int /*line*/)
{
    if (size == 0 && ! ptr)
        return nullptr;
    if (size == 0 && s_free)
    {
        s_free(ptr);
        return nullptr;
    }
    void* mem = s_realloc(ptr, size);
    XA_DEBUG_ASSERT(size <= 0 || (size > 0 && mem));
    return mem;
}
static constexpr float kEpsilon = 0.0001f;
static constexpr float kAreaEpsilon = FLT_EPSILON;

static int align(int x, int a)
{
    return (x + a - 1) & ~(a - 1);
}

template<typename T>
static T max(const T& a, const T& b)
{
    return a > b ? a : b;
}

template<typename T>
static T min(const T& a, const T& b)
{
    return a < b ? a : b;
}

template<typename T>
static T max3(const T& a, const T& b, const T& c)
{
    return max(a, max(b, c));
}

/// Return the maximum of the three arguments.
template<typename T>
static T min3(const T& a, const T& b, const T& c)
{
    return min(a, min(b, c));
}

/// Clamp between two values.
template<typename T>
static T clamp(const T& x, const T& a, const T& b)
{
    return min(max(x, a), b);
}

template<typename T>
static void swap(T& a, T& b)
{
    T temp = a;
    a = b;
    b = temp;
}

union FloatUint32
{
    float f;
    uint32_t u;
};

static bool isFinite(float f)
{
    FloatUint32 fu;
    fu.f = f;
    return fu.u != 0x7F800000u && fu.u != 0x7F800001u;
}

static bool isNan(float f)
{
    return f != f;
}

// Robust floating point comparisons:
// http://realtimecollisiondetection.net/blog/?p=89
static bool equal(const float f0, const float f1, const float epsilon)
{
    // return fabs(f0-f1) <= epsilon;
    return fabs(f0 - f1) <= epsilon * max3(1.0f, fabsf(f0), fabsf(f1));
}

static int ftoi_ceil(float val)
{
    return (int)ceilf(val);
}

static bool isZero(const float f, const float epsilon)
{
    return fabs(f) <= epsilon;
}

static float square(float f)
{
    return f * f;
}

/** Return the next power of two.
 * @see http://graphics.stanford.edu/~seander/bithacks.html
 * @warning Behaviour for 0 is undefined.
 * @note isPowerOfTwo(x) == true -> nextPowerOfTwo(x) == x
 * @note nextPowerOfTwo(x) = 2 << log2(x-1)
 */
static uint32_t nextPowerOfTwo(uint32_t x)
{
    XA_DEBUG_ASSERT(x != 0);
    // On modern CPUs this is supposed to be as fast as using the bsr instruction.
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

class Vector2
{
public:
    Vector2()
    {
    }
    explicit Vector2(float f)
        : x(f)
        , y(f)
    {
    }
    Vector2(float _x, float _y)
        : x(_x)
        , y(_y)
    {
    }

    Vector2 operator-() const
    {
        return Vector2(-x, -y);
    }

    void operator+=(const Vector2& v)
    {
        x += v.x;
        y += v.y;
    }

    void operator-=(const Vector2& v)
    {
        x -= v.x;
        y -= v.y;
    }

    void operator*=(float s)
    {
        x *= s;
        y *= s;
    }

    void operator*=(const Vector2& v)
    {
        x *= v.x;
        y *= v.y;
    }

    float x, y;
};

static bool operator==(const Vector2& a, const Vector2& b)
{
    return a.x == b.x && a.y == b.y;
}

static bool operator!=(const Vector2& a, const Vector2& b)
{
    return a.x != b.x || a.y != b.y;
}

/*static Vector2 operator+(const Vector2 &a, const Vector2 &b)
{
    return Vector2(a.x + b.x, a.y + b.y);
}*/

static Vector2 operator-(const Vector2& a, const Vector2& b)
{
    return Vector2(a.x - b.x, a.y - b.y);
}

static Vector2 operator*(const Vector2& v, float s)
{
    return Vector2(v.x * s, v.y * s);
}

static float dot(const Vector2& a, const Vector2& b)
{
    return a.x * b.x + a.y * b.y;
}

static float lengthSquared(const Vector2& v)
{
    return v.x * v.x + v.y * v.y;
}

static float length(const Vector2& v)
{
    return sqrtf(lengthSquared(v));
}

static Vector2 normalize(const Vector2& v)
{
    const float l = length(v);
    XA_DEBUG_ASSERT(l > 0.0f); // Never negative.
    const Vector2 n = v * (1.0f / l);
    return n;
}

static Vector2 normalizeSafe(const Vector2& v, const Vector2& fallback)
{
    const float l = length(v);
    if (l > 0.0f) // Never negative.
        return v * (1.0f / l);
    return fallback;
}

static bool equal(const Vector2& v1, const Vector2& v2, float epsilon)
{
    return equal(v1.x, v2.x, epsilon) && equal(v1.y, v2.y, epsilon);
}

static Vector2 min(const Vector2& a, const Vector2& b)
{
    return Vector2(min(a.x, b.x), min(a.y, b.y));
}

static Vector2 max(const Vector2& a, const Vector2& b)
{
    return Vector2(max(a.x, b.x), max(a.y, b.y));
}

static bool isFinite(const Vector2& v)
{
    return isFinite(v.x) && isFinite(v.y);
}

static float triangleArea(const Vector2& a, const Vector2& b, const Vector2& c)
{
    // IC: While it may be appealing to use the following expression:
    // return (c.x * a.y + a.x * b.y + b.x * c.y - b.x * a.y - c.x * b.y - a.x * c.y) * 0.5f;
    // That's actually a terrible idea. Small triangles far from the origin can end up producing fairly large floating point
    // numbers and the results becomes very unstable and dependent on the order of the factors.
    // Instead, it's preferable to subtract the vertices first, and multiply the resulting small values together. The result
    // in this case is always much more accurate (as long as the triangle is small) and less dependent of the location of
    // the triangle.
    // return ((a.x - c.x) * (b.y - c.y) - (a.y - c.y) * (b.x - c.x)) * 0.5f;
    const Vector2 v0 = a - c;
    const Vector2 v1 = b - c;
    return (v0.x * v1.y - v0.y * v1.x) * 0.5f;
}

static bool linesIntersect(const Vector2& a1, const Vector2& a2, const Vector2& b1, const Vector2& b2, float epsilon)
{
    const Vector2 v0 = a2 - a1;
    const Vector2 v1 = b2 - b1;
    const float denom = -v1.x * v0.y + v0.x * v1.y;
    if (equal(denom, 0.0f, epsilon))
        return false;
    const float s = (-v0.y * (a1.x - b1.x) + v0.x * (a1.y - b1.y)) / denom;
    if (s > epsilon && s < 1.0f - epsilon)
    {
        const float t = (v1.x * (a1.y - b1.y) - v1.y * (a1.x - b1.x)) / denom;
        return t > epsilon && t < 1.0f - epsilon;
    }
    return false;
}

struct Vector2i
{
    Vector2i()
    {
    }
    Vector2i(int32_t _x, int32_t _y)
        : x(_x)
        , y(_y)
    {
    }

    int32_t x, y;
};

struct Extents2
{
    Vector2 min, max;

    Extents2()
    {
    }

    Extents2(Vector2 p1, Vector2 p2)
    {
        min = xatlas::internal::min(p1, p2);
        max = xatlas::internal::max(p1, p2);
    }

    void reset()
    {
        min.x = min.y = FLT_MAX;
        max.x = max.y = -FLT_MAX;
    }

    void add(Vector2 p)
    {
        min = xatlas::internal::min(min, p);
        max = xatlas::internal::max(max, p);
    }

    Vector2 midpoint() const
    {
        return Vector2(min.x + (max.x - min.x) * 0.5f, min.y + (max.y - min.y) * 0.5f);
    }

    static bool intersect(const Extents2& e1, const Extents2& e2)
    {
        return e1.min.x <= e2.max.x && e1.max.x >= e2.min.x && e1.min.y <= e2.max.y && e1.max.y >= e2.min.y;
    }
};

struct ArrayBase
{
    ArrayBase(uint32_t _elementSize)
        : buffer(nullptr)
        , elementSize(_elementSize)
        , size(0)
        , capacity(0)
    {
    }

    ~ArrayBase()
    {
        XA_FREE(buffer);
    }

    XA_INLINE void clear()
    {
        size = 0;
    }

    void copyFrom(const uint8_t* data, uint32_t length)
    {
        XA_DEBUG_ASSERT(data);
        XA_DEBUG_ASSERT(length > 0);
        resize(length, true);
        if (buffer && data && length > 0)
            memcpy(buffer, data, length * elementSize);
    }

    void copyTo(ArrayBase& other) const
    {
        XA_DEBUG_ASSERT(elementSize == other.elementSize);
        XA_DEBUG_ASSERT(size > 0);
        other.resize(size, true);
        if (other.buffer && buffer && size > 0)
            memcpy(other.buffer, buffer, size * elementSize);
    }

    void destroy()
    {
        size = 0;
        XA_FREE(buffer);
        buffer = nullptr;
        capacity = 0;
        size = 0;
    }

    // Insert the given element at the given index shifting all the elements up.
    void insertAt(uint32_t index, const uint8_t* value)
    {
        XA_DEBUG_ASSERT(index >= 0 && index <= size);
        XA_DEBUG_ASSERT(value);
        resize(size + 1, false);
        XA_DEBUG_ASSERT(buffer);
        if (buffer && index < size - 1)
            memmove(buffer + elementSize * (index + 1), buffer + elementSize * index, elementSize * (size - 1 - index));
        if (buffer && value)
            memcpy(&buffer[index * elementSize], value, elementSize);
    }

    void moveTo(ArrayBase& other)
    {
        XA_DEBUG_ASSERT(elementSize == other.elementSize);
        other.destroy();
        other.buffer = buffer;
        other.elementSize = elementSize;
        other.size = size;
        other.capacity = capacity;
        buffer = nullptr;
        elementSize = size = capacity = 0;
    }

    void pop_back()
    {
        XA_DEBUG_ASSERT(size > 0);
        resize(size - 1, false);
    }

    void push_back(const uint8_t* value)
    {
        XA_DEBUG_ASSERT(value < buffer || value >= buffer + size);
        XA_DEBUG_ASSERT(value);
        resize(size + 1, false);
        XA_DEBUG_ASSERT(buffer);
        if (buffer && value)
            memcpy(&buffer[(size - 1) * elementSize], value, elementSize);
    }

    void push_back(const ArrayBase& other)
    {
        XA_DEBUG_ASSERT(elementSize == other.elementSize);
        if (other.size > 0)
        {
            const uint32_t oldSize = size;
            resize(size + other.size, false);
            XA_DEBUG_ASSERT(buffer);
            if (buffer)
                memcpy(buffer + oldSize * elementSize, other.buffer, other.size * other.elementSize);
        }
    }

    // Remove the element at the given index. This is an expensive operation!
    void removeAt(uint32_t index)
    {
        XA_DEBUG_ASSERT(index >= 0 && index < size);
        XA_DEBUG_ASSERT(buffer);
        if (buffer)
        {
            if (size > 1)
                memmove(buffer + elementSize * index, buffer + elementSize * (index + 1), elementSize * (size - 1 - index));
            if (size > 0)
                size--;
        }
    }

    // Element at index is swapped with the last element, then the array length is decremented.
    void removeAtFast(uint32_t index)
    {
        XA_DEBUG_ASSERT(index >= 0 && index < size);
        XA_DEBUG_ASSERT(buffer);
        if (buffer)
        {
            if (size > 1 && index != size - 1)
                memcpy(buffer + elementSize * index, buffer + elementSize * (size - 1), elementSize);
            if (size > 0)
                size--;
        }
    }

    void reserve(uint32_t desiredSize)
    {
        if (desiredSize > capacity)
            setArrayCapacity(desiredSize);
    }

    void resize(uint32_t newSize, bool exact)
    {
        size = newSize;
        if (size > capacity)
        {
            // First allocation is always exact. Otherwise, following allocations grow array to 150% of desired size.
            uint32_t newBufferSize;
            if (capacity == 0 || exact)
                newBufferSize = size;
            else
                newBufferSize = size + (size >> 2);
            setArrayCapacity(newBufferSize);
        }
    }

    void setArrayCapacity(uint32_t newCapacity)
    {
        XA_DEBUG_ASSERT(newCapacity >= size);
        if (newCapacity == 0)
        {
            // free the buffer.
            if (buffer != nullptr)
            {
                XA_FREE(buffer);
                buffer = nullptr;
            }
        }
        else
        {
            // realloc the buffer
            buffer = XA_REALLOC_SIZE(buffer, newCapacity * elementSize);
        }
        capacity = newCapacity;
    }

    uint8_t* buffer;
    uint32_t elementSize;
    uint32_t size;
    uint32_t capacity;
};

template<typename T>
class Array
{
public:
    Array()
        : m_base(sizeof(T))
    {
    }
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    XA_INLINE const T& operator[](uint32_t index) const
    {
        XA_DEBUG_ASSERT(index < m_base.size);
        XA_DEBUG_ASSERT(m_base.buffer);
        return ((const T*)m_base.buffer)[index];
    }

    XA_INLINE T& operator[](uint32_t index)
    {
        XA_DEBUG_ASSERT(index < m_base.size);
        XA_DEBUG_ASSERT(m_base.buffer);
        return ((T*)m_base.buffer)[index];
    }

    XA_INLINE const T& back() const
    {
        XA_DEBUG_ASSERT(! isEmpty());
        return ((const T*)m_base.buffer)[m_base.size - 1];
    }

    XA_INLINE T* begin()
    {
        return (T*)m_base.buffer;
    }
    XA_INLINE void clear()
    {
        m_base.clear();
    }

    bool contains(const T& value) const
    {
        for (uint32_t i = 0; i < m_base.size; i++)
        {
            if (((const T*)m_base.buffer)[i] == value)
                return true;
        }
        return false;
    }

    void copyFrom(const T* data, uint32_t length)
    {
        m_base.copyFrom((const uint8_t*)data, length);
    }
    void copyTo(Array& other) const
    {
        m_base.copyTo(other.m_base);
    }
    XA_INLINE const T* data() const
    {
        return (const T*)m_base.buffer;
    }
    XA_INLINE T* data()
    {
        return (T*)m_base.buffer;
    }
    void destroy()
    {
        m_base.destroy();
    }
    XA_INLINE T* end()
    {
        return (T*)m_base.buffer + m_base.size;
    }
    XA_INLINE bool isEmpty() const
    {
        return m_base.size == 0;
    }
    void insertAt(uint32_t index, const T& value)
    {
        m_base.insertAt(index, (const uint8_t*)&value);
    }
    void moveTo(Array& other)
    {
        m_base.moveTo(other.m_base);
    }
    void push_back(const T& value)
    {
        m_base.push_back((const uint8_t*)&value);
    }
    void push_back(const Array& other)
    {
        m_base.push_back(other.m_base);
    }
    void pop_back()
    {
        m_base.pop_back();
    }
    void removeAt(uint32_t index)
    {
        m_base.removeAt(index);
    }
    void removeAtFast(uint32_t index)
    {
        m_base.removeAtFast(index);
    }
    void reserve(uint32_t desiredSize)
    {
        m_base.reserve(desiredSize);
    }
    void resize(uint32_t newSize)
    {
        m_base.resize(newSize, true);
    }

    void runCtors()
    {
        for (uint32_t i = 0; i < m_base.size; i++)
            new (&((T*)m_base.buffer)[i]) T;
    }

    void runDtors()
    {
        for (uint32_t i = 0; i < m_base.size; i++)
            ((T*)m_base.buffer)[i].~T();
    }

    void fill(const T& value)
    {
        auto buffer = (T*)m_base.buffer;
        for (uint32_t i = 0; i < m_base.size; i++)
            buffer[i] = value;
    }

    void fillBytes(uint8_t value)
    {
        if (m_base.buffer && m_base.size > 0)
            memset(m_base.buffer, (int)value, m_base.size * m_base.elementSize);
    }

    XA_INLINE uint32_t size() const
    {
        return m_base.size;
    }

    XA_INLINE void zeroOutMemory()
    {
        if (m_base.buffer && m_base.size > 0)
            memset(m_base.buffer, 0, m_base.elementSize * m_base.size);
    }

private:
    ArrayBase m_base;
};

template<typename T>
struct ArrayView
{
    ArrayView()
        : data(nullptr)
        , length(0)
    {
    }
    ArrayView(Array<T>& a)
        : data(a.data())
        , length(a.size())
    {
    }
    ArrayView(T* _data, uint32_t _length)
        : data(_data)
        , length(_length)
    {
    }
    ArrayView& operator=(Array<T>& a)
    {
        data = a.data();
        length = a.size();
        return *this;
    }
    XA_INLINE const T& operator[](uint32_t index) const
    {
        XA_DEBUG_ASSERT(index < length);
        return data[index];
    }
    XA_INLINE T& operator[](uint32_t index)
    {
        XA_DEBUG_ASSERT(index < length);
        return data[index];
    }
    T* data;
    uint32_t length;
};

template<typename T>
struct ConstArrayView
{
    ConstArrayView()
        : data(nullptr)
        , length(0)
    {
    }
    ConstArrayView(const Array<T>& a)
        : data(a.data())
        , length(a.size())
    {
    }
    ConstArrayView(ArrayView<T> av)
        : data(av.data)
        , length(av.length)
    {
    }
    ConstArrayView(const T* _data, uint32_t _length)
        : data(_data)
        , length(_length)
    {
    }
    ConstArrayView& operator=(const Array<T>& a)
    {
        data = a.data();
        length = a.size();
        return *this;
    }
    XA_INLINE const T& operator[](uint32_t index) const
    {
        XA_DEBUG_ASSERT(index < length);
        return data[index];
    }
    const T* data;
    uint32_t length;
};

// Simple bit array.
class BitArray
{
public:
    BitArray()
        : m_size(0)
    {
    }

    BitArray(uint32_t sz)
    {
        resize(sz);
    }

    void resize(uint32_t new_size)
    {
        m_size = new_size;
        m_wordArray.resize((m_size + 31) >> 5);
    }

    bool get(uint32_t index) const
    {
        XA_DEBUG_ASSERT(index < m_size);
        return (m_wordArray[index >> 5] & (1 << (index & 31))) != 0;
    }

    void set(uint32_t index)
    {
        XA_DEBUG_ASSERT(index < m_size);
        m_wordArray[index >> 5] |= (1 << (index & 31));
    }

    void unset(uint32_t index)
    {
        XA_DEBUG_ASSERT(index < m_size);
        m_wordArray[index >> 5] &= ~(1 << (index & 31));
    }

    void zeroOutMemory()
    {
        m_wordArray.zeroOutMemory();
    }

private:
    uint32_t m_size; // Number of bits stored.
    Array<uint32_t> m_wordArray;
};

class BitImage
{
public:
    BitImage()
        : m_width(0)
        , m_height(0)
        , m_rowStride(0)
        , m_data()
    {
    }

    BitImage(uint32_t w, uint32_t h)
        : m_width(w)
        , m_height(h)
        , m_data()
    {
        m_rowStride = (m_width + 63) >> 6;
        m_data.resize(m_rowStride * m_height);
        m_data.zeroOutMemory();
    }

    BitImage(const BitImage& other) = delete;
    BitImage& operator=(const BitImage& other) = delete;
    uint32_t width() const
    {
        return m_width;
    }
    uint32_t height() const
    {
        return m_height;
    }

    void copyTo(BitImage& other)
    {
        other.m_width = m_width;
        other.m_height = m_height;
        other.m_rowStride = m_rowStride;
        m_data.copyTo(other.m_data);
    }

    void resize(uint32_t w, uint32_t h, bool discard)
    {
        const uint32_t rowStride = (w + 63) >> 6;
        if (discard)
        {
            m_data.resize(rowStride * h);
            m_data.zeroOutMemory();
        }
        else
        {
            Array<uint64_t> tmp;
            tmp.resize(rowStride * h);
            memset(tmp.data(), 0, tmp.size() * sizeof(uint64_t));
            // If only height has changed, can copy all rows at once.
            if (rowStride == m_rowStride)
            {
                memcpy(tmp.data(), m_data.data(), m_rowStride * min(m_height, h) * sizeof(uint64_t));
            }
            else if (m_width > 0 && m_height > 0)
            {
                const uint32_t height = min(m_height, h);
                for (uint32_t i = 0; i < height; i++)
                    memcpy(&tmp[i * rowStride], &m_data[i * m_rowStride], min(rowStride, m_rowStride) * sizeof(uint64_t));
            }
            tmp.moveTo(m_data);
        }
        m_width = w;
        m_height = h;
        m_rowStride = rowStride;
    }

    bool get(uint32_t x, uint32_t y) const
    {
        XA_DEBUG_ASSERT(x < m_width && y < m_height);
        const uint32_t index = (x >> 6) + y * m_rowStride;
        return (m_data[index] & (UINT64_C(1) << (uint64_t(x) & UINT64_C(63)))) != 0;
    }

    void set(uint32_t x, uint32_t y)
    {
        XA_DEBUG_ASSERT(x < m_width && y < m_height);
        const uint32_t index = (x >> 6) + y * m_rowStride;
        m_data[index] |= UINT64_C(1) << (uint64_t(x) & UINT64_C(63));
        XA_DEBUG_ASSERT(get(x, y));
    }

    void zeroOutMemory()
    {
        m_data.zeroOutMemory();
    }

    bool canBlit(const BitImage& image, uint32_t offsetX, uint32_t offsetY) const
    {
        for (uint32_t y = 0; y < image.m_height; y++)
        {
            const uint32_t thisY = y + offsetY;
            if (thisY >= m_height)
                continue;
            uint32_t x = 0;
            for (;;)
            {
                const uint32_t thisX = x + offsetX;
                if (thisX >= m_width)
                    break;
                const uint32_t thisBlockShift = thisX % 64;
                const uint64_t thisBlock = m_data[(thisX >> 6) + thisY * m_rowStride] >> thisBlockShift;
                const uint32_t blockShift = x % 64;
                const uint64_t block = image.m_data[(x >> 6) + y * image.m_rowStride] >> blockShift;
                if ((thisBlock & block) != 0)
                    return false;
                x += 64 - max(thisBlockShift, blockShift);
                if (x >= image.m_width)
                    break;
            }
        }
        return true;
    }

    void dilate(uint32_t padding)
    {
        BitImage tmp(m_width, m_height);
        for (uint32_t p = 0; p < padding; p++)
        {
            tmp.zeroOutMemory();
            for (uint32_t y = 0; y < m_height; y++)
            {
                for (uint32_t x = 0; x < m_width; x++)
                {
                    bool b = get(x, y);
                    if (! b)
                    {
                        if (x > 0)
                        {
                            b |= get(x - 1, y);
                            if (y > 0)
                                b |= get(x - 1, y - 1);
                            if (y < m_height - 1)
                                b |= get(x - 1, y + 1);
                        }
                        if (y > 0)
                            b |= get(x, y - 1);
                        if (y < m_height - 1)
                            b |= get(x, y + 1);
                        if (x < m_width - 1)
                        {
                            b |= get(x + 1, y);
                            if (y > 0)
                                b |= get(x + 1, y - 1);
                            if (y < m_height - 1)
                                b |= get(x + 1, y + 1);
                        }
                    }
                    if (b)
                        tmp.set(x, y);
                }
            }
            tmp.m_data.copyTo(m_data);
        }
    }

private:
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_rowStride; // In uint64_t's
    Array<uint64_t> m_data;
};

static uint32_t sdbmHash(const void* data_in, uint32_t size, uint32_t h = 5381)
{
    const uint8_t* data = (const uint8_t*)data_in;
    uint32_t i = 0;
    while (i < size)
    {
        h = (h << 16) + (h << 6) - h + (uint32_t)data[i++];
    }
    return h;
}

template<typename T>
static uint32_t hash(const T& t, uint32_t h = 5381)
{
    return sdbmHash(&t, sizeof(T), h);
}

template<typename Key>
struct Hash
{
    uint32_t operator()(const Key& k) const
    {
        return hash(k);
    }
};

template<typename Key>
struct PassthroughHash
{
    uint32_t operator()(const Key& k) const
    {
        return (uint32_t)k;
    }
};

template<typename Key>
struct Equal
{
    bool operator()(const Key& k0, const Key& k1) const
    {
        return k0 == k1;
    }
};

template<typename Key, typename H = Hash<Key>, typename E = Equal<Key>>
class HashMap
{
public:
    HashMap(uint32_t size)
        : m_size(size)
        , m_numSlots(0)
        , m_slots(nullptr)
        , m_keys()
        , m_next()
    {
    }

    ~HashMap()
    {
        if (m_slots)
            XA_FREE(m_slots);
    }

    void destroy()
    {
        if (m_slots)
        {
            XA_FREE(m_slots);
            m_slots = nullptr;
        }
        m_keys.destroy();
        m_next.destroy();
    }

    uint32_t add(const Key& key)
    {
        if (! m_slots)
            alloc();
        const uint32_t hash = computeHash(key);
        m_keys.push_back(key);
        m_next.push_back(m_slots[hash]);
        m_slots[hash] = m_next.size() - 1;
        return m_keys.size() - 1;
    }

    uint32_t get(const Key& key) const
    {
        if (! m_slots)
            return UINT32_MAX;
        return find(key, m_slots[computeHash(key)]);
    }

    uint32_t getNext(const Key& key, uint32_t current) const
    {
        return find(key, m_next[current]);
    }

private:
    void alloc()
    {
        XA_DEBUG_ASSERT(m_size > 0);
        m_numSlots = nextPowerOfTwo(m_size);
        auto minNumSlots = uint32_t(m_size * 1.3);
        if (m_numSlots < minNumSlots)
            m_numSlots = nextPowerOfTwo(minNumSlots);
        m_slots = XA_ALLOC_ARRAY(uint32_t, m_numSlots);
        for (uint32_t i = 0; i < m_numSlots; i++)
            m_slots[i] = UINT32_MAX;
        m_keys.reserve(m_size);
        m_next.reserve(m_size);
    }

    uint32_t computeHash(const Key& key) const
    {
        H hash;
        return hash(key) & (m_numSlots - 1);
    }

    uint32_t find(const Key& key, uint32_t current) const
    {
        E equal;
        while (current != UINT32_MAX)
        {
            if (equal(m_keys[current], key))
                return current;
            current = m_next[current];
        }
        return current;
    }

    uint32_t m_size;
    uint32_t m_numSlots;
    uint32_t* m_slots;
    Array<Key> m_keys;
    Array<uint32_t> m_next;
};

template<typename T>
static void insertionSort(T* data, uint32_t length)
{
    for (int32_t i = 1; i < (int32_t)length; i++)
    {
        T x = data[i];
        int32_t j = i - 1;
        while (j >= 0 && x < data[j])
        {
            data[j + 1] = data[j];
            j--;
        }
        data[j + 1] = x;
    }
}

class KISSRng
{
public:
    KISSRng()
    {
        reset();
    }

    void reset()
    {
        x = 123456789;
        y = 362436000;
        z = 521288629;
        c = 7654321;
    }

    uint32_t getRange(uint32_t range)
    {
        if (range == 0)
            return 0;
        x = 69069 * x + 12345;
        y ^= (y << 13);
        y ^= (y >> 17);
        y ^= (y << 5);
        uint64_t t = 698769069ULL * z + c;
        c = (t >> 32);
        return (x + y + (z = (uint32_t)t)) % (range + 1);
    }

private:
    uint32_t x, y, z, c;
};

// Based on Pierre Terdiman's and Michael Herf's source code.
// http://www.codercorner.com/RadixSortRevisited.htm
// http://www.stereopsis.com/radix.html
class RadixSort
{
public:
    void sort(ConstArrayView<float> input)
    {
        if (input.length == 0)
        {
            m_buffer1.clear();
            m_buffer2.clear();
            m_ranks = m_buffer1.data();
            m_ranks2 = m_buffer2.data();
            return;
        }
        // Resize lists if needed
        m_buffer1.resize(input.length);
        m_buffer2.resize(input.length);
        m_ranks = m_buffer1.data();
        m_ranks2 = m_buffer2.data();
        m_validRanks = false;
        if (input.length < 32)
            insertionSort(input);
        else
        {
            // @@ Avoid touching the input multiple times.
            for (uint32_t i = 0; i < input.length; i++)
            {
                floatFlip((uint32_t&)input[i]);
            }
            radixSort(ConstArrayView<uint32_t>((const uint32_t*)input.data, input.length));
            for (uint32_t i = 0; i < input.length; i++)
            {
                ifloatFlip((uint32_t&)input[i]);
            }
        }
    }

    // Access to results. m_ranks is a list of indices in sorted order, i.e. in the order you may further process your data
    const uint32_t* ranks() const
    {
        XA_DEBUG_ASSERT(m_validRanks);
        return m_ranks;
    }

private:
    uint32_t *m_ranks, *m_ranks2;
    Array<uint32_t> m_buffer1, m_buffer2;
    bool m_validRanks = false;

    void floatFlip(uint32_t& f)
    {
        int32_t mask = (int32_t(f) >> 31) | 0x80000000; // Warren Hunt, Manchor Ko.
        f ^= mask;
    }

    void ifloatFlip(uint32_t& f)
    {
        uint32_t mask = ((f >> 31) - 1) | 0x80000000; // Michael Herf.
        f ^= mask;
    }

    void createHistograms(ConstArrayView<uint32_t> input, uint32_t* histogram)
    {
        const uint32_t bucketCount = sizeof(uint32_t);
        // Init bucket pointers.
        uint32_t* h[bucketCount];
        for (uint32_t i = 0; i < bucketCount; i++)
        {
            h[i] = histogram + 256 * i;
        }
        // Clear histograms.
        memset(histogram, 0, 256 * bucketCount * sizeof(uint32_t));
        // @@ Add support for signed integers.
        // Build histograms.
        const uint8_t* p = (const uint8_t*)input.data; // @@ Does this break aliasing rules?
        const uint8_t* pe = p + input.length * sizeof(uint32_t);
        while (p != pe)
        {
            h[0][*p++]++, h[1][*p++]++, h[2][*p++]++, h[3][*p++]++;
        }
    }

    void insertionSort(ConstArrayView<float> input)
    {
        if (! m_validRanks)
        {
            m_ranks[0] = 0;
            for (uint32_t i = 1; i != input.length; ++i)
            {
                int rank = m_ranks[i] = i;
                uint32_t j = i;
                while (j != 0 && input[rank] < input[m_ranks[j - 1]])
                {
                    m_ranks[j] = m_ranks[j - 1];
                    --j;
                }
                if (i != j)
                {
                    m_ranks[j] = rank;
                }
            }
            m_validRanks = true;
        }
        else
        {
            for (uint32_t i = 1; i != input.length; ++i)
            {
                int rank = m_ranks[i];
                uint32_t j = i;
                while (j != 0 && input[rank] < input[m_ranks[j - 1]])
                {
                    m_ranks[j] = m_ranks[j - 1];
                    --j;
                }
                if (i != j)
                {
                    m_ranks[j] = rank;
                }
            }
        }
    }

    void radixSort(ConstArrayView<uint32_t> input)
    {
        const uint32_t P = sizeof(uint32_t); // pass count
        // Allocate histograms & offsets on the stack
        uint32_t histogram[256 * P];
        uint32_t* link[256];
        createHistograms(input, histogram);
        // Radix sort, j is the pass number (0=LSB, P=MSB)
        for (uint32_t j = 0; j < P; j++)
        {
            // Pointer to this bucket.
            const uint32_t* h = &histogram[j * 256];
            auto inputBytes = (const uint8_t*)input.data; // @@ Is this aliasing legal?
            inputBytes += j;
            if (h[inputBytes[0]] == input.length)
            {
                // Skip this pass, all values are the same.
                continue;
            }
            // Create offsets
            link[0] = m_ranks2;
            for (uint32_t i = 1; i < 256; i++)
                link[i] = link[i - 1] + h[i - 1];
            // Perform Radix Sort
            if (! m_validRanks)
            {
                for (uint32_t i = 0; i < input.length; i++)
                {
                    *link[inputBytes[i * P]]++ = i;
                }
                m_validRanks = true;
            }
            else
            {
                for (uint32_t i = 0; i < input.length; i++)
                {
                    const uint32_t idx = m_ranks[i];
                    *link[inputBytes[idx * P]]++ = idx;
                }
            }
            // Swap pointers for next pass. Valid indices - the most recent ones - are in m_ranks after the swap.
            swap(m_ranks, m_ranks2);
        }
        // All values were equal, generate linear ranks.
        if (! m_validRanks)
        {
            for (uint32_t i = 0; i < input.length; i++)
                m_ranks[i] = i;
            m_validRanks = true;
        }
    }
};

// Wrapping this in a class allows temporary arrays to be re-used.
class BoundingBox2D
{
public:
    Vector2 majorAxis, minorAxis, minCorner, maxCorner;

    void clear()
    {
        m_boundaryVertices.clear();
    }

    void appendBoundaryVertex(Vector2 v)
    {
        m_boundaryVertices.push_back(v);
    }

    // This should compute convex hull and use rotating calipers to find the best box. Currently it uses a brute force method.
    // If vertices are empty, the boundary vertices are used.
    void compute(ConstArrayView<Vector2> vertices = ConstArrayView<Vector2>())
    {
        XA_DEBUG_ASSERT(! m_boundaryVertices.isEmpty());
        if (vertices.length == 0)
            vertices = m_boundaryVertices;
        convexHull(m_boundaryVertices, m_hull, 0.00001f);
        // @@ Ideally I should use rotating calipers to find the best box. Using brute force for now.
        float best_area = FLT_MAX;
        Vector2 best_min(0);
        Vector2 best_max(0);
        Vector2 best_axis(0);
        const uint32_t hullCount = m_hull.size();
        for (uint32_t i = 0, j = hullCount - 1; i < hullCount; j = i, i++)
        {
            if (equal(m_hull[i], m_hull[j], kEpsilon))
                continue;
            Vector2 axis = normalize(m_hull[i] - m_hull[j]);
            XA_DEBUG_ASSERT(isFinite(axis));
            // Compute bounding box.
            Vector2 box_min(FLT_MAX, FLT_MAX);
            Vector2 box_max(-FLT_MAX, -FLT_MAX);
            // Consider all points, not only boundary points, in case the input chart is malformed.
            for (uint32_t v = 0; v < vertices.length; v++)
            {
                const Vector2& point = vertices[v];
                const float x = dot(axis, point);
                const float y = dot(Vector2(-axis.y, axis.x), point);
                box_min.x = min(box_min.x, x);
                box_max.x = max(box_max.x, x);
                box_min.y = min(box_min.y, y);
                box_max.y = max(box_max.y, y);
            }
            // Compute box area.
            const float area = (box_max.x - box_min.x) * (box_max.y - box_min.y);
            if (area < best_area)
            {
                best_area = area;
                best_min = box_min;
                best_max = box_max;
                best_axis = axis;
            }
        }
        majorAxis = best_axis;
        minorAxis = Vector2(-best_axis.y, best_axis.x);
        minCorner = best_min;
        maxCorner = best_max;
        XA_ASSERT(isFinite(majorAxis) && isFinite(minorAxis) && isFinite(minCorner));
    }

private:
    // Compute the convex hull using Graham Scan.
    void convexHull(ConstArrayView<Vector2> input, Array<Vector2>& output, float epsilon)
    {
        m_coords.resize(input.length);
        for (uint32_t i = 0; i < input.length; i++)
            m_coords[i] = input[i].x;
        m_radix.sort(m_coords);
        const uint32_t* ranks = m_radix.ranks();
        m_top.clear();
        m_bottom.clear();
        m_top.reserve(input.length);
        m_bottom.reserve(input.length);
        Vector2 P = input[ranks[0]];
        Vector2 Q = input[ranks[input.length - 1]];
        float topy = max(P.y, Q.y);
        float boty = min(P.y, Q.y);
        for (uint32_t i = 0; i < input.length; i++)
        {
            Vector2 p = input[ranks[i]];
            if (p.y >= boty)
                m_top.push_back(p);
        }
        for (uint32_t i = 0; i < input.length; i++)
        {
            Vector2 p = input[ranks[input.length - 1 - i]];
            if (p.y <= topy)
                m_bottom.push_back(p);
        }
        // Filter top list.
        output.clear();
        XA_DEBUG_ASSERT(m_top.size() >= 2);
        output.push_back(m_top[0]);
        output.push_back(m_top[1]);
        for (uint32_t i = 2; i < m_top.size();)
        {
            Vector2 a = output[output.size() - 2];
            Vector2 b = output[output.size() - 1];
            Vector2 c = m_top[i];
            float area = triangleArea(a, b, c);
            if (area >= -epsilon)
                output.pop_back();
            if (area < -epsilon || output.size() == 1)
            {
                output.push_back(c);
                i++;
            }
        }
        uint32_t top_count = output.size();
        XA_DEBUG_ASSERT(m_bottom.size() >= 2);
        output.push_back(m_bottom[1]);
        // Filter bottom list.
        for (uint32_t i = 2; i < m_bottom.size();)
        {
            Vector2 a = output[output.size() - 2];
            Vector2 b = output[output.size() - 1];
            Vector2 c = m_bottom[i];
            float area = triangleArea(a, b, c);
            if (area >= -epsilon)
                output.pop_back();
            if (area < -epsilon || output.size() == top_count)
            {
                output.push_back(c);
                i++;
            }
        }
        // Remove duplicate element.
        XA_DEBUG_ASSERT(output.size() > 0);
        output.pop_back();
    }

    Array<Vector2> m_boundaryVertices;
    Array<float> m_coords;
    Array<Vector2> m_top, m_bottom, m_hull;
    RadixSort m_radix;
};

struct EdgeKey
{
    EdgeKey(const EdgeKey& k)
        : v0(k.v0)
        , v1(k.v1)
    {
    }
    EdgeKey(uint32_t _v0, uint32_t _v1)
        : v0(_v0)
        , v1(_v1)
    {
    }
    bool operator==(const EdgeKey& k) const
    {
        return v0 == k.v0 && v1 == k.v1;
    }

    uint32_t v0;
    uint32_t v1;
};

struct EdgeHash
{
    uint32_t operator()(const EdgeKey& k) const
    {
        return k.v0 * 32768u + k.v1;
    }
};

static uint32_t meshEdgeFace(uint32_t edge)
{
    return edge / 3;
}
static uint32_t meshEdgeIndex0(uint32_t edge)
{
    return edge;
}

static uint32_t meshEdgeIndex1(uint32_t edge)
{
    const uint32_t faceFirstEdge = edge / 3 * 3;
    return faceFirstEdge + (edge - faceFirstEdge + 1) % 3;
}

struct Spinlock
{
    void lock()
    {
        while (m_lock.test_and_set(std::memory_order_acquire))
        {
        }
    }
    void unlock()
    {
        m_lock.clear(std::memory_order_release);
    }

private:
    std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
};

struct TaskGroupHandle
{
    uint32_t value = UINT32_MAX;
};

struct Task
{
    void (*func)(void* groupUserData, void* taskUserData);
    void* userData; // Passed to func as taskUserData.
};

#if XA_MULTITHREADED
class TaskScheduler
{
public:
    TaskScheduler()
        : m_shutdown(false)
    {
        m_threadIndex = 0;
        // Max with current task scheduler usage is 1 per thread + 1 deep nesting, but allow for some slop.
        m_maxGroups = std::thread::hardware_concurrency() * 4;
        m_groups = XA_ALLOC_ARRAY(TaskGroup, m_maxGroups);
        for (uint32_t i = 0; i < m_maxGroups; i++)
        {
            new (&m_groups[i]) TaskGroup();
            m_groups[i].free = true;
            m_groups[i].ref = 0;
            m_groups[i].userData = nullptr;
        }
        m_workers.resize(std::thread::hardware_concurrency() <= 1 ? 1 : std::thread::hardware_concurrency() - 1);
        for (uint32_t i = 0; i < m_workers.size(); i++)
        {
            new (&m_workers[i]) Worker();
            m_workers[i].wakeup = false;
            m_workers[i].thread = XA_NEW_ARGS(std::thread, workerThread, this, &m_workers[i], i + 1);
        }
    }

    ~TaskScheduler()
    {
        m_shutdown = true;
        for (uint32_t i = 0; i < m_workers.size(); i++)
        {
            Worker& worker = m_workers[i];
            XA_DEBUG_ASSERT(worker.thread);
            worker.wakeup = true;
            worker.cv.notify_one();
            if (worker.thread->joinable())
                worker.thread->join();
            worker.thread->~thread();
            XA_FREE(worker.thread);
            worker.~Worker();
        }
        for (uint32_t i = 0; i < m_maxGroups; i++)
            m_groups[i].~TaskGroup();
        XA_FREE(m_groups);
    }

    uint32_t threadCount() const
    {
        return max(1u, std::thread::hardware_concurrency()); // Including the main thread.
    }

    // userData is passed to Task::func as groupUserData.
    TaskGroupHandle createTaskGroup(void* userData = nullptr, uint32_t reserveSize = 0)
    {
        // Claim the first free group.
        for (uint32_t i = 0; i < m_maxGroups; i++)
        {
            TaskGroup& group = m_groups[i];
            bool expected = true;
            if (! group.free.compare_exchange_strong(expected, false))
                continue;
            group.queueLock.lock();
            group.queueHead = 0;
            group.queue.clear();
            group.queue.reserve(reserveSize);
            group.queueLock.unlock();
            group.userData = userData;
            group.ref = 0;
            TaskGroupHandle handle;
            handle.value = i;
            return handle;
        }
        XA_DEBUG_ASSERT(false);
        TaskGroupHandle handle;
        handle.value = UINT32_MAX;
        return handle;
    }

    void run(TaskGroupHandle handle, const Task& task)
    {
        XA_DEBUG_ASSERT(handle.value != UINT32_MAX);
        TaskGroup& group = m_groups[handle.value];
        group.queueLock.lock();
        group.queue.push_back(task);
        group.queueLock.unlock();
        group.ref++;
        // Wake up a worker to run this task.
        for (uint32_t i = 0; i < m_workers.size(); i++)
        {
            m_workers[i].wakeup = true;
            m_workers[i].cv.notify_one();
        }
    }

    void wait(TaskGroupHandle* handle)
    {
        if (handle->value == UINT32_MAX)
        {
            XA_DEBUG_ASSERT(false);
            return;
        }
        // Run tasks from the group queue until empty.
        TaskGroup& group = m_groups[handle->value];
        for (;;)
        {
            Task* task = nullptr;
            group.queueLock.lock();
            if (group.queueHead < group.queue.size())
                task = &group.queue[group.queueHead++];
            group.queueLock.unlock();
            if (! task)
                break;
            task->func(group.userData, task->userData);
            group.ref--;
        }
        // Even though the task queue is empty, workers can still be running tasks.
        while (group.ref > 0)
            std::this_thread::yield();
        group.free = true;
        handle->value = UINT32_MAX;
    }

    static uint32_t currentThreadIndex()
    {
        return m_threadIndex;
    }

private:
    struct TaskGroup
    {
        std::atomic<bool> free;
        Array<Task> queue; // Items are never removed. queueHead is incremented to pop items.
        uint32_t queueHead = 0;
        Spinlock queueLock;
        std::atomic<uint32_t> ref; // Increment when a task is enqueued, decrement when a task finishes.
        void* userData;
    };

    struct Worker
    {
        std::thread* thread = nullptr;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> wakeup;
    };

    TaskGroup* m_groups;
    Array<Worker> m_workers;
    std::atomic<bool> m_shutdown;
    uint32_t m_maxGroups;
    static thread_local uint32_t m_threadIndex;

    static void workerThread(TaskScheduler* scheduler, Worker* worker, uint32_t threadIndex)
    {
        m_threadIndex = threadIndex;
        std::unique_lock<std::mutex> lock(worker->mutex);
        for (;;)
        {
            worker->cv.wait(
                lock,
                [=]
                {
                    return worker->wakeup.load();
                });
            worker->wakeup = false;
            for (;;)
            {
                if (scheduler->m_shutdown)
                    return;
                // Look for a task in any of the groups and run it.
                TaskGroup* group = nullptr;
                Task* task = nullptr;
                for (uint32_t i = 0; i < scheduler->m_maxGroups; i++)
                {
                    group = &scheduler->m_groups[i];
                    if (group->free || group->ref == 0)
                        continue;
                    group->queueLock.lock();
                    if (group->queueHead < group->queue.size())
                    {
                        task = &group->queue[group->queueHead++];
                        group->queueLock.unlock();
                        break;
                    }
                    group->queueLock.unlock();
                }
                if (! task)
                    break;
                task->func(group->userData, task->userData);
                group->ref--;
            }
        }
    }
};

thread_local uint32_t TaskScheduler::m_threadIndex;
#else
class TaskScheduler
{
public:
    ~TaskScheduler()
    {
        for (uint32_t i = 0; i < m_groups.size(); i++)
            destroyGroup({ i });
    }

    uint32_t threadCount() const
    {
        return 1;
    }

    TaskGroupHandle createTaskGroup(void* userData = nullptr, uint32_t reserveSize = 0)
    {
        TaskGroup* group = XA_NEW(TaskGroup);
        group->queue.reserve(reserveSize);
        group->userData = userData;
        m_groups.push_back(group);
        TaskGroupHandle handle;
        handle.value = m_groups.size() - 1;
        return handle;
    }

    void run(TaskGroupHandle handle, Task task)
    {
        m_groups[handle.value]->queue.push_back(task);
    }

    void wait(TaskGroupHandle* handle)
    {
        if (handle->value == UINT32_MAX)
        {
            XA_DEBUG_ASSERT(false);
            return;
        }
        TaskGroup* group = m_groups[handle->value];
        for (uint32_t i = 0; i < group->queue.size(); i++)
            group->queue[i].func(group->userData, group->queue[i].userData);
        group->queue.clear();
        destroyGroup(*handle);
        handle->value = UINT32_MAX;
    }

    static uint32_t currentThreadIndex()
    {
        return 0;
    }

private:
    void destroyGroup(TaskGroupHandle handle)
    {
        TaskGroup* group = m_groups[handle.value];
        if (group)
        {
            group->~TaskGroup();
            XA_FREE(group);
            m_groups[handle.value] = nullptr;
        }
    }

    struct TaskGroup
    {
        Array<Task> queue;
        void* userData;
    };

    Array<TaskGroup*> m_groups;
};
#endif

template<typename T>
class ThreadLocal
{
public:
    ThreadLocal()
    {
#if XA_MULTITHREADED
        const uint32_t n = std::thread::hardware_concurrency();
#else
        const uint32_t n = 1;
#endif
        m_array = XA_ALLOC_ARRAY(T, n);
        for (uint32_t i = 0; i < n; i++)
            new (&m_array[i]) T;
    }

    ~ThreadLocal()
    {
#if XA_MULTITHREADED
        const uint32_t n = std::thread::hardware_concurrency();
#else
        const uint32_t n = 1;
#endif
        for (uint32_t i = 0; i < n; i++)
            m_array[i].~T();
        XA_FREE(m_array);
    }

    T& get() const
    {
        return m_array[TaskScheduler::currentThreadIndex()];
    }

private:
    T* m_array;
};

class UniformGrid2
{
public:
    // indices are optional.
    void reset(ConstArrayView<Vector2> positions, ConstArrayView<uint32_t> indices = ConstArrayView<uint32_t>(), uint32_t reserveEdgeCount = 0)
    {
        m_edges.clear();
        if (reserveEdgeCount > 0)
            m_edges.reserve(reserveEdgeCount);
        m_positions = positions;
        m_indices = indices;
        m_cellDataOffsets.clear();
    }

    void append(uint32_t edge)
    {
        XA_DEBUG_ASSERT(m_cellDataOffsets.isEmpty());
        m_edges.push_back(edge);
    }

    bool intersect(Vector2 v1, Vector2 v2, float epsilon)
    {
        const uint32_t edgeCount = m_edges.size();
        bool bruteForce = edgeCount <= 20;
        if (! bruteForce && m_cellDataOffsets.isEmpty())
            bruteForce = ! createGrid();
        if (bruteForce)
        {
            for (uint32_t j = 0; j < edgeCount; j++)
            {
                const uint32_t edge = m_edges[j];
                if (linesIntersect(v1, v2, edgePosition0(edge), edgePosition1(edge), epsilon))
                    return true;
            }
        }
        else
        {
            computePotentialEdges(v1, v2);
            uint32_t prevEdge = UINT32_MAX;
            for (uint32_t j = 0; j < m_potentialEdges.size(); j++)
            {
                const uint32_t edge = m_potentialEdges[j];
                if (edge == prevEdge)
                    continue;
                if (linesIntersect(v1, v2, edgePosition0(edge), edgePosition1(edge), epsilon))
                    return true;
                prevEdge = edge;
            }
        }
        return false;
    }

    // If edges is empty, checks for intersection with all edges in the grid.
    bool intersect(float epsilon, ConstArrayView<uint32_t> edges = ConstArrayView<uint32_t>(), ConstArrayView<uint32_t> ignoreEdges = ConstArrayView<uint32_t>())
    {
        bool bruteForce = m_edges.size() <= 20;
        if (! bruteForce && m_cellDataOffsets.isEmpty())
            bruteForce = ! createGrid();
        const uint32_t *edges1, *edges2 = nullptr;
        uint32_t edges1Count, edges2Count = 0;
        if (edges.length == 0)
        {
            edges1 = m_edges.data();
            edges1Count = m_edges.size();
        }
        else
        {
            edges1 = edges.data;
            edges1Count = edges.length;
        }
        if (bruteForce)
        {
            edges2 = m_edges.data();
            edges2Count = m_edges.size();
        }
        for (uint32_t i = 0; i < edges1Count; i++)
        {
            const uint32_t edge1 = edges1[i];
            const uint32_t edge1Vertex[2] = { vertexAt(meshEdgeIndex0(edge1)), vertexAt(meshEdgeIndex1(edge1)) };
            const Vector2& edge1Position1 = m_positions[edge1Vertex[0]];
            const Vector2& edge1Position2 = m_positions[edge1Vertex[1]];
            const Extents2 edge1Extents(edge1Position1, edge1Position2);
            uint32_t j = 0;
            if (bruteForce)
            {
                // If checking against self, test each edge pair only once.
                if (edges.length == 0)
                {
                    j = i + 1;
                    if (j == edges1Count)
                        break;
                }
            }
            else
            {
                computePotentialEdges(edgePosition0(edge1), edgePosition1(edge1));
                edges2 = m_potentialEdges.data();
                edges2Count = m_potentialEdges.size();
            }
            uint32_t prevEdge = UINT32_MAX; // Handle potential edges duplicates.
            for (; j < edges2Count; j++)
            {
                const uint32_t edge2 = edges2[j];
                if (edge1 == edge2)
                    continue;
                if (edge2 == prevEdge)
                    continue;
                prevEdge = edge2;
                // Check if edge2 is ignored.
                bool ignore = false;
                for (uint32_t k = 0; k < ignoreEdges.length; k++)
                {
                    if (edge2 == ignoreEdges[k])
                    {
                        ignore = true;
                        break;
                    }
                }
                if (ignore)
                    continue;
                const uint32_t edge2Vertex[2] = { vertexAt(meshEdgeIndex0(edge2)), vertexAt(meshEdgeIndex1(edge2)) };
                // Ignore connected edges, since they can't intersect (only overlap), and may be detected as false positives.
                if (edge1Vertex[0] == edge2Vertex[0] || edge1Vertex[0] == edge2Vertex[1] || edge1Vertex[1] == edge2Vertex[0] || edge1Vertex[1] == edge2Vertex[1])
                    continue;
                const Vector2& edge2Position1 = m_positions[edge2Vertex[0]];
                const Vector2& edge2Position2 = m_positions[edge2Vertex[1]];
                if (! Extents2::intersect(edge1Extents, Extents2(edge2Position1, edge2Position2)))
                    continue;
                if (linesIntersect(edge1Position1, edge1Position2, edge2Position1, edge2Position2, epsilon))
                    return true;
            }
        }
        return false;
    }

private:
    bool createGrid()
    {
        // Compute edge extents. Min will be the grid origin.
        const uint32_t edgeCount = m_edges.size();
        Extents2 edgeExtents;
        edgeExtents.reset();
        for (uint32_t i = 0; i < edgeCount; i++)
        {
            const uint32_t edge = m_edges[i];
            edgeExtents.add(edgePosition0(edge));
            edgeExtents.add(edgePosition1(edge));
        }
        m_gridOrigin = edgeExtents.min;
        // Size grid to approximately one edge per cell in the largest dimension.
        const Vector2 extentsSize(edgeExtents.max - edgeExtents.min);
        m_cellSize = max(extentsSize.x, extentsSize.y) / (float)clamp(edgeCount, 32u, 512u);
        if (m_cellSize <= 0.0f)
            return false;
        m_gridWidth = uint32_t(ceilf(extentsSize.x / m_cellSize));
        m_gridHeight = uint32_t(ceilf(extentsSize.y / m_cellSize));
        if (m_gridWidth <= 1 || m_gridHeight <= 1)
            return false;
        // Insert edges into cells.
        m_cellDataOffsets.resize(m_gridWidth * m_gridHeight);
        for (uint32_t i = 0; i < m_cellDataOffsets.size(); i++)
            m_cellDataOffsets[i] = UINT32_MAX;
        m_cellData.clear();
        m_cellData.reserve(edgeCount * 2);
        for (uint32_t i = 0; i < edgeCount; i++)
        {
            const uint32_t edge = m_edges[i];
            traverse(edgePosition0(edge), edgePosition1(edge));
            XA_DEBUG_ASSERT(! m_traversedCellOffsets.isEmpty());
            for (uint32_t j = 0; j < m_traversedCellOffsets.size(); j++)
            {
                const uint32_t cell = m_traversedCellOffsets[j];
                uint32_t offset = m_cellDataOffsets[cell];
                if (offset == UINT32_MAX)
                    m_cellDataOffsets[cell] = m_cellData.size();
                else
                {
                    for (;;)
                    {
                        uint32_t& nextOffset = m_cellData[offset + 1];
                        if (nextOffset == UINT32_MAX)
                        {
                            nextOffset = m_cellData.size();
                            break;
                        }
                        offset = nextOffset;
                    }
                }
                m_cellData.push_back(edge);
                m_cellData.push_back(UINT32_MAX);
            }
        }
        return true;
    }

    void computePotentialEdges(Vector2 p1, Vector2 p2)
    {
        m_potentialEdges.clear();
        traverse(p1, p2);
        for (uint32_t j = 0; j < m_traversedCellOffsets.size(); j++)
        {
            const uint32_t cell = m_traversedCellOffsets[j];
            uint32_t offset = m_cellDataOffsets[cell];
            while (offset != UINT32_MAX)
            {
                const uint32_t edge2 = m_cellData[offset];
                m_potentialEdges.push_back(edge2);
                offset = m_cellData[offset + 1];
            }
        }
        if (m_potentialEdges.isEmpty())
            return;
        insertionSort(m_potentialEdges.data(), m_potentialEdges.size());
    }

    // "A Fast Voxel Traversal Algorithm for Ray Tracing"
    void traverse(Vector2 p1, Vector2 p2)
    {
        const Vector2 dir = p2 - p1;
        const Vector2 normal = normalizeSafe(dir, Vector2(0.0f));
        const int stepX = dir.x >= 0 ? 1 : -1;
        const int stepY = dir.y >= 0 ? 1 : -1;
        const uint32_t firstCell[2] = { cellX(p1.x), cellY(p1.y) };
        const uint32_t lastCell[2] = { cellX(p2.x), cellY(p2.y) };
        float distToNextCellX;
        if (stepX == 1)
            distToNextCellX = (firstCell[0] + 1) * m_cellSize - (p1.x - m_gridOrigin.x);
        else
            distToNextCellX = (p1.x - m_gridOrigin.x) - firstCell[0] * m_cellSize;
        float distToNextCellY;
        if (stepY == 1)
            distToNextCellY = (firstCell[1] + 1) * m_cellSize - (p1.y - m_gridOrigin.y);
        else
            distToNextCellY = (p1.y - m_gridOrigin.y) - firstCell[1] * m_cellSize;
        float tMaxX, tMaxY, tDeltaX, tDeltaY;
        if (normal.x > kEpsilon || normal.x < -kEpsilon)
        {
            tMaxX = (distToNextCellX * stepX) / normal.x;
            tDeltaX = (m_cellSize * stepX) / normal.x;
        }
        else
            tMaxX = tDeltaX = FLT_MAX;
        if (normal.y > kEpsilon || normal.y < -kEpsilon)
        {
            tMaxY = (distToNextCellY * stepY) / normal.y;
            tDeltaY = (m_cellSize * stepY) / normal.y;
        }
        else
            tMaxY = tDeltaY = FLT_MAX;
        m_traversedCellOffsets.clear();
        m_traversedCellOffsets.push_back(firstCell[0] + firstCell[1] * m_gridWidth);
        uint32_t currentCell[2] = { firstCell[0], firstCell[1] };
        while (! (currentCell[0] == lastCell[0] && currentCell[1] == lastCell[1]))
        {
            if (tMaxX < tMaxY)
            {
                tMaxX += tDeltaX;
                currentCell[0] += stepX;
            }
            else
            {
                tMaxY += tDeltaY;
                currentCell[1] += stepY;
            }
            if (currentCell[0] >= m_gridWidth || currentCell[1] >= m_gridHeight)
                break;
            if (stepX == -1 && currentCell[0] < lastCell[0])
                break;
            if (stepX == 1 && currentCell[0] > lastCell[0])
                break;
            if (stepY == -1 && currentCell[1] < lastCell[1])
                break;
            if (stepY == 1 && currentCell[1] > lastCell[1])
                break;
            m_traversedCellOffsets.push_back(currentCell[0] + currentCell[1] * m_gridWidth);
        }
    }

    uint32_t cellX(float x) const
    {
        return min((uint32_t)max(0.0f, (x - m_gridOrigin.x) / m_cellSize), m_gridWidth - 1u);
    }

    uint32_t cellY(float y) const
    {
        return min((uint32_t)max(0.0f, (y - m_gridOrigin.y) / m_cellSize), m_gridHeight - 1u);
    }

    Vector2 edgePosition0(uint32_t edge) const
    {
        return m_positions[vertexAt(meshEdgeIndex0(edge))];
    }

    Vector2 edgePosition1(uint32_t edge) const
    {
        return m_positions[vertexAt(meshEdgeIndex1(edge))];
    }

    uint32_t vertexAt(uint32_t index) const
    {
        return m_indices.length > 0 ? m_indices[index] : index;
    }

    Array<uint32_t> m_edges;
    ConstArrayView<Vector2> m_positions;
    ConstArrayView<uint32_t> m_indices; // Optional. Empty if unused.
    float m_cellSize;
    Vector2 m_gridOrigin;
    uint32_t m_gridWidth, m_gridHeight; // in cells
    Array<uint32_t> m_cellDataOffsets;
    Array<uint32_t> m_cellData;
    Array<uint32_t> m_potentialEdges;
    Array<uint32_t> m_traversedCellOffsets;
};

struct UvMeshChart
{
    Array<uint32_t> faces;
    Array<uint32_t> indices;
    uint32_t material;
};

struct UvMesh
{
    UvMeshDecl decl;
    BitArray faceIgnore;
    Array<uint32_t> faceMaterials;
    Array<uint32_t> indices;
    Array<Vector2>
        texcoords; // Copied from input and never modified, UvMeshInstance::texcoords are. Used to restore UvMeshInstance::texcoords so packing can be run multiple times.
    Array<UvMeshChart*> charts;
    Array<uint32_t> vertexToChartMap;
};

struct UvMeshInstance
{
    UvMesh* mesh;
    Array<Vector2> texcoords;
};

namespace raster
{
class ClippedTriangle
{
public:
    ClippedTriangle(const Vector2& a, const Vector2& b, const Vector2& c)
    {
        m_numVertices = 3;
        m_activeVertexBuffer = 0;
        m_verticesA[0] = a;
        m_verticesA[1] = b;
        m_verticesA[2] = c;
        m_vertexBuffers[0] = m_verticesA;
        m_vertexBuffers[1] = m_verticesB;
        m_area = 0;
    }

    void clipHorizontalPlane(float offset, float clipdirection)
    {
        Vector2* v = m_vertexBuffers[m_activeVertexBuffer];
        m_activeVertexBuffer ^= 1;
        Vector2* v2 = m_vertexBuffers[m_activeVertexBuffer];
        v[m_numVertices] = v[0];
        float dy2, dy1 = offset - v[0].y;
        int dy2in, dy1in = clipdirection * dy1 >= 0;
        uint32_t p = 0;
        for (uint32_t k = 0; k < m_numVertices; k++)
        {
            dy2 = offset - v[k + 1].y;
            dy2in = clipdirection * dy2 >= 0;
            if (dy1in)
                v2[p++] = v[k];
            if (dy1in + dy2in == 1)
            { // not both in/out
                float dx = v[k + 1].x - v[k].x;
                float dy = v[k + 1].y - v[k].y;
                v2[p++] = Vector2(v[k].x + dy1 * (dx / dy), offset);
            }
            dy1 = dy2;
            dy1in = dy2in;
        }
        m_numVertices = p;
    }

    void clipVerticalPlane(float offset, float clipdirection)
    {
        Vector2* v = m_vertexBuffers[m_activeVertexBuffer];
        m_activeVertexBuffer ^= 1;
        Vector2* v2 = m_vertexBuffers[m_activeVertexBuffer];
        v[m_numVertices] = v[0];
        float dx2, dx1 = offset - v[0].x;
        int dx2in, dx1in = clipdirection * dx1 >= 0;
        uint32_t p = 0;
        for (uint32_t k = 0; k < m_numVertices; k++)
        {
            dx2 = offset - v[k + 1].x;
            dx2in = clipdirection * dx2 >= 0;
            if (dx1in)
                v2[p++] = v[k];
            if (dx1in + dx2in == 1)
            { // not both in/out
                float dx = v[k + 1].x - v[k].x;
                float dy = v[k + 1].y - v[k].y;
                v2[p++] = Vector2(offset, v[k].y + dx1 * (dy / dx));
            }
            dx1 = dx2;
            dx1in = dx2in;
        }
        m_numVertices = p;
    }

    void computeArea()
    {
        Vector2* v = m_vertexBuffers[m_activeVertexBuffer];
        v[m_numVertices] = v[0];
        m_area = 0;
        for (uint32_t k = 0; k < m_numVertices; k++)
        {
            // http://local.wasp.uwa.edu.au/~pbourke/geometry/polyarea/
            float f = v[k].x * v[k + 1].y - v[k + 1].x * v[k].y;
            m_area += f;
        }
        m_area = 0.5f * fabsf(m_area);
    }

    void clipAABox(float x0, float y0, float x1, float y1)
    {
        clipVerticalPlane(x0, -1);
        clipHorizontalPlane(y0, -1);
        clipVerticalPlane(x1, 1);
        clipHorizontalPlane(y1, 1);
        computeArea();
    }

    float area() const
    {
        return m_area;
    }

private:
    Vector2 m_verticesA[7 + 1];
    Vector2 m_verticesB[7 + 1];
    Vector2* m_vertexBuffers[2];
    uint32_t m_numVertices;
    uint32_t m_activeVertexBuffer;
    float m_area;
};

/// A callback to sample the environment. Return false to terminate rasterization.
typedef bool (*SamplingCallback)(void* param, int x, int y);

/// A triangle for rasterization.
struct Triangle
{
    Triangle(const Vector2& _v0, const Vector2& _v1, const Vector2& _v2)
        : v1(_v0)
        , v2(_v2)
        , v3(_v1)
        , n1(0.0f)
        , n2(0.0f)
        , n3(0.0f)
    {
        // make sure every triangle is front facing.
        flipBackface();
        // Compute deltas.
        if (isValid())
            computeUnitInwardNormals();
    }

    bool isValid()
    {
        const Vector2 e0 = v3 - v1;
        const Vector2 e1 = v2 - v1;
        const float area = e0.y * e1.x - e1.y * e0.x;
        return area != 0.0f;
    }

    // extents has to be multiple of BK_SIZE!!
    bool drawAA(const Vector2& extents, SamplingCallback cb, void* param)
    {
        const float PX_INSIDE = 1.0f / sqrtf(2.0f);
        const float PX_OUTSIDE = -1.0f / sqrtf(2.0f);
        const float BK_SIZE = 8;
        const float BK_INSIDE = sqrtf(BK_SIZE * BK_SIZE / 2.0f);
        const float BK_OUTSIDE = -sqrtf(BK_SIZE * BK_SIZE / 2.0f);
        // Bounding rectangle
        float minx = floorf(max(min3(v1.x, v2.x, v3.x), 0.0f));
        float miny = floorf(max(min3(v1.y, v2.y, v3.y), 0.0f));
        float maxx = ceilf(min(max3(v1.x, v2.x, v3.x), extents.x - 1.0f));
        float maxy = ceilf(min(max3(v1.y, v2.y, v3.y), extents.y - 1.0f));
        // There's no reason to align the blocks to the viewport, instead we align them to the origin of the triangle bounds.
        minx = floorf(minx);
        miny = floorf(miny);
        // minx = (float)(((int)minx) & (~((int)BK_SIZE - 1))); // align to blocksize (we don't need to worry about blocks partially out of viewport)
        // miny = (float)(((int)miny) & (~((int)BK_SIZE - 1)));
        minx += 0.5;
        miny += 0.5; // sampling at texel centers!
        maxx += 0.5;
        maxy += 0.5;
        // Half-edge constants
        float C1 = n1.x * (-v1.x) + n1.y * (-v1.y);
        float C2 = n2.x * (-v2.x) + n2.y * (-v2.y);
        float C3 = n3.x * (-v3.x) + n3.y * (-v3.y);
        // Loop through blocks
        for (float y0 = miny; y0 <= maxy; y0 += BK_SIZE)
        {
            for (float x0 = minx; x0 <= maxx; x0 += BK_SIZE)
            {
                // Corners of block
                float xc = (x0 + (BK_SIZE - 1) / 2.0f);
                float yc = (y0 + (BK_SIZE - 1) / 2.0f);
                // Evaluate half-space functions
                float aC = C1 + n1.x * xc + n1.y * yc;
                float bC = C2 + n2.x * xc + n2.y * yc;
                float cC = C3 + n3.x * xc + n3.y * yc;
                // Skip block when outside an edge
                if ((aC <= BK_OUTSIDE) || (bC <= BK_OUTSIDE) || (cC <= BK_OUTSIDE))
                    continue;
                // Accept whole block when totally covered
                if ((aC >= BK_INSIDE) && (bC >= BK_INSIDE) && (cC >= BK_INSIDE))
                {
                    for (float y = y0; y < y0 + BK_SIZE; y++)
                    {
                        for (float x = x0; x < x0 + BK_SIZE; x++)
                        {
                            if (! cb(param, (int)x, (int)y))
                                return false;
                        }
                    }
                }
                else
                { // Partially covered block
                    float CY1 = C1 + n1.x * x0 + n1.y * y0;
                    float CY2 = C2 + n2.x * x0 + n2.y * y0;
                    float CY3 = C3 + n3.x * x0 + n3.y * y0;
                    for (float y = y0; y < y0 + BK_SIZE; y++)
                    { // @@ This is not clipping to scissor rectangle correctly.
                        float CX1 = CY1;
                        float CX2 = CY2;
                        float CX3 = CY3;
                        for (float x = x0; x < x0 + BK_SIZE; x++)
                        { // @@ This is not clipping to scissor rectangle correctly.
                            if (CX1 >= PX_INSIDE && CX2 >= PX_INSIDE && CX3 >= PX_INSIDE)
                            {
                                if (! cb(param, (int)x, (int)y))
                                    return false;
                            }
                            else if ((CX1 >= PX_OUTSIDE) && (CX2 >= PX_OUTSIDE) && (CX3 >= PX_OUTSIDE))
                            {
                                // triangle partially covers pixel. do clipping.
                                ClippedTriangle ct(v1 - Vector2(x, y), v2 - Vector2(x, y), v3 - Vector2(x, y));
                                ct.clipAABox(-0.5, -0.5, 0.5, 0.5);
                                if (ct.area() > 0.0f)
                                {
                                    if (! cb(param, (int)x, (int)y))
                                        return false;
                                }
                            }
                            CX1 += n1.x;
                            CX2 += n2.x;
                            CX3 += n3.x;
                        }
                        CY1 += n1.y;
                        CY2 += n2.y;
                        CY3 += n3.y;
                    }
                }
            }
        }
        return true;
    }

private:
    void flipBackface()
    {
        // check if triangle is backfacing, if so, swap two vertices
        if (((v3.x - v1.x) * (v2.y - v1.y) - (v3.y - v1.y) * (v2.x - v1.x)) < 0)
        {
            Vector2 hv = v1;
            v1 = v2;
            v2 = hv; // swap pos
        }
    }

    // compute unit inward normals for each edge.
    void computeUnitInwardNormals()
    {
        n1 = v1 - v2;
        n1 = Vector2(-n1.y, n1.x);
        n1 = n1 * (1.0f / sqrtf(dot(n1, n1)));
        n2 = v2 - v3;
        n2 = Vector2(-n2.y, n2.x);
        n2 = n2 * (1.0f / sqrtf(dot(n2, n2)));
        n3 = v3 - v1;
        n3 = Vector2(-n3.y, n3.x);
        n3 = n3 * (1.0f / sqrtf(dot(n3, n3)));
    }

    // Vertices.
    Vector2 v1, v2, v3;
    Vector2 n1, n2, n3; // unit inward normals
};

// Process the given triangle. Returns false if rasterization was interrupted by the callback.
static bool drawTriangle(const Vector2& extents, const Vector2 v[3], SamplingCallback cb, void* param)
{
    Triangle tri(v[0], v[1], v[2]);
    // @@ It would be nice to have a conservative drawing mode that enlarges the triangle extents by one texel and is able to handle degenerate triangles.
    // @@ Maybe the simplest thing to do would be raster triangle edges.
    if (tri.isValid())
        return tri.drawAA(extents, cb, param);
    return true;
}

} // namespace raster

namespace segment
{

// - Insertion is o(n)
// - Smallest element goes at the end, so that popping it is o(1).
struct CostQueue
{
    CostQueue(uint32_t size = UINT32_MAX)
        : m_maxSize(size)
        , m_pairs()
    {
    }

    float peekCost() const
    {
        return m_pairs.back().cost;
    }

    uint32_t peekFace() const
    {
        return m_pairs.back().face;
    }

    void push(float cost, uint32_t face)
    {
        const Pair p = { cost, face };
        if (m_pairs.isEmpty() || cost < peekCost())
            m_pairs.push_back(p);
        else
        {
            uint32_t i = 0;
            const uint32_t count = m_pairs.size();
            for (; i < count; i++)
            {
                if (m_pairs[i].cost < cost)
                    break;
            }
            m_pairs.insertAt(i, p);
            if (m_pairs.size() > m_maxSize)
                m_pairs.removeAt(0);
        }
    }

    uint32_t pop()
    {
        XA_DEBUG_ASSERT(! m_pairs.isEmpty());
        uint32_t f = m_pairs.back().face;
        m_pairs.pop_back();
        return f;
    }

    XA_INLINE void clear()
    {
        m_pairs.clear();
    }

    XA_INLINE uint32_t count() const
    {
        return m_pairs.size();
    }

private:
    const uint32_t m_maxSize;

    struct Pair
    {
        float cost;
        uint32_t face;
    };

    Array<Pair> m_pairs;
};

// Charts are found by floodfilling faces without crossing UV seams.
struct SetUvMeshChartsTask
{
    SetUvMeshChartsTask(UvMesh* const mesh, const std::vector<std::vector<size_t>>& grouped_faces)
        : m_mesh(mesh)
        , m_grouped_faces(grouped_faces)
        , m_faceAssigned(m_mesh->indices.size() / 3)
    {
    }

    void run()
    {
        const uint32_t vertexCount = m_mesh->texcoords.size();
        const uint32_t indexCount = m_mesh->indices.size();
        const uint32_t faceCount = indexCount / 3;

        // A vertex can only be assigned to one chart.
        m_mesh->vertexToChartMap.resize(vertexCount);
        m_mesh->vertexToChartMap.fill(UINT32_MAX);

        // Assign charts
        m_faceAssigned.zeroOutMemory();
        for (const std::vector<size_t>& face_group : m_grouped_faces)
        {
            const uint32_t chartIndex = m_mesh->charts.size();
            UvMeshChart* chart = XA_NEW(UvMeshChart);
            m_mesh->charts.push_back(chart);
            chart->material = 0;

            for (const size_t face_index : face_group)
            {
                if (canAddFaceToChart(chartIndex, face_index))
                {
                    addFaceToChart(chartIndex, face_index);
                }
            }
        }
    }

private:
    // The chart at chartIndex doesn't have to exist yet.
    bool canAddFaceToChart(uint32_t chartIndex, uint32_t face) const
    {
        if (m_faceAssigned.get(face))
            return false; // Already assigned to a chart.
        if (m_mesh->faceIgnore.get(face))
            return false; // Face is ignored (zero area or nan UVs).
        if (! m_mesh->faceMaterials.isEmpty() && chartIndex < m_mesh->charts.size())
        {
            if (m_mesh->faceMaterials[face] != m_mesh->charts[chartIndex]->material)
                return false; // Materials don't match.
        }
        for (uint32_t i = 0; i < 3; i++)
        {
            const uint32_t vertex = m_mesh->indices[face * 3 + i];
            if (m_mesh->vertexToChartMap[vertex] != UINT32_MAX && m_mesh->vertexToChartMap[vertex] != chartIndex)
                return false; // Vertex already assigned to another chart.
        }
        return true;
    }

    void addFaceToChart(uint32_t chartIndex, uint32_t face)
    {
        UvMeshChart* chart = m_mesh->charts[chartIndex];
        m_faceAssigned.set(face);
        chart->faces.push_back(face);
        for (uint32_t i = 0; i < 3; i++)
        {
            const uint32_t vertex = m_mesh->indices[face * 3 + i];
            m_mesh->vertexToChartMap[vertex] = chartIndex;
            chart->indices.push_back(vertex);
        }
    }

    UvMesh* const m_mesh;
    const std::vector<std::vector<size_t>>& m_grouped_faces;
    BitArray m_faceAssigned;
};

} // namespace segment

namespace pack
{

struct Chart
{
    int32_t atlasIndex;
    uint32_t material;
    ConstArrayView<uint32_t> indices;
    float parametricArea;
    float surfaceArea;
    ArrayView<Vector2> vertices;
    Array<uint32_t> uniqueVertices;
    // bounding box
    Vector2 majorAxis, minorAxis, minCorner, maxCorner;
    // Mesh only
    const Array<uint32_t>* boundaryEdges;
    // UvMeshChart only
    Array<uint32_t> faces;

    Vector2& uniqueVertexAt(uint32_t v)
    {
        return uniqueVertices.isEmpty() ? vertices[v] : vertices[uniqueVertices[v]];
    }
    uint32_t uniqueVertexCount() const
    {
        return uniqueVertices.isEmpty() ? vertices.length : uniqueVertices.size();
    }
};

struct Atlas
{
    ~Atlas()
    {
        for (uint32_t i = 0; i < m_bitImages.size(); i++)
        {
            m_bitImages[i]->~BitImage();
            XA_FREE(m_bitImages[i]);
        }
        for (uint32_t i = 0; i < m_charts.size(); i++)
        {
            m_charts[i]->~Chart();
            XA_FREE(m_charts[i]);
        }
    }

    uint32_t getWidth() const
    {
        return m_width;
    }
    uint32_t getHeight() const
    {
        return m_height;
    }
    uint32_t getNumAtlases() const
    {
        return m_bitImages.size();
    }
    float getTexelsPerUnit() const
    {
        return m_texelsPerUnit;
    }
    const Chart* getChart(uint32_t index) const
    {
        return m_charts[index];
    }
    uint32_t getChartCount() const
    {
        return m_charts.size();
    }
    float getUtilization(uint32_t atlas) const
    {
        return m_utilization[atlas];
    }

    void addUvMeshCharts(UvMeshInstance* mesh)
    {
        // Copy texcoords from mesh.
        mesh->texcoords.resize(mesh->mesh->texcoords.size());
        memcpy(mesh->texcoords.data(), mesh->mesh->texcoords.data(), mesh->texcoords.size() * sizeof(Vector2));
        BitArray vertexUsed(mesh->texcoords.size());
        BoundingBox2D boundingBox;
        for (uint32_t c = 0; c < mesh->mesh->charts.size(); c++)
        {
            UvMeshChart* uvChart = mesh->mesh->charts[c];
            Chart* chart = XA_NEW(Chart);
            chart->atlasIndex = -1;
            chart->material = uvChart->material;
            chart->indices = uvChart->indices;
            chart->vertices = mesh->texcoords;
            chart->boundaryEdges = nullptr;
            chart->faces.resize(uvChart->faces.size());
            memcpy(chart->faces.data(), uvChart->faces.data(), sizeof(uint32_t) * uvChart->faces.size());
            // Find unique vertices.
            vertexUsed.zeroOutMemory();
            for (uint32_t i = 0; i < chart->indices.length; i++)
            {
                const uint32_t vertex = chart->indices[i];
                if (! vertexUsed.get(vertex))
                {
                    vertexUsed.set(vertex);
                    chart->uniqueVertices.push_back(vertex);
                }
            }
            // Compute parametric and surface areas.
            chart->parametricArea = 0.0f;
            for (uint32_t f = 0; f < chart->indices.length / 3; f++)
            {
                const Vector2& v1 = chart->vertices[chart->indices[f * 3 + 0]];
                const Vector2& v2 = chart->vertices[chart->indices[f * 3 + 1]];
                const Vector2& v3 = chart->vertices[chart->indices[f * 3 + 2]];
                chart->parametricArea += fabsf(triangleArea(v1, v2, v3));
            }
            chart->parametricArea *= 0.5f;
            if (chart->parametricArea < kAreaEpsilon)
            {
                // When the parametric area is too small we use a rough approximation to prevent divisions by very small numbers.
                Vector2 minCorner(FLT_MAX, FLT_MAX);
                Vector2 maxCorner(-FLT_MAX, -FLT_MAX);
                for (uint32_t v = 0; v < chart->uniqueVertexCount(); v++)
                {
                    minCorner = min(minCorner, chart->uniqueVertexAt(v));
                    maxCorner = max(maxCorner, chart->uniqueVertexAt(v));
                }
                const Vector2 bounds = (maxCorner - minCorner) * 0.5f;
                chart->parametricArea = bounds.x * bounds.y;
            }
            XA_DEBUG_ASSERT(isFinite(chart->parametricArea));
            XA_DEBUG_ASSERT(! isNan(chart->parametricArea));
            chart->surfaceArea = chart->parametricArea; // Identical for UV meshes.
            // Compute bounding box of chart.
            // Using all unique vertices for simplicity, can compute real boundaries if this is too slow.
            boundingBox.clear();
            for (uint32_t v = 0; v < chart->uniqueVertexCount(); v++)
                boundingBox.appendBoundaryVertex(chart->uniqueVertexAt(v));
            boundingBox.compute();
            chart->majorAxis = boundingBox.majorAxis;
            chart->minorAxis = boundingBox.minorAxis;
            chart->minCorner = boundingBox.minCorner;
            chart->maxCorner = boundingBox.maxCorner;
            m_charts.push_back(chart);
        }
    }

    // Pack charts in the smallest possible rectangle.
    bool packCharts(const PackOptions& options)
    {
        const uint32_t chartCount = m_charts.size();
        XA_PRINT("Packing %u charts\n", chartCount);
        if (chartCount == 0)
        {
            return true;
        }
        // Estimate resolution and/or texels per unit if not specified.
        m_texelsPerUnit = options.texelsPerUnit;
        uint32_t resolution = options.resolution > 0 ? options.resolution + options.padding * 2 : 0;
        const uint32_t maxResolution = m_texelsPerUnit > 0.0f ? resolution : 0;
        if (resolution <= 0 || m_texelsPerUnit <= 0)
        {
            if (resolution <= 0 && m_texelsPerUnit <= 0)
                resolution = 1024;
            float meshArea = 0;
            for (uint32_t c = 0; c < chartCount; c++)
                meshArea += m_charts[c]->surfaceArea;
            if (resolution <= 0)
            {
                // Estimate resolution based on the mesh surface area and given texel scale.
                const float texelCount = max(1.0f, meshArea * square(m_texelsPerUnit) / 0.75f); // Assume 75% utilization.
                resolution = max(1u, nextPowerOfTwo(uint32_t(sqrtf(texelCount))));
            }
            if (m_texelsPerUnit <= 0)
            {
                // Estimate a suitable texelsPerUnit to fit the given resolution.
                const float texelCount = max(1.0f, meshArea / 0.75f); // Assume 75% utilization.
                m_texelsPerUnit = sqrtf((resolution * resolution) / texelCount);
                XA_PRINT("   Estimating texelsPerUnit as %g\n", m_texelsPerUnit);
            }
        }
        Array<float> chartOrderArray;
        chartOrderArray.resize(chartCount);
        Array<Vector2> chartExtents;
        chartExtents.resize(chartCount);
        float minChartPerimeter = FLT_MAX, maxChartPerimeter = 0.0f;
        for (uint32_t c = 0; c < chartCount; c++)
        {
            Chart* chart = m_charts[c];
            // Compute chart scale
            float scale = 1.0f;
            if (chart->parametricArea != 0.0f)
            {
                scale = sqrtf(chart->surfaceArea / chart->parametricArea) * m_texelsPerUnit;
                XA_ASSERT(isFinite(scale));
            }
            // Translate, rotate and scale vertices. Compute extents.
            Vector2 minCorner(FLT_MAX, FLT_MAX);
            if (! options.rotateChartsToAxis)
            {
                for (uint32_t i = 0; i < chart->uniqueVertexCount(); i++)
                    minCorner = min(minCorner, chart->uniqueVertexAt(i));
            }
            Vector2 extents(0.0f);
            for (uint32_t i = 0; i < chart->uniqueVertexCount(); i++)
            {
                Vector2& texcoord = chart->uniqueVertexAt(i);
                if (options.rotateChartsToAxis)
                {
                    const float x = dot(texcoord, chart->majorAxis);
                    const float y = dot(texcoord, chart->minorAxis);
                    texcoord.x = x;
                    texcoord.y = y;
                    texcoord -= chart->minCorner;
                }
                else
                {
                    texcoord -= minCorner;
                }
                texcoord *= scale;
                XA_DEBUG_ASSERT(texcoord.x >= 0.0f && texcoord.y >= 0.0f);
                XA_DEBUG_ASSERT(isFinite(texcoord.x) && isFinite(texcoord.y));
                extents = max(extents, texcoord);
            }
            XA_DEBUG_ASSERT(extents.x >= 0 && extents.y >= 0);
            // Scale the charts to use the entire texel area available. So, if the width is 0.1 we could scale it to 1 without increasing the lightmap usage and making a better use
            // of it. In many cases this also improves the look of the seams, since vertices on the chart boundaries have more chances of being aligned with the texel centers.
            if (extents.x > 0.0f && extents.y > 0.0f)
            {
                // Block align: align all chart extents to 4x4 blocks, but taking padding and texel center offset into account.
                const int blockAlignSizeOffset = options.padding * 2 + 1;
                int width = ftoi_ceil(extents.x);
                if (options.blockAlign)
                    width = align(width + blockAlignSizeOffset, 4) - blockAlignSizeOffset;
                int height = ftoi_ceil(extents.y);
                if (options.blockAlign)
                    height = align(height + blockAlignSizeOffset, 4) - blockAlignSizeOffset;
                for (uint32_t v = 0; v < chart->uniqueVertexCount(); v++)
                {
                    Vector2& texcoord = chart->uniqueVertexAt(v);
                    texcoord.x = texcoord.x / extents.x * (float)width;
                    texcoord.y = texcoord.y / extents.y * (float)height;
                }
                extents.x = (float)width;
                extents.y = (float)height;
            }
            // Limit chart size, either to PackOptions::maxChartSize or maxResolution (if set), whichever is smaller.
            // If limiting chart size to maxResolution, print a warning, since that may not be desirable to the user.
            uint32_t maxChartSize = options.maxChartSize;
            bool warnChartResized = false;
            if (maxResolution > 0 && (maxChartSize == 0 || maxResolution < maxChartSize))
            {
                maxChartSize = maxResolution - options.padding * 2; // Don't include padding.
                warnChartResized = true;
            }
            if (maxChartSize > 0)
            {
                const float realMaxChartSize = (float)maxChartSize - 1.0f; // Aligning to texel centers increases texel footprint by 1.
                if (extents.x > realMaxChartSize || extents.y > realMaxChartSize)
                {
                    if (warnChartResized)
                        XA_PRINT("   Resizing chart %u from %gx%g to %ux%u to fit atlas\n", c, extents.x, extents.y, maxChartSize, maxChartSize);
                    scale = realMaxChartSize / max(extents.x, extents.y);
                    for (uint32_t i = 0; i < chart->uniqueVertexCount(); i++)
                    {
                        Vector2& texcoord = chart->uniqueVertexAt(i);
                        texcoord = min(texcoord * scale, Vector2(realMaxChartSize));
                    }
                }
            }
            // Align to texel centers and add padding offset.
            extents.x = extents.y = 0.0f;
            for (uint32_t v = 0; v < chart->uniqueVertexCount(); v++)
            {
                Vector2& texcoord = chart->uniqueVertexAt(v);
                texcoord.x += 0.5f + options.padding;
                texcoord.y += 0.5f + options.padding;
                extents = max(extents, texcoord);
            }
            if (extents.x > resolution || extents.y > resolution)
                XA_PRINT("   Chart %u extents are large (%gx%g)\n", c, extents.x, extents.y);
            chartExtents[c] = extents;
            chartOrderArray[c] = extents.x + extents.y; // Use perimeter for chart sort key.
            minChartPerimeter = min(minChartPerimeter, chartOrderArray[c]);
            maxChartPerimeter = max(maxChartPerimeter, chartOrderArray[c]);
        }
        // Sort charts by perimeter.
        m_radix.sort(chartOrderArray);
        const uint32_t* ranks = m_radix.ranks();
        // Divide chart perimeter range into buckets.
        const float chartPerimeterBucketSize = (maxChartPerimeter - minChartPerimeter) / 16.0f;
        uint32_t currentChartBucket = 0;
        Array<Vector2i> chartStartPositions; // per atlas
        chartStartPositions.push_back(Vector2i(0, 0));
        // Pack sorted charts.
        // chartImage: result from conservative rasterization
        // chartImageBilinear: chartImage plus any texels that would be sampled by bilinear filtering.
        // chartImagePadding: either chartImage or chartImageBilinear depending on options, with a dilate filter applied options.padding times.
        // Rotated versions swap x and y.
        BitImage chartImage, chartImageBilinear, chartImagePadding;
        BitImage chartImageRotated, chartImageBilinearRotated, chartImagePaddingRotated;
        UniformGrid2 boundaryEdgeGrid;
        Array<Vector2i> atlasSizes;
        atlasSizes.push_back(Vector2i(0, 0));
        int progress = 0;
        for (uint32_t i = 0; i < chartCount; i++)
        {
            uint32_t c = ranks[chartCount - i - 1]; // largest chart first
            Chart* chart = m_charts[c];
            // @@ Add special cases for dot and line charts. @@ Lightmap rasterizer also needs to handle these special cases.
            // @@ We could also have a special case for chart quads. If the quad surface <= 4 texels, align vertices with texel centers and do not add padding. May be very useful
            // for foliage.
            // @@ In general we could reduce the padding of all charts by one texel by using a rasterizer that takes into account the 2-texel footprint of the tent bilinear filter.
            // For example, if we have a chart that is less than 1 texel wide currently we add one texel to the left and one texel to the right creating a 3-texel-wide bitImage.
            // However, if we know that the chart is only 1 texel wide we could align it so that it only touches the footprint of two texels:
            //      |   |      <- Touches texels 0, 1 and 2.
            //    |   |        <- Only touches texels 0 and 1.
            // \   \ / \ /   /
            //  \   X   X   /
            //   \ / \ / \ /
            //    V   V   V
            //    0   1   2
            // Resize and clear (discard = true) chart images.
            // Leave room for padding at extents.
            chartImage.resize(ftoi_ceil(chartExtents[c].x) + options.padding, ftoi_ceil(chartExtents[c].y) + options.padding, true);
            if (options.rotateCharts)
                chartImageRotated.resize(chartImage.height(), chartImage.width(), true);
            if (options.bilinear)
            {
                chartImageBilinear.resize(chartImage.width(), chartImage.height(), true);
                if (options.rotateCharts)
                    chartImageBilinearRotated.resize(chartImage.height(), chartImage.width(), true);
            }
            // Rasterize chart faces.
            const uint32_t faceCount = chart->indices.length / 3;
            for (uint32_t f = 0; f < faceCount; f++)
            {
                Vector2 vertices[3];
                for (uint32_t v = 0; v < 3; v++)
                    vertices[v] = chart->vertices[chart->indices[f * 3 + v]];
                DrawTriangleCallbackArgs args;
                args.chartBitImage = &chartImage;
                args.chartBitImageRotated = options.rotateCharts ? &chartImageRotated : nullptr;
                raster::drawTriangle(Vector2((float)chartImage.width(), (float)chartImage.height()), vertices, drawTriangleCallback, &args);
            }
            // Expand chart by pixels sampled by bilinear interpolation.
            if (options.bilinear)
                bilinearExpand(chart, &chartImage, &chartImageBilinear, options.rotateCharts ? &chartImageBilinearRotated : nullptr, boundaryEdgeGrid);
            // Expand chart by padding pixels (dilation).
            if (options.padding > 0)
            {
                // Copy into the same BitImage instances for every chart to avoid reallocating BitImage buffers (largest chart is packed first).
                if (options.bilinear)
                    chartImageBilinear.copyTo(chartImagePadding);
                else
                    chartImage.copyTo(chartImagePadding);
                chartImagePadding.dilate(options.padding);
                if (options.rotateCharts)
                {
                    if (options.bilinear)
                        chartImageBilinearRotated.copyTo(chartImagePaddingRotated);
                    else
                        chartImageRotated.copyTo(chartImagePaddingRotated);
                    chartImagePaddingRotated.dilate(options.padding);
                }
            }
            // Update brute force bucketing.
            if (options.bruteForce)
            {
                if (chartOrderArray[c] > minChartPerimeter && chartOrderArray[c] <= maxChartPerimeter - (chartPerimeterBucketSize * (currentChartBucket + 1)))
                {
                    // Moved to a smaller bucket, reset start location.
                    for (uint32_t j = 0; j < chartStartPositions.size(); j++)
                        chartStartPositions[j] = Vector2i(0, 0);
                    currentChartBucket++;
                }
            }
            // Find a location to place the chart in the atlas.
            BitImage *chartImageToPack, *chartImageToPackRotated;
            if (options.padding > 0)
            {
                chartImageToPack = &chartImagePadding;
                chartImageToPackRotated = &chartImagePaddingRotated;
            }
            else if (options.bilinear)
            {
                chartImageToPack = &chartImageBilinear;
                chartImageToPackRotated = &chartImageBilinearRotated;
            }
            else
            {
                chartImageToPack = &chartImage;
                chartImageToPackRotated = &chartImageRotated;
            }
            uint32_t currentAtlas = 0;
            int best_x = 0, best_y = 0;
            int best_cw = 0, best_ch = 0;
            int best_r = 0;
            for (;;)
            {
#if XA_DEBUG
                bool firstChartInBitImage = false;
#endif
                if (currentAtlas + 1 > m_bitImages.size())
                {
                    // Chart doesn't fit in the current bitImage, create a new one.
                    BitImage* bi = XA_NEW_ARGS(BitImage, resolution, resolution);
                    m_bitImages.push_back(bi);
                    atlasSizes.push_back(Vector2i(0, 0));
#if XA_DEBUG
                    firstChartInBitImage = true;
#endif
                    // Start positions are per-atlas, so create a new one of those too.
                    chartStartPositions.push_back(Vector2i(0, 0));
                }
                const bool foundLocation = findChartLocation(
                    options,
                    chartStartPositions[currentAtlas],
                    m_bitImages[currentAtlas],
                    chartImageToPack,
                    chartImageToPackRotated,
                    atlasSizes[currentAtlas].x,
                    atlasSizes[currentAtlas].y,
                    &best_x,
                    &best_y,
                    &best_cw,
                    &best_ch,
                    &best_r,
                    maxResolution);
                XA_DEBUG_ASSERT(! (firstChartInBitImage && ! foundLocation)); // Chart doesn't fit in an empty, newly allocated bitImage. Shouldn't happen, since charts are resized
                                                                              // if they are too big to fit in the atlas.
                if (maxResolution == 0)
                {
                    XA_DEBUG_ASSERT(foundLocation); // The atlas isn't limited to a fixed resolution, a chart location should be found on the first attempt.
                    break;
                }
                if (foundLocation)
                    break;
                // Chart doesn't fit in the current bitImage, try the next one.
                currentAtlas++;
            }
            // Update brute force start location.
            if (options.bruteForce)
            {
                // Reset start location if the chart expanded the atlas.
                if (best_x + best_cw > atlasSizes[currentAtlas].x || best_y + best_ch > atlasSizes[currentAtlas].y)
                {
                    for (uint32_t j = 0; j < chartStartPositions.size(); j++)
                        chartStartPositions[j] = Vector2i(0, 0);
                }
                else
                {
                    chartStartPositions[currentAtlas] = Vector2i(best_x, best_y);
                }
            }
            // Update parametric extents.
            atlasSizes[currentAtlas].x = max(atlasSizes[currentAtlas].x, best_x + best_cw);
            atlasSizes[currentAtlas].y = max(atlasSizes[currentAtlas].y, best_y + best_ch);
            // Resize bitImage if necessary.
            // If maxResolution > 0, the bitImage is always set to maxResolutionIncludingPadding on creation and doesn't need to be dynamically resized.
            if (maxResolution == 0)
            {
                const uint32_t w = (uint32_t)atlasSizes[currentAtlas].x;
                const uint32_t h = (uint32_t)atlasSizes[currentAtlas].y;
                if (w > m_bitImages[0]->width() || h > m_bitImages[0]->height())
                {
                    m_bitImages[0]->resize(nextPowerOfTwo(w), nextPowerOfTwo(h), false);
                }
            }
            else
            {
                XA_DEBUG_ASSERT(atlasSizes[currentAtlas].x <= (int)maxResolution);
                XA_DEBUG_ASSERT(atlasSizes[currentAtlas].y <= (int)maxResolution);
            }
            addChart(m_bitImages[currentAtlas], chartImageToPack, chartImageToPackRotated, atlasSizes[currentAtlas].x, atlasSizes[currentAtlas].y, best_x, best_y, best_r);
            chart->atlasIndex = (int32_t)currentAtlas;
            // Modify texture coordinates:
            //  - rotate if the chart should be rotated
            //  - translate to chart location
            //  - translate to remove padding from top and left atlas edges (unless block aligned)
            for (uint32_t v = 0; v < chart->uniqueVertexCount(); v++)
            {
                Vector2& texcoord = chart->uniqueVertexAt(v);
                Vector2 t = texcoord;
                if (best_r)
                {
                    XA_DEBUG_ASSERT(options.rotateCharts);
                    swap(t.x, t.y);
                }
                texcoord.x = best_x + t.x;
                texcoord.y = best_y + t.y;
                texcoord.x -= (float)options.padding;
                texcoord.y -= (float)options.padding;
                XA_ASSERT(texcoord.x >= 0 && texcoord.y >= 0);
                XA_ASSERT(isFinite(texcoord.x) && isFinite(texcoord.y));
            }
        }
        // Remove padding from outer edges.
        if (maxResolution == 0)
        {
            m_width = max(0, atlasSizes[0].x - (int)options.padding * 2);
            m_height = max(0, atlasSizes[0].y - (int)options.padding * 2);
        }
        else
        {
            m_width = m_height = maxResolution - (int)options.padding * 2;
        }
        XA_PRINT("   %dx%d resolution\n", m_width, m_height);
        m_utilization.resize(m_bitImages.size());
        for (uint32_t i = 0; i < m_utilization.size(); i++)
        {
            if (m_width == 0 || m_height == 0)
                m_utilization[i] = 0.0f;
            else
            {
                uint32_t count = 0;
                for (uint32_t y = 0; y < m_height; y++)
                {
                    for (uint32_t x = 0; x < m_width; x++)
                        count += m_bitImages[i]->get(x, y);
                }
                m_utilization[i] = float(count) / (m_width * m_height);
            }
            if (m_utilization.size() > 1)
            {
                XA_PRINT("   %u: %f%% utilization\n", i, m_utilization[i] * 100.0f);
            }
            else
            {
                XA_PRINT("   %f%% utilization\n", m_utilization[i] * 100.0f);
            }
        }
        return true;
    }

private:
    bool findChartLocation(
        const PackOptions& options,
        const Vector2i& startPosition,
        const BitImage* atlasBitImage,
        const BitImage* chartBitImage,
        const BitImage* chartBitImageRotated,
        int w,
        int h,
        int* best_x,
        int* best_y,
        int* best_w,
        int* best_h,
        int* best_r,
        uint32_t maxResolution)
    {
        const int attempts = 4096;
        if (options.bruteForce || attempts >= w * h)
            return findChartLocation_bruteForce(
                options,
                startPosition,
                atlasBitImage,
                chartBitImage,
                chartBitImageRotated,
                w,
                h,
                best_x,
                best_y,
                best_w,
                best_h,
                best_r,
                maxResolution);
        return findChartLocation_random(options, atlasBitImage, chartBitImage, chartBitImageRotated, w, h, best_x, best_y, best_w, best_h, best_r, attempts, maxResolution);
    }

    bool findChartLocation_bruteForce(
        const PackOptions& options,
        const Vector2i& startPosition,
        const BitImage* atlasBitImage,
        const BitImage* chartBitImage,
        const BitImage* chartBitImageRotated,
        int w,
        int h,
        int* best_x,
        int* best_y,
        int* best_w,
        int* best_h,
        int* best_r,
        uint32_t maxResolution)
    {
        const int stepSize = options.blockAlign ? 4 : 1;
        int best_metric = INT_MAX;
        // Try two different orientations.
        for (int r = 0; r < 2; r++)
        {
            int cw = chartBitImage->width();
            int ch = chartBitImage->height();
            if (r == 1)
            {
                if (options.rotateCharts)
                    swap(cw, ch);
                else
                    break;
            }
            for (int y = startPosition.y; y <= h + stepSize; y += stepSize)
            {
                if (maxResolution > 0 && y > (int)maxResolution - ch)
                    break;
                for (int x = (y == startPosition.y ? startPosition.x : 0); x <= w + stepSize; x += stepSize)
                {
                    if (maxResolution > 0 && x > (int)maxResolution - cw)
                        break;
                    // Early out if metric is not better.
                    const int extentX = max(w, x + cw), extentY = max(h, y + ch);
                    const int area = extentX * extentY;
                    const int extents = max(extentX, extentY);
                    const int metric = extents * extents + area;
                    if (metric > best_metric)
                        continue;
                    // If metric is the same, pick the one closest to the origin.
                    if (metric == best_metric && max(x, y) >= max(*best_x, *best_y))
                        continue;
                    if (! atlasBitImage->canBlit(r == 1 ? *chartBitImageRotated : *chartBitImage, x, y))
                        continue;
                    best_metric = metric;
                    *best_x = x;
                    *best_y = y;
                    *best_w = cw;
                    *best_h = ch;
                    *best_r = r;
                    if (area == w * h)
                        return true; // Chart is completely inside, do not look at any other location.
                }
            }
        }
        return best_metric != INT_MAX;
    }

    bool findChartLocation_random(
        const PackOptions& options,
        const BitImage* atlasBitImage,
        const BitImage* chartBitImage,
        const BitImage* chartBitImageRotated,
        int w,
        int h,
        int* best_x,
        int* best_y,
        int* best_w,
        int* best_h,
        int* best_r,
        int attempts,
        uint32_t maxResolution)
    {
        bool result = false;
        const int BLOCK_SIZE = 4;
        int best_metric = INT_MAX;
        for (int i = 0; i < attempts; i++)
        {
            int cw = chartBitImage->width();
            int ch = chartBitImage->height();
            int r = options.rotateCharts ? m_rand.getRange(1) : 0;
            if (r == 1)
                swap(cw, ch);
            // + 1 to extend atlas in case atlas full. We may want to use a higher number to increase probability of extending atlas.
            int xRange = w + 1;
            int yRange = h + 1;
            // Clamp to max resolution.
            if (maxResolution > 0)
            {
                xRange = min(xRange, (int)maxResolution - cw);
                yRange = min(yRange, (int)maxResolution - ch);
            }
            int x = m_rand.getRange(xRange);
            int y = m_rand.getRange(yRange);
            if (options.blockAlign)
            {
                x = align(x, BLOCK_SIZE);
                y = align(y, BLOCK_SIZE);
                if (maxResolution > 0 && (x > (int)maxResolution - cw || y > (int)maxResolution - ch))
                    continue; // Block alignment pushed the chart outside the atlas.
            }
            // Early out.
            int area = max(w, x + cw) * max(h, y + ch);
            // int perimeter = max(w, x+cw) + max(h, y+ch);
            int extents = max(max(w, x + cw), max(h, y + ch));
            int metric = extents * extents + area;
            if (metric > best_metric)
            {
                continue;
            }
            if (metric == best_metric && min(x, y) > min(*best_x, *best_y))
            {
                // If metric is the same, pick the one closest to the origin.
                continue;
            }
            if (atlasBitImage->canBlit(r == 1 ? *chartBitImageRotated : *chartBitImage, x, y))
            {
                result = true;
                best_metric = metric;
                *best_x = x;
                *best_y = y;
                *best_w = cw;
                *best_h = ch;
                *best_r = options.rotateCharts ? r : 0;
                if (area == w * h)
                {
                    // Chart is completely inside, do not look at any other location.
                    break;
                }
            }
        }
        return result;
    }

    void addChart(BitImage* atlasBitImage, const BitImage* chartBitImage, const BitImage* chartBitImageRotated, int atlas_w, int atlas_h, int offset_x, int offset_y, int r)
    {
        XA_DEBUG_ASSERT(r == 0 || r == 1);
        const BitImage* image = r == 0 ? chartBitImage : chartBitImageRotated;
        const int w = image->width();
        const int h = image->height();
        for (int y = 0; y < h; y++)
        {
            int yy = y + offset_y;
            if (yy >= 0)
            {
                for (int x = 0; x < w; x++)
                {
                    int xx = x + offset_x;
                    if (xx >= 0)
                    {
                        if (image->get(x, y))
                        {
                            if (xx < atlas_w && yy < atlas_h)
                            {
                                XA_DEBUG_ASSERT(atlasBitImage->get(xx, yy) == false);
                                atlasBitImage->set(xx, yy);
                            }
                        }
                    }
                }
            }
        }
    }

    void bilinearExpand(const Chart* chart, BitImage* source, BitImage* dest, BitImage* destRotated, UniformGrid2& boundaryEdgeGrid) const
    {
        boundaryEdgeGrid.reset(chart->vertices, chart->indices);
        if (chart->boundaryEdges)
        {
            const uint32_t edgeCount = chart->boundaryEdges->size();
            for (uint32_t i = 0; i < edgeCount; i++)
                boundaryEdgeGrid.append((*chart->boundaryEdges)[i]);
        }
        else
        {
            for (uint32_t i = 0; i < chart->indices.length; i++)
                boundaryEdgeGrid.append(i);
        }
        const int xOffsets[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        const int yOffsets[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
        for (uint32_t y = 0; y < source->height(); y++)
        {
            for (uint32_t x = 0; x < source->width(); x++)
            {
                // Copy pixels from source.
                if (source->get(x, y))
                    goto setPixel;
                // Empty pixel. If none of of the surrounding pixels are set, this pixel can't be sampled by bilinear interpolation.
                {
                    uint32_t s = 0;
                    for (; s < 8; s++)
                    {
                        const int sx = (int)x + xOffsets[s];
                        const int sy = (int)y + yOffsets[s];
                        if (sx < 0 || sy < 0 || sx >= (int)source->width() || sy >= (int)source->height())
                            continue;
                        if (source->get((uint32_t)sx, (uint32_t)sy))
                            break;
                    }
                    if (s == 8)
                        continue;
                }
                {
                    // If a 2x2 square centered on the pixels centroid intersects the triangle, this pixel will be sampled by bilinear interpolation.
                    // See "Precomputed Global Illumination in Frostbite (GDC 2018)" page 95
                    const Vector2 centroid((float)x + 0.5f, (float)y + 0.5f);
                    const Vector2 squareVertices[4] = { Vector2(centroid.x - 1.0f, centroid.y - 1.0f),
                                                        Vector2(centroid.x + 1.0f, centroid.y - 1.0f),
                                                        Vector2(centroid.x + 1.0f, centroid.y + 1.0f),
                                                        Vector2(centroid.x - 1.0f, centroid.y + 1.0f) };
                    for (uint32_t j = 0; j < 4; j++)
                    {
                        if (boundaryEdgeGrid.intersect(squareVertices[j], squareVertices[(j + 1) % 4], 0.0f))
                            goto setPixel;
                    }
                }
                continue;
            setPixel:
                dest->set(x, y);
                if (destRotated)
                    destRotated->set(y, x);
            }
        }
    }

    struct DrawTriangleCallbackArgs
    {
        BitImage *chartBitImage, *chartBitImageRotated;
    };

    static bool drawTriangleCallback(void* param, int x, int y)
    {
        auto args = (DrawTriangleCallbackArgs*)param;
        args->chartBitImage->set(x, y);
        if (args->chartBitImageRotated)
            args->chartBitImageRotated->set(y, x);
        return true;
    }

    Array<float> m_utilization;
    Array<BitImage*> m_bitImages;
    Array<Chart*> m_charts;
    RadixSort m_radix;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    float m_texelsPerUnit = 0.0f;
    KISSRng m_rand;
};

} // namespace pack
} // namespace internal

struct Context
{
    Atlas atlas;
    internal::TaskGroupHandle addMeshTaskGroup;
    internal::TaskScheduler* taskScheduler;
    internal::Array<internal::UvMesh*> uvMeshes;
    internal::Array<internal::UvMeshInstance*> uvMeshInstances;
    bool uvMeshChartsComputed = false;
};

Atlas* Create()
{
    Context* ctx = XA_NEW(Context);
    memset(&ctx->atlas, 0, sizeof(Atlas));
    ctx->taskScheduler = XA_NEW(internal::TaskScheduler);
    return &ctx->atlas;
}

static void DestroyOutputMeshes(Context* ctx)
{
    if (! ctx->atlas.meshes)
        return;
    for (int i = 0; i < (int)ctx->atlas.meshCount; i++)
    {
        Mesh& mesh = ctx->atlas.meshes[i];
        if (mesh.chartArray)
        {
            for (uint32_t j = 0; j < mesh.chartCount; j++)
            {
                if (mesh.chartArray[j].faceArray)
                    XA_FREE(mesh.chartArray[j].faceArray);
            }
            XA_FREE(mesh.chartArray);
        }
        if (mesh.vertexArray)
            XA_FREE(mesh.vertexArray);
        if (mesh.indexArray)
            XA_FREE(mesh.indexArray);
    }
    XA_FREE(ctx->atlas.meshes);
    ctx->atlas.meshes = nullptr;
}

void Destroy(Atlas* atlas)
{
    XA_DEBUG_ASSERT(atlas);
    Context* ctx = (Context*)atlas;
    if (atlas->utilization)
        XA_FREE(atlas->utilization);
    if (atlas->image)
        XA_FREE(atlas->image);
    DestroyOutputMeshes(ctx);
    ctx->taskScheduler->~TaskScheduler();
    XA_FREE(ctx->taskScheduler);
    for (uint32_t i = 0; i < ctx->uvMeshes.size(); i++)
    {
        internal::UvMesh* mesh = ctx->uvMeshes[i];
        for (uint32_t j = 0; j < mesh->charts.size(); j++)
        {
            mesh->charts[j]->~UvMeshChart();
            XA_FREE(mesh->charts[j]);
        }
        mesh->~UvMesh();
        XA_FREE(mesh);
    }
    for (uint32_t i = 0; i < ctx->uvMeshInstances.size(); i++)
    {
        internal::UvMeshInstance* mesh = ctx->uvMeshInstances[i];
        mesh->~UvMeshInstance();
        XA_FREE(mesh);
    }
    ctx->~Context();
    XA_FREE(ctx);
}

static uint32_t DecodeIndex(IndexFormat format, const void* indexData, int32_t offset, uint32_t i)
{
    XA_DEBUG_ASSERT(indexData);
    if (format == IndexFormat::UInt16)
        return uint16_t((int32_t)((const uint16_t*)indexData)[i] + offset);
    return uint32_t((int32_t)((const uint32_t*)indexData)[i] + offset);
}

AddMeshError AddUvMesh(Atlas* atlas, const UvMeshDecl& decl)
{
    XA_DEBUG_ASSERT(atlas);
    if (! atlas)
    {
        XA_PRINT_WARNING("AddUvMesh: atlas is null.\n");
        return AddMeshError::Error;
    }
    Context* ctx = (Context*)atlas;
    const bool hasIndices = decl.indexCount > 0;
    const uint32_t indexCount = hasIndices ? decl.indexCount : decl.vertexCount;
    XA_PRINT("Adding UV mesh %d: %u vertices, %u triangles\n", ctx->uvMeshes.size(), decl.vertexCount, indexCount / 3);
    // Expecting triangle faces.
    if ((indexCount % 3) != 0)
        return AddMeshError::InvalidIndexCount;
    if (hasIndices)
    {
        // Check if any index is out of range.
        for (uint32_t i = 0; i < indexCount; i++)
        {
            const uint32_t index = DecodeIndex(decl.indexFormat, decl.indexData, decl.indexOffset, i);
            if (index >= decl.vertexCount)
                return AddMeshError::IndexOutOfRange;
        }
    }
    // Create a mesh instance.
    internal::UvMeshInstance* meshInstance = XA_NEW(internal::UvMeshInstance);
    meshInstance->mesh = nullptr;
    ctx->uvMeshInstances.push_back(meshInstance);
    // See if this is an instance of an already existing mesh.
    internal::UvMesh* mesh = nullptr;
    for (uint32_t m = 0; m < ctx->uvMeshes.size(); m++)
    {
        if (memcmp(&ctx->uvMeshes[m]->decl, &decl, sizeof(UvMeshDecl)) == 0)
        {
            mesh = ctx->uvMeshes[m];
            XA_PRINT("   instance of a previous UV mesh\n");
            break;
        }
    }
    if (! mesh)
    {
        // Copy geometry to mesh.
        mesh = XA_NEW(internal::UvMesh);
        ctx->uvMeshes.push_back(mesh);
        mesh->decl = decl;
        if (decl.faceMaterialData)
        {
            mesh->faceMaterials.resize(decl.indexCount / 3);
            memcpy(mesh->faceMaterials.data(), decl.faceMaterialData, mesh->faceMaterials.size() * sizeof(uint32_t));
        }
        mesh->indices.resize(decl.indexCount);
        for (uint32_t i = 0; i < indexCount; i++)
            mesh->indices[i] = hasIndices ? DecodeIndex(decl.indexFormat, decl.indexData, decl.indexOffset, i) : i;
        mesh->texcoords.resize(decl.vertexCount);
        for (uint32_t i = 0; i < decl.vertexCount; i++)
            mesh->texcoords[i] = *((const internal::Vector2*)&((const uint8_t*)decl.vertexUvData)[decl.vertexStride * i]);

        mesh->faceIgnore.resize(decl.indexCount / 3);
        mesh->faceIgnore.zeroOutMemory();
    }
    meshInstance->mesh = mesh;
    return AddMeshError::Success;
}

void SetCharts(Atlas* atlas, const std::vector<std::vector<size_t>>& grouped_faces)
{
    if (! atlas)
    {
        XA_PRINT_WARNING("ComputeCharts: atlas is null.\n");
        return;
    }
    Context* ctx = (Context*)atlas;
    // AddMeshJoin(atlas);
    if (ctx->uvMeshInstances.isEmpty())
    {
        XA_PRINT_WARNING("ComputeCharts: No meshes. Call AddUvMesh first.\n");
        return;
    }
    // Reset atlas state. This function may be called multiple times, or again after PackCharts.
    if (atlas->utilization)
        XA_FREE(atlas->utilization);
    if (atlas->image)
        XA_FREE(atlas->image);
    DestroyOutputMeshes(ctx);
    memset(&ctx->atlas, 0, sizeof(Atlas));

    for (size_t i = 0; i < ctx->uvMeshes.size(); ++i)
    {
        internal::UvMesh* mesh = ctx->uvMeshes[i];
        internal::segment::SetUvMeshChartsTask task(mesh, grouped_faces);
        task.run();
    }

    ctx->uvMeshChartsComputed = true;
}

void PackCharts(Atlas* atlas, PackOptions packOptions)
{
    // Validate arguments and context state.
    if (! atlas)
    {
        XA_PRINT_WARNING("PackCharts: atlas is null.\n");
        return;
    }
    Context* ctx = (Context*)atlas;
    if (ctx->uvMeshInstances.isEmpty())
    {
        XA_PRINT_WARNING("PackCharts: No meshes. Call AddUvMesh first.\n");
        return;
    }
    else if (! ctx->uvMeshChartsComputed)
    {
        XA_PRINT_WARNING("PackCharts: ComputeCharts must be called first.\n");
        return;
    }
    if (packOptions.texelsPerUnit < 0.0f)
    {
        XA_PRINT_WARNING("PackCharts: PackOptions::texelsPerUnit is negative.\n");
        packOptions.texelsPerUnit = 0.0f;
    }
    // Cleanup atlas.
    DestroyOutputMeshes(ctx);
    if (atlas->utilization)
    {
        XA_FREE(atlas->utilization);
        atlas->utilization = nullptr;
    }
    if (atlas->image)
    {
        XA_FREE(atlas->image);
        atlas->image = nullptr;
    }
    atlas->meshCount = 0;
    // Pack charts.
    internal::pack::Atlas packAtlas;
    for (uint32_t i = 0; i < ctx->uvMeshInstances.size(); i++)
        packAtlas.addUvMeshCharts(ctx->uvMeshInstances[i]);
    if (! packAtlas.packCharts(packOptions))
        return;
    // Populate atlas object with pack results.
    atlas->atlasCount = packAtlas.getNumAtlases();
    atlas->chartCount = packAtlas.getChartCount();
    atlas->width = packAtlas.getWidth();
    atlas->height = packAtlas.getHeight();
    atlas->texelsPerUnit = packAtlas.getTexelsPerUnit();
    if (atlas->atlasCount > 0)
    {
        atlas->utilization = XA_ALLOC_ARRAY(float, atlas->atlasCount);
        for (uint32_t i = 0; i < atlas->atlasCount; i++)
            atlas->utilization[i] = packAtlas.getUtilization(i);
    }
    XA_PRINT("Building output meshes\n");
    int progress = 0;
    atlas->meshCount = ctx->uvMeshInstances.size();
    atlas->meshes = XA_ALLOC_ARRAY(Mesh, atlas->meshCount);
    memset(atlas->meshes, 0, sizeof(Mesh) * atlas->meshCount);

    uint32_t chartIndex = 0;
    for (uint32_t m = 0; m < ctx->uvMeshInstances.size(); m++)
    {
        Mesh& outputMesh = atlas->meshes[m];
        const internal::UvMeshInstance* mesh = ctx->uvMeshInstances[m];
        // Alloc arrays.
        outputMesh.vertexCount = mesh->texcoords.size();
        outputMesh.indexCount = mesh->mesh->indices.size();
        outputMesh.chartCount = mesh->mesh->charts.size();
        outputMesh.vertexArray = XA_ALLOC_ARRAY(PlacedVertex, outputMesh.vertexCount);
        outputMesh.indexArray = XA_ALLOC_ARRAY(uint32_t, outputMesh.indexCount);
        outputMesh.chartArray = XA_ALLOC_ARRAY(Chart, outputMesh.chartCount);
        XA_PRINT("   UV mesh %u: %u vertices, %u triangles, %u charts\n", m, outputMesh.vertexCount, outputMesh.indexCount / 3, outputMesh.chartCount);
        // Copy mesh data.
        // Vertices.
        for (uint32_t v = 0; v < mesh->texcoords.size(); v++)
        {
            PlacedVertex& vertex = outputMesh.vertexArray[v];
            vertex.uv[0] = mesh->texcoords[v].x;
            vertex.uv[1] = mesh->texcoords[v].y;
            vertex.xref = v;
            const uint32_t meshChartIndex = mesh->mesh->vertexToChartMap[v];
            if (meshChartIndex == UINT32_MAX)
            {
                // Vertex doesn't exist in any chart.
                vertex.atlasIndex = -1;
                vertex.chartIndex = -1;
            }
            else
            {
                const internal::pack::Chart* chart = packAtlas.getChart(chartIndex + meshChartIndex);
                vertex.atlasIndex = chart->atlasIndex;
                vertex.chartIndex = (int32_t)chartIndex + meshChartIndex;
            }
        }
        // Indices.
        memcpy(outputMesh.indexArray, mesh->mesh->indices.data(), mesh->mesh->indices.size() * sizeof(uint32_t));
        // Charts.
        for (uint32_t c = 0; c < mesh->mesh->charts.size(); c++)
        {
            Chart* outputChart = &outputMesh.chartArray[c];
            const internal::pack::Chart* chart = packAtlas.getChart(chartIndex);
            XA_DEBUG_ASSERT(chart->atlasIndex >= 0);
            outputChart->atlasIndex = (uint32_t)chart->atlasIndex;
            outputChart->faceCount = chart->faces.size();
            outputChart->faceArray = XA_ALLOC_ARRAY(uint32_t, outputChart->faceCount);
            outputChart->material = chart->material;
            for (uint32_t f = 0; f < outputChart->faceCount; f++)
                outputChart->faceArray[f] = chart->faces[f];
            chartIndex++;
        }
    }
}

} // namespace xatlas
