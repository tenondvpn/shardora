//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/json
//

#ifndef BOOST_JSON_DETAIL_EXCEPT_HPP
#define BOOST_JSON_DETAIL_EXCEPT_HPP

#include <boost/json/error.hpp>

BOOST_JSON_NS_BEGIN
namespace detail {

#define BOOST_JSON_FAIL(ec, e) \
    BOOST_STATIC_CONSTEXPR source_location loc = BOOST_CURRENT_LOCATION; \
    (ec).assign(e, &loc);

BOOST_JSON_DECL void BOOST_NORETURN throw_bad_alloc(source_location const& loc);
BOOST_JSON_DECL void BOOST_NORETURN throw_invalid_argument(char const* what, source_location const& loc);
BOOST_JSON_DECL void BOOST_NORETURN throw_length_error(char const* what, source_location const& loc);
BOOST_JSON_DECL void BOOST_NORETURN throw_out_of_range(source_location const& loc);
BOOST_JSON_DECL void BOOST_NORETURN throw_system_error(error_code const& ec, source_location const& loc);

} // detail
BOOST_JSON_NS_END

#endif
