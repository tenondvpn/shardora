#ifndef MAXMINDDB_CONFIG_H
#define MAXMINDDB_CONFIG_H

#ifndef MMDB_UINT128_USING_MODE
/* Define as 1 if we use unsigned int __atribute__ ((__mode__(TI))) for uint128 values */
/* #undef MMDB_UINT128_USING_MODE */
#endif

#ifndef MMDB_UINT128_IS_BYTE_ARRAY
/* Define as 1 if we don't have an unsigned __int128 type */
/* #undef MMDB_UINT128_IS_BYTE_ARRAY */
#endif

#endif                          /* MAXMINDDB_CONFIG_H */
