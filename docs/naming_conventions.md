# 코어 이름 규칙

파일·디렉터리·네임스페이스·타입·함수·변수·상수·템플릿 인자·매크로는 snake_case를 사용한다. Windows SDK와 표준 라이브러리 이름은 원래 API를 따른다. private 멤버에는 끝에 `_`를 붙인다.

`ycnet`은 라이브러리를 식별하는 이름으로 유지한다. 그 아래 구성요소는 개인 접두사를 반복하지 않고 역할로 구분한다. `rio`, `rudp`, `rto`, `srw` 같은 기술 용어는 유지하고 `pkt_vrfct`, `intr_pkt`, `prc_resend` 같은 불명확한 축약은 풀어 쓴다.

| 공개 경로 | 네임스페이스 | 역할 |
| --- | --- | --- |
| `include/ycnet/ycnet.hpp` | `ycnet` | RUDP handler 및 서버 API |
| `include/ycnet/rio_transport.hpp` | `ycnet::rio` | Windows RIO UDP 전송 |
| `include/ycnet/packet.hpp` | `ycnet::packet` | 패킷 조립·검증, ACK 헤더 |
| `include/ycnet/rudp.hpp` | `ycnet::rudp` | 시퀀스·ACK·재전송·RTO |
| `include/ycnet/mpsc_buffer.hpp` | `ycnet::detail` | 생산자별 버퍼 저장소 |
| `include/ycnet/support/result.hpp` | `ycnet::detail` | 값 또는 오류 반환 |
| `include/ycnet/support/srw_lock.hpp` | `ycnet::detail` | SRW 잠금 보조 타입 |

공개 헤더는 `#include "ycnet/ycnet.hpp"`처럼 라이브러리 경로를 포함한다. CMake 프로젝트·라이브러리 타깃은 `ycnet`, 테스트 옵션은 `ycnet_build_tests`다.

전역 매크로는 `ycnet_packet`, `ycnet_reliable_packet`, `ycnet_variable_packet`, `ycnet_reliable_variable_packet`으로 이름을 구분한다. 생성 타입은 `packet_*`, `reliable_packet_*`, `variable_packet_*`, `reliable_variable_packet_*`이고 속성은 `packet_id`, `is_reliable`이다. 기존 전역 패킷 별칭·상수·이벤트 레지스트리는 유지한다.

```cpp
#include "ycnet/ycnet.hpp"

ycnet::rudp_server<8, 1, 51234> server(log_message);
server.set_connect_callback(on_connect);
server.set_timeout_callback(on_timeout);
server.start_io();
server.process_received_packets();
server.process_retransmissions(client_id);
server.stop();
```

이 변경은 소스 API 변경이다. 이전 이름에 대한 호환 별칭은 제공하지 않는다. 패킷 필드 순서·크기, ID 생성 순서와 알고리즘은 유지한다.

`mpsc`는 생산자/소비자 구성을 설명한다. 현재 구현의 동시성 안전성이나 lock-free 보장을 의미하지 않는다.
