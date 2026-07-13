#!/usr/bin/env bash
# compare_dig.sh - compare ./resolve output against `dig +short` (R7).
#
# Compares the IP/NS *set* (order-independent, TTL excluded) for a fixed list
# of stable domains, for types A, AAAA, and NS.
#
# Usage: bash tests/compare_dig.sh
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
RESOLVE="$HERE/resolve"

if [ ! -x "$RESOLVE" ]; then
    echo "build first: make" >&2
    exit 2
fi
if ! command -v dig >/dev/null 2>&1; then
    echo "SKIP: dig not available" >&2
    exit 0
fi

# Stable domains (IANA-operated / anycast: deterministic sets).
A_DOMAINS="example.com example.org example.net www.iana.org iana.org"
AAAA_DOMAINS="example.com example.org www.iana.org"
NS_DOMAINS="example.com iana.org google.com"

pass=0
fail=0

# Keep only IPv4/IPv6 literals, strip trailing dots, lowercase, sort unique.
norm_ip() { grep -E '^[0-9a-fA-F:.]+$' | grep -E '[.:]' | tr 'A-Z' 'a-z' | sort -u; }
# For NS: strip trailing dot, lowercase, sort unique.
norm_ns() { sed 's/\.$//' | tr 'A-Z' 'a-z' | grep -E '.' | sort -u; }

compare() {
    local dom="$1" typ="$2" flag="$3" normfn="$4"
    local mine digout
    mine="$("$RESOLVE" $flag "$dom" 2>/dev/null | $normfn)"
    digout="$(dig +short "$dom" "$typ" 2>/dev/null | $normfn)"
    if [ "$mine" = "$digout" ] && [ -n "$digout" ]; then
        echo "PASS  $typ $dom"
        pass=$((pass+1))
    else
        echo "FAIL  $typ $dom"
        echo "  --- resolve ---"; echo "$mine" | sed 's/^/    /'
        echo "  --- dig     ---"; echo "$digout" | sed 's/^/    /'
        fail=$((fail+1))
    fi
}

echo "== A =="
for d in $A_DOMAINS; do compare "$d" A "" norm_ip; done
echo "== AAAA =="
for d in $AAAA_DOMAINS; do compare "$d" AAAA "-t AAAA" norm_ip; done
echo "== NS =="
for d in $NS_DOMAINS; do compare "$d" NS "-t NS" norm_ns; done

echo
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] && echo "ALL PASS" && exit 0
echo "SOME FAILED" && exit 1
