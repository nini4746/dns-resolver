# Plan 03: 루트부터 반복 해석

## 목표

루트 힌트에서 시작해 위임을 따라가며 A/AAAA/NS를 해석하는 핵심 루프를 만든다 (R4). 이 단계가 끝나면 `./resolve example.com`이 처음으로 진짜 답을 낸다.

## 컨텍스트

- 명세: .planning/SPEC.md 의 R4
- 선행: Plan 01 (메시지), Plan 02 (query_server)
- CNAME 체이싱과 캐시는 아직 없음 (Plan 04, 05). 이 plan에서는 answer에 CNAME만 오면 일단 에러 또는 미지원 메시지로 두고 넘어가도 된다.
- 반복 해석 알고리즘 개념: 현재 서버 목록(처음엔 루트 힌트)에 질의 → answer에 원하는 타입이 있으면 끝 → answer가 비고 authority에 NS가 있으면 referral. additional의 글루 A로 다음 서버 IP를 얻어 반복 → 글루가 없으면 NS 이름 자체를 루트부터 다시 해석 (자기 자신을 재귀 호출, 깊이 상한 8).
- 관찰 도구: `dig +trace example.com`이 정확히 이 과정을 보여준다. 내 출력과 비교하며 개발하면 좋다.

## 작업

1. `src/root_hints.c` - 루트 서버 IPv4 주소 4개 이상 하드코딩 (a.root-servers.net=198.41.0.4 등, IANA root hints 파일 참조).
2. `src/resolve.c` - `resolve(name, qtype, depth, path_log)` 반복 해석 함수:
   - 서버 목록을 돌며 query_server 호출. 실패(타임아웃)하면 목록의 다음 서버로 폴백. 전부 실패면 에러.
   - RCODE 3 → NXDOMAIN 에러 코드. NOERROR + answer 0 + authority에 NS 없음 → NODATA.
   - TC=1 → truncation 에러 (부분 답 사용 금지).
   - referral 처리: authority의 NS 이름 목록 + additional의 글루 매칭. 글루 없으면 `resolve(ns_name, A, depth+1, ...)` 재호출, depth > 8이면 에러.
   - 각 단계를 path_log에 기록: 질의한 서버 IP, 대상 존, 결과(referral to X / answer / error).
3. `src/main.c` - 최소 CLI: `./resolve <domain>` (A 고정), 경로와 최종 레코드 출력. 플래그 파싱은 Plan 05에서 완성.
4. 수동 테스트: example.com, www.iana.org, 존재하지 않는 도메인, 존재하지 않는 TLD.

## must-haves

- 무한 루프 불가능: 위임 추적과 글루 없는 NS 재귀 모두 깊이 상한
- TC=1 부분 답 미사용 (must-NOT)
- NXDOMAIN과 NODATA 구분
- 경로 기록이 실제 질의 순서와 일치

## 검증

- [ ] `./resolve example.com`이 루트 → com. → 권위 서버 위임을 거쳐 dig +short와 같은 A 레코드 출력 (R4)
- [ ] 글루 없는 위임 도메인이 해석되고 NS 별도 해석 경로가 로그에 보인다 (R4) - 후보 도메인은 `dig +trace`로 additional 섹션이 비는 위임을 직접 찾을 것
- [ ] 없는 도메인 → "NXDOMAIN" 출력, 비0 종료 (R4)
- [ ] AAAA 없는 도메인에 AAAA 질의 → "NODATA" 출력, 비0 종료 (R4)
- [ ] TC=1 응답 시 truncation 에러 비0 종료 (R4) - 큰 응답을 주는 질의(예: 일부 TLD NS 질의)로 유발하거나 로컬 가짜 서버로 확인
- [ ] `./resolve foo.invalidtld12345`가 유한 시간 내 에러로 끝난다 (R4)

테스트 명령: `make` 후 위 시나리오 수동 실행 + `dig +trace` 대조
