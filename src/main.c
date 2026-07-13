/* main.c - ./resolve CLI.
 *
 *   ./resolve [-t A|AAAA|NS] [-v] <domain> [<domain>...]
 *
 * Final records (dig +short style) go to stdout; the delegation path and
 * verbose logs go to stderr. Exit 0 only if every domain resolved to records.
 */
#include "resolve.h"
#include "dns_query.h"
#include "dns_message.h"

#include <arpa/inet.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [-t A|AAAA|NS] [-v] <domain> [<domain>...]\n"
        "  -t TYPE  record type: A (default), AAAA, or NS\n"
        "  -v       verbose: log cache hits and every network query\n",
        prog);
}

static int parse_type(const char *s, uint16_t *out)
{
    if (strcasecmp(s, "A") == 0)    { *out = TYPE_A;    return 0; }
    if (strcasecmp(s, "AAAA") == 0) { *out = TYPE_AAAA; return 0; }
    if (strcasecmp(s, "NS") == 0)   { *out = TYPE_NS;   return 0; }
    return -1;
}

static const char *type_name(uint16_t t)
{
    switch (t) {
    case TYPE_A:    return "A";
    case TYPE_AAAA: return "AAAA";
    case TYPE_NS:   return "NS";
    case TYPE_CNAME:return "CNAME";
    default:        return "?";
    }
}

static void print_record(const dns_rr *rr)
{
    if (rr->type == TYPE_A) {
        char buf[46];
        inet_ntop(AF_INET, rr->addr, buf, sizeof(buf));
        printf("%s\n", buf);
    } else if (rr->type == TYPE_AAAA) {
        char buf[46];
        inet_ntop(AF_INET6, rr->addr, buf, sizeof(buf));
        printf("%s\n", buf);
    } else if (rr->type == TYPE_NS || rr->type == TYPE_CNAME) {
        printf("%s\n", rr->rdata_name);
    }
}

int main(int argc, char **argv)
{
    uint16_t qtype = TYPE_A;
    int i;
    int first_domain = -1;
    int exit_code = 0;
    int any_domain = 0;

    /* Manual arg parse (options may precede domains). */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc || parse_type(argv[i + 1], &qtype) != 0) {
                usage(argv[0]);
                return 2;
            }
            i++;
        } else if (strcmp(argv[i], "-v") == 0) {
            g_verbose = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            usage(argv[0]);
            return 2;
        } else {
            first_domain = i;
            break;
        }
    }

    if (first_domain < 0) {
        usage(argv[0]);
        return 2;
    }

    for (i = first_domain; i < argc; i++) {
        const char *domain = argv[i];
        resolve_result result;
        path_log log;
        int rc;

        if (domain[0] == '-') {   /* stray option after domains */
            usage(argv[0]);
            return 2;
        }
        any_domain = 1;
        log.n = 0;

        fprintf(stderr, "; resolving %s type %s\n", domain, type_name(qtype));
        rc = resolve(domain, qtype, &result, &log);

        /* Path output (stderr): the root-to-authoritative delegation. */
        {
            size_t k;
            for (k = 0; k < log.n; k++)
                fprintf(stderr, "  %s\n", log.lines[k]);
        }

        if (rc == RES_OK && result.n_records > 0) {
            size_t k;
            for (k = 0; k < result.n_records; k++)
                print_record(&result.records[k]);
        } else {
            const char *why = (rc == RES_OK) ? "NODATA" : res_status_str(rc);
            fprintf(stderr, "; %s: %s\n", domain, why);
            if (exit_code == 0)
                exit_code = 1;
        }
        fflush(stdout);
    }

    if (!any_domain) {
        usage(argv[0]);
        return 2;
    }
    return exit_code;
}
