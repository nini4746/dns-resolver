/* test_message.c - unit tests for dns_encode / dns_decode.
 * assert-based, no framework. Build with -fsanitize=address for the fuzz part.
 */
#include "../src/dns_message.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int tests_run = 0;
#define CHECK(cond) do { tests_run++; if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    return 1; } } while (0)

/* R1: query serialization matches expected bytes. */
static int test_encode_query(void)
{
    uint8_t buf[512];
    int n;
    const uint8_t expect[] = {
        0x12, 0x34,             /* id */
        0x00, 0x00,             /* flags: RD=0 */
        0x00, 0x01,             /* qdcount */
        0x00, 0x00,             /* ancount */
        0x00, 0x00,             /* nscount */
        0x00, 0x00,             /* arcount */
        0x07, 'e','x','a','m','p','l','e',
        0x03, 'c','o','m',
        0x00,                   /* root */
        0x00, 0x01,             /* QTYPE A */
        0x00, 0x01              /* QCLASS IN */
    };

    n = dns_build_query(buf, sizeof(buf), 0x1234, "example.com", TYPE_A);
    CHECK(n == (int)sizeof(expect));
    CHECK(memcmp(buf, expect, sizeof(expect)) == 0);
    return 0;
}

/* R1: over-long label / name rejected. */
static int test_encode_limits(void)
{
    uint8_t buf[512];
    char longlabel[80];
    char longname[400];
    size_t i;

    memset(longlabel, 'a', 64);   /* 64 > 63 */
    longlabel[64] = '\0';
    CHECK(dns_build_query(buf, sizeof(buf), 1, longlabel, TYPE_A) < 0);

    /* Build "aaaa....aaa" of many labels exceeding 255 total. */
    for (i = 0; i + 2 < sizeof(longname); i += 2) {
        longname[i] = 'a';
        longname[i + 1] = '.';
    }
    longname[sizeof(longname) - 1] = '\0';
    CHECK(dns_build_query(buf, sizeof(buf), 1, longname, TYPE_A) < 0);
    return 0;
}

/* Build a valid response packet with a compression pointer in the answer. */
static size_t make_good_response(uint8_t *p)
{
    size_t n = 0;
    /* header */
    uint8_t hdr[] = {
        0x12, 0x34, 0x81, 0x80, /* id, QR=1 RD=1 RA=1 rcode=0 */
        0x00, 0x01,             /* qd=1 */
        0x00, 0x01,             /* an=1 */
        0x00, 0x00, 0x00, 0x00
    };
    memcpy(p + n, hdr, sizeof(hdr)); n += sizeof(hdr);
    /* question: example.com A IN (qname starts at offset 12) */
    {
        uint8_t q[] = { 0x07,'e','x','a','m','p','l','e',0x03,'c','o','m',0x00,
                        0x00,0x01, 0x00,0x01 };
        memcpy(p + n, q, sizeof(q)); n += sizeof(q);
    }
    /* answer: name = pointer to 0x0c, A IN ttl=3600 rdlen=4 93.184.216.34 */
    {
        uint8_t a[] = { 0xc0, 0x0c, 0x00,0x01, 0x00,0x01,
                        0x00,0x00,0x0e,0x10, 0x00,0x04,
                        93,184,216,34 };
        memcpy(p + n, a, sizeof(a)); n += sizeof(a);
    }
    return n;
}

/* R2: compression pointer decodes correctly, record extracted. */
static int test_decode_good(void)
{
    uint8_t pkt[512];
    size_t len = make_good_response(pkt);
    dns_message msg;

    CHECK(dns_parse_message(pkt, len, &msg) == 0);
    CHECK(msg.header.qr == 1);
    CHECK(msg.header.rcode == RCODE_NOERROR);
    CHECK(msg.n_answers == 1);
    CHECK(strcmp(msg.answers[0].name, "example.com") == 0); /* via pointer */
    CHECK(msg.answers[0].type == TYPE_A);
    CHECK(msg.answers[0].ttl == 3600);
    CHECK(msg.answers[0].addr[0] == 93 && msg.answers[0].addr[3] == 34);
    return 0;
}

/* R2: pointer loop must error in finite time. */
static int test_decode_pointer_loop(void)
{
    uint8_t pkt[64];
    dns_message msg;
    char out[256];
    size_t off, n = 0;

    /* header qd=1 an=0 */
    memset(pkt, 0, sizeof(pkt));
    pkt[2] = 0x81; pkt[3] = 0x80;
    pkt[4] = 0x00; pkt[5] = 0x01;
    n = 12;
    /* qname at 12: 0xc0 0x0e -> 14 ; at 14: 0xc0 0x0c -> 12 (loop) */
    pkt[12] = 0xc0; pkt[13] = 0x0e;
    pkt[14] = 0xc0; pkt[15] = 0x0c;
    n = 16;
    (void)n;

    off = 12;
    CHECK(dns_decode_name(pkt, sizeof(pkt), &off, out, sizeof(out)) < 0);
    /* full parse should also fail, not hang */
    CHECK(dns_parse_message(pkt, 16, &msg) < 0);
    return 0;
}

/* R2: forward pointer (>= current) rejected. */
static int test_decode_forward_pointer(void)
{
    uint8_t pkt[64];
    char out[256];
    size_t off;
    memset(pkt, 0, sizeof(pkt));
    pkt[12] = 0xc0; pkt[13] = 0x20;  /* points forward to 0x20 */
    off = 12;
    CHECK(dns_decode_name(pkt, sizeof(pkt), &off, out, sizeof(out)) < 0);
    return 0;
}

/* R2: RDATA beyond packet boundary rejected. */
static int test_decode_rdata_overflow(void)
{
    uint8_t pkt[512];
    size_t len = make_good_response(pkt);
    dns_message msg;
    /* Corrupt the answer rdlength to claim 200 bytes. */
    /* answer rdlength is the two bytes right before the 4-byte address. */
    pkt[len - 6] = 0x00;
    pkt[len - 5] = 0xC8;  /* 200 */
    CHECK(dns_parse_message(pkt, len, &msg) < 0);
    return 0;
}

/* R2: truncation fuzz - parsing any prefix must not crash. */
static int test_decode_truncation_fuzz(void)
{
    uint8_t pkt[512];
    size_t len = make_good_response(pkt);
    size_t i;
    dns_message msg;
    for (i = 0; i <= len; i++) {
        /* Return value ignored; the point is no crash / no OOB (ASan). */
        (void)dns_parse_message(pkt, i, &msg);
    }
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_encode_query();
    rc |= test_encode_limits();
    rc |= test_decode_good();
    rc |= test_decode_pointer_loop();
    rc |= test_decode_forward_pointer();
    rc |= test_decode_rdata_overflow();
    rc |= test_decode_truncation_fuzz();

    if (rc == 0)
        printf("test_message: ALL PASS (%d checks)\n", tests_run);
    else
        printf("test_message: FAILURES\n");
    return rc;
}
