# RIO / RUDP 코어 포트폴리오 구성 검토

2026-10-15 기준 코드 검토. 기존 13개 테스트 중 게임 데모 전용 2개를 제거하고 독립적인 unreliable 송신 검증 1개를 추가했다. 현재 12개 테스트를 사용한다. 테스트 결과는 동시성 안전성이나 실제 인터넷 환경의 성능을 입증하지 않는다.

## 중심 주제: UDP에서 손실을 재현하고 신뢰성 계층을 개선한 과정

포트폴리오에서는 게임 데모보다 네트워크 코어의 실패 조건, 처리 방식, 검증 근거를 먼저 보여준다. Godot 게임은 이후 코어를 사용하는 시각적 사례로 연결한다.

| 주제 | 코드와 검증 근거 | 설명할 내용 |
| --- | --- | --- |
| ACK 유실과 중복 처리 | RUDP 수신 처리, `ack_loss_tests`, `rio_ack_loss_smoke` | 데이터는 도착했지만 ACK가 유실되는 상황을 재현하고, 중복 데이터는 다시 전달하지 않으면서 ACK를 재전송하는 흐름 |
| 적응형 재전송 타이머 | `update_rto`, `set_send_complete`, `get_resend_packets`, `rudp_tests`, `metrics_tests` | SRTT/RTTVAR 계산, 재전송 후 RTT 표본 제외, timeout backoff를 입력값과 결과로 설명 |
| 송신 상태와 시퀀스 경계 | 송신 버퍼 및 시퀀스 비교, `send_window_tests`, `rudp_tests` | 미확인 패킷의 슬롯 유지, 포화 시 거부, 63→0 wrap 및 half-range 비교의 전제 |
| 엔드포인트별 연결 수명 | 서버의 endpoint→id 매핑, `rio_multi_client_smoke`, `rio_capacity_smoke`, `rio_timeout_reuse_smoke` | 수용량 초과 거부, timeout 시 매핑과 상태 제거, id 재사용 시 이전 연결과의 분리 |
| Windows RIO 전송 경로 | 등록 버퍼, request/completion queue, 수신 재등록, `rio_smoke` 등 | 버퍼 등록부터 완료 처리까지의 수명과 RUDP 계층으로 넘기는 경계. 성능 이점은 별도 측정 전까지 주장하지 않음 |
| 재현 가능한 손실 검증 | `benchmarks/include/network_simulator.hpp`, `network_loss_tests`, `benchmark_tests`, 손실 CSV | 데이터/ACK 손실 조건, 전달 수, 재전송 수, RTO를 비교. 인터넷 처리량 벤치마크와는 범위가 다름 |

## 현재 상태에서 과장하면 안 되는 부분

- 다중 생산자/단일 소비자 버퍼는 공유 상태에 `volatile bool`을 사용한다. C++의 원자 연산 및 스레드 간 happens-before를 보장하는 구현으로 정리하기 전에는 lock-free/thread-safe를 입증했다고 쓰지 않는다.
- 시퀀스 공간은 64이고 순서 비교는 half-range 32를 사용하지만 송신 포화 테스트는 64개 슬롯을 허용한다. 큰 outstanding window, 지연된 이전 ACK, 시퀀스 재사용을 함께 검증해야 일반적인 신뢰성 보장을 주장할 수 있다.
- 패킷 ID는 `__COUNTER__`와 선언 순서에 의존하고 payload는 C++ 구조체 메모리를 사용한다. Godot와 통신하기 전 ID, 필드 크기, byte order, 직렬화 규칙을 고정해야 한다.
- RIO 전역 상태와 종료/자원 정리의 재시작 가능성, 완료 오류 처리, 다중 스레드 공유 상태는 추가 검토 대상이다. 현재 loopback smoke 통과만으로 운영 수준의 안정성을 주장하지 않는다.
- RUDP 런타임 메트릭은 현재 클라이언트 handler에 있다. 서버 전체나 클라이언트별 메트릭을 이미 제공한다고 쓰지 않는다.

## 권장 서술 순서

1. UDP에서 데이터 또는 ACK가 사라지는 구체적인 실패 시나리오.
2. RIO 전송 계층 → 엔드포인트별 RUDP 상태 → 게임 패킷 처리의 구조.
3. 실패 재현 테스트와 수정 전후 동작.
4. 손실 조건별 전달·재전송·RTO 결과와 측정 범위.
5. 현재 한계와 Godot 연동에서 보완할 프로토콜 계약.

예시 제목: **Windows RIO 기반 UDP 서버와 손실 재현으로 검증한 RUDP 신뢰성 계층**.

## Godot 연동 전 정리 순서

1. 코어 이름과 공개 헤더 디렉터리 정리 완료: `ycnet`, `include/ycnet`.
2. 기존 게임 데모와 전용 테스트·렌더 도구·문서·빌드 타깃 제거 완료.
3. 엔진과 독립적인 wire protocol 명세를 확정한다.
4. Godot 클라이언트의 UDP 입출력 또는 네이티브 연결 경로를 결정한다.
5. 실제 게임 입력과 상태 동기화를 연결하고 손실·중복·재접속 시나리오를 검증한다.

Godot 기본 멀티플레이 기능과 이 커스텀 RUDP가 자동으로 호환된다고 가정하지 않는다. 연결 방식을 선택할 때 엔진의 공식 문서와 실제 패킷 경로를 확인한다.
