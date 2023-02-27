#pragma once

#include <map>
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>
#include <random>
#include <boost/functional/hash.hpp>

#include "common/encode.h"
#include "big_num/bignum_utils.h"

using namespace zjchain;
extern std::random_device s_fixedHashEngine;
template <unsigned N> struct StaticLog2 { enum { result = 1 + StaticLog2<N / 2>::result }; };
template <> struct StaticLog2<1> { enum { result = 0 }; };

template <class _T>
class vector_ref
{
public:
    using value_type = _T;
    using element_type = _T;
    using mutable_value_type = typename std::conditional<std::is_const<_T>::value, typename std::remove_const<_T>::type, _T>::type;

    static_assert(std::is_pod<value_type>::value, "vector_ref can only be used with PODs due to its low-level treatment of data.");

    vector_ref() : m_data(nullptr), m_count(0) {}
    /// Creates a new vector_ref to point to @a _count elements starting at @a _data.
    vector_ref(_T* _data, size_t _count) : m_data(_data), m_count(_count) {}
    /// Creates a new vector_ref pointing to the data part of a string (given as pointer).
    vector_ref(typename std::conditional<std::is_const<_T>::value, std::string const*, std::string*>::type _data) : m_data(reinterpret_cast<_T*>(_data->data())), m_count(_data->size() / sizeof(_T)) {}
    /// Creates a new vector_ref pointing to the data part of a vector (given as pointer).
    vector_ref(typename std::conditional<std::is_const<_T>::value, std::vector<typename std::remove_const<_T>::type> const*, std::vector<_T>*>::type _data) : m_data(_data->data()), m_count(_data->size()) {}
    /// Creates a new vector_ref pointing to the data part of a string (given as reference).
    vector_ref(typename std::conditional<std::is_const<_T>::value, std::string const&, std::string&>::type _data) : m_data(reinterpret_cast<_T*>(_data.data())), m_count(_data.size() / sizeof(_T)) {}
    explicit operator bool() const { return m_data && m_count; }

    bool contentsEqual(std::vector<mutable_value_type> const& _c) const { if (!m_data || m_count == 0) return _c.empty(); else return _c.size() == m_count && !memcmp(_c.data(), m_data, m_count * sizeof(_T)); }
    std::vector<mutable_value_type> toVector() const { return std::vector<mutable_value_type>(m_data, m_data + m_count); }
    std::vector<unsigned char> toBytes() const { return std::vector<unsigned char>(reinterpret_cast<unsigned char const*>(m_data), reinterpret_cast<unsigned char const*>(m_data) + m_count * sizeof(_T)); }
    std::string toString() const { return std::string((char const*)m_data, ((char const*)m_data) + m_count * sizeof(_T)); }

    template <class _T2> explicit operator vector_ref<_T2>() const { assert(m_count * sizeof(_T) / sizeof(_T2) * sizeof(_T2) / sizeof(_T) == m_count); return vector_ref<_T2>(reinterpret_cast<_T2*>(m_data), m_count * sizeof(_T) / sizeof(_T2)); }
    operator vector_ref<_T const>() const { return vector_ref<_T const>(m_data, m_count); }

    _T* data() const { return m_data; }
    /// @returns the number of elements referenced (not necessarily number of bytes).
    size_t count() const { return m_count; }
    /// @returns the number of elements referenced (not necessarily number of bytes).
    size_t size() const { return m_count; }
    bool empty() const { return !m_count; }
    /// @returns a new vector_ref pointing at the next chunk of @a size() elements.
    vector_ref<_T> next() const { if (!m_data) return *this; else return vector_ref<_T>(m_data + m_count, m_count); }
    /// @returns a new vector_ref which is a shifted and shortened view of the original data.
    /// If this goes out of bounds in any way, returns an empty vector_ref.
    /// If @a _count is ~size_t(0), extends the view to the end of the data.
    vector_ref<_T> cropped(size_t _begin, size_t _count) const { if (m_data && _begin <= m_count && _count <= m_count && _begin + _count <= m_count) return vector_ref<_T>(m_data + _begin, _count == ~size_t(0) ? m_count - _begin : _count); else return vector_ref<_T>(); }
    /// @returns a new vector_ref which is a shifted view of the original data (not going beyond it).
    vector_ref<_T> cropped(size_t _begin) const { if (m_data && _begin <= m_count) return vector_ref<_T>(m_data + _begin, m_count - _begin); else return vector_ref<_T>(); }
    void retarget(_T* _d, size_t _s) { m_data = _d; m_count = _s; }
    void retarget(std::vector<_T> const& _t) { m_data = _t.data(); m_count = _t.size(); }
    template <class T> bool overlapsWith(vector_ref<T> _t) const { void const* f1 = data(); void const* t1 = data() + size(); void const* f2 = _t.data(); void const* t2 = _t.data() + _t.size(); return f1 < t2 && t1 > f2; }
    /// Copies the contents of this vector_ref to the contents of @a _t, up to the max size of @a _t.
    void copyTo(vector_ref<typename std::remove_const<_T>::type> _t) const { if (overlapsWith(_t)) memmove(_t.data(), m_data, std::min(_t.size(), m_count) * sizeof(_T)); else memcpy(_t.data(), m_data, std::min(_t.size(), m_count) * sizeof(_T)); }
    /// Copies the contents of this vector_ref to the contents of @a _t, and zeros further trailing elements in @a _t.
    void populate(vector_ref<typename std::remove_const<_T>::type> _t) const { copyTo(_t); memset(_t.data() + m_count, 0, std::max(_t.size(), m_count) - m_count); }
    /// Securely overwrite the memory.
    /// @note adapted from OpenSSL's implementation.
    void cleanse()
    {
        static std::atomic<unsigned char> s_cleanseCounter{ 0u };
        uint8_t* p = (uint8_t*)begin();
        size_t const len = (uint8_t*)end() - p;
        size_t loop = len;
        size_t count = s_cleanseCounter;
        while (loop--)
        {
            *(p++) = (uint8_t)count;
            count += (17 + ((size_t)p & 0xf));
        }
        p = (uint8_t*)memchr((uint8_t*)begin(), (uint8_t)count, len);
        if (p)
            count += (63 + (size_t)p);
        s_cleanseCounter = (uint8_t)count;
        memset((uint8_t*)begin(), 0, len);
    }

    _T* begin() { return m_data; }
    _T* end() { return m_data + m_count; }
    _T const* begin() const { return m_data; }
    _T const* end() const { return m_data + m_count; }

    _T& operator[](size_t _i) { assert(m_data); assert(_i < m_count); return m_data[_i]; }
    _T const& operator[](size_t _i) const { assert(m_data); assert(_i < m_count); return m_data[_i]; }

    bool operator==(vector_ref<_T> const& _cmp) const { return m_data == _cmp.m_data && m_count == _cmp.m_count; }
    bool operator!=(vector_ref<_T> const& _cmp) const { return !operator==(_cmp); }

    void reset() { m_data = nullptr; m_count = 0; }

private:
    _T* m_data;
    size_t m_count;
};

template<class _T> vector_ref<_T const> ref(_T const& _t) { return vector_ref<_T const>(&_t, 1); }
template<class _T> vector_ref<_T> ref(_T& _t) { return vector_ref<_T>(&_t, 1); }
template<class _T> vector_ref<_T const> ref(std::vector<_T> const& _t) { return vector_ref<_T const>(&_t); }
template<class _T> vector_ref<_T> ref(std::vector<_T>& _t) { return vector_ref<_T>(&_t); }
// Binary data types.
using byte = uint8_t;
using bytes = std::vector<byte>;
using bytesRef = vector_ref<byte>;
using bytesConstRef = vector_ref<byte const>;

inline bytes asBytes(std::string const& _b)
{
    return bytes((byte const*)_b.data(), (byte const*)(_b.data() + _b.size()));
}

template <unsigned N>
class FixedHash
{
public:
    /// The corresponding arithmetic type.
    using Arith = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<N * 8, N * 8, boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;

    /// The size of the container.
    enum { size = N };

    /// A dummy flag to avoid accidental construction from pointer.
    enum ConstructFromPointerType { ConstructFromPointer };

    /// Method to convert from a string.
    enum ConstructFromStringType { FromHex, FromBinary };

    /// Method to convert from a string.
    enum ConstructFromHashType { AlignLeft, AlignRight, FailIfDifferent };

    /// Construct an empty hash.
    FixedHash() { m_data.fill(0); }

    /// Construct from another hash, filling with zeroes or cropping as necessary.
    template <unsigned M> explicit FixedHash(FixedHash<M> const& _h, ConstructFromHashType _t = AlignLeft) { m_data.fill(0); unsigned c = std::min(M, N); for (unsigned i = 0; i < c; ++i) m_data[_t == AlignRight ? N - 1 - i : i] = _h[_t == AlignRight ? M - 1 - i : i]; }

    /// Convert from the corresponding arithmetic type.
    FixedHash(Arith const& _arith) { bignum::ToBigEndian(_arith, m_data); }

    /// Convert from unsigned
    explicit FixedHash(unsigned _u) { bignum::ToBigEndian(_u, m_data); }

    /// Explicitly construct, copying from a byte array.
    explicit FixedHash(bytes const& _b, ConstructFromHashType _t = FailIfDifferent) { if (_b.size() == N) memcpy(m_data.data(), _b.data(), std::min<unsigned>(_b.size(), N)); else { m_data.fill(0); if (_t != FailIfDifferent) { auto c = std::min<unsigned>(_b.size(), N); for (unsigned i = 0; i < c; ++i) m_data[_t == AlignRight ? N - 1 - i : i] = _b[_t == AlignRight ? _b.size() - 1 - i : i]; } } }

    /// Explicitly construct, copying from a byte array.
    explicit FixedHash(bytesConstRef _b, ConstructFromHashType _t = FailIfDifferent) { if (_b.size() == N) memcpy(m_data.data(), _b.data(), std::min<unsigned>(_b.size(), N)); else { m_data.fill(0); if (_t != FailIfDifferent) { auto c = std::min<unsigned>(_b.size(), N); for (unsigned i = 0; i < c; ++i) m_data[_t == AlignRight ? N - 1 - i : i] = _b[_t == AlignRight ? _b.size() - 1 - i : i]; } } }

    /// Explicitly construct, copying from a bytes in memory with given pointer.
    explicit FixedHash(byte const* _bs, ConstructFromPointerType) { memcpy(m_data.data(), _bs, N); }

    /// Explicitly construct, copying from a  string.
    explicit FixedHash(std::string const& _s, ConstructFromStringType _t = FromHex, ConstructFromHashType _ht = FailIfDifferent) : FixedHash(_t == FromHex ? common::Encode::HexDecode(_s) : asBytes(_s), _ht) {}

    /// Convert to arithmetic type.
    operator Arith() const { return bignum::FromBigEndian<Arith>(m_data); }

    /// @returns true iff this is the empty hash.
    explicit operator bool() const { return std::any_of(m_data.begin(), m_data.end(), [](byte _b) { return _b != 0; }); }

    // The obvious comparison operators.
    bool operator==(FixedHash const& _c) const { return m_data == _c.m_data; }
    bool operator!=(FixedHash const& _c) const { return m_data != _c.m_data; }
    bool operator<(FixedHash const& _c) const { for (unsigned i = 0; i < N; ++i) if (m_data[i] < _c.m_data[i]) return true; else if (m_data[i] > _c.m_data[i]) return false; return false; }
    bool operator>=(FixedHash const& _c) const { return !operator<(_c); }
    bool operator<=(FixedHash const& _c) const { return operator==(_c) || operator<(_c); }
    bool operator>(FixedHash const& _c) const { return !operator<=(_c); }

    // The obvious binary operators.
    FixedHash& operator^=(FixedHash const& _c) { for (unsigned i = 0; i < N; ++i) m_data[i] ^= _c.m_data[i]; return *this; }
    FixedHash operator^(FixedHash const& _c) const { return FixedHash(*this) ^= _c; }
    FixedHash& operator|=(FixedHash const& _c) { for (unsigned i = 0; i < N; ++i) m_data[i] |= _c.m_data[i]; return *this; }
    FixedHash operator|(FixedHash const& _c) const { return FixedHash(*this) |= _c; }
    FixedHash& operator&=(FixedHash const& _c) { for (unsigned i = 0; i < N; ++i) m_data[i] &= _c.m_data[i]; return *this; }
    FixedHash operator&(FixedHash const& _c) const { return FixedHash(*this) &= _c; }
    FixedHash operator~() const { FixedHash ret; for (unsigned i = 0; i < N; ++i) ret[i] = ~m_data[i]; return ret; }

    // Big-endian increment.
    FixedHash& operator++() { for (unsigned i = size; i > 0 && !++m_data[--i]; ) {} return *this; }

    /// @returns true if all one-bits in @a _c are set in this object.
    bool contains(FixedHash const& _c) const { return (*this & _c) == _c; }

    /// @returns a particular byte from the hash.
    byte& operator[](unsigned _i) { return m_data[_i]; }
    /// @returns a particular byte from the hash.
    byte operator[](unsigned _i) const { return m_data[_i]; }

    /// @returns an abridged version of the hash as a user-readable hex string.
    std::string abridged() const { return toHex(ref().cropped(0, 4)) + "\342\200\246"; }

    /// @returns a version of the hash as a user-readable hex string that leaves out the middle part.
    std::string abridgedMiddle() const { return toHex(ref().cropped(0, 4)) + "\342\200\246" + toHex(ref().cropped(N - 4)); }

    /// @returns the hash as a user-readable hex string.
    std::string hex() const { return toHex(ref()); }

    /// @returns a mutable byte vector_ref to the object's data.
    bytesRef ref() { return bytesRef(m_data.data(), N); }

    /// @returns a constant byte vector_ref to the object's data.
    bytesConstRef ref() const { return bytesConstRef(m_data.data(), N); }

    /// @returns a mutable byte pointer to the object's data.
    byte* data() { return m_data.data(); }

    /// @returns a constant byte pointer to the object's data.
    byte const* data() const { return m_data.data(); }

    /// @returns begin iterator.
    auto begin() const -> typename std::array<byte, N>::const_iterator{ return m_data.begin(); }

        /// @returns end iterator.
    auto end() const -> typename std::array<byte, N>::const_iterator{ return m_data.end(); }

        /// @returns a copy of the object's data as a byte vector.
    bytes asBytes() const { return bytes(data(), data() + N); }

    /// @returns a mutable reference to the object's data as an STL array.
    std::array<byte, N>& asArray() { return m_data; }

    /// @returns a constant reference to the object's data as an STL array.
    std::array<byte, N> const& asArray() const { return m_data; }

    /// Populate with random data.
    template <class Engine>
    void randomize(Engine& _eng)
    {
        for (auto& i : m_data)
            i = (uint8_t)std::uniform_int_distribution<uint16_t>(0, 255)(_eng);
    }

    /// @returns a random valued object.
    static FixedHash random() { FixedHash ret; ret.randomize(s_fixedHashEngine); return ret; }

    struct hash
    {
        /// Make a hash of the object's data.
        size_t operator()(FixedHash const& _value) const { return boost::hash_range(_value.m_data.cbegin(), _value.m_data.cend()); }
    };

    template <unsigned P, unsigned M> inline FixedHash& shiftBloom(FixedHash<M> const& _h)
    {
        return (*this |= _h.template bloomPart<P, N>());
    }

    template <unsigned P, unsigned M> inline bool containsBloom(FixedHash<M> const& _h)
    {
        return contains(_h.template bloomPart<P, N>());
    }

    template <unsigned P, unsigned M> inline FixedHash<M> bloomPart() const
    {
        unsigned const c_bloomBits = M * 8;
        unsigned const c_mask = c_bloomBits - 1;
        unsigned const c_bloomBytes = (StaticLog2<c_bloomBits>::result + 7) / 8;

        static_assert((M & (M - 1)) == 0, "M must be power-of-two");
        static_assert(P * c_bloomBytes <= N, "out of range");

        FixedHash<M> ret;
        byte const* p = data();
        for (unsigned i = 0; i < P; ++i)
        {
            unsigned index = 0;
            for (unsigned j = 0; j < c_bloomBytes; ++j, ++p)
                index = (index << 8) | *p;
            index &= c_mask;
            ret[M - 1 - index / 8] |= (1 << (index % 8));
        }
        return ret;
    }

    /// Returns the index of the first bit set to one, or size() * 8 if no bits are set.
    inline unsigned firstBitSet() const
    {
        unsigned ret = 0;
        for (auto d : m_data)
            if (d)
            {
                for (;; ++ret, d <<= 1)
                {
                    if (d & 0x80)
                        return ret;
                }
            }
            else
                ret += 8;
        return ret;
    }

    void clear() { m_data.fill(0); }

private:
    std::array<byte, N> m_data;        ///< The binary data.
};

using h256 = FixedHash<32>;

std::pair<bool, bytes> alt_bn128_pairing_product(bytesConstRef _in);
std::pair<bool, bytes> alt_bn128_G1_add(bytesConstRef _in);
std::pair<bool, bytes> alt_bn128_G1_mul(bytesConstRef _in);

