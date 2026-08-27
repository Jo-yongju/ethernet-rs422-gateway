# G474 Integration Test Plan

This plan is intentionally pending hardware validation. A test is complete
only when its measured result and raw log are attached.

## Common prerequisites

- Protocol V1 remains unchanged: `0xAA55`, version `0x01`, Big Endian,
  CRC-16/CCITT-FALSE.
- Record firmware/tool versions, board serial numbers, cabling, IP/port, baud
  rate, test count, and UTC/KST start time.
- Save raw events using `test_log_format.md` and preserve the TCP/UDP summary
  CSV produced by the host tools.

## Test 1 — PC ↔ G474 TCP

Purpose: verify the W5500 TCP connection and PING/PONG command path.

Measure RTT minimum, average, maximum, jitter, timeouts, and response sequence
mismatches. Run at least 1,000 requests after link establishment.

## Test 2 — G474 ↔ F429 RS-422

Purpose: verify Protocol V1 PING/PONG over UART5 and the four-wire RS-422 link.

Confirm echoed request sequence, CRC validity, UART settings (115200 8N1), and
F429 communication/error counters.

## Test 3 — End-to-End

Path: PC → Ethernet → W5500 → G474 → RS-422 → F429 → RS-422 → G474 → Ethernet
→ PC.

Measure end-to-end RTT minimum, average, maximum, jitter, and timeouts.

## Test 4 — UDP Telemetry

Path: F429 → RS-422 → G474 → UDP → PC.

Measure received packets, telemetry sequence gaps, duplicates, packet delivery
ratio, and packet loss. Verify that HEARTBEAT sequence numbers do not affect
TELEMETRY loss calculation.

## Test 5 — RS-422 Line Disconnect

Disconnect the line during steady traffic. Measure fault detection time from
physical disconnect to the first reported communication fault.

## Test 6 — RS-422 Reconnect

Reconnect without resetting either MCU. Measure recovery time to the first
valid request/response and telemetry frame.

## Test 7 — Ethernet Cable Disconnect

Disconnect Ethernet during TCP and UDP traffic. Measure disconnect detection
at the PC and G474 and record socket/error status.

## Test 8 — Ethernet Reconnect

Reconnect Ethernet without MCU reset. Measure link, DHCP/static network, TCP,
and telemetry recovery times as separate observations.

## Test 9 — CRC Corrupted Frame

Inject one intentional CRC error. Confirm no application response is generated
for that frame and verify the CRC error counter increases by exactly one.

## Test 10 — F429 Reset

Reset F429 during traffic. Measure communication fault detection and recovery
time. Confirm periodic sequence restart behavior is recorded rather than
misclassified silently.

## Test 11 — G474 Reset

Reset G474 during traffic. Measure PC disconnect detection and reconnect time,
then verify the RS-422 session and UDP telemetry recover.

## Test 12 — PC Program Restart

Restart each host tool while both MCUs continue running. Measure connection
recovery and confirm a mid-stream first telemetry sequence is handled.

## Pass criteria record

Numerical limits must be agreed before formal qualification. For each test,
record the proposed limit, measured value, PASS/FAIL, issue reference, and
whether the run used mock, bench, or final hardware.
