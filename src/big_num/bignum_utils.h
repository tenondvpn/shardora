#pragma once

#include <type_traits>

#pragma warning(push)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/detail/integer_ops.hpp>
#pragma warning(pop)
#pragma GCC diagnostic pop

#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g2.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_pairing.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_pp.hpp>

using namespace boost::multiprecision::literals;
using bigint = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>>;
using u256 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<256, 256, boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;

namespace zjchain {

namespace bignum {

template <class T, class _In>
inline T FromBigEndian(_In const& _bytes) {
    T ret = (T)0;
    for (auto i : _bytes)
        ret = (T)((ret << 8) | (uint8_t)(typename std::make_unsigned<decltype(i)>::type)i);
    return ret;
}

template <class T, class Out>
inline void ToBigEndian(T _val, Out& o_out) {
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed, "only unsigned types or bigint supported");
    for (auto i = o_out.size(); i != 0; _val >>= 8, i--) {
        T v = _val & (T)0xff;
        o_out[i - 1] = (typename Out::value_type)(uint8_t)v;
    }
}

inline void ToBigEndian(bigint _val, uint8_t* out, uint32_t out_size) {
    for (auto i = out_size; i != 0; _val >>= 8, i--) {
        bigint v = _val & (bigint)0xff;
        out[i - 1] = (uint8_t)v;
    }
}

};  // namespace bignum

};  // namespace zjchain
