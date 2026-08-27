# Host test tools

Build from the repository root with MinGW GCC:

```powershell
gcc -c common\crc16.c common\protocol.c common\stream_parser.c -Icommon -std=c11 -Wall -Wextra -Wpedantic
g++ host\tcp_mock_server.cpp crc16.o protocol.o stream_parser.o -Icommon -std=c++17 -Wall -Wextra -Wpedantic -lws2_32 -o tcp_mock_server.exe
g++ host\tcp_client.cpp crc16.o protocol.o stream_parser.o -Icommon -std=c++17 -Wall -Wextra -Wpedantic -lws2_32 -o tcp_client.exe
g++ host\udp_mock_server.cpp crc16.o protocol.o -Icommon -std=c++17 -Wall -Wextra -Wpedantic -lws2_32 -o udp_mock_server.exe
g++ host\udp_client.cpp crc16.o protocol.o -Icommon -std=c++17 -Wall -Wextra -Wpedantic -lws2_32 -o udp_client.exe
g++ host\fault_client.cpp crc16.o protocol.o stream_parser.o -Icommon -std=c++17 -Wall -Wextra -Wpedantic -lws2_32 -o fault_client.exe
```

Examples:

```powershell
.\tcp_client.exe --ip 192.168.0.20 --port 5000 --count 1000 --timeout-ms 1000
.\udp_client.exe --port 5002 --expected 1000 --initial-timeout-ms 10000 --idle-timeout-ms 1500
.\fault_client.exe --ip 192.168.0.20 --port 5000 --test bad-crc
.\fault_client.exe --test unknown-msg --dry-run
```

`udp_client` accepts both the six-byte F429 TELEMETRY V1 payload and the
existing two-byte legacy mock payload so the localhost regression remains
usable.
