# F429 remote node application

`remote_node.c` contains hardware-independent Protocol V1 behavior. The UART5
HAL adapter and the interrupt-safe 512-byte RX ring are separate so the
application logic can be compiled and tested on a PC.

UART5 is initialized by CubeMX-generated HAL code as 115200 8N1 on PC12
(TX, AF8) and PD2 (RX, AF8), with the UART5 global interrupt enabled at
preemption priority 5. The adapter uses the generated `huart5` handle; it owns
only the application state and one-byte interrupt receive re-arming. Receive
callbacks put bytes into the ring. Parsing, CRC validation, responses, and
blocking transmit calls happen in task context.

The F429 project remains in its existing FreeRTOS environment. The Remote Node
application runs in the background of the existing `defaultTask`, which calls
`remote_node_port_process()` and then `osDelay(1)`. No FreeRTOS component is
removed, and no additional RTOS object or task is introduced.
