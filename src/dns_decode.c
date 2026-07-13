/* dns_decode.c - parse DNS response messages (RFC 1035 §4).
 *
 * Every read is bounds-checked. Compression pointers (§4.1.4) may only point
 * strictly backward, and total jumps are capped, so parsing always terminates.
 */
#include "dns_message.h"
#include <string.h>

static int read16(const uint8_t *pkt, size_t pktlen, size_t off, uint16_t *out)
{
    if (off + 2 > pktlen)
        return -1;
    *out = (uint16_t)((pkt[off] << 8) | pkt[off + 1]);
    return 0;
}

static int read32(const uint8_t *pkt, size_t pktlen, size_t off, uint32_t *out)
{
    if (off + 4 > pktlen)
        return -1;
    *out = ((uint32_t)pkt[off] << 24) | ((uint32_t)pkt[off + 1] << 16) |
           ((uint32_t)pkt[off + 2] << 8) | (uint32_t)pkt[off + 3];
    return 0;
}

int dns_decode_name(const uint8_t *pkt, size_t pktlen, size_t *offset,
                    char *out, size_t outlen)
{
    size_t pos = *offset;
    size_t out_len = 0;
    int jumps = 0;
    int followed_pointer = 0;
    size_t after_first_pointer = 0;

    if (outlen == 0)
        return -1;
    out[0] = '\0';

    for (;;) {
        uint8_t len;

        if (pos >= pktlen)
            return -1;                         /* boundary read */
        len = pkt[pos];

        if ((len & 0xc0) == 0xc0) {
            /* Compression pointer: two bytes, low 14 bits are the target. */
            uint16_t ptr;
            if (pos + 2 > pktlen)
                return -1;
            ptr = (uint16_t)(((len & 0x3f) << 8) | pkt[pos + 1]);

            if (!followed_pointer) {
                after_first_pointer = pos + 2;
                followed_pointer = 1;
            }
            if (++jumps > DNS_MAX_JUMPS)
                return -1;                     /* jump cap */
            if (ptr >= pos)
                return -1;                     /* must point strictly backward */
            pos = ptr;
            continue;
        }

        if ((len & 0xc0) != 0)
            return -1;                         /* reserved label bits */

        if (len == 0) {
            /* End of name. */
            pos += 1;
            break;
        }

        /* Regular label. */
        if (len > DNS_MAX_LABEL)
            return -1;
        if (pos + 1 + len > pktlen)
            return -1;                         /* boundary read */
        /* +1 for a separating dot, +1 for terminating NUL. */
        if (out_len + len + 2 > outlen)
            return -1;                         /* name too long */

        if (out_len != 0)
            out[out_len++] = '.';
        memcpy(out + out_len, pkt + pos + 1, len);
        out_len += len;
        out[out_len] = '\0';
        pos += 1 + len;
    }

    *offset = followed_pointer ? after_first_pointer : pos;
    return 0;
}

/* Parse one resource record at *offset. */
static int parse_rr(const uint8_t *pkt, size_t pktlen, size_t *offset, dns_rr *rr)
{
    size_t off = *offset;
    uint16_t type, cls, rdlength;
    uint32_t ttl;
    size_t rdata_start;

    memset(rr, 0, sizeof(*rr));

    if (dns_decode_name(pkt, pktlen, &off, rr->name, sizeof(rr->name)) < 0)
        return -1;
    if (read16(pkt, pktlen, off, &type) < 0) return -1; off += 2;
    if (read16(pkt, pktlen, off, &cls) < 0) return -1; off += 2;
    if (read32(pkt, pktlen, off, &ttl) < 0) return -1; off += 4;
    if (read16(pkt, pktlen, off, &rdlength) < 0) return -1; off += 2;

    if (off + rdlength > pktlen)
        return -1;                             /* RDATA boundary */

    rr->type = type;
    rr->rr_class = cls;
    rr->ttl = ttl;
    rr->rdlength = rdlength;
    rdata_start = off;

    if (type == TYPE_A) {
        if (rdlength != 4)
            return -1;
        memcpy(rr->addr, pkt + rdata_start, 4);
    } else if (type == TYPE_AAAA) {
        if (rdlength != 16)
            return -1;
        memcpy(rr->addr, pkt + rdata_start, 16);
    } else if (type == TYPE_NS || type == TYPE_CNAME) {
        /* RDATA is a (possibly compressed) domain name. */
        size_t name_off = rdata_start;
        if (dns_decode_name(pkt, pktlen, &name_off,
                            rr->rdata_name, sizeof(rr->rdata_name)) < 0)
            return -1;
    }
    /* Unknown types: skip RDLENGTH bytes (already bounds-checked). */

    *offset = rdata_start + rdlength;
    return 0;
}

static int parse_section(const uint8_t *pkt, size_t pktlen, size_t *offset,
                         uint16_t count, dns_rr *arr, size_t *n_out)
{
    size_t i;
    *n_out = 0;
    for (i = 0; i < count; i++) {
        if (i >= DNS_MAX_RECORDS) {
            /* Skip remaining records but keep parsing to stay in sync is
             * unnecessary here; a 512B packet cannot hold this many. Treat
             * as malformed to be safe. */
            return -1;
        }
        if (parse_rr(pkt, pktlen, offset, &arr[i]) < 0)
            return -1;
        (*n_out)++;
    }
    return 0;
}

int dns_parse_message(const uint8_t *pkt, size_t pktlen, dns_message *out)
{
    size_t off = 0;
    uint16_t flags;
    uint16_t i;

    memset(out, 0, sizeof(*out));

    if (pktlen < 12)
        return -1;

    out->header.id = (uint16_t)((pkt[0] << 8) | pkt[1]);
    flags = (uint16_t)((pkt[2] << 8) | pkt[3]);
    out->header.qr     = (flags >> 15) & 1;
    out->header.opcode = (flags >> 11) & 0xf;
    out->header.aa     = (flags >> 10) & 1;
    out->header.tc     = (flags >> 9) & 1;
    out->header.rd     = (flags >> 8) & 1;
    out->header.ra     = (flags >> 7) & 1;
    out->header.rcode  = flags & 0xf;
    out->header.qdcount = (uint16_t)((pkt[4] << 8) | pkt[5]);
    out->header.ancount = (uint16_t)((pkt[6] << 8) | pkt[7]);
    out->header.nscount = (uint16_t)((pkt[8] << 8) | pkt[9]);
    out->header.arcount = (uint16_t)((pkt[10] << 8) | pkt[11]);
    off = 12;

    /* Question section. Store the first, skip the rest. */
    for (i = 0; i < out->header.qdcount; i++) {
        char qname[DNS_MAX_NAME];
        uint16_t qtype, qclass;
        if (dns_decode_name(pkt, pktlen, &off, qname, sizeof(qname)) < 0)
            return -1;
        if (read16(pkt, pktlen, off, &qtype) < 0) return -1; off += 2;
        if (read16(pkt, pktlen, off, &qclass) < 0) return -1; off += 2;
        if (i == 0) {
            strncpy(out->qname, qname, sizeof(out->qname) - 1);
            out->qtype = qtype;
            out->qclass = qclass;
        }
    }

    if (parse_section(pkt, pktlen, &off, out->header.ancount,
                      out->answers, &out->n_answers) < 0)
        return -1;
    if (parse_section(pkt, pktlen, &off, out->header.nscount,
                      out->authorities, &out->n_authorities) < 0)
        return -1;
    if (parse_section(pkt, pktlen, &off, out->header.arcount,
                      out->additionals, &out->n_additionals) < 0)
        return -1;

    return 0;
}
