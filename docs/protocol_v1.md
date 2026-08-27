# Ethernet–RS-422 Gateway Application Protocol V1

**Document:** `protocol_v1.md`  
**Protocol Version:** 1.0  
**Date:** 2026-08-16

---

## 1. 목적

본 프로토콜은 서로 다른 통신 구간인 **Ethernet(TCP/UDP)** 과 **UART 기반 RS-422** 사이에서 동일한 Application Frame을 사용하기 위해 설계한다.

전체 시스템은 다음과 같다.

```text
PC
 │
 │ TCP : 명령 / 응답
 │ UDP : 주기 Telemetry
 ▼
WIZ850io (W5500)
 │ SPI
 ▼
STM32G474 + FreeRTOS
 │
 │ UART 115200 8N1
 ▼
MAX490
 ║
 ║ RS-422 4-wire Full Duplex
 ║
MAX490
 │
 ▼
STM32F429
```

핵심 설계 원칙은 다음과 같다.

> Ethernet과 RS-422이라는 서로 다른 통신 구간에서도 동일한 Application Protocol을 사용한다.

즉, PC가 생성한 Application Frame을 Ethernet으로 G474까지 전달하고, G474가 이를 RS-422 구간으로 중계하여 F429가 같은 규격으로 해석하도록 한다.

---

## 2. 설계 요구사항

프로토콜은 다음 요구사항을 만족해야 한다.

1. Byte stream에서 프레임 시작 위치를 식별할 수 있어야 한다.
2. 메시지의 종류를 구분할 수 있어야 한다.
3. 요청과 응답을 서로 대응시킬 수 있어야 한다.
4. UDP Telemetry의 누락 여부를 확인할 수 있어야 한다.
5. 메시지마다 다른 Payload 길이를 처리할 수 있어야 한다.
6. Application Frame의 데이터 손상을 검출할 수 있어야 한다.
7. 향후 Protocol 규격 변경 시 버전을 구분할 수 있어야 한다.
8. RS-422 Remote Node의 통신 장애를 감지하고 복구할 수 있어야 한다.
9. PC, G474, F429 사이에서 동일한 직렬화 규칙을 사용해야 한다.

---

## 3. Protocol Frame

### 3.1 Frame Format

| 순서 | Field | 크기 | 역할 |
|---|---|---:|---|
| 1 | `MAGIC` | 2 B | 프레임 시작 식별 |
| 2 | `VERSION` | 1 B | 프로토콜 버전 |
| 3 | `MSG_ID` | 1 B | 메시지 종류 |
| 4 | `SEQ` | 2 B | 요청-응답 대응 및 누락 검출 |
| 5 | `LENGTH` | 2 B | Payload 길이 |
| 6 | `PAYLOAD` | 0~256 B | 실제 데이터 |
| 7 | `CRC16` | 2 B | Application Frame 오류 검출 |

최대 프레임 크기:

```text
2 + 1 + 1 + 2 + 2 + 256 + 2
= 266 bytes
```

### 3.2 고정 규격

```text
MAGIC       = 0xAA55
VERSION     = 0x01
MAX_PAYLOAD = 256 bytes
Endian      = Big Endian
CRC         = CRC-16/CCITT-FALSE
```

모든 2바이트 이상의 정수 필드는 **Big Endian(Network Byte Order)** 으로 직렬화한다.

---

## 4. 각 필드의 설계 의도

| 통신 문제 | 설계 대응 |
|---|---|
| TCP/UART는 byte stream이므로 프레임 시작 위치를 알 수 없음 | `MAGIC` |
| 향후 Protocol 구조가 변경될 수 있음 | `VERSION` |
| PING, STATUS, TELEMETRY 등 메시지 의미를 구분해야 함 | `MSG_ID` |
| 요청과 응답을 대응시켜야 함 | `SEQ` |
| UDP Telemetry 누락 여부를 확인해야 함 | `SEQ` |
| 메시지마다 Payload 크기가 다름 | `LENGTH` |
| Application Frame의 데이터 손상을 검출해야 함 | `CRC16` |

프레임은 **필드를 먼저 정한 뒤 이유를 붙인 것이 아니라**, 통신 과정에서 발생하는 문제를 하나씩 해결하는 과정에서 위 구조로 도출하였다.

---

## 5. TIMESTAMP 설계

Protocol V1의 공통 Header에는 `TIMESTAMP`를 포함하지 않는다.

RTT는 PC에서 다음 방식으로 측정할 수 있다.

```text
송신 직전 시각 t1
        ↓
응답 수신
        ↓
수신 시각 t2

RTT = t2 - t1
```

따라서 RTT 측정만을 위해 모든 Frame에 Timestamp를 넣지 않는다.

향후 BNO085 등 실제 Telemetry의 **데이터 생성 시각**이 필요할 경우, 공통 Header가 아니라 `TELEMETRY` Payload 내부에 `sample_time_ms`를 추가한다.

---

## 6. Message ID

Protocol V1의 Message ID는 다음과 같이 정의한다.

| MSG_ID | 이름 | 방향 / 용도 |
|---:|---|---|
| `0x01` | `PING` | Request |
| `0x81` | `PONG` | Response |
| `0x10` | `READ_STATUS` | Request |
| `0x90` | `STATUS_RESP` | Response |
| `0x20` | `HEARTBEAT` | Remote 생존 확인 |
| `0x40` | `TELEMETRY` | 주기 데이터 |
| `0xE0` | `ERROR` | 정상 Frame에 대한 Protocol 오류 응답 |

Request에 대한 Response는 가능한 경우 Request `MSG_ID`의 상위 bit `0x80`을 설정하는 규칙을 사용한다.

```text
PING         0x01
PONG         0x81

READ_STATUS  0x10
STATUS_RESP  0x90
```

---

## 7. SEQ 규칙

### 7.1 Request / Response

PC가 Request를 생성할 때 `SEQ`를 증가시킨다.

```text
PING SEQ=100
PING SEQ=101
PING SEQ=102
```

F429는 Request에 대한 Response를 생성할 때 **동일한 SEQ를 그대로 사용한다.**

```text
PC   → PING SEQ=100
F429 → PONG SEQ=100
```

이를 통해 PC는 수신된 응답이 어떤 요청에 대한 응답인지 확인할 수 있다.

`SEQ`는 `uint16_t`이며 wrap-around를 허용한다.

```text
65534
65535
0
1
```

### 7.2 Telemetry

Telemetry는 F429가 독립적인 SEQ를 관리하여 증가시킨다.

```text
TELEMETRY SEQ=1
TELEMETRY SEQ=2
TELEMETRY SEQ=3
TELEMETRY SEQ=5
```

PC는 `SEQ=4`가 누락되었음을 검출할 수 있다.

이 값은 추후 UDP Telemetry의 Packet Loss 측정에 사용한다.

---

## 8. CRC16

### 8.1 CRC 규격

Protocol V1은 다음 CRC를 사용한다.

```text
CRC-16/CCITT-FALSE

Polynomial = 0x1021
Initial    = 0xFFFF
RefIn      = false
RefOut     = false
XorOut     = 0x0000
```

표준 검증 벡터:

```text
Input : "123456789"
CRC   : 0x29B1
```

CRC 구현 후 위 값이 출력되는지 단위테스트로 확인한다.

### 8.2 CRC 계산 범위

CRC 계산 대상:

```text
VERSION | MSG_ID | SEQ | LENGTH | PAYLOAD
```

CRC 계산에서 제외:

```text
MAGIC
CRC16 필드 자체
```

즉, `MAGIC`은 Frame Synchronization을 위한 정보이고, CRC16은 프레임 내부의 Protocol Data가 정상인지 검증하는 데 사용한다.

### 8.3 CRC16을 Application Layer에 추가하는 이유

Ethernet/TCP/UDP에도 자체 오류 검출 기능이 존재하지만, 이 프로젝트는 다음과 같이 서로 다른 통신 구간을 통과한다.

```text
PC
 │ Ethernet TCP/UDP
 ▼
G474
 │ UART
 ▼
RS-422
 ▼
F429
```

RS-422은 물리 계층 전기 규격이며 자체적인 Application Frame 무결성 검증 기능을 제공하지 않는다.

따라서 Ethernet 구간 이후의 UART/RS-422 구간까지 포함하여 **동일한 Application Frame 자체의 무결성을 검증하기 위해 CRC16을 사용한다.**

---

## 9. TCP / UDP 역할 분리

### 9.1 TCP

TCP는 다음 메시지에 사용한다.

```text
PING / PONG
READ_STATUS / STATUS_RESP
향후 Configuration / Command
```

설계 이유:

> 명령과 응답은 유실되는 것보다 전달 신뢰성이 중요하므로 TCP를 사용한다.

### 9.2 UDP

UDP는 다음 메시지에 사용한다.

```text
주기적 TELEMETRY
```

설계 이유:

> 주기 데이터는 오래된 데이터를 재전송하는 것보다 최신 데이터가 지속적으로 전달되는 것이 중요하며, SEQ를 이용해 Application 수준에서 누락률을 측정할 수 있으므로 UDP를 사용한다.

---

## 10. 기본 동작 시나리오

### 10.1 PING / PONG

첫 번째 End-to-End 검증 시나리오이다.

```text
PC
 │ PING, SEQ=152
 ▼
G474
 │ 동일 Application Frame 중계
 ▼
F429
 │ PONG, SEQ=152
 ▼
G474
 │
 ▼
PC
```

PC는 송신 직전과 응답 수신 시각의 차이로 RTT를 계산한다.

```text
TX PING seq=152
RX PONG seq=152

RTT = 5.31 ms
```

### 10.2 READ_STATUS / STATUS_RESP

```text
PC
 │ READ_STATUS, SEQ=153
 ▼
G474
 ▼
F429
 │ STATUS_RESP, SEQ=153
 ▼
G474
 ▼
PC
```

F429의 초기 Status Payload 논리 필드는 다음과 같다.

```text
device_state
uptime_ms
rx_ok_count
crc_error_count
seq_gap_count
```

논리 구조 예시:

```c
typedef struct
{
    uint8_t  device_state;
    uint32_t uptime_ms;
    uint32_t rx_ok_count;
    uint32_t crc_error_count;
    uint32_t seq_gap_count;
} status_payload_t;
```

**주의:** 위 struct는 논리적 데이터 구조 예시일 뿐이며 메모리 이미지를 그대로 송신하지 않는다. 각 필드는 Protocol의 Big Endian 규칙에 따라 byte buffer로 명시적으로 직렬화한다.

예시 출력:

```text
STATE        = NORMAL
UPTIME       = 35221 ms
RX_OK        = 429
CRC_ERROR    = 0
SEQ_GAP      = 0
```

---

## 11. Heartbeat / Timeout

F429는 주기적으로 G474에 Heartbeat를 송신한다.

초기 설정:

```text
Heartbeat Period      = 500 ms
Communication Timeout = 1500 ms
```

G474는 F429에서 **정상적으로 검증된 Frame을 1500 ms 동안 하나도 받지 못하면** 통신 상태를 다음과 같이 변경한다.

```text
COMM_OK
   ↓
COMM_TIMEOUT
```

정상 Frame을 다시 수신하면:

```text
COMM_TIMEOUT
   ↓
COMM_OK
```

으로 복구한다.

초기 V1에서는 복구 시 `3회 연속 정상 수신`과 같은 추가 조건을 사용하지 않는다. 실제 시험 결과에 따라 필요성이 확인될 경우 추가한다.

### 정상 수신의 정의

단순히 UART byte가 들어온 것만으로 Heartbeat/Timeout 시간을 갱신하지 않는다.

다음 조건을 모두 만족해야 정상 Frame으로 인정한다.

```text
MAGIC 검출
    ↓
VERSION 정상
    ↓
LENGTH 정상
    ↓
CRC 정상
    ↓
Valid Frame
    ↓
last_valid_rx_time 갱신
```

---

## 12. 잘못된 Frame 처리 정책

| 오류 | 처리 |
|---|---|
| `MAGIC` 불일치 | 다음 `MAGIC` 탐색 |
| `VERSION` 불일치 | Frame 폐기 + error count 증가 |
| `LENGTH > 256` | Frame 폐기 |
| CRC 불일치 | Frame 폐기 + `crc_error_count++` |
| 알 수 없는 `MSG_ID` | 정상 CRC Frame인 경우 `ERROR` 응답 |
| SEQ 누락 | `seq_gap_count` 증가 |
| Duplicate SEQ | duplicate count 증가 또는 log 기록 |

CRC가 잘못된 Frame에는 `ERROR` Response를 보내지 않는다.

이유:

> CRC가 틀렸다면 해당 Frame 내부의 `MSG_ID`, `SEQ`, `LENGTH`, `PAYLOAD` 자체를 신뢰할 수 없으므로, 손상된 Frame의 정보를 기반으로 Response를 생성하지 않는다.

---

## 13. Stream Parser

TCP와 UART는 모두 수신 함수 1회가 Application Frame 1개와 일치한다고 가정할 수 없다.

```text
TX Frame
AA 55 01 01 00 01 00 00 ...

TCP recv #1
AA 55 01 01

TCP recv #2
00 01 00 00 ...
```

따라서 TCP와 UART 모두 다음 개념의 Parser를 사용한다.

```text
WAIT_MAGIC
    ↓
READ_HEADER
    ↓
VERSION / LENGTH 검사
    ↓
READ_PAYLOAD
    ↓
READ_CRC
    ↓
CRC 검사
    ↓
PACKET_COMPLETE
```

Protocol parser는 가능한 범위에서 PC, G474, F429가 동일한 규칙을 공유하도록 구현한다.

---

## 14. Implementation Rules

### 14.1 C struct 메모리를 그대로 송신하지 않는다

다음과 같은 방식은 사용하지 않는다.

```c
send(&packet, sizeof(packet));
```

이유:

- 구조체 padding
- alignment
- CPU endian 차이

모든 Frame은 byte buffer에 명시적으로 serialize하고, 수신 측도 byte 단위로 deserialize한다.

### 14.2 Payload Length Bounds Check

수신한 `LENGTH`는 Payload buffer에 데이터를 복사하기 전에 반드시 검사한다.

```c
if (length > PROTOCOL_MAX_PAYLOAD)
{
    /* reject frame */
}
```

`PROTOCOL_MAX_PAYLOAD`는 256이다.

### 14.3 MAGIC Resynchronization

Payload 내부에도 우연히 `0xAA55`가 존재할 수 있다.

정상 Frame을 읽고 있는 동안에는 이미 `LENGTH`를 알고 있으므로 Payload 내부의 `0xAA55`를 새로운 Frame 시작으로 해석하지 않는다.

Frame 검증 실패 등으로 synchronization을 잃은 경우:

```text
MAGIC 재탐색
→ Header 검사
→ LENGTH 검사
→ CRC 검사
```

순서로 resynchronization한다.

Protocol V1에서는 별도의 Byte Stuffing, COBS, SLIP을 사용하지 않는다. 실제 검증에서 반복적인 resynchronization 문제가 확인되는 경우 개선 후보로 검토한다.

### 14.4 Valid Frame 기준

상위 Application Logic에는 검증이 완료된 Frame만 전달한다.

```text
MAGIC OK
VERSION OK
LENGTH OK
CRC OK
    ↓
Application Handler
```

---

## 15. G474 FreeRTOS Gateway 구조

초기 Gateway Task 구조는 다음과 같이 설계한다.

```text
        EthernetRxTask
               │
               ▼
          CommandQueue
               │
               ▼
          RS422TxTask


          RS422RxTask
               │
               ▼
         ResponseQueue
               │
               ▼
        EthernetTxTask
```

설계 의도:

1. Ethernet socket 처리와 UART/RS-422 처리를 분리한다.
2. 한 통신 구간의 blocking이 다른 통신 구간을 직접 지연시키지 않도록 한다.
3. Task 간 데이터 전달은 FreeRTOS Queue를 사용한다.
4. 공유 전역 데이터 사용을 최소화한다.

Task 우선순위는 하드웨어 bring-up 이후 실제 실행 주기와 blocking 특성을 확인한 뒤 결정한다.

---

## 16. 검증 계획과 Protocol 설계의 연결

| 설계 기능 | 검증 방법 |
|---|---|
| `CRC16` | 정상 Frame Payload 1 byte 고의 변조 후 reject 확인 |
| `SEQ` | 1 packet을 의도적으로 skip하여 누락 검출 확인 |
| `HEARTBEAT` | F429 또는 RS-422 연결 단절 후 timeout 확인 |
| Recovery | RS-422/Ethernet 재연결 후 `COMM_OK` 복구 확인 |
| TCP | Ethernet 명령/응답 End-to-End 왕복 |
| UDP | Telemetry 반복 송신 후 Packet Loss 측정 |
| `SEQ + PC Clock` | RTT / Jitter 측정 |

프로젝트 검증 흐름은 다음과 같다.

```text
요구사항
   ↓
Protocol 설계
   ↓
구현
   ↓
Fault Injection
   ↓
정량 검증
```

---

## 17. 정량 측정 정의

### 17.1 RTT

PC에서 Request 송신 직전 시각과 Response 수신 시각의 차이로 계산한다.

```text
RTT = response_receive_time - request_send_time
```

### 17.2 Jitter

Protocol V1 시험에서는 다음과 같이 정의한다.

```text
Jitter = 연속 RTT 차이의 절댓값 평균
```

사용하는 Jitter 정의는 시험 결과 문서에 명시한다.

### 17.3 UDP Packet Loss

Telemetry의 `SEQ` 및 송수신 개수를 이용하여 계산한다.

```text
Packet Loss (%) =
(TX Count - RX Count) / TX Count × 100
```

TCP는 자체적으로 재전송을 수행하므로 위 방식의 Application Packet Loss 측정 대상은 주로 UDP Telemetry로 한다.

---

## 18. Protocol V1 설계 요약

Protocol V1의 최종 Frame은 다음과 같다.

```text
MAGIC(2)
VERSION(1)
MSG_ID(1)
SEQ(2)
LENGTH(2)
PAYLOAD(0~256)
CRC16(2)
```

핵심 설계 설명:

> Ethernet과 RS-422 구간에서 동일한 Application Protocol을 사용하도록 설계하였다. TCP와 UART의 stream 특성을 고려하여 MAGIC과 LENGTH로 framing하고, MSG_ID로 메시지를 구분하며, SEQ로 요청-응답 매칭과 UDP Telemetry 누락을 검출한다. 또한 Gateway 이후 UART/RS-422 구간까지 동일한 Application Frame의 무결성을 확인하기 위해 CRC16을 사용한다. 명령/응답은 전달 신뢰성이 중요한 TCP, 주기 Telemetry는 최신성이 중요한 UDP로 분리한다.

Protocol V1은 실제 하드웨어 시험을 통해 검증하며, 변경은 측정 결과 또는 명확한 구현 요구가 확인될 때만 수행한다.
