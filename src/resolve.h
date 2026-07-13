/* resolve.h - iterative resolver with CNAME chasing and caching. */
#ifndef RESOLVE_H
#define RESOLVE_H

#include "dns_message.h"

/* Resolver status codes. */
#define RES_OK          0
#define RES_NXDOMAIN    1
#define RES_NODATA      2
#define RES_TRUNCATED   3
#define RES_TIMEOUT     4   /* all nameservers for a zone failed */
#define RES_CNAME_LOOP  5   /* chain too long / loop */
#define RES_MAXDEPTH    6   /* glue-less NS recursion too deep */
#define RES_ERROR       7   /* other / malformed */

/* Path log: one human-readable line per resolution step. */
#define PATH_MAX_LINES 512
#define PATH_LINE_LEN  200
typedef struct {
    char   lines[PATH_MAX_LINES][PATH_LINE_LEN];
    size_t n;
} path_log;

typedef struct {
    dns_rr  records[DNS_MAX_RECORDS];
    size_t  n_records;
    char    final_name[DNS_MAX_NAME]; /* after any CNAME chasing */
} resolve_result;

/* Resolve `name` for `qtype` (A/AAAA/NS). Fills result and log.
 * Returns a RES_* code. */
int resolve(const char *name, uint16_t qtype,
            resolve_result *result, path_log *log);

const char *res_status_str(int status);

#endif /* RESOLVE_H */
