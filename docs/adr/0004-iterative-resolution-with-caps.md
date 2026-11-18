# 0004. 상한으로 제한한 반복 해석과 CNAME 체이싱

- 상태: 채택
- 날짜: 2026-07-13

## 맥락
루트 -> TLD -> 권위 서버로 referral을 따라가야 한다. glue 없는 위임은 NS 이름을 따로 해석해야 하고, 이 재귀와 CNAME 체인은 루프가 생길 수 있다.

## 결정
- referral의 authority NS와 additional glue A로 다음 서버 목록을 만든다 (`iterative_lookup`, `src/resolve.c`).
- glue가 하나도 없을 때만 NS 이름을 별도로 해석한다. 재귀 깊이 상한 `MAX_NS_DEPTH` 8, 위임 홉 상한 `MAX_DELEGATIONS` 32.
- CNAME은 타깃으로 해석을 다시 시작한다. 같은 응답에 타깃 레코드가 있으면 바로 쓴다. 체인 상한 `MAX_CNAME` 8로만 루프를 막는다. NS 질의는 CNAME을 따라가지 않는다.
- 결과는 `RES_NXDOMAIN`, `RES_NODATA`, `RES_TIMEOUT`, `RES_CNAME_LOOP`, `RES_MAXDEPTH` 등으로 구분한다.

## 대안
- 항상 모든 NS 이름 해석: 불필요한 하위 해석이 늘어난다.
- 방문 이름 목록으로 CNAME 루프 감지: 상한만으로 유한 종료가 보장된다.

## 결과
- 장점: 어떤 위임 구조에서도 유한 시간 안에 끝난다. 실패 원인이 종료 메시지로 구분된다.
- 단점/감수한 점: 루프와 단순히 긴 체인을 구분하지 않고 둘 다 "CNAME chain too long"으로 끝난다.
