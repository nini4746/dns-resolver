/* dns_encode.c - serialize a DNS query message (RFC 1035 §4).
 *
 * Builds header + QNAME + QTYPE + QCLASS by hand. RD=0 (iterative).
 */
#include "dns_message.h"
#include <string.h>

/* Write a 16-bit value big-endian (network order) without htons dependency. */
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

int dns_encode_name(uint8_t *buf, size_t buflen, const char *name)
{
    size_t written = 0;
    const char *p = name;

    /* Root or empty name -> single zero byte. */
    if (name == NULL)
        return -1;

    /* Reject an over-long overall name early (255 on wire). */
    if (strlen(name) > 253)
        return -1;

    while (*p != '\0') {
        const char *dot = strchr(p, '.');
        size_t label_len = dot ? (size_t)(dot - p) : strlen(p);

        if (label_len == 0)
            return -1;                 /* empty label ("a..b") */
        if (label_len > DNS_MAX_LABEL)
            return -1;                 /* label too long */
        if (written + 1 + label_len + 1 > buflen)
            return -1;                 /* +1 for terminating zero */

        buf[written++] = (uint8_t)label_len;
        memcpy(buf + written, p, label_len);
        written += label_len;

        if (!dot)
            break;
        p = dot + 1;
        if (*p == '\0')                /* trailing dot "example.com." */
            break;
    }

    if (written + 1 > buflen)
        return -1;
    buf[written++] = 0x00;             /* root terminator */

    if (written > 255)                 /* total encoded name limit */
        return -1;
    return (int)written;
}

int dns_build_query(uint8_t *buf, size_t buflen,
                    uint16_t id, const char *name, uint16_t qtype)
{
    size_t off = 0;
    int n;

    if (buflen < 12)
        return -1;

    /* Header: ID, flags (RD=0), QDCOUNT=1, others 0. */
    put16(buf + 0, id);
    put16(buf + 2, 0x0000);            /* QR=0 opcode=0 RD=0 */
    put16(buf + 4, 1);                 /* QDCOUNT */
    put16(buf + 6, 0);                 /* ANCOUNT */
    put16(buf + 8, 0);                 /* NSCOUNT */
    put16(buf + 10, 0);                /* ARCOUNT */
    off = 12;

    n = dns_encode_name(buf + off, buflen - off, name);
    if (n < 0)
        return -1;
    off += (size_t)n;

    if (off + 4 > buflen)
        return -1;
    put16(buf + off, qtype);           /* QTYPE */
    off += 2;
    put16(buf + off, CLASS_IN);        /* QCLASS */
    off += 2;

    return (int)off;
}
