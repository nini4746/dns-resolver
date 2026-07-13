# Plan 04: CNAME 체이싱

## 목표

답이 CNAME일 때 타깃으로 해석을 이어가 최종 A/AAAA에 도달한다 (R5). 루프와 긴 체인은 에러 처리.

## 컨텍스트

- 명세: .planning/SPEC.md 의 R5
- 선행: Plan 03의 resolve() 반복 해석 루프. 그 위에 얹는 층이다.
- CNAME이 오는 두 가지 형태를 구분해야 한다:
  - answer에 CNAME + 타깃의 A가 함께 옴 (권위 서버가 친절한 경우): 추가 질의 없이 그 A를 쓴다.
  - answer에 CNAME만 옴: 타깃 이름으로 새 반복 해석을 시작한다 (루트부터. 캐시가 붙는 Plan 05 이후엔 캐시 덕에 싸진다).
- 관찰: `dig www.github.com` 해보면 CNAME 체인이 보인다.

## 작업

1. `src/resolve.c` 수정 - resolve() 상위에 체이싱 래퍼 또는 루프 내 처리:
   - answer에서 질의 이름과 일치하는 레코드를 찾을 때 CNAME이면 현재 질의 이름을 타깃으로 교체.
   - 같은 answer 안에서 타깃의 A/AAAA를 먼저 스캔 (재질의 절약).
   - 체인 카운터: CNAME을 따라갈 때마다 +1, 8 초과 시 "CNAME chain too long" 에러.
   - 방문 이름 목록으로 루프(a→b→a) 감지도 가능하지만, 깊이 상한만으로도 종료는 보장된다. 어느 쪽이든 유한 종료가 must.
   - 경로 출력에 CNAME 단계 기록 (예: `www.example.org CNAME -> example.org`).
2. 루프/긴 체인 테스트: 실제 도메인으로 루프를 만들기 어려우므로 로컬 가짜 DNS 서버(파이썬 스크립트, a.test CNAME b.test / b.test CNAME a.test 응답)를 tests/에 두고 확인. 또는 파서 수준 단위 테스트로 대체 가능하나 e2e 확인이 더 확실하다.

## must-haves

- 어떤 CNAME 구성에서도 유한 종료
- 최종 출력은 A/AAAA (사용자가 -t NS를 준 경우 제외, NS 질의에는 체이싱 미적용)
- 경로에 CNAME 추적이 드러날 것

## 검증

- [ ] CNAME 가진 실제 도메인(예: www.github.com)이 최종 IP까지 해석되고 경로에 CNAME 단계 표시 (R5)
- [ ] 체인 깊이 9 이상 또는 루프 시뮬레이션에서 "CNAME chain too long" 에러 비0 종료 (R5)

테스트 명령: `./resolve www.github.com` + 로컬 가짜 서버 루프 테스트 스크립트
