/* root_hints.h - hardcoded IANA root server IPv4 addresses. */
#ifndef ROOT_HINTS_H
#define ROOT_HINTS_H

#include <stddef.h>

typedef struct {
    const char *name;  /* e.g. "a.root-servers.net" */
    const char *ipv4;  /* dotted IPv4 */
} root_hint;

extern const root_hint ROOT_HINTS[];
extern const size_t ROOT_HINTS_COUNT;

#endif /* ROOT_HINTS_H */
