# DNS 리졸버

> 토이 4번. 언어: C. 리졸버 라이브러리 없이 POSIX 소켓/시간 API만 사용.

루트 네임서버부터 직접 내려가는 반복(iterative) DNS 리졸버. DNS 쿼리 패킷을 바이트 단위로 직접 조립해 UDP로 보내고, 응답을 직접 파싱한다 (`getaddrinfo`, `res_query` 등 금지). 최종 레코드는 stdout에 dig +short 스타일로, 루트부터 내려간 위임 경로는 stderr에 출력한다.

## CS 지식

네트워크 바이너리 프로토콜 파싱, 반복 질의(RFC 1034 §5.3.3), 압축 포인터(RFC 1035 §4.1.4), TTL 캐싱.

## 왜

- ft_irc/webserv는 텍스트 프로토콜 - 바이너리 프로토콜 파싱은 미개척
- LedgerTCP로 소켓은 익숙하니 프로토콜 자체에 집중 가능
- 압축 포인터 파싱이 별미 - 오프셋 점프하는 바이너리 포맷

## 빌드

```bash
make          # ./resolve 생성 (-Wall -Wextra, 경고 0)
```

## 사용

```bash
./resolve [-t A|AAAA|NS] [-v] <domain> [<domain>...]
```

- `-t TYPE` - 레코드 타입: A(기본), AAAA, NS
- `-v` - 캐시 히트와 모든 네트워크 쿼리를 stderr에 로그

실제 실행 예 (경로는 stderr, 답은 stdout):

```
$ ./resolve example.com
; resolving example.com type A
  @198.41.0.4 (.) example.com 1 -> referral to com [13 ns, 6 addrs]
  @192.41.162.30 (com) example.com 1 -> referral to example.com [2 ns, 6 addrs]
  @108.162.192.162 (example.com) example.com 1 -> answer (2 records)
104.20.23.154
172.66.147.243
```

CNAME 체이싱 예 (www.iana.org는 cloudflare.net CDN으로 CNAME):

```
$ ./resolve www.iana.org
; resolving www.iana.org type A
  @198.41.0.4 (.) www.iana.org 1 -> referral to org [6 ns, 6 addrs]
  @199.249.112.1 (org) www.iana.org 1 -> referral to iana.org [4 ns, 1 addrs]
  @199.4.138.53 (iana.org) www.iana.org 1 -> answer (1 records)
  www.iana.org CNAME -> www.iana.org.cdn.cloudflare.net
  @198.41.0.4 (.) www.iana.org.cdn.cloudflare.net 1 -> referral to net [13 ns, 6 addrs]
  @192.55.83.30 (net) www.iana.org.cdn.cloudflare.net 1 -> referral to cloudflare.net [5 ns, 9 addrs]
  @108.162.198.250 (cloudflare.net) www.iana.org.cdn.cloudflare.net 1 -> answer (2 records)
104.18.24.232
104.18.25.232
```

도메인 여러 개를 넘기면 한 프로세스 안에서 캐시가 이어진다. 모든 도메인이 레코드로 풀려야 exit 0.

## 테스트

```bash
make test                    # 오프라인 단위 테스트 3종, AddressSanitizer 빌드
bash tests/compare_dig.sh    # ./resolve 결과를 dig +short와 집합 비교 (네트워크/dig 필요)
```

- `test_message` (16 checks) - 쿼리 인코딩 + 응답 디코딩, 압축 포인터 경계/루프 케이스
- `test_cache` (7 checks) - TTL 만료, 대소문자 무시 매칭, TTL 0 미저장, 타입 불일치
- `test_cname` (4 checks) - 캐시만으로 CNAME 체인 종결 (네트워크 없이 resolve() 구동)

## 내부 설계

- **반복 질의** (`src/resolve.c`) - 루트 힌트(하드코딩된 루트 서버 IPv4 8개)에서 시작해 authority 섹션의 NS 위임을 따라 내려간다. glue A는 additional 섹션에서 줍고, glue 없는 NS는 제한된 깊이(8)의 서브 해석으로 직접 푼다. 위임 홉 32회 제한. 서버가 응답하지 않으면 목록의 다음 서버로 폴백
- **CNAME 체이싱** - 답이 CNAME이면 타깃 이름으로 해석을 재시작 (체인 8회 제한). 같은 응답에 타깃 레코드가 이미 실려 있으면 바로 사용. NS 질의는 CNAME을 따라가지 않음
- **압축 포인터 파싱** (`src/dns_decode.c`) - 모든 읽기가 경계 검사됨. 포인터는 엄격히 뒤쪽만 가리킬 수 있고 점프 횟수에 상한이 있어 파싱이 항상 종료됨
- **TTL 캐시** (`src/cache.c`) - 고정 8192 슬롯 선형 탐색. 만료 레코드는 절대 반환 안 함, TTL 0은 저장 안 함, 가득 차면 가장 먼저 만료될 항목 축출. 응답의 모든 A/AAAA/NS/CNAME을 캐싱하고, 다음 해석은 캐시된 가장 가까운 위임 존에서 시작 (루트 재방문 생략)
- **UDP 전송** (`src/dns_query.c`) - IPv4 UDP 포트 53, 512바이트 패킷. 트랜잭션 ID 불일치 응답은 버리고 데드라인까지 대기. 쿼리당 3초 타임아웃 + 1회 재시도. TC=1 truncation은 최종 답에서는 에러 처리, referral에서는 허용 (포함된 레코드는 완전하므로)

## 확장 (선택)

- EDNS0, TCP 폴백 (512바이트 초과 응답)
- 미니 Redis(토이 5번)의 캐시 백엔드로 연결 가능
