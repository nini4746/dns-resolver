/* dns_query.c - send a DNS query over UDP and receive a validated response.
 *
 * IPv4 UDP only. No system resolver: the server is always a numeric IP passed
 * to inet_pton. Transaction ID is validated; mismatches are discarded and we
 * keep waiting until the per-attempt deadline. select() gives a finite bound.
 */
#include "dns_query.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

int g_verbose = 0;

/* Monotonic-ish seconds as double, for deadline tracking. */
static double now_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static uint16_t random_id(void)
{
    /* Not cryptographic; enough for ID validation per SPEC. */
    return (uint16_t)(rand() & 0xffff);
}

/* One attempt with its own transaction ID and deadline. */
static int query_once(const char *server_ip, const char *name, uint16_t qtype,
                      dns_message *out)
{
    uint8_t qbuf[DNS_MAX_PACKET];
    uint8_t rbuf[DNS_MAX_PACKET];
    struct sockaddr_in dst;
    uint16_t id = random_id();
    int qlen, fd, rc = QERR_TIMEOUT;
    double deadline;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    if (inet_pton(AF_INET, server_ip, &dst.sin_addr) != 1)
        return QERR_INPUT;

    qlen = dns_build_query(qbuf, sizeof(qbuf), id, name, qtype);
    if (qlen < 0)
        return QERR_INPUT;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return QERR_SOCKET;

    if (sendto(fd, qbuf, (size_t)qlen, 0,
               (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        close(fd);
        return QERR_SOCKET;
    }

    if (g_verbose)
        fprintf(stderr, "    -> query @%s %s type %u (id=%u)\n",
                server_ip, name, (unsigned)qtype, (unsigned)id);

    deadline = now_sec() + QUERY_TIMEOUT_SEC;

    for (;;) {
        fd_set rfds;
        struct timeval tv;
        double remaining = deadline - now_sec();
        int sel;

        if (remaining <= 0) {
            rc = QERR_TIMEOUT;
            break;
        }
        tv.tv_sec = (time_t)remaining;
        tv.tv_usec = (suseconds_t)((remaining - (double)tv.tv_sec) * 1e6);

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        sel = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR)
                continue;
            rc = QERR_SOCKET;
            break;
        }
        if (sel == 0) {
            rc = QERR_TIMEOUT;
            break;
        }

        {
            struct sockaddr_in src;
            socklen_t srclen = sizeof(src);
            ssize_t n = recvfrom(fd, rbuf, sizeof(rbuf), 0,
                                 (struct sockaddr *)&src, &srclen);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                rc = QERR_SOCKET;
                break;
            }
            if (n < 12)
                continue;  /* runt; keep waiting */

            /* Transaction ID must match; else discard and keep waiting. */
            {
                uint16_t rid = (uint16_t)((rbuf[0] << 8) | rbuf[1]);
                if (rid != id) {
                    if (g_verbose)
                        fprintf(stderr, "    (discard: id %u != %u)\n",
                                (unsigned)rid, (unsigned)id);
                    continue;
                }
            }

            if (dns_parse_message(rbuf, (size_t)n, out) < 0) {
                rc = QERR_PARSE;
                break;
            }
            if (!out->header.qr)
                continue;  /* not a response; ignore */

            rc = QOK;
            break;
        }
    }

    close(fd);
    return rc;
}

int query_server(const char *server_ip, const char *name, uint16_t qtype,
                 dns_message *out)
{
    int attempt;
    int rc = QERR_TIMEOUT;

    for (attempt = 0; attempt <= QUERY_RETRIES; attempt++) {
        rc = query_once(server_ip, name, qtype, out);
        if (rc == QOK || rc == QERR_PARSE || rc == QERR_INPUT ||
            rc == QERR_SOCKET)
            return rc;
        /* QERR_TIMEOUT: retry with a fresh id. */
    }
    return rc;
}
