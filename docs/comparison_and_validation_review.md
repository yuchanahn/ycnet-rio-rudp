# YCNet 비교 대상과 검증 감사

후속 변경: 현재 연결/실패 정책과 최종 재실행은 [신뢰성 정책 수정](reliability_policy.md)에 정리했다. 아래는 해당 수정 전의 기록이다.

아래는 시퀀스 수정 전 검토 기록이다. 이후 동일 연결의 시퀀스/슬롯 재사용 결함을 수정하고 회귀·비교를 재실행했다. 현재 범위와 남은 문제는 [시퀀스 식별 수정](sequence_identity_fix.md)을 참고한다.

2026-10-15. 현재 로컬 소스, 저장된 비교 CSV, 공개 저장소의 README·설계 문서·예제 코드를 확인했다. 외부 구현의 전체 코드를 검증하거나 새 성능 비교를 실행한 보고서는 아니다. 공개 벤치마크의 수치를 YCNet의 수치와 직접 합치지 않는다.

## 현재 판정

YCNet은 ACK·RTO·RIO 송수신의 일부 기능을 갖춘 실험적 구현이며, 일반적인 신뢰성 보장을 검증한 라이브러리 상태는 아니다. 기존 13개 회귀/무손실 smoke 통과와 전체 손실 실험의 실패는 양립한다. 그 통과만으로 코어가 안정적이라고 설명한 것은 과장이다.

기존 CSV에서 YCNet은 144개 구성 중 메시지 전달 완료 69, 내용/순서 검증 실패 12, 나머지 미완료 63이다. 두 KCP 모드는 메시지 전달 판정 144다. 이 숫자는 현재 harness 범위의 관측이며 일반적인 성능 순위가 아니다. RIO·실제 소켓을 거치지 않았으므로 RIO 서버 성능이나 안정성의 판정도 아니다.

## 비교 후보

| 대상 | 확인한 기능과 용도 | YCNet과 비교하는 방식 |
| --- | --- | --- |
| [KCP](https://github.com/skywind3000/kcp/blob/master/README.en.md) | I/O와 분리된 ARQ. [test.cpp](https://github.com/skywind3000/kcp/blob/master/test.cpp)에 손실/지연 시뮬레이터 예제가 있음 | RUDP 프로토콜의 첫 비교 대상으로 유지. 같은 network model·메시지·update schedule로 비교 |
| [ENet 설계](https://github.com/lsalzman/enet/blob/master/docs/design.dox) | reliable/unreliable, 독립 채널, 연결 감시, fragmentation/reassembly, aggregation, 전송량 조절 | 다음 비교 대상. reliable ordered 단일 채널부터 맞추고, 이후 게임 메시지 기능과 API 비교 |
| [Yojimbo](https://github.com/mas-bandwidth/yojimbo) | client/server 연결·timeout, reliable ordered/unreliable unordered, 직렬화, 인증·암호화 | 게임 네트워크 API와 계층 분리 참고. 단순 ARQ와 전체 기능을 한 숫자로 비교하지 않음 |
| [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) | reliable/unreliable, fragmentation, 암호화, 네트워크 시뮬레이션·통계, lanes | 장기 기능 설계 참고. 첫 성능 비교 대상으로는 범위가 큼 |
| [RIORING](https://github.com/newbiediver/rioring) | C++17, Windows RIO/Linux io_uring, TCP/UDP socket/server, I/O 서비스 객체 | RIO 서버 추상화의 직접적인 구조 참고 후보. 유명한 성능 표준으로 간주하지 않으며 Windows UDP 경로를 실제 빌드·계측해야 함 |
| [CppServer](https://github.com/chronoxor/CppServer) | Asio 기반 UDP 서버·클라이언트, UDP echo 및 benchmark 예제, Start/Stop/Restart | raw UDP 서버 비교 후보. RUDP를 제거한 echo workload 또는 동일 프로토콜을 연결한 backend 비교 |
| [Microsoft ctsTraffic](https://github.com/microsoft/ctsTraffic) | 여러 I/O 모델을 연결하는 부하·신뢰성·goodput 측정 도구 | 부하 생성·측정 방법 참고. YCNet과 패킷 프로토콜이 자동 호환되는 직접 대체 구현이 아님 |

`mas-bandwidth/reliable`는 저장소 설명부터 UDP packet acknowledgement 계층이다. 이름이 reliable이라는 이유만으로 완성된 메시지 재전송/ordered 전달 라이브러리와 동일하게 분류하면 안 된다. Yojimbo와 조합한 역할을 확인해야 한다.

## 공개 벤치마크

1. [KCP 자체 테스트](https://github.com/skywind3000/kcp/blob/master/test.cpp): LatencySimulator로 loss/jitter가 있는 전달을 비교하는 작은 출발점. 보편적인 전체 네트워크 벤치마크 표준은 아니다.
2. [NetworkBenchmarkDotNet](https://github.com/JohannesDeml/NetworkBenchmarkDotNet): ENet/Kcp2k/LiteNetLib/NetCoreServer의 비교와 재현 방법, adapter 구조가 있다. .NET/일부 wrapper 조건이므로 C++ YCNet과 공개 숫자를 직접 비교하지 않는다. 프로젝트 자체도 local client 부하 때문에 CCU나 실제 네트워크 지연 측정으로 읽으면 안 된다고 밝힌다.
3. [reliable_udp_bench_mark](https://github.com/libinzhangyuan/reliable_udp_bench_mark/blob/master/bench_mark.md): KCP/ENet/UDT를 작은 메시지의 주기적 echo로 비교한다. 50/500 B, 50ms 주기와 실제 인터넷/Wi-Fi 조건을 명시한다. 해당 시점·환경·설정의 결과이므로 현재 라이브러리의 우열을 대입하지 않는다.
4. [CppServer](https://github.com/chronoxor/CppServer): UDP echo와 multicast benchmark 예제. 프로토콜 reliability와 raw I/O를 구분해 측정하는 데 참고한다.
5. [ctsTraffic](https://github.com/microsoft/ctsTraffic): goodput, 데이터 무결성, 장시간 부하와 환경 기록 방법 참고. 선택한 버전의 I/O 모델·프로토콜 지원 조합을 확인한 뒤 사용한다.

검토한 자료에서는 최신의 동일 환경에서 YCNet/KCP/ENet/RIO/IOCP를 함께 공정하게 비교해 주는 하나의 표준 도구는 확인하지 못했다. 하나의 adapter harness에 명시적인 계약을 두는 편이 적절하다.

## 벤치마크와 독립적인 직접 재현

64개 메시지를 순서대로 전달하고 ACK도 모두 처리했다. 첫 데이터와 첫 ACK의 복사본을 보관했다가 시퀀스가 0으로 돌아온 뒤 주입했다. 실제 소켓이나 RIO를 사용하지 않았다.

```text
delivered_before_old_data=64 delivered_after_old_data=65 last_value=0
acks_before_old_ack=64 acks_after_old_ack=65 new_data_held=1
```

첫 줄은 이전 값 0을 65번째 메시지로 다시 전달한 것이다. 둘째 줄은 새 메시지의 datagram을 네트워크에 전달하지 않았는데도 과거 ACK로 확인 처리된 것이다. 재현 코드는 `docs/research/sequence_replay_probe.cpp`에 보존했다. 이 코드는 진단값을 출력하는 probe이며 CTest의 성공 테스트로 등록하지 않았다.

단순한 63→0 산술 검사로는 이런 세대 혼동을 검증할 수 없다. 더 넓은 시퀀스, oldest-unacked 기준의 window 범위, receive window, 세션 세대, 패킷 생존 시간/시퀀스 재사용 조건을 함께 설계해야 한다. outstanding 개수 16 제한만으로 오래된 시퀀스와 최신 시퀀스의 거리가 제한되는 것은 아니다.

## 부족한 부분: 먼저 고쳐야 하는 보장

| 영역 | 소스 근거 | 필요한 계약/수정 |
| --- | --- | --- |
| 지연 데이터·ACK 구분 | `packet.hpp` 6-bit sequence, `rudp.hpp`의 half-range 비교·슬롯 처리 | seq 재사용·세션 세대·송수신 window의 일관된 조건과 실제 replay 회귀 테스트 |
| timeout | `get_resend_packets`는 backoff가 threshold를 넘으면 후보를 버리고 timed_out. client handler는 종료 이벤트 없음 | RTO와 전체 연결 timeout의 분리, explicit disconnect/error, pending 메시지 처리·재접속 규칙 |
| 입력 크기·형식 검증 | `validate_packet`, 고정 1024 B 버퍼, macro의 raw struct cast/memcpy | buffer 경계·payload별 최소/최대 크기·handler 등록 여부·불법 ID/가변 길이 검증 및 fuzz |
| 버퍼 포화 | `push_packet`의 unreliable 빈 슬롯 탐색 실패 시 idx=-1 경로 | 포화 전 명시적 거부/drop 정책과 경계 검사. 이 경로는 코드 검토상의 결함 후보이며 이번에 직접 실행하지 않음 |
| 스레드 간 동기화 | `mpsc_buffer.hpp`의 volatile used flag, RIO send flag와 is_running | ownership 및 atomic acquire/release 또는 lock으로 C++ 메모리 모델의 보장 확립 |
| I/O 완료 오류 | RIODequeueCompletion 결과 개수·개별 Status 처리 | corrupt CQ와 실패 completion을 정상 수신으로 취급하지 않게 오류 분기·상태 정리 |
| 수명·재시작 | 전역 socket/CQ/RQ/worker/buffer, start/stop | 인스턴스별 RAII, 초기화 중간 실패 rollback, outstanding I/O 종료와 등록 메모리 해제, 반복 start/stop |

## API와 라이브러리로서의 부족함

- 패킷 매크로의 `__COUNTER__` ID와 raw struct 메모리는 다른 빌드·컴파일러·Godot 사이 wire contract로 쓰기 어렵다. 버전·ID·endianness·필드 크기를 고정해야 한다. signed 8-bit ID와 max_packet_types=1000 선언도 맞지 않는다.
- 전역 `packet_events`는 서버 인스턴스별 handler 격리를 어렵게 한다. 헤더의 non-inline 전역 정의/함수도 있으므로 여러 `.cpp`에서 포함하는 소비자 link test가 필요하다. 현재 단일 `.cpp` executable 위주의 테스트가 이 문제를 가릴 수 있다.
- reliable send의 bool은 queued, window full, transport error, disconnected를 구분하지 않는다. unreliable send는 void다. 특히 서버 send는 `send_udp`의 오류를 확인하지 않고 성공으로 돌아갈 수 있다. 송신 수용과 실제 전달 보장을 분리해 문서화해야 한다.
- callback이 정해진 struct 타입으로 바로 dispatch된다. 프로토콜 코어가 bytes/span과 메시지 정책을 받고 사용자 직렬화·게임 이벤트는 바깥에 두는 구조가 재사용·fuzz·다른 언어 연결에 유리하다.
- client handler는 sender callback/시계를 주입할 수 있으나 server는 RIO와 template의 port/수용량/thread 설정에 결합돼 있다. backend, runtime config, scheduler 경계를 분리해야 같은 프로토콜로 RIO/IOCP를 비교할 수 있다.
- 연결/해제 이벤트, heartbeat/idle timeout, send queue의 bounded backpressure와 cancellation, 다음 update deadline, 서버측 peer별 통계의 계약이 부족하다.
- 채널, unreliable sequenced, aggregation, fragmentation, pacing/혼잡 대응은 ENet 등의 기능과 비교해 범위를 정해야 한다. 전부 구현할 필요는 없다. MTU 이하만 지원한다면 정확한 상한과 거부 동작을 보장해야 한다.

## RIO 성능을 논하기 전

전역 CQ/RQ와 하나의 lock, completion이 없을 때 1ms sleep, send slot 탐색 busy wait, per-packet log와 buffer copy가 있다. 이것이 병목인지 profile로 확인해야 한다. RIO를 사용했다는 것만으로 zero-copy나 높은 확장성이 증명되지 않는다. 단일 머신 loopback, 8-client smoke로 처리량이나 thread 확장성을 주장할 수 없다.

## 기존 테스트가 확인한 것과 놓친 것

| 기존 검증 | 유효한 범위 | 놓친 조합 |
| --- | --- | --- |
| RIO loopback smoke | socket/등록 버퍼/completion의 기본 경로 | 지연/손실/지속 부하/완료 오류/수명 반복 |
| ACK 유실 회귀 | 짧은 구간 중복의 재-ACK와 한 번 전달 | seq가 한 바퀴 돈 뒤의 old data·old ACK |
| sequence wrap 단위 검사 | modulo 및 근처 순서 비교 | 실제 payload 세대와 send/receive window 연동 |
| send window 64-slot 포화 | 슬롯 덮어쓰기 거부 | half-range=32의 조건과 end-to-end 전달 가능성 |
| network_loss_tests | 한 메시지씩 전달하는 주기적 drop 상황 | 복수 in-flight, RTT/jitter/burst/reorder, 각 메시지의 index와 body 일치 |
| capacity/timeout reuse | 작은 수용량, 특정 연결 교체 시나리오 | 여러 번 재접속, 동일 endpoint의 세대, 지연된 과거 연결 패킷 |

특히 기존 loss 테스트는 같은 문자열을 보내며 전달 횟수를 센다. 잘못된 메시지가 같은 횟수로 도착하는 경우를 구별하기 어렵다. 정상 ACK와 실제 송신 윈도우를 모두 끝까지 검증하는 것도 보완해야 한다.

## 현재 harness도 먼저 보완할 점

1. `complete`에 최종 ACK 대기를 넣고 `message_complete`, `all_acked`, `timed_out`, `invalid`를 별도 열로 기록한다.
2. KCP 실제 receive window=128과 기본/튜닝 설정을 기록한다. request=32를 effective=32로 설명하지 않는다.
3. YCNet의 RTO backoff threshold 1000ms와 KCP 종료/재시도 정책이 다르므로 동일 시간·실패 계약으로 맞추거나 차이를 명시한다.
4. 미완료 실행의 전달된 일부 메시지 p95를 완료 실행의 p95와 우열 비교하지 않는다. timeout/censored 결과를 별도로 본다.
5. 순서·중복·body·최종 ACK·backlog의 불변식을 먼저 검증한다. 이후 RTT/jitter/burst/reorder/bandwidth 모델, 여러 seed/반복 및 raw 결과를 추가한다.
6. RIO/IOCP는 같은 raw UDP workload 또는 같은 RUDP를 두고 backend만 바꾼다. pps/goodput/p95/p99/CPU/memory/drop을 실제 NIC 두 대에서도 측정한다.

## 권장 순서

1. 기존 smoke는 유지한다. 전체 신뢰성의 증거라는 표현을 줄인다.
2. 이번 replay와 timeout을 최소 실패 회귀로 만들고, 프로토콜 계약 및 동기화·파싱 경계를 고친다.
3. 비교 harness의 판정·설정 계측을 바로잡아 KCP와 단일 reliable channel ENet을 비교한다.
4. I/O를 분리한 뒤 RIO vs overlapped Winsock/IOCP를 같은 workload로 측정한다.
5. 그 위에 작은 Godot 게임을 올린다. 포트폴리오는 기능 수보다 실패 재현→원인→수정→동일 실험 재검증으로 구성한다.
