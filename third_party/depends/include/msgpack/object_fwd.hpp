//
// MessagePack for C++ static resolution routine
//
// Copyright (C) 2008-2014 FURUHASHI Sadayuki and KONDO Takatoshi
//
//    Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//    http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MSGPACK_OBJECT_FWD_HPP
#define MSGPACK_OBJECT_FWD_HPP

#include "msgpack/versioning.hpp"
#include "msgpack/zone.hpp"
#include "msgpack/object.h"

#include <typeinfo>

namespace msgpack {

/// @cond
MSGPACK_API_VERSION_NAMESPACE(v1) {
/// @endcond


namespace type {
    enum object_type {
        NIL                 = MSGPACK_OBJECT_NIL,
        BOOLEAN             = MSGPACK_OBJECT_BOOLEAN,
        POSITIVE_INTEGER    = MSGPACK_OBJECT_POSITIVE_INTEGER,
        NEGATIVE_INTEGER    = MSGPACK_OBJECT_NEGATIVE_INTEGER,
        FLOAT               = MSGPACK_OBJECT_FLOAT,
#if defined(MSGPACK_USE_LEGACY_NAME_AS_FLOAT)
        DOUBLE              = MSGPACK_OBJECT_DOUBLE, // obsolete
#endif // MSGPACK_USE_LEGACY_NAME_AS_FLOAT
        STR                 = MSGPACK_OBJECT_STR,
        BIN                 = MSGPACK_OBJECT_BIN,
        ARRAY               = MSGPACK_OBJECT_ARRAY,
        MAP                 = MSGPACK_OBJECT_MAP,
        EXT                 = MSGPACK_OBJECT_EXT
    };
}


struct object;
struct object_kv;

struct object_array {
    uint32_t size;
    msgpack::object* ptr;
};

struct object_map {
    uint32_t size;
    msgpack::object_kv* ptr;
};

struct object_str {
    uint32_t size;
    const char* ptr;
};

struct object_bin {
    uint32_t size;
    const char* ptr;
};

struct object_ext {
    int8_t type() const { return ptr[0]; }
    const char* data() const { return &ptr[1]; }
    uint32_t size;
    const char* ptr;
};


#if !defined(MSGPACK_USE_CPP03)
struct object;

namespace adaptor {
template <typename T, typename Enabler = void>
struct as;
} // namespace adaptor

template <typename T>
struct has_as {
private:
    template <typename U>
    static auto check(U*) ->
        typename std::is_same<
            decltype(msgpack::adaptor::as<U>()(std::declval<msgpack::object>())),
            T>::type;
    template <typename>
    static std::false_type check(...);
public:
    using type = decltype(check<T>(nullptr));
    static constexpr bool value = type::value;
};

#endif // !defined(MSGPACK_USE_CPP03)

/// Object class that corresponding to MessagePack format object
/**
 * See https://github.com/msgpack/msgpack-c/wiki/v1_1_cpp_object
 */
struct object {
    union union_type {
        bool boolean;
        uint64_t u64;
        int64_t  i64;
#if defined(MSGPACK_USE_LEGACY_NAME_AS_FLOAT)
        double   dec; // obsolete
#endif // MSGPACK_USE_LEGACY_NAME_AS_FLOAT
        double   f64;
        msgpack::object_array array;
        msgpack::object_map map;
        msgpack::object_str str;
        msgpack::object_bin bin;
        msgpack::object_ext ext;
    };

    msgpack::type::object_type type;
    union_type via;

    /// Cheking nil
    /**
     * @return If the object is nil, then return true, else return false.
     */
    bool is_nil() const { return type == msgpack::type::NIL; }

#if defined(MSGPACK_USE_CPP03)

    /// Get value as T
    /**
     * If the object can't be converted to T, msgpack::type_error would be thrown.
     * @tparam T The type you want to get.
     * @return The converted object.
     */
    template <typename T>
    T as() const;

#else  // defined(MSGPACK_USE_CPP03)

    /// Get value as T
    /**
     * If the object can't be converted to T, msgpack::type_error would be thrown.
     * @tparam T The type you want to get.
     * @return The converted object.
     */
    template <typename T>
    typename std::enable_if<msgpack::has_as<T>::value, T>::type as() const;

    /// Get value as T
    /**
     * If the object can't be converted to T, msgpack::type_error would be thrown.
     * @tparam T The type you want to get.
     * @return The converted object.
     */
    template <typename T>
    typename std::enable_if<!msgpack::has_as<T>::value, T>::type as() const;

#endif // defined(MSGPACK_USE_CPP03)

    /// Convert the object
    /**
     * If the object can't be converted to T, msgpack::type_error would be thrown.
     * @tparam T The type of v.
     * @param v The value you want to get. `v` is output parameter. `v` is overwritten by converted value from the object.
     * @return The reference of `v`.
     */
    template <typename T>
    T& convert(T& v) const;


#if !defined(MSGPACK_DISABLE_LEGACY_CONVERT)
    /// Convert the object (obsolete)
    /**
     * If the object can't be converted to T, msgpack::type_error would be thrown.
     * @tparam T The type of v.
     * @param v The pointer of the value you want to get. `v` is output parameter. `*v` is overwritten by converted value from the object.
     * @return The pointer of `v`.
     */
    template <typename T>
    T* convert(T* v) const;
#endif // !defined(MSGPACK_DISABLE_LEGACY_CONVERT)

    /// Convert the object if not nil
    /**
     * If the object is not nil and can't be converted to T, msgpack::type_error would be thrown.
     * @tparam T The type of v.
     * @param v The value you want to get. `v` is output parameter. `v` is overwritten by converted value from the object if the object is not nil.
     * @return If the object is nil, then return false, else return true.
     */
    template <typename T>
    bool convert_if_not_nil(T& v) const;

    /// Default constructor. The object is set to nil.
    object();

    /// Copy constructor. Object is shallow copied.
    object(const msgpack_object& o);

    /// Construct object from T
    /**
     * If `v` is the type that is corresponding to MessegePack format str, bin, ext, array, or map,
     * you need to call `object(const T& v, msgpack::zone& z)` instead of this constructor.
     *
     * @tparam T The type of `v`.
     * @param v The value you want to convert.
     */
    template <typename T>
    explicit object(const T& v);

    /// Construct object from T
    /**
     * The object is constructed on the zone `z`.
     * See https://github.com/msgpack/msgpack-c/wiki/v1_1_cpp_object
     *
     * @tparam T The type of `v`.
     * @param v The value you want to convert.
     * @param z The zone that is used by the object.
     */
    template <typename T>
    object(const T& v, msgpack::zone& z);

    /// Construct object from T (obsolete)
    /**
     * The object is constructed on the zone `z`.
     * Use `object(const T& v, msgpack::zone& z)` instead of this constructor.
     * See https://github.com/msgpack/msgpack-c/wiki/v1_1_cpp_object
     *
     * @tparam T The type of `v`.
     * @param v The value you want to convert.
     * @param z The pointer to the zone that is used by the object.
     */
    template <typename T>
    object(const T& v, msgpack::zone* z);

    template <typename T>
    object& operator=(const T& v);

    operator msgpack_object() const;

    struct with_zone;

private:
    struct implicit_type;

public:
    implicit_type convert() const;
};

class type_error : public std::bad_cast { };

struct object_kv {
    msgpack::object key;
    msgpack::object val;
};

struct object::with_zone : object {
    with_zone(msgpack::zone& z) : zone(z) { }
    msgpack::zone& zone;
private:
    with_zone();
};

/// @cond
} // MSGPACK_API_VERSION_NAMESPACE(v1)
/// @endcond

} // namespace msgpack

#endif // MSGPACK_OBJECT_FWD_HPP
