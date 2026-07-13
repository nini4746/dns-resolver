/* root_hints.c - IANA root name server IPv4 addresses (named.root).
 * These change on the order of decades; hardcoding is fine for a toy. */
#include "root_hints.h"

const root_hint ROOT_HINTS[] = {
    { "a.root-servers.net", "198.41.0.4" },
    { "b.root-servers.net", "170.247.170.2" },
    { "c.root-servers.net", "192.33.4.12" },
    { "d.root-servers.net", "199.7.91.13" },
    { "f.root-servers.net", "192.5.5.241" },
    { "i.root-servers.net", "192.36.148.17" },
    { "k.root-servers.net", "193.0.14.129" },
    { "m.root-servers.net", "202.12.27.33" },
};

const size_t ROOT_HINTS_COUNT = sizeof(ROOT_HINTS) / sizeof(ROOT_HINTS[0]);
