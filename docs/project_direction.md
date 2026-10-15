# YCNet 포트폴리오 프로젝트 방향

2026-10-15 공개 저장소·공식 문서 및 현재 코드를 검토한 제안이다. 이후 KCP 비교를 실제 실행했고 [첫 비교 결과](protocol_comparison.md)에 기록했다. 게임과 ENet·RIO/IOCP 비교는 아직 구현하지 않았다.

## 추천: Godot 게임 하나와 계층별 비교 실험

게임 서버 직무용 포트폴리오라면 작은 완성 게임을 사용 사례로 보여주고, 네트워크 코어의 판단 근거는 별도 실험으로 설명하는 구성이 좋다. 장르를 크게 늘리기보다 한 판을 끝까지 플레이할 수 있게 만든다.

첫 후보는 **2~4인 협동 운송 게임**이다. 작은 맵에서 짐을 줍고, 운반하고, 목적지에 내려 점수를 얻는다. 위치·방향은 최신 상태를 계속 갱신하고, 짐 소유권·도착·점수는 서버가 확정한다. 같은 짐을 동시에 줍는 경쟁, 이벤트 재전송에도 점수가 중복되지 않는 처리, 연결 종료 후 짐 반환을 시각적으로 보여줄 수 있다. 이는 제안이며 현재 구현 기능이 아니다.

| 후보 | 코어를 보여줄 장면 | 구현 부담과 주의점 |
| --- | --- | --- |
| 협동 운송 게임 | 연속 위치 갱신 + 확실히 전달해야 하는 소유권/점수 이벤트 | 권장. 한 맵·한 판으로 범위를 제한하기 쉽다. 충돌과 상호작용은 서버 판정 |
| 소형 레이싱/타임어택 | 조향·위치 갱신, 체크포인트·완주 확정, 손실 시 화면 변화 | 상태 보간을 설명하기 좋지만 물리 예측 및 보정 부담이 더 큼 |
| 소형 실시간 슈팅 | 입력, 스냅샷, 발사·피격 이벤트 | 네트워크 기술을 많이 보여주지만 판정 보정·예측·지연 보상으로 범위가 커짐 |
| 턴제 보드게임/채팅 | 이벤트 전달·재접속 | 구현은 작지만 연속 상태 갱신이나 RIO 사용 이유를 보여주기 약함 |

화면은 간단한 2D 도형 또는 소수의 픽셀 에셋으로 만들고, 상태를 읽기 쉽게 표시한다. 네트워크 패널은 옵션으로 열어 RTT, 재전송, 서버 보정량과 실험 조건을 볼 수 있게 한다. 손실률 숫자는 측정값인지 주입 설정값인지 구별한다.

## 비교 대상과 각자의 역할

- [KCP](https://github.com/skywind3000/kcp/blob/master/README.en.md): 소켓 I/O와 분리된 ARQ 알고리즘. 같은 시뮬레이터 또는 같은 UDP 백엔드에 연결해 YCNet의 RUDP와 비교하기 좋은 첫 후보다. 공식 README의 성능 수치는 저자의 실험이며 YCNet 성능의 근거로 사용하지 않는다.
- [ENet](https://github.com/lsalzman/enet) 및 [설계 문서](https://github.com/lsalzman/enet/blob/master/docs/design.dox): reliable/unreliable, 시퀀싱, 채널, 연결 관리 등을 제공한다. 게임용 메시지 계층의 기능과 지연 비교 후보다. KCP보다 비교 범위가 넓으므로 채널 수와 전송 의미를 맞춘다.
- [Valve GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets): reliable/unreliable 메시지, 단편화·재조립, 암호화 및 P2P 기능까지 포함한다. 기능 설계 참고에 유용하지만 초기 성능 비교에는 범위가 크다. 보안·연결 기능의 비용까지 단순 RUDP와 한 숫자로 비교하지 않는다.
- [Microsoft ctsTraffic](https://github.com/microsoft/ctsTraffic): 네트워크 부하·신뢰성·goodput 분석 도구. Windows I/O 모델과 측정 방법의 참고 대상이다. 선택한 버전에서 프로토콜과 I/O 모델 지원 조합을 소스로 확인한 뒤 사용한다. YCNet RUDP의 직접 대체재로 취급하지 않는다.

## 실험 A: RUDP와 KCP/ENet

비교 질문은 “같은 메시지 의미와 부하에서 손실·지연이 있을 때 얼마나 늦게 전달되고, 어떤 비용이 드는가?”다.

1. 단일 peer의 reliable ordered 메시지부터 시작한다. KCP의 메시지 모드를 사용하고 ENet은 단일 reliable 채널로 맞춘다. 순서·중복·payload 무결성을 별도로 검증한다.
2. 동일 payload 크기(예: 64/256/512 bytes), 송신 주기와 부하, MTU 상한, 타이머 호출 주기, 수용 가능한 in-flight 수를 명시한다. enqueue와 거부/queue 처리도 기록한다. 구조가 달라 완전히 같게 만들 수 없는 설정은 표로 남긴다.
3. RTT 0/30/80/150ms, 손실 0/1/5/10%, jitter·burst loss·reorder 조건을 단계적으로 추가한다. 모두 제안된 조건이며 아직 측정하지 않았다.
4. 전달 지연 p50/p95/p99, 완료율, 중복/순서 위반, 유효 payload 대비 전송 바이트, 재전송, queue 길이, CPU 및 메모리를 기록한다. 순수 프로토콜 시뮬레이터에서는 실제 NIC 성능을 주장하지 않는다.
5. 동일 손실·지연 모델과 여러 seed를 사용한다. 프로토콜별 datagram 구성은 다르므로 같은 seed가 같은 메시지를 떨어뜨리는 것은 아니다. 무작위 drop, burst loss, 주기적 drop을 구분한다.
6. 기본 설정과 명시적인 latency 설정을 따로 보고한다. 특정 라이브러리만 튜닝해서 얻은 차이를 일반적인 우열로 해석하지 않는다. Release 빌드, commit SHA, 설정, 반복 수, raw CSV를 남긴다.

현재 loss benchmark는 메시지 하나를 전달한 후 다음 메시지를 보내고, `sleep_for(2ms)`를 호출하며, 양방향 datagram에 누적기 기반의 주기적 손실을 적용한다. RTT/jitter/reorder 모델은 없다. 따라서 현재 elapsed 값은 고부하 처리량이나 게임 지연 성능의 근거로 부족하다. 외부 비교 전에 시계 주입, 지속 부하, backlog 계측과 네트워크 모델 확장이 필요하다.

## 실험 B: RIO와 UDP I/O 백엔드

비교 질문은 “동일한 서버 작업을 처리할 때 I/O 모델의 선택이 CPU, 처리량, tail latency에 어떤 영향을 주는가?”다.

- 소켓 I/O만 측정할 때는 RUDP를 모두 제거한 동일 UDP echo/workload를 사용한다.
- 게임 서버를 측정할 때는 같은 RUDP·게임 로직을 두고 RIO와 overlapped Winsock + IOCP 백엔드만 교체한다.
- payload, 클라이언트 수, offered packet rate, 스레드 수, 버퍼 수, batch 크기 및 polling/notification 방식을 공개한다. logging은 동일하게 끈다.
- 실제 처리 pps/goodput, 누락률, RTT p95/p99, CPU 사용, 메모리, 포화 후 backlog를 측정한다. 현재 코드의 1ms completion polling과 전역 CQ/RQ 잠금도 함께 설명한다.
- loopback 결과는 초기 점검으로 제한하고, 가능하면 두 머신과 실제 NIC로 재현한다. 같은 컴퓨터의 송수신 CPU 경합을 네트워크 성능으로 오해하지 않는다.

RIO와 ENet을 직접 비교한 숫자만으로 “RIO가 더 빠르다”는 결론을 내릴 수 없다. 프로토콜과 I/O 모델이 동시에 달라지기 때문이다.

## Godot 연결 방향

Godot의 [PacketPeerUDP](https://docs.godotengine.org/en/stable/classes/class_packetpeerudp.html)는 raw UDP 송수신을 제공한다. [기본 고수준 멀티플레이](https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html)는 수정된 ENet을 사용한다. 기존 YCNet 서버에 Godot RPC를 그대로 연결하는 방식을 가정하지 않는다.

YCNet 코어를 재사용하려면 데스크톱 Godot 클라이언트에 GDExtension으로 C++ RUDP handler를 연결하고 UDP 소켓 경로를 분리하는 구성을 우선 검토한다. RIO는 Windows 서버 측 I/O로 둔다. GDScript + PacketPeerUDP로 클라이언트 프로토콜을 따로 구현할 수도 있지만 두 구현의 일치 검증 비용이 커진다. 확장 모듈 없이 먼저 wire protocol을 검증할 수 있는 작은 도구를 만드는 것도 가능하다.

## 선행 작업

1. `volatile` 기반 공유 상태를 C++ 메모리 모델에 맞게 수정하고 스트레스 검증.
2. 6-bit 시퀀스, half-range 비교, 64-slot 송신 윈도우의 조건을 정리하고 delayed ACK·wrap·재접속 검증.
3. 패킷 ID·endianness·필드 크기·길이 검증을 고정한 wire protocol. 현재 구조체 메모리 복사와 선언 순서 의존을 제거.
4. 전송 백엔드와 시계를 주입할 수 있도록 경계 분리.
5. KCP 비교부터 실행한 뒤, 작은 Godot 게임을 연결. ENet 비교와 RIO/IOCP 실험은 이후 확장.

면접에서 설명할 목표는 유명 라이브러리를 이겼다는 선언보다, 어떤 조건에서 어떤 설계가 유리하거나 불리한지 재현 가능한 근거로 설명하는 것이다.
