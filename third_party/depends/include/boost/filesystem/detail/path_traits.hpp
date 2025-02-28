//  filesystem path_traits.hpp  --------------------------------------------------------//

//  Copyright Beman Dawes 2009
//  Copyright Andrey Semashev 2022

//  Distributed under the Boost Software License, Version 1.0.
//  See http://www.boost.org/LICENSE_1_0.txt

//  Library home page: http://www.boost.org/libs/filesystem

#ifndef BOOST_FILESYSTEM_DETAIL_PATH_TRAITS_HPP
#define BOOST_FILESYSTEM_DETAIL_PATH_TRAITS_HPP

#include <boost/filesystem/config.hpp>
#include <cstddef>
#include <cstring> // for strlen
#include <cwchar> // for mbstate_t, wcslen
#include <locale>
#include <string>
#include <iterator>
#if !defined(BOOST_NO_CXX17_HDR_STRING_VIEW)
#include <string_view>
#endif
#include <boost/assert.hpp>
#include <boost/system/error_category.hpp>
#include <boost/type_traits/declval.hpp>
#include <boost/type_traits/remove_cv.hpp>
#if defined(BOOST_FILESYSTEM_CXX23_STRING_VIEW_HAS_IMPLICIT_RANGE_CTOR)
#include <boost/type_traits/disjunction.hpp>
#include <boost/core/enable_if.hpp>
#endif
#if !defined(BOOST_FILESYSTEM_NO_DEPRECATED) && BOOST_FILESYSTEM_VERSION < 4
#include <vector>
#include <list>
#endif

#include <boost/filesystem/detail/header.hpp> // must be the last #include

namespace boost {

template< typename, typename > class basic_string_view;

namespace container {
template< typename, typename, typename > class basic_string;
} // namespace container

namespace filesystem {

BOOST_FILESYSTEM_DECL system::error_category const& codecvt_error_category() BOOST_NOEXCEPT;

class directory_entry;

namespace detail {
namespace path_traits {

#if defined(BOOST_WINDOWS_API)
typedef wchar_t path_native_char_type;
#define BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE false
#define BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE true
#else
typedef char path_native_char_type;
#define BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE true
#define BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE false
#endif

typedef std::codecvt< wchar_t, char, std::mbstate_t > codecvt_type;

struct unknown_type_tag {};
struct ntcts_type_tag {};
struct char_ptr_tag : ntcts_type_tag {};
struct char_array_tag : ntcts_type_tag {};
struct string_class_tag {};
struct std_string_tag : string_class_tag {};
struct boost_container_string_tag : string_class_tag {};
struct std_string_view_tag : string_class_tag {};
struct boost_string_view_tag : string_class_tag {};
struct range_type_tag {};
struct directory_entry_tag {};

//! The traits define a number of properties of a path source
template< typename T >
struct path_source_traits
{
    //! The kind of the path source. Useful for dispatching.
    typedef unknown_type_tag tag_type;
    //! Character type that the source contains
    typedef void char_type;
    //! Indicates whether the source is natively supported by \c path::string_type as arguments for constructors/assignment/appending
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

template< >
struct path_source_traits< char* >
{
    typedef char_ptr_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE;
};

template< >
struct path_source_traits< const char* >
{
    typedef char_ptr_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE;
};

template< >
struct path_source_traits< wchar_t* >
{
    typedef char_ptr_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE;
};

template< >
struct path_source_traits< const wchar_t* >
{
    typedef char_ptr_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE;
};

template< >
struct path_source_traits< char[] >
{
    typedef char_array_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE;
};

template< >
struct path_source_traits< const char[] >
{
    typedef char_array_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE;
};

template< >
struct path_source_traits< wchar_t[] >
{
    typedef char_array_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE;
};

template< >
struct path_source_traits< const wchar_t[] >
{
    typedef char_array_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE;
};

template< std::size_t N >
struct path_source_traits< char[N] >
{
    typedef char_array_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE;
};

template< std::size_t N >
struct path_source_traits< const char[N] >
{
    typedef char_array_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE;
};

template< std::size_t N >
struct path_source_traits< wchar_t[N] >
{
    typedef char_array_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE;
};

template< std::size_t N >
struct path_source_traits< const wchar_t[N] >
{
    typedef char_array_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE;
};

template< >
struct path_source_traits< std::string >
{
    typedef std_string_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE;
};

template< >
struct path_source_traits< std::wstring >
{
    typedef std_string_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE;
};

template< >
struct path_source_traits< boost::container::basic_string< char, std::char_traits< char >, void > >
{
    typedef boost_container_string_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

template< >
struct path_source_traits< boost::container::basic_string< wchar_t, std::char_traits< wchar_t >, void > >
{
    typedef boost_container_string_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

#if !defined(BOOST_NO_CXX17_HDR_STRING_VIEW)

template< >
struct path_source_traits< std::string_view >
{
    typedef std_string_view_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE;
};

template< >
struct path_source_traits< std::wstring_view >
{
    typedef std_string_view_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE;
};

#endif // !defined(BOOST_NO_CXX17_HDR_STRING_VIEW)

template< >
struct path_source_traits< boost::basic_string_view< char, std::char_traits< char > > >
{
    typedef boost_string_view_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

template< >
struct path_source_traits< boost::basic_string_view< wchar_t, std::char_traits< wchar_t > > >
{
    typedef boost_string_view_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

#if !defined(BOOST_FILESYSTEM_NO_DEPRECATED) && BOOST_FILESYSTEM_VERSION < 4
template< >
struct
BOOST_FILESYSTEM_DETAIL_DEPRECATED("Boost.Filesystem path construction/assignment/appending from containers is deprecated, use strings or iterators instead.")
path_source_traits< std::vector< char > >
{
    // Since C++11 this could be string_class_tag as std::vector gained data() member
    typedef range_type_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

template< >
struct
BOOST_FILESYSTEM_DETAIL_DEPRECATED("Boost.Filesystem path construction/assignment/appending from containers is deprecated, use strings or iterators instead.")
path_source_traits< std::vector< wchar_t > >
{
    // Since C++11 this could be string_class_tag as std::vector gained data() member
    typedef range_type_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

template< >
struct
BOOST_FILESYSTEM_DETAIL_DEPRECATED("Boost.Filesystem path construction/assignment/appending from containers is deprecated, use strings or iterators instead.")
path_source_traits< std::list< char > >
{
    typedef range_type_tag tag_type;
    typedef char char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

template< >
struct
BOOST_FILESYSTEM_DETAIL_DEPRECATED("Boost.Filesystem path construction/assignment/appending from containers is deprecated, use strings or iterators instead.")
path_source_traits< std::list< wchar_t > >
{
    typedef range_type_tag tag_type;
    typedef wchar_t char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};
#endif // !defined(BOOST_FILESYSTEM_NO_DEPRECATED) && BOOST_FILESYSTEM_VERSION < 4

template< >
struct path_source_traits< directory_entry >
{
    typedef directory_entry_tag tag_type;
    typedef path_native_char_type char_type;
    static BOOST_CONSTEXPR_OR_CONST bool is_native = false;
};

#undef BOOST_FILESYSTEM_DETAIL_IS_CHAR_NATIVE
#undef BOOST_FILESYSTEM_DETAIL_IS_WCHAR_T_NATIVE


//! The trait tests if the type is a known path Source tag
template< typename Tag >
struct is_known_path_source_tag
{
    static BOOST_CONSTEXPR_OR_CONST bool value = true;
};

template< >
struct is_known_path_source_tag< unknown_type_tag >
{
    static BOOST_CONSTEXPR_OR_CONST bool value = false;
};

//! The trait tests if the type is compatible with path Source requirements
template< typename T >
struct is_path_source :
    public is_known_path_source_tag< typename path_source_traits< T >::tag_type >
{
};


//! The trait indicates whether the type is a path Source that is natively supported by path::string_type as the source for construction/assignment/appending
template< typename T >
struct is_native_path_source
{
    static BOOST_CONSTEXPR_OR_CONST bool value = path_source_traits< T >::is_native;
};


//! The trait indicates whether the type is one of the supported path character types
template< typename T >
struct is_path_char_type
{
    static BOOST_CONSTEXPR_OR_CONST bool value = false;
};

template< >
struct is_path_char_type< char >
{
    static BOOST_CONSTEXPR_OR_CONST bool value = true;
};

template< >
struct is_path_char_type< wchar_t >
{
    static BOOST_CONSTEXPR_OR_CONST bool value = true;
};


//! The trait indicates whether the type is an iterator over a sequence of path characters
template< typename Iterator >
struct is_path_source_iterator :
    public is_path_char_type< typename std::iterator_traits< Iterator >::value_type >
{
};


//! The trait indicates whether the type is a pointer to a sequence of native path characters
template< typename T >
struct is_native_char_ptr
{
    static BOOST_CONSTEXPR_OR_CONST bool value = false;
};

template< >
struct is_native_char_ptr< path_native_char_type* >
{
    static BOOST_CONSTEXPR_OR_CONST bool value = true;
};

template< >
struct is_native_char_ptr< const path_native_char_type* >
{
    static BOOST_CONSTEXPR_OR_CONST bool value = true;
};


//! Converts character encoding using the supplied codecvt facet. If \a cvt is \c NULL then \c path::codecvt() will be used.
BOOST_FILESYSTEM_DECL
void convert(const char* from, const char* from_end, std::wstring& to, const codecvt_type* cvt = NULL);

//! \overload convert
BOOST_FILESYSTEM_DECL
void convert(const wchar_t* from, const wchar_t* from_end, std::string& to, const codecvt_type* cvt = NULL);


//  Source dispatch  -----------------------------------------------------------------//

template< typename Source, typename Callback >
inline void dispatch(Source const& source, Callback cb, const codecvt_type* cvt = NULL);

template< typename Callback >
BOOST_FORCEINLINE void dispatch(const char* source, Callback cb, const codecvt_type* cvt, ntcts_type_tag)
{
    cb(source, source + std::strlen(source), cvt);
}

template< typename Callback >
BOOST_FORCEINLINE void dispatch(const wchar_t* source, Callback cb, const codecvt_type* cvt, ntcts_type_tag)
{
    cb(source, source + std::wcslen(source), cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch(Source const& source, Callback cb, const codecvt_type* cvt, string_class_tag)
{
    cb(source.data(), source.data() + source.size(), cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch(Source const& source, Callback cb, const codecvt_type* cvt, range_type_tag)
{
    std::basic_string< typename Source::value_type > src(source.begin(), source.end());
    cb(src.data(), src.data() + src.size(), cvt);
}

#if !defined(BOOST_FILESYSTEM_NO_DEPRECATED) && BOOST_FILESYSTEM_VERSION < 4

template< typename Callback >
BOOST_FORCEINLINE void dispatch(std::vector< char > const& source, Callback cb, const codecvt_type* cvt, range_type_tag)
{
    const char* data = NULL, *data_end = NULL;
    if (!source.empty())
    {
        data = &source[0];
        data_end = data + source.size();
    }
    cb(data, data_end, cvt);
}

template< typename Callback >
BOOST_FORCEINLINE void dispatch(std::vector< wchar_t > const& source, Callback cb, const codecvt_type* cvt, range_type_tag)
{
    const wchar_t* data = NULL, *data_end = NULL;
    if (!source.empty())
    {
        data = &source[0];
        data_end = data + source.size();
    }
    cb(data, data_end, cvt);
}

#endif // !defined(BOOST_FILESYSTEM_NO_DEPRECATED) && BOOST_FILESYSTEM_VERSION < 4

// Defined in directory.hpp to avoid circular header dependencies
template< typename Callback >
void dispatch(directory_entry const& de, Callback cb, const codecvt_type* cvt, directory_entry_tag);

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch(Source const& source, Callback cb, const codecvt_type* cvt)
{
    path_traits::dispatch(source, cb, cvt,
        typename path_traits::path_source_traits< typename boost::remove_cv< Source >::type >::tag_type());
}


typedef char yes_type;
struct no_type { char buf[2]; };

// Note: The obscure naming of the _check* functions below is a workaround for an MSVC-9.0 bug, which looks up the function outside the class scope

#if !defined(BOOST_FILESYSTEM_CXX23_STRING_VIEW_HAS_IMPLICIT_RANGE_CTOR)

//! The type trait indicates whether the type has a conversion path to one of the path source types
template< typename T >
struct is_convertible_to_path_source
{
    // Note: The obscure naming of this function is a workaround for an MSVC-9.0 bug, which looks up the function outside the class scope
    static yes_type _check_convertible_to_path_source(const char*);
    static yes_type _check_convertible_to_path_source(const wchar_t*);
    static yes_type _check_convertible_to_path_source(std::string const&);
    static yes_type _check_convertible_to_path_source(std::wstring const&);
    static yes_type _check_convertible_to_path_source(boost::container::basic_string< char, std::char_traits< char >, void > const&);
    static yes_type _check_convertible_to_path_source(boost::container::basic_string< wchar_t, std::char_traits< wchar_t >, void > const&);
#if !defined(BOOST_NO_CXX17_HDR_STRING_VIEW)
    static yes_type _check_convertible_to_path_source(std::string_view const&);
    static yes_type _check_convertible_to_path_source(std::wstring_view const&);
#endif
    static yes_type _check_convertible_to_path_source(boost::basic_string_view< char, std::char_traits< char > > const&);
    static yes_type _check_convertible_to_path_source(boost::basic_string_view< wchar_t, std::char_traits< wchar_t > > const&);
    static no_type _check_convertible_to_path_source(...);

    static BOOST_CONSTEXPR_OR_CONST bool value =
        sizeof(is_convertible_to_path_source< T >::_check_convertible_to_path_source(boost::declval< T const& >())) == sizeof(yes_type);
};

#else // !defined(BOOST_FILESYSTEM_CXX23_STRING_VIEW_HAS_IMPLICIT_RANGE_CTOR)

// Note: We use separate checks for convertibility to std::string_view and other types to avoid ambiguity with an implicit range constructor
//       of std::string_view in the early C++23 draft (N4892). If a user's type is convertible to e.g. std::string and also satisfies
//       ranges::contiguous_range and ranges::sized_range concepts then the conversion is ambiguous: the type is convertible to std::string
//       through the conversion operator in the user's class and is also convertible to std::string_view through the implicit conversion
//       constructor in std::string_view. The solution is to check convertibility to std::string_view separately first.

template< typename T >
struct is_convertible_to_std_string_view
{
    static yes_type _check_convertible_to_std_string_view(std::string_view const&);
    static yes_type _check_convertible_to_std_string_view(std::wstring_view const&);
    static no_type _check_convertible_to_std_string_view(...);

    static BOOST_CONSTEXPR_OR_CONST bool value =
        sizeof(is_convertible_to_std_string_view< T >::_check_convertible_to_std_string_view(boost::declval< T const& >())) == sizeof(yes_type);
};

template< typename T >
struct is_convertible_to_path_source_non_std_string_view
{
    // Note: The obscure naming of this function is a workaround for an MSVC-9.0 bug, which looks up the function outside the class scope
    static yes_type _check_convertible_to_path_source(const char*);
    static yes_type _check_convertible_to_path_source(const wchar_t*);
    static yes_type _check_convertible_to_path_source(std::string const&);
    static yes_type _check_convertible_to_path_source(std::wstring const&);
    static yes_type _check_convertible_to_path_source(boost::container::basic_string< char, std::char_traits< char >, void > const&);
    static yes_type _check_convertible_to_path_source(boost::container::basic_string< wchar_t, std::char_traits< wchar_t >, void > const&);
    static yes_type _check_convertible_to_path_source(boost::basic_string_view< char, std::char_traits< char > > const&);
    static yes_type _check_convertible_to_path_source(boost::basic_string_view< wchar_t, std::char_traits< wchar_t > > const&);
    static no_type _check_convertible_to_path_source(...);

    static BOOST_CONSTEXPR_OR_CONST bool value =
        sizeof(is_convertible_to_path_source_non_std_string_view< T >::_check_convertible_to_path_source(boost::declval< T const& >())) == sizeof(yes_type);
};

//! The type trait indicates whether the type has a conversion path to one of the path source types
template< typename T >
struct is_convertible_to_path_source :
    public boost::disjunction<
        is_convertible_to_std_string_view< T >,
        is_convertible_to_path_source_non_std_string_view< T >
    >
{
};

#endif // !defined(BOOST_FILESYSTEM_CXX23_STRING_VIEW_HAS_IMPLICIT_RANGE_CTOR)

//! The type trait makes \a T dependent on the second template argument. Used to delay type resolution and name binding.
template< typename T, typename >
struct make_dependent
{
    typedef T type;
};

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl(const char* source, Callback cb, const codecvt_type* cvt)
{
    typedef typename path_traits::make_dependent< const char*, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl(const wchar_t* source, Callback cb, const codecvt_type* cvt)
{
    typedef typename path_traits::make_dependent< const wchar_t*, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl(std::string const& source, Callback cb, const codecvt_type* cvt)
{
    typedef typename path_traits::make_dependent< std::string, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl(std::wstring const& source, Callback cb, const codecvt_type* cvt)
{
    typedef typename path_traits::make_dependent< std::wstring, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl
(
    boost::container::basic_string< char, std::char_traits< char >, void > const& source,
    Callback cb,
    const codecvt_type* cvt
)
{
    typedef typename path_traits::make_dependent< boost::container::basic_string< char, std::char_traits< char >, void >, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl
(
    boost::container::basic_string< wchar_t, std::char_traits< wchar_t >, void > const& source,
    Callback cb,
    const codecvt_type* cvt
)
{
    typedef typename path_traits::make_dependent< boost::container::basic_string< wchar_t, std::char_traits< wchar_t >, void >, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl
(
    boost::basic_string_view< char, std::char_traits< char > > const& source,
    Callback cb,
    const codecvt_type* cvt
)
{
    typedef typename path_traits::make_dependent< boost::basic_string_view< char, std::char_traits< char > >, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl
(
    boost::basic_string_view< wchar_t, std::char_traits< wchar_t > > const& source,
    Callback cb,
    const codecvt_type* cvt
)
{
    typedef typename path_traits::make_dependent< boost::basic_string_view< wchar_t, std::char_traits< wchar_t > >, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

#if !defined(BOOST_FILESYSTEM_CXX23_STRING_VIEW_HAS_IMPLICIT_RANGE_CTOR)

#if !defined(BOOST_NO_CXX17_HDR_STRING_VIEW)

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl(std::string_view const& source, Callback cb, const codecvt_type* cvt)
{
    typedef typename path_traits::make_dependent< std::string_view, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_impl(std::wstring_view const& source, Callback cb, const codecvt_type* cvt)
{
    typedef typename path_traits::make_dependent< std::wstring_view, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

#endif // !defined(BOOST_NO_CXX17_HDR_STRING_VIEW)

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible(Source const& source, Callback cb, const codecvt_type* cvt = NULL)
{
    typedef typename boost::remove_cv< Source >::type source_t;
    path_traits::dispatch_convertible_impl< source_t >(source, cb, cvt);
}

#else // !defined(BOOST_FILESYSTEM_CXX23_STRING_VIEW_HAS_IMPLICIT_RANGE_CTOR)

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_sv_impl(std::string_view const& source, Callback cb, const codecvt_type* cvt)
{
    typedef typename path_traits::make_dependent< std::string_view, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE void dispatch_convertible_sv_impl(std::wstring_view const& source, Callback cb, const codecvt_type* cvt)
{
    typedef typename path_traits::make_dependent< std::wstring_view, Source >::type source_t;
    path_traits::dispatch(static_cast< source_t const& >(source), cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE typename boost::disable_if_c<
    is_convertible_to_std_string_view< typename boost::remove_cv< Source >::type >::value
>::type dispatch_convertible(Source const& source, Callback cb, const codecvt_type* cvt = NULL)
{
    typedef typename boost::remove_cv< Source >::type source_t;
    path_traits::dispatch_convertible_impl< source_t >(source, cb, cvt);
}

template< typename Source, typename Callback >
BOOST_FORCEINLINE typename boost::enable_if_c<
    is_convertible_to_std_string_view< typename boost::remove_cv< Source >::type >::value
>::type dispatch_convertible(Source const& source, Callback cb, const codecvt_type* cvt = NULL)
{
    typedef typename boost::remove_cv< Source >::type source_t;
    path_traits::dispatch_convertible_sv_impl< source_t >(source, cb, cvt);
}

#endif // !defined(BOOST_FILESYSTEM_CXX23_STRING_VIEW_HAS_IMPLICIT_RANGE_CTOR)

} // namespace path_traits
} // namespace detail
} // namespace filesystem
} // namespace boost

#include <boost/filesystem/detail/footer.hpp>

#endif // BOOST_FILESYSTEM_DETAIL_PATH_TRAITS_HPP
