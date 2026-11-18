# 설계 결정 기록 (ADR)

| 번호 | 결정 | 상태 | 날짜 |
|------|------|------|------|
| [0001](0001-no-system-resolver-hardcoded-root-hints.md) | 시스템 리졸버 없이 루트 힌트부터 직접 질의 | 채택 | 2026-07-13 |
| [0002](0002-bounded-packet-parsing.md) | 경계 검사와 점프 상한으로 패킷 파싱 종료 보장 | 채택 | 2026-07-13 |
| [0003](0003-ipv4-udp-select-deadline.md) | IPv4 UDP 전송과 select() 데드라인 기반 타임아웃 | 채택 | 2026-07-13 |
| [0004](0004-iterative-resolution-with-caps.md) | 상한으로 제한한 반복 해석과 CNAME 체이싱 | 채택 | 2026-07-13 |
| [0005](0005-truncation-final-answer-only.md) | TC=1은 최종 답에서만 에러로 처리 | 채택 | 2026-07-13 |
| [0006](0006-fixed-slot-ttl-cache.md) | 고정 슬롯 TTL 캐시와 가장 가까운 존에서 해석 시작 | 채택 | 2026-07-13 |
| [0007](0007-stdout-answer-stderr-path.md) | 답은 stdout, 위임 경로는 stderr로 분리 | 채택 | 2026-07-13 |
