# Gateway Test Log Format

Use UTF-8 CSV with a header row and one event per row. Timestamps are monotonic
milliseconds from the start of the test unless a test explicitly records an
additional wall-clock timestamp.

## Required columns

```csv
timestamp_ms,transport,direction,seq,msg_id,rtt_ms,status,crc_error_count,seq_gap_count
```

| Column | Format | Meaning |
|---|---|---|
| `timestamp_ms` | unsigned decimal | Monotonic time since test start |
| `transport` | `TCP`, `UDP`, or `RS422` | Transport observed at this log point |
| `direction` | `TX` or `RX` | Direction relative to the component producing the log |
| `seq` | 0..65535 | Protocol V1 sequence; blank if unavailable |
| `msg_id` | symbolic name preferred | `PING`, `PONG`, `READ_STATUS`, `STATUS_RESP`, `HEARTBEAT`, `TELEMETRY`, or `ERROR` |
| `rtt_ms` | decimal milliseconds | Present on a completed response event; otherwise blank |
| `status` | token | `OK`, `TIMEOUT`, `CRC_ERROR`, `BAD_LENGTH`, `UNKNOWN_MSG`, `MISSING`, `DUPLICATE`, or a documented extension |
| `crc_error_count` | unsigned decimal | Cumulative count at this event |
| `seq_gap_count` | unsigned decimal | Cumulative missing TELEMETRY sequence count |

## Example

```csv
timestamp_ms,transport,direction,seq,msg_id,rtt_ms,status,crc_error_count,seq_gap_count
12340,TCP,TX,152,PING,,OK,0,0
12343,TCP,RX,152,PONG,3.120,OK,0,0
12400,UDP,RX,18,TELEMETRY,,OK,0,0
12500,UDP,RX,20,TELEMETRY,,MISSING,0,1
```

## Logging rules

- Quote fields according to RFC 4180 if a value contains commas or quotes.
- Do not mix HEARTBEAT and TELEMETRY sequence-gap accounting; they have
  independent sequence spaces.
- Sequence arithmetic wraps from 65535 to 0.
- Log CRC-corrupt frames as rejected observations only. Do not infer or trust
  their message ID, sequence, length, or payload.
- Preserve raw CSV files even when a summarized result is produced.
