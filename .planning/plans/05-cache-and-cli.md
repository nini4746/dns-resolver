# Plan 05: TTL 캐시와 CLI 완성

## 목표

TTL 기반 인메모리 캐시(R6)를 붙이고 CLI를 완성해 dig 비교로 최종 완료 조건(R7)을 달성한다.

## 컨텍스트

- 명세: .planning/SPEC.md 의 R6, R7과 부정 수용 기준
- 선행: Plan 03~04의 resolve() 완성본. main.c는 최소 CLI 상태.
- 캐시가 있으면 다중 도메인 해석 시 루트/TLD 재질의가 사라진다. 그게 검증 포인트.

## 작업

1. `src/cache.c` - (이름, 타입) 키 인메모리 캐시:
   - 저장: 레코드 + 삽입 시각 + TTL → 만료 시각. 자료구조는 단순하게 (고정 크기 배열 + 선형 탐색이면 충분, 해시는 선택).
   - 조회: 만료된 엔트리는 없는 것으로 취급 (반환 금지, must-NOT). TTL 0은 저장 안 함 또는 즉시 만료.
   - resolve() 통합: 질의 전 캐시 조회, 응답의 answer/authority/additional 레코드 캐시 저장. 위임용 NS와 글루 A가 캐시되면 두 번째 도메인 해석 때 루트/TLD를 건너뛴다.
   - 단위 테스트: 시각을 주입 가능하게 (now를 파라미터나 함수 포인터로) 만들면 만료 테스트가 쉽다.
2. `src/main.c` - CLI 완성:
   - `./resolve [-t A|AAAA|NS] [-v] <domain> [<domain>...]`. 기본 A.
   - `-v`: 캐시 히트/미스와 모든 네트워크 질의를 로그.
   - 경로 출력 형식 확정: 단계마다 한 줄, 서버 IP + 존 + 결과. 마지막에 최종 레코드 목록.
   - 인자 없음 / 잘못된 -t → usage 출력, 비0 종료.
3. `tests/compare_dig.sh` - dig 비교 스크립트:
   - 고정 도메인 목록 (example.com, www.iana.org 등 안정 도메인 5개 이상).
   - `./resolve <d>`와 `dig +short <d>`의 IP를 각각 정렬해 집합 비교 (sort + diff). A, AAAA, NS 각각.
   - 전부 일치하면 PASS 출력, 하나라도 다르면 FAIL과 diff 출력.
4. 부정 기준 최종 점검: `grep -rn "getaddrinfo\|gethostbyname\|res_query" src/` 0건 확인.

## must-haves

- 만료 캐시 반환 금지 (must-NOT)
- 캐시 히트가 verbose 로그로 관찰 가능할 것 (검증 불가능한 캐시는 없는 것과 같다)
- dig 비교가 스크립트로 재실행 가능할 것

## 검증

- [ ] `./resolve -v a.example.com b.example.com`에서 두 번째 해석의 루트/TLD 질의 생략이 로그로 확인된다 (R6)
- [ ] 같은 도메인 2회 시 두 번째는 네트워크 질의 0회 (R6)
- [ ] TTL 0 레코드 미캐시 / 만료 엔트리 미반환 단위 테스트 통과 (R6, must-NOT)
- [ ] `tests/compare_dig.sh` PASS: 고정 도메인 목록에서 A 결과가 dig +short와 집합 일치 (R7)
- [ ] `-t AAAA`, `-t NS`도 dig와 집합 일치 (R7)
- [ ] 경로 출력에 루트, TLD, 권위 서버 단계가 각 최소 한 줄 (R7)
- [ ] 인자 없음 / 잘못된 -t 에서 usage와 비0 종료 (R7)
- [ ] grep으로 시스템 리졸버 호출 0건 (must-NOT)

테스트 명령: `make test && bash tests/compare_dig.sh`
