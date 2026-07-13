/* cache.c - fixed-size linear-scan TTL cache.
 *
 * Expired entries are never returned (must-NOT). TTL 0 records are not stored.
 */
#include "cache.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#define CACHE_CAP 8192

typedef struct {
    dns_rr rr;
    time_t expiry;
    int    used;
} cache_entry;

static cache_entry g_cache[CACHE_CAP];

void cache_clear(void)
{
    memset(g_cache, 0, sizeof(g_cache));
}

/* Two records are "the same key+value" when name (ci), type, and rdata match. */
static int same_record(const dns_rr *a, const dns_rr *b)
{
    if (a->type != b->type)
        return 0;
    if (strcasecmp(a->name, b->name) != 0)
        return 0;
    if (a->type == TYPE_A)
        return memcmp(a->addr, b->addr, 4) == 0;
    if (a->type == TYPE_AAAA)
        return memcmp(a->addr, b->addr, 16) == 0;
    if (a->type == TYPE_NS || a->type == TYPE_CNAME)
        return strcasecmp(a->rdata_name, b->rdata_name) == 0;
    return 0;
}

void cache_put_at(const dns_rr *rr, time_t now)
{
    size_t i, free_slot = CACHE_CAP;
    time_t oldest = 0;
    size_t oldest_slot = 0;
    int have_oldest = 0;

    if (rr->ttl == 0)
        return;  /* not cached */
    if (rr->type != TYPE_A && rr->type != TYPE_AAAA &&
        rr->type != TYPE_NS && rr->type != TYPE_CNAME)
        return;  /* only cache what we can serve */

    /* Refresh identical record, or find a free/expired slot. */
    for (i = 0; i < CACHE_CAP; i++) {
        if (!g_cache[i].used) {
            if (free_slot == CACHE_CAP)
                free_slot = i;
            continue;
        }
        if (g_cache[i].expiry <= now) {
            if (free_slot == CACHE_CAP)
                free_slot = i;   /* reuse expired */
            continue;
        }
        if (same_record(&g_cache[i].rr, rr)) {
            g_cache[i].rr = *rr;
            g_cache[i].expiry = now + (time_t)rr->ttl;
            return;
        }
        if (!have_oldest || g_cache[i].expiry < oldest) {
            oldest = g_cache[i].expiry;
            oldest_slot = i;
            have_oldest = 1;
        }
    }

    if (free_slot == CACHE_CAP) {
        /* Full of live entries: evict the soonest-to-expire. */
        free_slot = have_oldest ? oldest_slot : 0;
    }
    g_cache[free_slot].rr = *rr;
    g_cache[free_slot].expiry = now + (time_t)rr->ttl;
    g_cache[free_slot].used = 1;
}

void cache_put(const dns_rr *rr)
{
    cache_put_at(rr, time(NULL));
}

size_t cache_get_at(const char *name, uint16_t type,
                    dns_rr *out, size_t max, time_t now)
{
    size_t i, n = 0;
    for (i = 0; i < CACHE_CAP && n < max; i++) {
        if (!g_cache[i].used)
            continue;
        if (g_cache[i].expiry <= now)
            continue;  /* expired: never returned */
        if (g_cache[i].rr.type != type)
            continue;
        if (strcasecmp(g_cache[i].rr.name, name) != 0)
            continue;
        out[n++] = g_cache[i].rr;
    }
    return n;
}

size_t cache_get(const char *name, uint16_t type, dns_rr *out, size_t max)
{
    return cache_get_at(name, type, out, max, time(NULL));
}
