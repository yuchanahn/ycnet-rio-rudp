# 시퀀스 식별 번호와 64칸 버퍼 분리

후속 변경: 현재 연결/실패 정책과 최종 재실행은 [신뢰성 정책 수정](reliability_policy.md)에 정리했다. 아래는 해당 수정 전의 기록이다.

2026-10-15. 이번 변경은 동일 연결 안에서 버퍼 슬롯 재사용 때문에 과거 데이터·ACK를 새 패킷으로 잘못 처리하는 문제에 한정한다. timeout 정책, RIO 동기화, 세션 handshake는 변경하지 않았다.

## 원인

기존에는 wire sequence와 버퍼 index가 모두 0~63이었다. 64개 메시지를 처리한 뒤의 새 패킷 64와 과거 패킷 0이 모두 wire에서 0이 됐다. 수신자는 과거 데이터를 새 데이터로 dispatch하고, 송신자는 과거 ACK로 새 슬롯을 해제할 수 있었다. 인접 숫자의 modulo/half-range 비교만으로는 세대를 구별할 수 없었다.

## 변경

- `reliable_window_size = 64`는 버퍼 크기로만 사용한다.
- 패킷과 ACK는 별도의 32-bit 전체 시퀀스를 전송한다. 내부 cursor는 64-bit signed 값으로 관리해 상한 다음의 exhausted 상태도 표현한다.
- `sequence_slot(sequence)`만 `sequence % 64`를 계산한다. 각 송·수신 슬롯에는 전체 번호도 보관한다.
- ACK는 슬롯의 전체 번호가 일치할 때만 완료 처리한다.
- 수신 순서 cursor는 64에서 0으로 돌아가지 않는다. receive window 밖의 미래 데이터가 현재 슬롯을 덮지 못하게 거부한다. 과거 데이터는 재-ACK하되 다시 dispatch하지 않는다.
- 미확인 가장 오래된 번호와 신규 번호의 간격은 64 미만이어야 한다. 슬롯 하나가 비었다는 이유만으로 window 밖 번호를 수용하지 않는다.
- 연속된 수신 슬롯 64개가 한꺼번에 찼을 때도 end cursor가 올바르게 전진한다.
- 32-bit 번호를 모두 사용하면 같은 연결에서 0으로 재사용하지 않고 추가 reliable send를 거부한다. 시퀀스를 더 넓히는 것만으로 wrap 문제를 뒤로 미루지 않도록 한 제한이다.

## wire 형식 변경

기존 1-byte ACK/sequence header는 5-byte header로 바뀌었다.

| byte | 내용 |
| --- | --- |
| 0 | bit 7 reliable, bit 6 ACK, 하위 6 bits는 형식 값 1 |
| 1~4 | uint32 sequence, big-endian |

ACK datagram은 5 bytes다. 데이터는 위 header 뒤에 기존 packet size/id/body가 온다. unreliable 데이터도 같은 5-byte envelope를 사용한다. 전체 datagram 상한 1024 B는 유지하므로 기존 3-byte packet size/id를 제외한 body 상한은 1016 B다.

이전 wire 형식과 호환되지 않는다. 클라이언트와 서버를 함께 다시 빌드해야 한다. `make_ack_packet`은 1 byte가 아닌 배열을 반환하므로 `.data()`와 `.size()`로 전송한다. 새 envelope에 맞춘 길이 검증 및 송신 크기 검사를 포함했다.

세션을 새로 만들면서 같은 endpoint와 sequence 0을 재사용할 때 과거 연결의 datagram을 구별하는 문제는 별도다. 이번 변경은 세션 ID/handshake를 추가하지 않으므로 그 보장까지 주장하지 않는다.

## 검증

수정 전에 `sequence_reuse_tests`를 실행해 old data/old ACK 재주입 실패를 확인했다. 수정 후에는 다음을 모두 확인했다.

- 64개 처리 후 과거 데이터·ACK 재주입 거부.
- 슬롯 재사용 이후의 새 데이터 전달 및 정상 ACK 완료.
- 4096개 payload의 ID·순서와 과거 데이터 재주입 거부.
- 63/64 경계에서 역순 도착, receive window 밖 패킷 주입.
- 64개 전체를 역순 수신한 뒤 순서대로 전달·ACK 완료.
- 최대 uint32 sequence의 wire 왕복과 slot-only ACK 거부.
- 시퀀스 상한 다음 송신 거부.
- 기존 RIO와 RUDP 회귀 포함 Release CTest **14/14 통과**. 비교 옵션이 꺼져 있으면 코어 테스트는 13개다.

## 동일 비교 실험 재실행

비교 코드의 판정/timeout 설정은 이번 범위에서 변경하지 않았다. 기존 CSV도 보존했다.

| YCNet / 144개 구성 | 수정 전 | 시퀀스 수정 후 |
| --- | ---: | ---: |
| 메시지 완료 판정 | 69 | 78 |
| 내용/순서 검증 실패 | 12 | 0 |
| timeout 미완료 | 63 | 66 |

수정 후 원본은 [CSV](protocol_comparison_sequence32.csv), [실행 진단](protocol_comparison_sequence32_diagnostics.txt)이다. 과거 시퀀스를 새 메시지로 잘못 전달하는 오류는 이 실험에서 사라졌다. timeout 정책은 그대로이므로 전체 미완료 문제까지 해결된 것은 아니다. 기존 `complete`의 최종 ACK 계측 누락 등 harness 한계도 유지되며 이 결과로 성능 우열을 주장하지 않는다.
