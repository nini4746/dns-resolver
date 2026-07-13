/* test_cache.c - TTL cache expiry and TTL-0 behavior (R6, must-NOT). */
#include "../src/cache.h"
#include "../src/dns_message.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int tests_run = 0;
#define CHECK(cond) do { tests_run++; if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    return 1; } } while (0)

static dns_rr make_a(const char *name, uint32_t ttl, uint8_t last)
{
    dns_rr rr;
    memset(&rr, 0, sizeof(rr));
    snprintf(rr.name, sizeof(rr.name), "%s", name);
    rr.type = TYPE_A;
    rr.rr_class = CLASS_IN;
    rr.ttl = ttl;
    rr.rdlength = 4;
    rr.addr[0] = 10; rr.addr[1] = 0; rr.addr[2] = 0; rr.addr[3] = last;
    return rr;
}

int main(void)
{
    dns_rr out[8];
    dns_rr rr;
    size_t n;

    cache_clear();

    /* Store with TTL 3600 at t=1000. */
    rr = make_a("example.com", 3600, 34);
    cache_put_at(&rr, 1000);

    /* Visible within TTL. */
    n = cache_get_at("example.com", TYPE_A, out, 8, 1000);
    CHECK(n == 1);
    CHECK(out[0].addr[3] == 34);

    /* Case-insensitive name match. */
    n = cache_get_at("EXAMPLE.COM", TYPE_A, out, 8, 2000);
    CHECK(n == 1);

    /* Expired entry is never returned (must-NOT). */
    n = cache_get_at("example.com", TYPE_A, out, 8, 1000 + 3600 + 1);
    CHECK(n == 0);

    /* TTL 0 is not cached. */
    cache_clear();
    rr = make_a("zero.example", 0, 9);
    cache_put_at(&rr, 5000);
    n = cache_get_at("zero.example", TYPE_A, out, 8, 5000);
    CHECK(n == 0);

    /* Wrong type does not match. */
    cache_clear();
    rr = make_a("a.test", 100, 1);
    cache_put_at(&rr, 0);
    n = cache_get_at("a.test", TYPE_AAAA, out, 8, 0);
    CHECK(n == 0);
    n = cache_get_at("a.test", TYPE_A, out, 8, 0);
    CHECK(n == 1);

    printf("test_cache: ALL PASS (%d checks)\n", tests_run);
    return 0;
}
