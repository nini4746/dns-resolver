# Plan 02: UDP 질의와 신뢰성

## 목표

지정한 네임서버 IP에 UDP로 질의를 보내고 검증된 응답을 받는 함수를 만든다 (R3). 타임아웃, 재시도, ID 검증까지.

## 컨텍스트

- 명세: .planning/SPEC.md 의 R3
- 선행: Plan 01이 만든 dns_encode/dns_decode (질의 조립, 응답 파싱)
- LedgerTCP에서 소켓 경험 있음. 여기서 새로운 것은 UDP(비연결)와 타임아웃 처리.
- 참조: recvfrom + SO_RCVTIMEO 또는 poll/select 타임아웃. select 계열이 재시도 루프 짜기에 더 유연하다.

## 작업

1. `src/dns_query.c` - `query_server(server_ip, name, qtype, out_response)` 함수:
   - UDP 소켓 생성, 질의 패킷 조립(Plan 01), sendto.
   - 512바이트 버퍼로 recvfrom. 타임아웃 3초.
   - 받은 패킷의 트랜잭션 ID가 보낸 ID와 다르면 폐기하고 남은 시간 동안 계속 대기 (전체 데드라인 유지 주의 - 폐기할 때마다 3초가 리셋되면 안 됨).
   - 타임아웃이면 1회 재시도 (새 ID로). 총 2회 실패 시 에러 코드 반환. 호출자가 다음 서버로 폴백할 수 있게 에러를 구분해서 반환.
   - 응답의 QR=1 확인, TC 플래그는 파싱 결과에 실어 상위로 전달 (처리는 Plan 03 몫).
2. `tests/test_query.c` 또는 임시 main - 통합 테스트:
   - 8.8.8.8에 example.com A 질의 → 파싱된 answer가 나오는지.
   - 192.0.2.1(TEST-NET, 무응답 보장)에 질의 → 약 6초 내 타임아웃 에러.
   - ID 불일치는 로컬에서 가짜 UDP 서버 스크립트(파이썬 몇 줄)로 틀린 ID 응답을 쏴서 확인하거나, 코드 리뷰 + 단위 수준 확인으로 대체 가능.

## must-haves

- ID 불일치 응답 폐기 (must-NOT 항목)
- 유한 시간 내 반드시 리턴 (블록 금지)
- 시스템 리졸버 함수 사용 금지 (서버 IP는 항상 숫자 IP 문자열 → inet_pton)

## 검증

- [ ] 8.8.8.8 단발 질의로 파싱된 응답 수신 (R3)
- [ ] ID 불일치 응답 폐기 후 계속 대기함 (R3)
- [ ] 무응답 IP에서 3초 x 2회 이내 타임아웃 에러, 프로세스 블록 없음 (R3)
- [ ] 소스에 getaddrinfo/gethostbyname/res_query 없음: `grep -rn "getaddrinfo\|gethostbyname\|res_query" src/` 결과 0건 (must-NOT)

테스트 명령: `make test` + 수동 통합 테스트 실행
