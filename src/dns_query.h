/* dns_query.h - UDP transport for DNS queries. */
#ifndef DNS_QUERY_H
#define DNS_QUERY_H

#include "dns_message.h"

/* Query result codes. */
#define QOK          0
#define QERR_SOCKET  -1   /* socket/setup failure */
#define QERR_TIMEOUT -2   /* no valid response before deadline (retried) */
#define QERR_PARSE   -3   /* got bytes but they did not parse */
#define QERR_INPUT   -4   /* bad server IP / name */

/* Per-query timeout (seconds) and retry count. */
#define QUERY_TIMEOUT_SEC 3
#define QUERY_RETRIES     1   /* total attempts = 1 + QUERY_RETRIES */

/* If non-NULL, verbose logging is written here. */
extern int g_verbose;

/* Send `name`/`qtype` to `server_ip` (dotted IPv4) over UDP. On success fills
 * *out and returns QOK. Discards responses whose transaction ID does not match
 * (keeps waiting within the deadline). Returns a QERR_* code otherwise.
 * Always returns in finite time (never blocks indefinitely). */
int query_server(const char *server_ip, const char *name, uint16_t qtype,
                 dns_message *out);

#endif /* DNS_QUERY_H */
