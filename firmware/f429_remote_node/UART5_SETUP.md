# UART5 CubeMX configuration

`f429_remote_node.ioc` owns the UART5 hardware configuration:

- UART5 TX: PC12, alternate function AF8
- UART5 RX: PD2, alternate function AF8
- 115200 baud, 8 data bits, no parity, 1 stop bit
- TX and RX enabled, no hardware flow control, 16x oversampling
- UART5 global interrupt enabled at preemption priority 5

CubeMX-generated code owns `MX_UART5_Init()`, GPIO/clock/NVIC setup,
`UART5_IRQHandler()`, and the `huart5` handle. `App/Src/remote_node_port.c`
uses that handle and owns the interrupt receive callbacks, the 512-byte ring
buffer, and Remote Node application processing. It must not duplicate the
GPIO, peripheral, or NVIC initialization.

The F429 remains a FreeRTOS project. The Remote Node application runs in the
background of the existing `defaultTask`; CubeMX regeneration must preserve
the USER CODE calls to `remote_node_port_init()` and
`remote_node_port_process()`, plus the App/Common CubeIDE source and include
paths.
