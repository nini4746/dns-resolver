/* test_cname.c - CNAME chain termination (R5), offline.
 *
 * Seeds the cache with a CNAME loop (a.test <-> b.test) and a finite chain, so
 * resolve() chases entirely from cache (no network) and must terminate:
 *   - loop         -> RES_CNAME_LOOP
 *   - finite chain -> RES_OK with the target's A.
 */
#include "../src/resolve.h"
#include "../src/cache.h"
#include "../src/dns_message.h"
#include <string.h>
#include <stdio.h>

static int tests_run = 0;
#define CHECK(cond) do { tests_run++; if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    return 1; } } while (0)

static void put_cname(const char *name, const char *target)
{
    dns_rr rr;
    memset(&rr, 0, sizeof(rr));
    snprintf(rr.name, sizeof(rr.name), "%s", name);
    rr.type = TYPE_CNAME;
    rr.rr_class = CLASS_IN;
    rr.ttl = 300;
    snprintf(rr.rdata_name, sizeof(rr.rdata_name), "%s", target);
    cache_put(&rr);
}

static void put_a(const char *name, uint8_t d)
{
    dns_rr rr;
    memset(&rr, 0, sizeof(rr));
    snprintf(rr.name, sizeof(rr.name), "%s", name);
    rr.type = TYPE_A;
    rr.rr_class = CLASS_IN;
    rr.ttl = 300;
    rr.rdlength = 4;
    rr.addr[0] = 10; rr.addr[1] = 0; rr.addr[2] = 0; rr.addr[3] = d;
    cache_put(&rr);
}

int main(void)
{
    resolve_result result;
    path_log log;
    int rc;

    /* Loop: a.test -> b.test -> a.test. Must terminate with CNAME_LOOP. */
    cache_clear();
    put_cname("a.test", "b.test");
    put_cname("b.test", "a.test");
    log.n = 0;
    rc = resolve("a.test", TYPE_A, &result, &log);
    CHECK(rc == RES_CNAME_LOOP);

    /* Finite chain: c.test -> d.test -> A 10.0.0.42. Must resolve. */
    cache_clear();
    put_cname("c.test", "d.test");
    put_a("d.test", 42);
    log.n = 0;
    rc = resolve("c.test", TYPE_A, &result, &log);
    CHECK(rc == RES_OK);
    CHECK(result.n_records == 1);
    CHECK(result.records[0].addr[3] == 42);

    printf("test_cname: ALL PASS (%d checks)\n", tests_run);
    return 0;
}
