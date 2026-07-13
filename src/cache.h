/* cache.h - TTL-based in-memory DNS record cache (process lifetime). */
#ifndef CACHE_H
#define CACHE_H

#include "dns_message.h"
#include <time.h>

/* Insert a record. Absolute expiry = now + rr->ttl. TTL 0 is not stored.
 * A record identical in (name,type,rdata) refreshes the existing entry. */
void cache_put_at(const dns_rr *rr, time_t now);
void cache_put(const dns_rr *rr);

/* Collect up to `max` non-expired records matching (name, type) into out[].
 * Name match is case-insensitive. Returns the number written. */
size_t cache_get_at(const char *name, uint16_t type,
                    dns_rr *out, size_t max, time_t now);
size_t cache_get(const char *name, uint16_t type, dns_rr *out, size_t max);

/* Clear all entries (for tests). */
void cache_clear(void);

#endif /* CACHE_H */
