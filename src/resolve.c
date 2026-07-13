/* resolve.c - root-first iterative resolution, CNAME chasing, TTL cache use.
 *
 * Algorithm (RFC 1034 §5.3.3, iterative variant):
 *   start with a server list (best cached delegation, else root hints)
 *   -> query for name/qtype
 *   -> answer present        : done
 *   -> authority has NS       : referral; next servers = glue A, or resolve
 *                               glue-less NS names (bounded recursion)
 *   -> repeat until authoritative answer or all servers fail.
 * CNAME answers restart resolution at the target name (bounded chain).
 */
#include "resolve.h"
#include "dns_query.h"
#include "cache.h"
#include "root_hints.h"

#include <arpa/inet.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define MAX_SERVERS      32
#define MAX_DELEGATIONS  32   /* outer referral hops guard */
#define MAX_NS_DEPTH     8    /* glue-less NS resolution recursion */
#define MAX_CNAME        8    /* CNAME chain cap */

typedef struct {
    char ip[46];
    char zone[DNS_MAX_NAME];  /* zone this server is authoritative for (log) */
    char nsname[DNS_MAX_NAME];
} nsaddr;

const char *res_status_str(int status)
{
    switch (status) {
    case RES_OK:         return "OK";
    case RES_NXDOMAIN:   return "NXDOMAIN";
    case RES_NODATA:     return "NODATA";
    case RES_TRUNCATED:  return "truncation error (TC=1)";
    case RES_TIMEOUT:    return "all nameservers failed / timeout";
    case RES_CNAME_LOOP: return "CNAME chain too long";
    case RES_MAXDEPTH:   return "max delegation/NS depth exceeded";
    default:             return "error";
    }
}

static void path_add(path_log *log, const char *fmt, ...)
{
    va_list ap;
    if (!log || log->n >= PATH_MAX_LINES)
        return;
    va_start(ap, fmt);
    vsnprintf(log->lines[log->n], PATH_LINE_LEN, fmt, ap);
    va_end(ap);
    log->n++;
}

/* Convert an A/AAAA rr to a printable address string. */
static void addr_str(const dns_rr *rr, char *out, size_t outlen)
{
    if (rr->type == TYPE_A)
        inet_ntop(AF_INET, rr->addr, out, (socklen_t)outlen);
    else if (rr->type == TYPE_AAAA)
        inet_ntop(AF_INET6, rr->addr, out, (socklen_t)outlen);
    else if (outlen)
        out[0] = '\0';
}

/* Cache every A/AAAA/NS/CNAME record in a response. */
static void cache_message(const dns_message *msg)
{
    size_t i;
    for (i = 0; i < msg->n_answers; i++)     cache_put(&msg->answers[i]);
    for (i = 0; i < msg->n_authorities; i++) cache_put(&msg->authorities[i]);
    for (i = 0; i < msg->n_additionals; i++) cache_put(&msg->additionals[i]);
}

/* Populate server list from root hints. */
static size_t servers_from_root(nsaddr *servers, size_t max)
{
    size_t i, n = 0;
    for (i = 0; i < ROOT_HINTS_COUNT && n < max; i++) {
        snprintf(servers[n].ip, sizeof(servers[n].ip), "%s", ROOT_HINTS[i].ipv4);
        snprintf(servers[n].zone, sizeof(servers[n].zone), "%s", ".");
        snprintf(servers[n].nsname, sizeof(servers[n].nsname), "%s",
                 ROOT_HINTS[i].name);
        n++;
    }
    return n;
}

/* Walk up the labels of `name` looking for the closest enclosing zone that has
 * cached NS records with cached glue A. Fills servers; returns count (0 = none,
 * caller should start from root). Sets *zone_out to the matched zone. */
static size_t servers_from_cache(const char *name, nsaddr *servers, size_t max,
                                 char *zone_out, size_t zone_out_len)
{
    const char *zone = name;
    for (;;) {
        dns_rr ns[DNS_MAX_RECORDS];
        size_t nns = cache_get(zone, TYPE_NS, ns, DNS_MAX_RECORDS);
        if (nns > 0) {
            size_t i, n = 0;
            for (i = 0; i < nns && n < max; i++) {
                dns_rr glue[DNS_MAX_RECORDS];
                size_t g = cache_get(ns[i].rdata_name, TYPE_A, glue,
                                     DNS_MAX_RECORDS);
                size_t j;
                for (j = 0; j < g && n < max; j++) {
                    addr_str(&glue[j], servers[n].ip, sizeof(servers[n].ip));
                    snprintf(servers[n].zone, sizeof(servers[n].zone), "%s",
                             zone[0] ? zone : ".");
                    snprintf(servers[n].nsname, sizeof(servers[n].nsname),
                             "%s", ns[i].rdata_name);
                    n++;
                }
            }
            if (n > 0) {
                snprintf(zone_out, zone_out_len, "%s", zone[0] ? zone : ".");
                return n;
            }
        }
        /* Move to parent zone. */
        {
            const char *dot = strchr(zone, '.');
            if (!dot)
                break;         /* was a TLD; parent is root, handled by hints */
            zone = dot + 1;
            if (*zone == '\0')
                break;
        }
    }
    return 0;
}

/* Forward decl. */
static int iterative_lookup(const char *name, uint16_t qtype, int ns_depth,
                            dns_message *out_msg, path_log *log);

/* Resolve an NS name to a single IPv4 string via a bounded sub-resolution. */
static int resolve_ns_ip(const char *nsname, int ns_depth, char *ip_out,
                         size_t ip_out_len, path_log *log)
{
    dns_rr cached[DNS_MAX_RECORDS];
    size_t nc;
    dns_message msg;
    size_t i;

    /* Cache first. */
    nc = cache_get(nsname, TYPE_A, cached, DNS_MAX_RECORDS);
    if (nc > 0) {
        addr_str(&cached[0], ip_out, ip_out_len);
        return 0;
    }
    if (ns_depth + 1 > MAX_NS_DEPTH)
        return -1;

    path_add(log, "  (resolving glue-less NS %s ...)", nsname);
    if (iterative_lookup(nsname, TYPE_A, ns_depth + 1, &msg, log) != RES_OK)
        return -1;
    for (i = 0; i < msg.n_answers; i++) {
        if (msg.answers[i].type == TYPE_A) {
            addr_str(&msg.answers[i], ip_out, ip_out_len);
            return 0;
        }
    }
    return -1;
}

/* Does the answer section contain any record for `name` of type qtype or a
 * CNAME for `name`? (Used to decide "answer reached".) */
static int answer_has_useful(const dns_message *msg, const char *name,
                             uint16_t qtype)
{
    size_t i;
    for (i = 0; i < msg->n_answers; i++) {
        if (strcasecmp(msg->answers[i].name, name) == 0 &&
            (msg->answers[i].type == qtype ||
             msg->answers[i].type == TYPE_CNAME))
            return 1;
    }
    /* Some servers return the whole set; accept any qtype match too. */
    for (i = 0; i < msg->n_answers; i++)
        if (msg->answers[i].type == qtype || msg->answers[i].type == TYPE_CNAME)
            return 1;
    return 0;
}

/* Core iterative loop. On RES_OK, out_msg holds the authoritative response
 * (which may contain answers, or be an empty NODATA response). */
static int iterative_lookup(const char *name, uint16_t qtype, int ns_depth,
                            dns_message *out_msg, path_log *log)
{
    nsaddr servers[MAX_SERVERS];
    size_t nservers;
    char start_zone[DNS_MAX_NAME];
    int hop;

    if (ns_depth > MAX_NS_DEPTH)
        return RES_MAXDEPTH;

    /* Choose starting servers: best cached delegation, else root. */
    nservers = servers_from_cache(name, servers, MAX_SERVERS,
                                  start_zone, sizeof(start_zone));
    if (nservers > 0) {
        path_add(log, "@cache start at zone (%s) [%zu servers]",
                 start_zone, nservers);
    } else {
        nservers = servers_from_root(servers, MAX_SERVERS);
    }

    for (hop = 0; hop < MAX_DELEGATIONS; hop++) {
        nsaddr next[MAX_SERVERS];
        size_t nnext = 0;
        char referral_zone[DNS_MAX_NAME] = "";
        size_t si;
        int got_response = 0;
        dns_message msg;

        /* Try each server in the current list; fall back on failure. */
        for (si = 0; si < nservers; si++) {
            int rc = query_server(servers[si].ip, name, qtype, &msg);
            if (rc != QOK) {
                path_add(log, "@%s (%s) %s %u -> no response (%s)",
                         servers[si].ip, servers[si].zone, name,
                         (unsigned)qtype,
                         rc == QERR_TIMEOUT ? "timeout" : "error");
                continue;  /* fall back to next server */
            }
            got_response = 1;
            cache_message(&msg);

            /* TC=1 is only fatal for a *final answer* (must-NOT: never output a
             * truncated partial answer). A truncated *referral* is still usable
             * because servers reduce record counts to fit 512B, leaving every
             * included record complete - so we let referral handling proceed. */
            if (msg.header.rcode == RCODE_NXDOMAIN) {
                path_add(log, "@%s (%s) %s %u -> NXDOMAIN",
                         servers[si].ip, servers[si].zone, name, (unsigned)qtype);
                *out_msg = msg;
                return RES_NXDOMAIN;
            }
            if (msg.header.rcode != RCODE_NOERROR) {
                path_add(log, "@%s (%s) %s %u -> rcode %d, next server",
                         servers[si].ip, servers[si].zone, name,
                         (unsigned)qtype, msg.header.rcode);
                got_response = 0;
                continue;  /* SERVFAIL etc: try another server */
            }

            /* Answer reached? */
            if (msg.n_answers > 0 && answer_has_useful(&msg, name, qtype)) {
                if (msg.header.tc) {
                    /* Truncated final answer: never use partial answers. */
                    path_add(log, "@%s (%s) %s %u -> TC=1 on answer (truncated)",
                             servers[si].ip, servers[si].zone, name,
                             (unsigned)qtype);
                    return RES_TRUNCATED;
                }
                path_add(log, "@%s (%s) %s %u -> answer (%zu records)",
                         servers[si].ip, servers[si].zone, name,
                         (unsigned)qtype, msg.n_answers);
                *out_msg = msg;
                return RES_OK;
            }

            /* No answer: look for a referral (NS in authority). */
            {
                size_t i;
                size_t ns_count = 0;
                for (i = 0; i < msg.n_authorities; i++) {
                    if (msg.authorities[i].type != TYPE_NS)
                        continue;
                    if (referral_zone[0] == '\0')
                        snprintf(referral_zone, sizeof(referral_zone), "%s",
                                 msg.authorities[i].name);
                    ns_count++;
                    /* Try glue from additional section. */
                    {
                        size_t j;
                        int glued = 0;
                        for (j = 0; j < msg.n_additionals && nnext < MAX_SERVERS; j++) {
                            if (msg.additionals[j].type == TYPE_A &&
                                strcasecmp(msg.additionals[j].name,
                                           msg.authorities[i].rdata_name) == 0) {
                                addr_str(&msg.additionals[j], next[nnext].ip,
                                         sizeof(next[nnext].ip));
                                snprintf(next[nnext].zone,
                                         sizeof(next[nnext].zone), "%s",
                                         msg.authorities[i].name);
                                snprintf(next[nnext].nsname,
                                         sizeof(next[nnext].nsname), "%s",
                                         msg.authorities[i].rdata_name);
                                nnext++;
                                glued = 1;
                            }
                        }
                        (void)glued;
                    }
                }

                if (ns_count == 0) {
                    if (msg.header.tc) {
                        /* Truncated, nothing usable (no answer, no referral). */
                        path_add(log, "@%s (%s) %s %u -> TC=1 (truncated)",
                                 servers[si].ip, servers[si].zone, name,
                                 (unsigned)qtype);
                        return RES_TRUNCATED;
                    }
                    /* Authoritative NOERROR with no answer, no delegation. */
                    path_add(log, "@%s (%s) %s %u -> NODATA",
                             servers[si].ip, servers[si].zone, name,
                             (unsigned)qtype);
                    *out_msg = msg;
                    return RES_OK;  /* caller sees 0 answers -> NODATA */
                }

                /* If we got no glue at all, resolve NS names ourselves. */
                if (nnext == 0) {
                    for (i = 0; i < msg.n_authorities && nnext < MAX_SERVERS; i++) {
                        char ip[46];
                        if (msg.authorities[i].type != TYPE_NS)
                            continue;
                        if (resolve_ns_ip(msg.authorities[i].rdata_name,
                                          ns_depth, ip, sizeof(ip), log) == 0) {
                            snprintf(next[nnext].ip, sizeof(next[nnext].ip),
                                     "%s", ip);
                            snprintf(next[nnext].zone,
                                     sizeof(next[nnext].zone), "%s",
                                     msg.authorities[i].name);
                            snprintf(next[nnext].nsname,
                                     sizeof(next[nnext].nsname), "%s",
                                     msg.authorities[i].rdata_name);
                            nnext++;
                        }
                    }
                }

                path_add(log, "@%s (%s) %s %u -> referral to %s [%zu ns, %zu addrs]",
                         servers[si].ip, servers[si].zone, name, (unsigned)qtype,
                         referral_zone, ns_count, nnext);

                if (nnext == 0)
                    return RES_TIMEOUT;  /* delegation but no reachable NS */
                break;  /* descend with the new server list */
            }
        }

        if (!got_response && nnext == 0) {
            /* Every server in the list failed to respond. */
            return RES_TIMEOUT;
        }

        /* Descend one level. */
        memcpy(servers, next, sizeof(nsaddr) * nnext);
        nservers = nnext;
    }

    return RES_MAXDEPTH;  /* too many delegation hops */
}

/* Collect answer records for `name`/`qtype` from a message into result. */
static size_t collect_answers(const dns_message *msg, const char *name,
                              uint16_t qtype, resolve_result *result)
{
    size_t i, added = 0;
    for (i = 0; i < msg->n_answers; i++) {
        if (msg->answers[i].type == qtype &&
            strcasecmp(msg->answers[i].name, name) == 0 &&
            result->n_records < DNS_MAX_RECORDS) {
            result->records[result->n_records++] = msg->answers[i];
            added++;
        }
    }
    return added;
}

/* Find a CNAME in the answer for `name`; copy target to out. Returns 1 if found. */
static int find_cname(const dns_message *msg, const char *name,
                      char *target, size_t tlen)
{
    size_t i;
    for (i = 0; i < msg->n_answers; i++) {
        if (msg->answers[i].type == TYPE_CNAME &&
            strcasecmp(msg->answers[i].name, name) == 0) {
            snprintf(target, tlen, "%s", msg->answers[i].rdata_name);
            return 1;
        }
    }
    return 0;
}

int resolve(const char *name, uint16_t qtype,
            resolve_result *result, path_log *log)
{
    char current[DNS_MAX_NAME];
    int cname_hops = 0;

    memset(result, 0, sizeof(*result));
    snprintf(current, sizeof(current), "%s", name);
    snprintf(result->final_name, sizeof(result->final_name), "%s", name);

    for (;;) {
        dns_message msg;
        int rc;

        /* Answer cache: exact (name, qtype) hit -> zero network queries. */
        {
            dns_rr cached[DNS_MAX_RECORDS];
            size_t nc = cache_get(current, qtype, cached, DNS_MAX_RECORDS);
            if (nc > 0) {
                size_t i;
                path_add(log, "cache hit: %s type %u (%zu records)",
                         current, (unsigned)qtype, nc);
                for (i = 0; i < nc && result->n_records < DNS_MAX_RECORDS; i++)
                    result->records[result->n_records++] = cached[i];
                snprintf(result->final_name, sizeof(result->final_name),
                         "%s", current);
                return RES_OK;
            }
            /* For non-NS types, a cached CNAME lets us chase without network. */
            if (qtype != TYPE_NS) {
                dns_rr cn[DNS_MAX_RECORDS];
                size_t ncn = cache_get(current, TYPE_CNAME, cn, DNS_MAX_RECORDS);
                if (ncn > 0) {
                    if (++cname_hops > MAX_CNAME)
                        return RES_CNAME_LOOP;
                    path_add(log, "cache hit: %s CNAME -> %s",
                             current, cn[0].rdata_name);
                    snprintf(current, sizeof(current), "%s", cn[0].rdata_name);
                    snprintf(result->final_name, sizeof(result->final_name),
                             "%s", current);
                    continue;
                }
            }
        }

        rc = iterative_lookup(current, qtype, 0, &msg, log);
        if (rc != RES_OK)
            return rc;

        /* Collect direct answers of the requested type. */
        if (collect_answers(&msg, current, qtype, result) > 0) {
            snprintf(result->final_name, sizeof(result->final_name),
                     "%s", current);
            return RES_OK;
        }

        /* NS queries do not chase CNAMEs. */
        if (qtype != TYPE_NS) {
            char target[DNS_MAX_NAME];
            if (find_cname(&msg, current, target, sizeof(target))) {
                if (++cname_hops > MAX_CNAME)
                    return RES_CNAME_LOOP;
                path_add(log, "%s CNAME -> %s", current, target);
                /* The same response may already carry the target's records. */
                if (collect_answers(&msg, target, qtype, result) > 0) {
                    snprintf(result->final_name, sizeof(result->final_name),
                             "%s", target);
                    return RES_OK;
                }
                snprintf(current, sizeof(current), "%s", target);
                snprintf(result->final_name, sizeof(result->final_name),
                         "%s", target);
                continue;  /* fresh resolution for the target */
            }
        }

        /* NOERROR, no matching answer, no CNAME -> NODATA. */
        if (msg.header.rcode == RCODE_NXDOMAIN)
            return RES_NXDOMAIN;
        return RES_NODATA;
    }
}
