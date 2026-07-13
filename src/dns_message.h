/* dns_message.h - RFC 1035 wire format structures and constants.
 *
 * No resolver library. All DNS types built by hand.
 */
#ifndef DNS_MESSAGE_H
#define DNS_MESSAGE_H

#include <stdint.h>
#include <stddef.h>

/* Record types (RFC 1035 §3.2.2 + RFC 3596 for AAAA). */
#define TYPE_A     1
#define TYPE_NS    2
#define TYPE_CNAME 5
#define TYPE_AAAA  28

/* Class. */
#define CLASS_IN 1

/* Response codes (RFC 1035 §4.1.1). */
#define RCODE_NOERROR  0
#define RCODE_FORMERR  1
#define RCODE_SERVFAIL 2
#define RCODE_NXDOMAIN 3

/* Limits from SPEC. */
#define DNS_MAX_NAME     256   /* incl. terminating NUL; on-wire max 255 */
#define DNS_MAX_LABEL    63
#define DNS_MAX_PACKET   512   /* no EDNS0 */
#define DNS_MAX_JUMPS    128   /* compression pointer jump cap */
#define DNS_MAX_RECORDS  64    /* per section; bounded by 512B packets */

/* Parsed resource record. Name and rdata_name are NUL-terminated dotted
 * strings ("example.com"). addr holds A (4 bytes) or AAAA (16 bytes). */
typedef struct {
    char     name[DNS_MAX_NAME];
    uint16_t type;
    uint16_t rr_class;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t  addr[16];              /* for A / AAAA */
    char     rdata_name[DNS_MAX_NAME]; /* for NS / CNAME target */
} dns_rr;

/* Parsed message header flags of interest. */
typedef struct {
    uint16_t id;
    int      qr;      /* response bit */
    int      opcode;
    int      aa;      /* authoritative answer */
    int      tc;      /* truncated */
    int      rd;
    int      ra;
    int      rcode;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header;

typedef struct {
    dns_header header;
    char       qname[DNS_MAX_NAME];
    uint16_t   qtype;
    uint16_t   qclass;

    dns_rr  answers[DNS_MAX_RECORDS];
    size_t  n_answers;
    dns_rr  authorities[DNS_MAX_RECORDS];
    size_t  n_authorities;
    dns_rr  additionals[DNS_MAX_RECORDS];
    size_t  n_additionals;
} dns_message;

/* ---- dns_encode.c ---- */

/* Build a query packet into buf (capacity buflen). Returns packet length on
 * success, or negative on error (name too long / buffer too small). */
int dns_build_query(uint8_t *buf, size_t buflen,
                    uint16_t id, const char *name, uint16_t qtype);

/* Encode a dotted name into wire label format. Returns bytes written or <0. */
int dns_encode_name(uint8_t *buf, size_t buflen, const char *name);

/* ---- dns_decode.c ---- */

/* Parse a full response packet. Returns 0 on success, negative on malformed
 * input (never reads out of bounds, never loops forever). */
int dns_parse_message(const uint8_t *pkt, size_t pktlen, dns_message *out);

/* Decode a (possibly compressed) name starting at *offset. Advances *offset
 * past the name in the *original* stream (following the first pointer, the
 * offset stops at the pointer). Returns 0 on success, negative on error. */
int dns_decode_name(const uint8_t *pkt, size_t pktlen, size_t *offset,
                    char *out, size_t outlen);

#endif /* DNS_MESSAGE_H */
