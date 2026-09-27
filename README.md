# YCNet RIO + RUDP

Windows Registered I/O(RIO) 기반 UDP 전송 계층과 신뢰성 보강 계층(RUDP)을 복구하고, 실패 재현 테스트를 먼저 추가하면서 개선한 포트폴리오 프로젝트입니다.

기존 공개 YCNet 저장소는 당시 구현을 남겨두기 위한 기록으로 유지하고, 이 저장소에서 검증 가능한 형태로 재구성했습니다.

## 구현 범위

- Windows RIO 기반 UDP 서버
- 엔드포인트별 RUDP 상태 관리
- 6-bit 시퀀스 / ACK / 재전송 처리
- ACK 유실 시 중복 패킷 재-ACK
- RTT 기반 적응형 RTO + timeout backoff
- 재전송 패킷 RTT 샘플 제외(Karn 방식)
- 64-slot reliable send window 포화 처리
- 시퀀스 wrap-around 순서 비교
- 런타임 RUDP 메트릭
- 다중 클라이언트 / 서버 수용량 / 패킷 손실 회귀 테스트

## 구조

```mermaid
flowchart LR
    A[Client packet] --> B[rudp_handler]
    B -->|sequence + payload| C[UDP datagram]
    C --> D[Windows RIO]
    D --> E[rio_udp_server_controller]
    E --> F[rudp_server]
    F --> G[endpoint RUDP state]
    G -->|ACK| D
    D --> B
```

`rudp_handler`는 reliable packet의 시퀀스, ACK, resend timer와 RTO를 관리합니다. 서버는 RIO completion queue에서 수신한 UDP datagram을 엔드포인트별 RUDP 상태로 전달합니다.

## 개선한 문제

| 문제 | 기존 실패 형태 | 개선 | 검증 |
| --- | --- | --- | --- |
| ACK 유실 | 수신자는 이미 받은 패킷을 무시하고 ACK도 다시 보내지 않아 송신측이 계속 재전송 | duplicate reliable packet 재-ACK | `ack_loss_tests`, `rio_ack_loss_smoke` |
| 고정 재전송 시간 | 네트워크 RTT 변화 반영 불가 | SRTT/RTTVAR 기반 RTO, timeout 2x backoff | `rudp_tests`, `metrics_tests` |
| 송신 윈도우 포화 | 64개 slot 이후 기존 상태 덮어쓰기 가능 | reliable send 거부 + 로그 | `send_window_tests` |
| 시퀀스 wrap | 31/32, 63/0 경계 순서 오판 | modulo half-range 비교 | `rudp_tests` |
| 서버 수용량 초과 | 빈 client id queue에서 `back()` 호출 가능 | 신규 엔드포인트 명시적 거부 | `rio_capacity_smoke` |
| timeout 후 id 재사용 | 만료된 endpoint→id 매핑이 남아 새 클라이언트와 같은 id를 가리킬 수 있음 | timeout 시 endpoint 매핑 제거 후 id 반환 | `rio_timeout_reuse_smoke` |

## 검증

Visual Studio 2022 / Windows SDK 10.0.19041 / Debug 구성에서 다음 11개 테스트를 `CTest`로 실행합니다.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

현재 회귀 테스트 결과: **11/11 통과**.

- RUDP ACK / sequence / reorder / wrap-around
- ACK 유실 재현
- reliable send window 포화
- 1% / 5% / 10% deterministic packet loss
- Windows RIO 단일 클라이언트
- Windows RIO 8-client isolation
- MaxClient 초과 거부
- timeout endpoint 정리 및 client id 재사용
- runtime metrics
- reproducible loss benchmark

## RUDP 손실 벤치마크

아래 값은 deterministic datagram loss simulator에서 reliable message 100개를 전송한 실제 실행 결과입니다.

```powershell
.\build\Debug\loss_benchmark.exe 100
```

| 손실률 | 메시지 전달 | 시도 datagram | drop | 재전송 | 최종 RTO |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0% | 100 / 100 | 200 | 0 | 0 | 10 ms |
| 1% | 100 / 100 | 204 | 2 | 2 | 10 ms |
| 5% | 100 / 100 | 222 | 11 | 11 | 10 ms |
| 10% | 100 / 100 | 249 | 25 | 25 | 20 ms |

이 벤치마크는 인터넷 환경의 처리량 측정이 아니라 손실 조건에서 재전송과 reliable delivery 동작을 반복 재현하기 위한 검증 도구입니다.

## 런타임 메트릭

`rudp_handler::metrics()`에서 다음 상태를 확인할 수 있습니다.

- `reliable_sent`
- `unreliable_sent`
- `retransmissions`
- `acks_received`
- `duplicate_reacks`
- `rejected_reliable_sends`
- `current_rto_ms`

## 다음 확장

YCNet을 실제 사용 사례와 연결하기 위해 작은 네트워크 게임을 별도 예제로 구성할 예정입니다. 목표는 이동/상태 동기화, reliable event와 unreliable state update의 역할 분리, 네트워크 상태 시각화입니다.
