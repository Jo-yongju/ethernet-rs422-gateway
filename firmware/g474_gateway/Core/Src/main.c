/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "protocol.h"
#include "stream_parser.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  RS422_PING_TEST_NOT_RUN = 0,
  RS422_PING_TEST_PASS = 1,
  RS422_PING_TEST_ENCODE_FAILURE = -1,
  RS422_PING_TEST_TX_FAILURE = -2,
  RS422_PING_TEST_PONG_TIMEOUT = -3,
  RS422_PING_TEST_RX_FAILURE = -4
} rs422_ping_test_result_t;

typedef enum
{
  COMM_INIT = 0,
  COMM_OK,
  COMM_TIMEOUT
} rs422_comm_state_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RS422_BRINGUP_TEST_COUNT          100u
#define RS422_BRINGUP_TX_TIMEOUT_MS       100u
#define RS422_BRINGUP_PONG_TIMEOUT_MS     1000u
#define RS422_BRINGUP_INTER_TEST_DELAY_MS 20u
#define RS422_EVENT_RX                     (1UL << 0)
#define RS422_EVENT_TX_DONE                (1UL << 1)
#define RS422_EVENT_MASK                   (RS422_EVENT_RX | RS422_EVENT_TX_DONE)
#define RS422_RX_RING_CAPACITY             512u
#define RS422_RX_RING_MASK                 (RS422_RX_RING_CAPACITY - 1u)
#define RS422_CMD_QUEUE_DEPTH              8u
#define TCP_RESPONSE_QUEUE_DEPTH           8u
#define UDP_TELEMETRY_QUEUE_DEPTH          4u
#define HEALTH_TASK_PERIOD_MS              100u
#define RS422_COMM_TIMEOUT_MS              1500u
#define W5500_RESET_DELAY_MS               2u
#define W5500_SPI_TIMEOUT_MS               100u
#define W5500_VERSIONR_ADDRESS             0x0039u
#define W5500_PHYCFGR_ADDRESS              0x002eu
#define W5500_PHYCFGR_LNK                  0x01u
#define W5500_PHY_POLL_PERIOD_MS           100u
#define W5500_GAR_ADDRESS                  0x0001u
#define W5500_SUBR_ADDRESS                 0x0005u
#define W5500_SHAR_ADDRESS                 0x0009u
#define W5500_SIPR_ADDRESS                 0x000fu
#define W5500_GAR_LENGTH                   4u
#define W5500_SUBR_LENGTH                  4u
#define W5500_SHAR_LENGTH                  6u
#define W5500_SIPR_LENGTH                  4u
#define W5500_NET_MAX_REGISTER_LENGTH      W5500_SHAR_LENGTH
#define W5500_COMMON_HEADER_LENGTH         3u
/* BSB[4:0] = 00000 (Common), RWB = 0 (Read), OM[1:0] = 00 (VDM). */
#define W5500_COMMON_READ_VDM              0x00u
/* BSB[4:0] = 00000 (Common), RWB = 1 (Write), OM[1:0] = 00 (VDM). */
#define W5500_COMMON_WRITE_VDM             0x04u
#define W5500_SOCKET0_READ_VDM             0x08u
#define W5500_SOCKET0_WRITE_VDM            0x0cu
#define W5500_SOCKET0_MR_ADDRESS           0x0000u
#define W5500_SOCKET0_CR_ADDRESS           0x0001u
#define W5500_SOCKET0_IR_ADDRESS           0x0002u
#define W5500_SOCKET0_SR_ADDRESS           0x0003u
#define W5500_SOCKET0_PORT_ADDRESS         0x0004u
#define W5500_SOCKET0_TX_FSR_ADDRESS       0x0020u
#define W5500_SOCKET0_TX_WR_ADDRESS        0x0024u
#define W5500_SOCKET0_RX_RSR_ADDRESS       0x0026u
#define W5500_SOCKET0_RX_RD_ADDRESS        0x0028u
#define W5500_SOCKET0_MAX_DATA_LENGTH      2u
#define W5500_SOCKET0_TX_BUFFER_WRITE_VDM  0x14u
#define W5500_SOCKET0_RX_BUFFER_READ_VDM   0x18u
#define W5500_SOCKET0_TCP_MODE             0x01u
#define W5500_SOCKET0_OPEN_COMMAND         0x01u
#define W5500_SOCKET0_LISTEN_COMMAND       0x02u
#define W5500_SOCKET0_SEND_COMMAND         0x20u
#define W5500_SOCKET0_RECV_COMMAND         0x40u
#define W5500_SOCKET0_IR_SEND_OK           0x10u
#define W5500_SOCKET0_IR_TIMEOUT           0x08u
#define W5500_SOCKET0_STATUS_CLOSED        0x00u
#define W5500_SOCKET0_STATUS_INIT          0x13u
#define W5500_SOCKET0_STATUS_LISTEN        0x14u
#define W5500_SOCKET0_STATUS_ESTABLISHED   0x17u
#define W5500_TCP_SERVER_PORT              5000u
#define W5500_SOCKET_STATE_TIMEOUT_MS      1000u
#define W5500_SOCKET_STATE_POLL_MS         10u
#define W5500_TCP_STABLE_READ_ATTEMPTS     8u
#define W5500_TCP_RX_CHUNK_SIZE            64u
#define W5500_TCP_TX_FREE_TIMEOUT_MS       1000u
#define W5500_TCP_SEND_TIMEOUT_MS          1000u
#define W5500_TCP_IO_POLL_MS               10u
#define W5500_SOCKET_COMMAND_TIMEOUT_MS    100u
#define W5500_SOCKET_COMMAND_POLL_MS       1u
#define W5500_SOCKET1_READ_VDM             0x28u
#define W5500_SOCKET1_WRITE_VDM            0x2cu
#define W5500_SOCKET1_TX_BUFFER_WRITE_VDM  0x34u
#define W5500_SOCKET1_MR_ADDRESS           0x0000u
#define W5500_SOCKET1_CR_ADDRESS           0x0001u
#define W5500_SOCKET1_IR_ADDRESS           0x0002u
#define W5500_SOCKET1_SR_ADDRESS           0x0003u
#define W5500_SOCKET1_PORT_ADDRESS         0x0004u
#define W5500_SOCKET1_DIPR_ADDRESS         0x000cu
#define W5500_SOCKET1_DPORT_ADDRESS        0x0010u
#define W5500_SOCKET1_TX_FSR_ADDRESS       0x0020u
#define W5500_SOCKET1_TX_WR_ADDRESS        0x0024u
#define W5500_SOCKET1_MAX_DATA_LENGTH      4u
#define W5500_SOCKET1_UDP_MODE             0x02u
#define W5500_SOCKET1_OPEN_COMMAND         0x01u
#define W5500_SOCKET1_SEND_COMMAND         0x20u
#define W5500_SOCKET1_IR_SEND_OK           0x10u
#define W5500_SOCKET1_IR_TIMEOUT           0x08u
#define W5500_SOCKET1_STATUS_CLOSED        0x00u
#define W5500_SOCKET1_STATUS_UDP           0x22u
#define W5500_UDP_LOCAL_PORT               5001u
#define W5500_UDP_DESTINATION_PORT         5002u
#define W5500_UDP_PAYLOAD_LENGTH           6u
#define W5500_UDP_STABLE_READ_ATTEMPTS     8u
#define W5500_UDP_TX_FREE_TIMEOUT_MS       1000u
#define W5500_UDP_SEND_TIMEOUT_MS          1000u
#define W5500_UDP_IO_POLL_MS               10u
#define W5500_UDP_SPI_CHUNK_SIZE           64u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart4;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 128 * 4
};
/* Definitions for RS422Task */
osThreadId_t RS422TaskHandle;
const osThreadAttr_t RS422Task_attributes = {
  .name = "RS422Task",
  .priority = (osPriority_t) osPriorityHigh,
  .stack_size = 512 * 4
};
/* Definitions for EthernetTask */
osThreadId_t EthernetTaskHandle;
const osThreadAttr_t EthernetTask_attributes = {
  .name = "EthernetTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};
/* Definitions for HealthTask */
osThreadId_t HealthTaskHandle;
const osThreadAttr_t HealthTask_attributes = {
  .name = "HealthTask",
  .priority = (osPriority_t) osPriorityLow,
  .stack_size = 128 * 4
};
/* USER CODE BEGIN PV */
static volatile int32_t g_rs422_ping_test_result = RS422_PING_TEST_NOT_RUN;
static volatile uint8_t g_rs422_last_msg_id = 0u;
static volatile uint16_t g_rs422_last_seq = 0u;
static volatile uint32_t g_rs422_rx_frame_count = 0u;
static volatile uint32_t g_rs422_test_total = 0u;
static volatile uint32_t g_rs422_test_success = 0u;
static volatile uint32_t g_rs422_test_fail = 0u;
static volatile uint32_t g_rs422_rx_overflow_count = 0u;
static volatile uint32_t g_rs422_uart_rx_error_count = 0u;
static volatile uint32_t g_rs422_tx_busy = 0u;
static volatile uint32_t g_rs422_tx_complete_count = 0u;
static volatile uint32_t g_rs422_tx_busy_count = 0u;
static volatile uint32_t g_rs422_tx_error_count = 0u;
static volatile uint32_t g_rs422_tx_timeout_count = 0u;
static volatile uint32_t g_rs422_last_valid_frame_tick = 0u;
static volatile uint32_t g_rs422_has_valid_frame = 0u;
static volatile rs422_comm_state_t g_rs422_comm_state = COMM_INIT;
static volatile uint32_t g_rs422_comm_timeout_count = 0u;
static volatile uint32_t g_rs422_comm_recovery_count = 0u;
static volatile uint32_t g_gateway_queue_init_ok = 0u;
static volatile uint32_t g_gateway_free_heap_bytes = 0u;
static volatile uint32_t g_gateway_min_ever_free_heap_bytes = 0u;
static volatile uint8_t g_w5500_version = 0u;
/* -1: not completed; otherwise HAL_OK/ERROR/BUSY/TIMEOUT = 0/1/2/3. */
static volatile int32_t g_w5500_spi_status = -1;
/* Retain the last successful PHY sample on error; always check PHY SPI status. */
static volatile uint8_t g_w5500_phycfgr = 0u;
static volatile uint32_t g_w5500_link_up = 0u;
static volatile int32_t g_w5500_phy_spi_status = -1;
static volatile uint32_t g_w5500_net_config_ok = 0u;
static volatile uint32_t g_w5500_net_readback_ok = 0u;
static volatile int32_t g_w5500_net_write_status = -1;
static volatile int32_t g_w5500_net_read_status = -1;
static uint8_t g_w5500_gar_readback[W5500_GAR_LENGTH] = {0u};
static uint8_t g_w5500_subr_readback[W5500_SUBR_LENGTH] = {0u};
static uint8_t g_w5500_shar_readback[W5500_SHAR_LENGTH] = {0u};
static uint8_t g_w5500_sipr_readback[W5500_SIPR_LENGTH] = {0u};
static const uint8_t g_w5500_net_gateway[W5500_GAR_LENGTH] = {
  0u, 0u, 0u, 0u
};
static const uint8_t g_w5500_net_subnet[W5500_SUBR_LENGTH] = {
  255u, 255u, 255u, 0u
};
static const uint8_t g_w5500_net_mac[W5500_SHAR_LENGTH] = {
  0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u
};
static const uint8_t g_w5500_net_ip[W5500_SIPR_LENGTH] = {
  192u, 168u, 77u, 2u
};
static volatile uint32_t g_w5500_tcp_init_ok = 0u;
static volatile uint32_t g_w5500_tcp_listen_ok = 0u;
static volatile uint8_t g_w5500_socket0_status = W5500_SOCKET0_STATUS_CLOSED;
static volatile int32_t g_w5500_socket0_spi_status = -1;
static volatile uint32_t g_w5500_tcp_rx_bytes = 0u;
static volatile uint32_t g_w5500_tcp_tx_bytes = 0u;
static volatile uint32_t g_w5500_tcp_valid_frame_count = 0u;
static volatile uint32_t g_w5500_tcp_ping_count = 0u;
static volatile uint32_t g_w5500_tcp_pong_count = 0u;
static volatile uint8_t g_w5500_tcp_last_rx_msg_id = 0u;
static volatile uint16_t g_w5500_tcp_last_rx_seq = 0u;
static volatile uint16_t g_w5500_tcp_last_tx_seq = 0u;
static volatile uint32_t g_w5500_tcp_rx_error_count = 0u;
static volatile uint32_t g_w5500_tcp_tx_error_count = 0u;
static volatile int32_t g_w5500_tcp_last_rx_status = -1;
static volatile int32_t g_w5500_tcp_last_tx_status = -1;
static volatile uint32_t g_w5500_udp_open_ok = 0u;
static volatile uint8_t g_w5500_udp_socket1_status = W5500_SOCKET1_STATUS_CLOSED;
static volatile int32_t g_w5500_udp_socket1_spi_status = -1;
static volatile uint32_t g_w5500_udp_tx_packet_count = 0u;
static volatile uint32_t g_w5500_udp_tx_bytes = 0u;
static volatile uint32_t g_w5500_udp_tx_error_count = 0u;
static volatile uint16_t g_w5500_udp_last_tx_seq = 0u;
static volatile int32_t g_w5500_udp_last_tx_status = -1;
static volatile uint16_t g_w5500_udp_next_seq = 1u;
static osMessageQueueId_t rs422_cmd_queue = NULL;
static osMessageQueueId_t tcp_response_queue = NULL;
static osMessageQueueId_t udp_telemetry_queue = NULL;
static const osMessageQueueAttr_t rs422_cmd_queue_attributes = {
  .name = "rs422_cmd_queue"
};
static const osMessageQueueAttr_t tcp_response_queue_attributes = {
  .name = "tcp_response_queue"
};
static const osMessageQueueAttr_t udp_telemetry_queue_attributes = {
  .name = "udp_telemetry_queue"
};
static protocol_packet_t g_rs422_ping_packet;
static protocol_packet_t g_rs422_received_packet;
static stream_parser_t g_rs422_parser;
static uint8_t g_rs422_tx_frame[PROTOCOL_MAX_FRAME_SIZE];
static uint8_t g_uart4_rx_byte;
static uint8_t g_rs422_rx_ring[RS422_RX_RING_CAPACITY];
static volatile uint16_t g_rs422_rx_head = 0u;
static volatile uint16_t g_rs422_rx_tail = 0u;
static stream_parser_t g_tcp_parser;
static protocol_packet_t g_tcp_received_packet;
static protocol_packet_t g_tcp_pong_packet;
static uint8_t g_w5500_tcp_rx_chunk[W5500_TCP_RX_CHUNK_SIZE];
static uint8_t g_w5500_tcp_rx_dummy[W5500_TCP_RX_CHUNK_SIZE];
static uint8_t g_w5500_tcp_tx_frame[PROTOCOL_MAX_FRAME_SIZE];
static const uint8_t g_w5500_udp_destination_ip[4] = {
  192u, 168u, 77u, 1u
};
static protocol_packet_t g_udp_telemetry_packet;
static uint8_t g_w5500_udp_tx_frame[PROTOCOL_MAX_FRAME_SIZE];
static uint8_t g_w5500_udp_spi_discard[W5500_UDP_SPI_CHUNK_SIZE];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI2_Init(void);
static void MX_UART4_Init(void);
void StartDefaultTask(void *argument);
void StartRS422Task(void *argument);
void StartEthernetTask(void *argument);
void StartHealthTask(void *argument);

/* USER CODE BEGIN PFP */
static void rs422_rx_ring_push_from_isr(uint8_t byte);
static uint32_t rs422_rx_ring_pop(uint8_t *byte);
static uint32_t rs422_ms_to_kernel_ticks(uint32_t time_ms);
static uint32_t rs422_process_rx_buffer(
  uint16_t expected_pong_sequence,
  uint32_t match_pong);
static void rs422_handle_tx_done_event(uint32_t flags);
static int32_t rs422_wait_for_tx_complete(
  uint16_t expected_pong_sequence,
  uint32_t *matching_pong_received);
static int32_t rs422_ping_bringup_test(uint16_t sequence);
static int32_t rs422_ping_bringup_run(void);
static void w5500_hardware_reset(void);
static HAL_StatusTypeDef w5500_read_version(uint8_t *version);
static HAL_StatusTypeDef w5500_read_common_register(uint16_t address, uint8_t *value);
static HAL_StatusTypeDef w5500_write_common_registers(
  uint16_t address,
  const uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_read_common_registers(
  uint16_t address,
  uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_write_network_config(void);
static HAL_StatusTypeDef w5500_read_network_config(void);
static uint32_t w5500_network_readback_matches(void);
static HAL_StatusTypeDef w5500_socket0_write_registers(
  uint16_t address,
  const uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_socket0_read_register(
  uint16_t address,
  uint8_t *value);
static HAL_StatusTypeDef w5500_socket0_execute_command(uint8_t command);
static HAL_StatusTypeDef w5500_socket0_read_registers(
  uint16_t address,
  uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_socket0_read_u16(
  uint16_t address,
  uint16_t *value);
static HAL_StatusTypeDef w5500_socket0_read_stable_u16(
  uint16_t address,
  uint16_t *value);
static HAL_StatusTypeDef w5500_socket0_write_tx_buffer(
  uint16_t address,
  const uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_socket0_read_rx_buffer(
  uint16_t address,
  uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_tcp_wait_for_tx_free(
  uint16_t required_size);
static HAL_StatusTypeDef w5500_tcp_wait_for_send_result(void);
static HAL_StatusTypeDef w5500_tcp_send_frame(
  const uint8_t *frame,
  uint16_t frame_length);
static void w5500_tcp_handle_valid_frame(void);
static HAL_StatusTypeDef w5500_tcp_process_rx(void);
static HAL_StatusTypeDef w5500_wait_for_socket0_status(
  uint8_t expected_status,
  uint32_t timeout_ms);
static void w5500_start_tcp_server(void);
static HAL_StatusTypeDef w5500_socket1_write_registers(
  uint16_t address,
  const uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_socket1_read_register(
  uint16_t address,
  uint8_t *value);
static HAL_StatusTypeDef w5500_socket1_read_registers(
  uint16_t address,
  uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_socket1_read_u16(
  uint16_t address,
  uint16_t *value);
static HAL_StatusTypeDef w5500_socket1_read_stable_u16(
  uint16_t address,
  uint16_t *value);
static HAL_StatusTypeDef w5500_socket1_write_tx_buffer(
  uint16_t address,
  const uint8_t *data,
  uint16_t length);
static HAL_StatusTypeDef w5500_socket1_execute_command(uint8_t command);
static HAL_StatusTypeDef w5500_wait_for_socket1_status(
  uint8_t expected_status,
  uint32_t timeout_ms);
static HAL_StatusTypeDef w5500_udp_wait_for_tx_free(
  uint16_t required_size);
static HAL_StatusTypeDef w5500_udp_wait_for_send_result(void);
static HAL_StatusTypeDef w5500_udp_send_frame(
  const uint8_t *frame,
  uint16_t frame_length);
static HAL_StatusTypeDef w5500_start_udp_socket(void);
static uint32_t w5500_kernel_uptime_ms(void);
static void w5500_udp_send_telemetry(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void w5500_hardware_reset(void)
{
  /* Add one tick so tick phase cannot shorten either delay below 2 ms.
     Reset low must be >= 500 us; PLL lock takes at most 1 ms after release. */
  const uint32_t delay_ticks = (uint32_t)(
    (((uint64_t)W5500_RESET_DELAY_MS * osKernelGetTickFreq()) + 999u) / 1000u) + 1u;

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_RESET);
  (void)osDelay(delay_ticks);
  HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_SET);
  (void)osDelay(delay_ticks);
}

static HAL_StatusTypeDef w5500_read_version(uint8_t *version)
{
  return w5500_read_common_register(W5500_VERSIONR_ADDRESS, version);
}

static HAL_StatusTypeDef w5500_read_common_register(uint16_t address, uint8_t *value)
{
  const uint8_t tx[4] = {
    (uint8_t)(address >> 8),
    (uint8_t)(address & 0xffu),
    W5500_COMMON_READ_VDM,
    0u /* Dummy byte clocks the register data out on MISO. */
  };
  uint8_t rx[4] = {0u};

  /* Keep CS low for address, control and data in one full-duplex transfer. */
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2, tx, rx, (uint16_t)sizeof(tx), W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  if (status == HAL_OK)
  {
    *value = rx[3];
  }

  return status;
}

static HAL_StatusTypeDef w5500_write_common_registers(
  uint16_t address,
  const uint8_t *data,
  uint16_t length)
{
  uint8_t tx[W5500_COMMON_HEADER_LENGTH + W5500_NET_MAX_REGISTER_LENGTH];

  /* length is the Data Phase byte count; it excludes address and control. */
  if ((data == NULL) || (length == 0u) ||
      (length > W5500_NET_MAX_REGISTER_LENGTH))
  {
    return HAL_ERROR;
  }

  tx[0] = (uint8_t)(address >> 8);
  tx[1] = (uint8_t)(address & 0xffu);
  tx[2] = W5500_COMMON_WRITE_VDM;
  for (uint16_t index = 0u; index < length; index++)
  {
    tx[W5500_COMMON_HEADER_LENGTH + index] = data[index];
  }

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_Transmit(
    &hspi2,
    tx,
    (uint16_t)(W5500_COMMON_HEADER_LENGTH + length),
    W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  return status;
}

static HAL_StatusTypeDef w5500_read_common_registers(
  uint16_t address,
  uint8_t *data,
  uint16_t length)
{
  uint8_t tx[W5500_COMMON_HEADER_LENGTH + W5500_NET_MAX_REGISTER_LENGTH] = {0u};
  uint8_t rx[W5500_COMMON_HEADER_LENGTH + W5500_NET_MAX_REGISTER_LENGTH] = {0u};

  /* length is the Data Phase byte count; it excludes address and control. */
  if ((data == NULL) || (length == 0u) ||
      (length > W5500_NET_MAX_REGISTER_LENGTH))
  {
    return HAL_ERROR;
  }

  tx[0] = (uint8_t)(address >> 8);
  tx[1] = (uint8_t)(address & 0xffu);
  tx[2] = W5500_COMMON_READ_VDM;

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2,
    tx,
    rx,
    (uint16_t)(W5500_COMMON_HEADER_LENGTH + length),
    W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  if (status == HAL_OK)
  {
    for (uint16_t index = 0u; index < length; index++)
    {
      data[index] = rx[W5500_COMMON_HEADER_LENGTH + index];
    }
  }

  return status;
}

static HAL_StatusTypeDef w5500_write_network_config(void)
{
  HAL_StatusTypeDef status = w5500_write_common_registers(
    W5500_GAR_ADDRESS, g_w5500_net_gateway, W5500_GAR_LENGTH);

  if (status == HAL_OK)
  {
    status = w5500_write_common_registers(
      W5500_SUBR_ADDRESS, g_w5500_net_subnet, W5500_SUBR_LENGTH);
  }
  if (status == HAL_OK)
  {
    status = w5500_write_common_registers(
      W5500_SHAR_ADDRESS, g_w5500_net_mac, W5500_SHAR_LENGTH);
  }
  if (status == HAL_OK)
  {
    status = w5500_write_common_registers(
      W5500_SIPR_ADDRESS, g_w5500_net_ip, W5500_SIPR_LENGTH);
  }

  return status;
}

static HAL_StatusTypeDef w5500_read_network_config(void)
{
  HAL_StatusTypeDef status = w5500_read_common_registers(
    W5500_GAR_ADDRESS, g_w5500_gar_readback, W5500_GAR_LENGTH);

  if (status == HAL_OK)
  {
    status = w5500_read_common_registers(
      W5500_SUBR_ADDRESS, g_w5500_subr_readback, W5500_SUBR_LENGTH);
  }
  if (status == HAL_OK)
  {
    status = w5500_read_common_registers(
      W5500_SHAR_ADDRESS, g_w5500_shar_readback, W5500_SHAR_LENGTH);
  }
  if (status == HAL_OK)
  {
    status = w5500_read_common_registers(
      W5500_SIPR_ADDRESS, g_w5500_sipr_readback, W5500_SIPR_LENGTH);
  }

  return status;
}

static uint32_t w5500_network_readback_matches(void)
{
  for (uint16_t index = 0u; index < W5500_GAR_LENGTH; index++)
  {
    if ((g_w5500_gar_readback[index] != g_w5500_net_gateway[index]) ||
        (g_w5500_subr_readback[index] != g_w5500_net_subnet[index]) ||
        (g_w5500_sipr_readback[index] != g_w5500_net_ip[index]))
    {
      return 0u;
    }
  }

  for (uint16_t index = 0u; index < W5500_SHAR_LENGTH; index++)
  {
    if (g_w5500_shar_readback[index] != g_w5500_net_mac[index])
    {
      return 0u;
    }
  }

  return 1u;
}

static HAL_StatusTypeDef w5500_socket0_write_registers(
  uint16_t address,
  const uint8_t *data,
  uint16_t length)
{
  uint8_t tx[W5500_COMMON_HEADER_LENGTH + W5500_SOCKET0_MAX_DATA_LENGTH];

  if ((data == NULL) || (length == 0u) ||
      (length > W5500_SOCKET0_MAX_DATA_LENGTH))
  {
    return HAL_ERROR;
  }

  tx[0] = (uint8_t)(address >> 8);
  tx[1] = (uint8_t)(address & 0xffu);
  tx[2] = W5500_SOCKET0_WRITE_VDM;
  for (uint16_t index = 0u; index < length; index++)
  {
    tx[W5500_COMMON_HEADER_LENGTH + index] = data[index];
  }

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_Transmit(
    &hspi2,
    tx,
    (uint16_t)(W5500_COMMON_HEADER_LENGTH + length),
    W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  return status;
}

static HAL_StatusTypeDef w5500_socket0_read_register(
  uint16_t address,
  uint8_t *value)
{
  const uint8_t tx[4] = {
    (uint8_t)(address >> 8),
    (uint8_t)(address & 0xffu),
    W5500_SOCKET0_READ_VDM,
    0u
  };
  uint8_t rx[4] = {0u};

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2, tx, rx, (uint16_t)sizeof(tx), W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  if (status == HAL_OK)
  {
    *value = rx[3];
  }

  return status;
}

static HAL_StatusTypeDef w5500_socket0_execute_command(uint8_t command)
{
  HAL_StatusTypeDef status = w5500_socket0_write_registers(
    W5500_SOCKET0_CR_ADDRESS, &command, 1u);
  if (status != HAL_OK)
  {
    return status;
  }

  const uint32_t timeout_ticks =
    rs422_ms_to_kernel_ticks(W5500_SOCKET_COMMAND_TIMEOUT_MS);
  const uint32_t poll_ticks =
    rs422_ms_to_kernel_ticks(W5500_SOCKET_COMMAND_POLL_MS);
  const uint32_t start_tick = osKernelGetTickCount();

  for (;;)
  {
    uint8_t command_register = command;
    status = w5500_socket0_read_register(
      W5500_SOCKET0_CR_ADDRESS, &command_register);
    if (status != HAL_OK)
    {
      return status;
    }
    if (command_register == 0u)
    {
      return HAL_OK;
    }
    if ((uint32_t)(osKernelGetTickCount() - start_tick) >= timeout_ticks)
    {
      return HAL_TIMEOUT;
    }

    (void)osDelay(poll_ticks);
  }
}

static HAL_StatusTypeDef w5500_socket0_read_registers(
  uint16_t address,
  uint8_t *data,
  uint16_t length)
{
  uint8_t tx[W5500_COMMON_HEADER_LENGTH + W5500_SOCKET0_MAX_DATA_LENGTH] = {0u};
  uint8_t rx[W5500_COMMON_HEADER_LENGTH + W5500_SOCKET0_MAX_DATA_LENGTH] = {0u};

  if ((data == NULL) || (length == 0u) ||
      (length > W5500_SOCKET0_MAX_DATA_LENGTH))
  {
    return HAL_ERROR;
  }

  tx[0] = (uint8_t)(address >> 8);
  tx[1] = (uint8_t)(address & 0xffu);
  tx[2] = W5500_SOCKET0_READ_VDM;

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2,
    tx,
    rx,
    (uint16_t)(W5500_COMMON_HEADER_LENGTH + length),
    W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  if (status == HAL_OK)
  {
    for (uint16_t index = 0u; index < length; index++)
    {
      data[index] = rx[W5500_COMMON_HEADER_LENGTH + index];
    }
  }

  return status;
}

static HAL_StatusTypeDef w5500_socket0_read_u16(
  uint16_t address,
  uint16_t *value)
{
  uint8_t data[2] = {0u};

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  const HAL_StatusTypeDef status = w5500_socket0_read_registers(
    address, data, (uint16_t)sizeof(data));
  if (status == HAL_OK)
  {
    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
  }

  return status;
}

static HAL_StatusTypeDef w5500_socket0_read_stable_u16(
  uint16_t address,
  uint16_t *value)
{
  uint16_t previous_value = 0u;
  uint16_t current_value = 0u;

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  for (uint32_t attempt = 0u;
       attempt < W5500_TCP_STABLE_READ_ATTEMPTS;
       attempt++)
  {
    const HAL_StatusTypeDef status =
      w5500_socket0_read_u16(address, &current_value);
    if (status != HAL_OK)
    {
      return status;
    }

    if ((attempt != 0u) && (current_value == previous_value))
    {
      *value = current_value;
      return HAL_OK;
    }

    previous_value = current_value;
  }

  /* Do not update the caller's value when no consecutive reads are stable. */
  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef w5500_socket0_write_tx_buffer(
  uint16_t address,
  const uint8_t *data,
  uint16_t length)
{
  const uint8_t header[W5500_COMMON_HEADER_LENGTH] = {
    (uint8_t)(address >> 8),
    (uint8_t)(address & 0xffu),
    W5500_SOCKET0_TX_BUFFER_WRITE_VDM
  };
  uint8_t header_rx[W5500_COMMON_HEADER_LENGTH] = {0u};

  if ((data == NULL) || (length == 0u) ||
      (length > PROTOCOL_MAX_FRAME_SIZE))
  {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2,
    header,
    header_rx,
    (uint16_t)sizeof(header),
    W5500_SPI_TIMEOUT_MS);
  uint16_t offset = 0u;
  while ((status == HAL_OK) && (offset < length))
  {
    const uint16_t remaining = (uint16_t)(length - offset);
    const uint16_t chunk_length = (remaining > W5500_TCP_RX_CHUNK_SIZE) ?
      W5500_TCP_RX_CHUNK_SIZE : remaining;
    status = HAL_SPI_TransmitReceive(
      &hspi2,
      &data[offset],
      g_w5500_tcp_rx_dummy,
      chunk_length,
      W5500_SPI_TIMEOUT_MS);
    offset = (uint16_t)(offset + chunk_length);
  }
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  return status;
}

static HAL_StatusTypeDef w5500_socket0_read_rx_buffer(
  uint16_t address,
  uint8_t *data,
  uint16_t length)
{
  const uint8_t header[W5500_COMMON_HEADER_LENGTH] = {
    (uint8_t)(address >> 8),
    (uint8_t)(address & 0xffu),
    W5500_SOCKET0_RX_BUFFER_READ_VDM
  };
  uint8_t header_rx[W5500_COMMON_HEADER_LENGTH] = {0u};

  if ((data == NULL) || (length == 0u) ||
      (length > W5500_TCP_RX_CHUNK_SIZE))
  {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2,
    header,
    header_rx,
    (uint16_t)sizeof(header),
    W5500_SPI_TIMEOUT_MS);
  if (status == HAL_OK)
  {
    status = HAL_SPI_TransmitReceive(
      &hspi2,
      g_w5500_tcp_rx_dummy,
      data,
      length,
      W5500_SPI_TIMEOUT_MS);
  }
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  return status;
}

static HAL_StatusTypeDef w5500_tcp_wait_for_tx_free(
  uint16_t required_size)
{
  const uint32_t timeout_ticks =
    rs422_ms_to_kernel_ticks(W5500_TCP_TX_FREE_TIMEOUT_MS);
  const uint32_t poll_ticks =
    rs422_ms_to_kernel_ticks(W5500_TCP_IO_POLL_MS);
  const uint32_t start_tick = osKernelGetTickCount();

  for (;;)
  {
    uint16_t free_size = 0u;
    const HAL_StatusTypeDef status = w5500_socket0_read_stable_u16(
      W5500_SOCKET0_TX_FSR_ADDRESS, &free_size);
    if (status != HAL_OK)
    {
      return status;
    }
    if (free_size >= required_size)
    {
      return HAL_OK;
    }
    if ((uint32_t)(osKernelGetTickCount() - start_tick) >= timeout_ticks)
    {
      return HAL_TIMEOUT;
    }

    (void)osDelay(poll_ticks);
  }
}

static HAL_StatusTypeDef w5500_tcp_wait_for_send_result(void)
{
  const uint32_t timeout_ticks =
    rs422_ms_to_kernel_ticks(W5500_TCP_SEND_TIMEOUT_MS);
  const uint32_t poll_ticks =
    rs422_ms_to_kernel_ticks(W5500_TCP_IO_POLL_MS);
  const uint32_t start_tick = osKernelGetTickCount();

  for (;;)
  {
    uint8_t socket_interrupt = 0u;
    HAL_StatusTypeDef status = w5500_socket0_read_register(
      W5500_SOCKET0_IR_ADDRESS, &socket_interrupt);
    if (status != HAL_OK)
    {
      return status;
    }

    const uint8_t send_result = (uint8_t)(socket_interrupt &
      (W5500_SOCKET0_IR_SEND_OK | W5500_SOCKET0_IR_TIMEOUT));
    if (send_result != 0u)
    {
      status = w5500_socket0_write_registers(
        W5500_SOCKET0_IR_ADDRESS, &send_result, 1u);
      if (status != HAL_OK)
      {
        return status;
      }

      if ((send_result & W5500_SOCKET0_IR_TIMEOUT) != 0u)
      {
        return HAL_TIMEOUT;
      }
      return HAL_OK;
    }

    if ((uint32_t)(osKernelGetTickCount() - start_tick) >= timeout_ticks)
    {
      return HAL_TIMEOUT;
    }

    (void)osDelay(poll_ticks);
  }
}

static HAL_StatusTypeDef w5500_tcp_send_frame(
  const uint8_t *frame,
  uint16_t frame_length)
{
  if ((frame == NULL) || (frame_length == 0u) ||
      (frame_length > PROTOCOL_MAX_FRAME_SIZE))
  {
    return HAL_ERROR;
  }

  HAL_StatusTypeDef status = w5500_tcp_wait_for_tx_free(frame_length);
  if (status != HAL_OK)
  {
    return status;
  }

  uint16_t tx_write_pointer = 0u;
  status = w5500_socket0_read_u16(
    W5500_SOCKET0_TX_WR_ADDRESS, &tx_write_pointer);
  if (status != HAL_OK)
  {
    return status;
  }

  status = w5500_socket0_write_tx_buffer(
    tx_write_pointer, frame, frame_length);
  if (status != HAL_OK)
  {
    return status;
  }

  const uint16_t new_tx_write_pointer =
    (uint16_t)(tx_write_pointer + frame_length);
  const uint8_t tx_write_pointer_bytes[2] = {
    (uint8_t)(new_tx_write_pointer >> 8),
    (uint8_t)(new_tx_write_pointer & 0xffu)
  };
  status = w5500_socket0_write_registers(
    W5500_SOCKET0_TX_WR_ADDRESS,
    tx_write_pointer_bytes,
    (uint16_t)sizeof(tx_write_pointer_bytes));
  if (status != HAL_OK)
  {
    return status;
  }

  /* Sn_IR is write-one-to-clear. Clear only SEND result bits before SEND. */
  const uint8_t stale_send_bits =
    W5500_SOCKET0_IR_SEND_OK | W5500_SOCKET0_IR_TIMEOUT;
  status = w5500_socket0_write_registers(
    W5500_SOCKET0_IR_ADDRESS, &stale_send_bits, 1u);
  if (status != HAL_OK)
  {
    return status;
  }

  status = w5500_socket0_execute_command(W5500_SOCKET0_SEND_COMMAND);
  if (status != HAL_OK)
  {
    return status;
  }

  return w5500_tcp_wait_for_send_result();
}

static void w5500_tcp_handle_valid_frame(void)
{
  g_w5500_tcp_valid_frame_count++;
  g_w5500_tcp_last_rx_msg_id = g_tcp_received_packet.msg_id;
  g_w5500_tcp_last_rx_seq = g_tcp_received_packet.seq;

  if (g_tcp_received_packet.msg_id != MSG_PING)
  {
    return;
  }

  g_w5500_tcp_ping_count++;
  g_tcp_pong_packet.version = PROTOCOL_VERSION;
  g_tcp_pong_packet.msg_id = MSG_PONG;
  g_tcp_pong_packet.seq = g_tcp_received_packet.seq;
  g_tcp_pong_packet.length = 0u;

  size_t encoded_length = 0u;
  const protocol_result_t encode_status = protocol_encode(
    &g_tcp_pong_packet,
    g_w5500_tcp_tx_frame,
    sizeof(g_w5500_tcp_tx_frame),
    &encoded_length);
  if ((encode_status != PROTO_OK) || (encoded_length == 0u) ||
      (encoded_length > UINT16_MAX))
  {
    g_w5500_tcp_last_tx_status = (int32_t)encode_status;
    g_w5500_tcp_tx_error_count++;
    return;
  }

  const HAL_StatusTypeDef send_status = w5500_tcp_send_frame(
    g_w5500_tcp_tx_frame, (uint16_t)encoded_length);
  g_w5500_tcp_last_tx_status = (int32_t)send_status;
  if (send_status == HAL_OK)
  {
    g_w5500_tcp_tx_bytes += (uint32_t)encoded_length;
    g_w5500_tcp_pong_count++;
    g_w5500_tcp_last_tx_seq = g_tcp_pong_packet.seq;
  }
  else
  {
    g_w5500_tcp_tx_error_count++;
  }
}

static HAL_StatusTypeDef w5500_tcp_process_rx(void)
{
  uint16_t received_size = 0u;
  HAL_StatusTypeDef status = w5500_socket0_read_stable_u16(
    W5500_SOCKET0_RX_RSR_ADDRESS, &received_size);
  if ((status != HAL_OK) || (received_size == 0u))
  {
    return status;
  }

  const uint16_t read_length = (received_size > W5500_TCP_RX_CHUNK_SIZE) ?
    W5500_TCP_RX_CHUNK_SIZE : received_size;
  uint16_t rx_read_pointer = 0u;
  status = w5500_socket0_read_u16(
    W5500_SOCKET0_RX_RD_ADDRESS, &rx_read_pointer);
  if (status != HAL_OK)
  {
    return status;
  }

  status = w5500_socket0_read_rx_buffer(
    rx_read_pointer, g_w5500_tcp_rx_chunk, read_length);
  if (status != HAL_OK)
  {
    return status;
  }

  const uint16_t new_rx_read_pointer =
    (uint16_t)(rx_read_pointer + read_length);
  const uint8_t rx_read_pointer_bytes[2] = {
    (uint8_t)(new_rx_read_pointer >> 8),
    (uint8_t)(new_rx_read_pointer & 0xffu)
  };
  status = w5500_socket0_write_registers(
    W5500_SOCKET0_RX_RD_ADDRESS,
    rx_read_pointer_bytes,
    (uint16_t)sizeof(rx_read_pointer_bytes));
  if (status != HAL_OK)
  {
    return status;
  }

  status = w5500_socket0_execute_command(W5500_SOCKET0_RECV_COMMAND);
  if (status != HAL_OK)
  {
    return status;
  }

  g_w5500_tcp_rx_bytes += read_length;
  for (uint16_t index = 0u; index < read_length; index++)
  {
    const stream_parser_event_t parser_event = stream_parser_feed_byte(
      &g_tcp_parser,
      g_w5500_tcp_rx_chunk[index],
      &g_tcp_received_packet);
    if (parser_event == STREAM_EVENT_FRAME)
    {
      w5500_tcp_handle_valid_frame();
    }
    else if (parser_event == STREAM_EVENT_ERROR)
    {
      g_w5500_tcp_rx_error_count++;
    }
  }

  return HAL_OK;
}

static HAL_StatusTypeDef w5500_wait_for_socket0_status(
  uint8_t expected_status,
  uint32_t timeout_ms)
{
  const uint32_t timeout_ticks = rs422_ms_to_kernel_ticks(timeout_ms);
  const uint32_t poll_ticks =
    rs422_ms_to_kernel_ticks(W5500_SOCKET_STATE_POLL_MS);
  const uint32_t start_tick = osKernelGetTickCount();

  for (;;)
  {
    uint8_t socket_status = W5500_SOCKET0_STATUS_CLOSED;
    const HAL_StatusTypeDef spi_status = w5500_socket0_read_register(
      W5500_SOCKET0_SR_ADDRESS, &socket_status);
    g_w5500_socket0_spi_status = (int32_t)spi_status;

    if (spi_status != HAL_OK)
    {
      return spi_status;
    }

    g_w5500_socket0_status = socket_status;
    if (socket_status == expected_status)
    {
      return HAL_OK;
    }

    if ((uint32_t)(osKernelGetTickCount() - start_tick) >= timeout_ticks)
    {
      return HAL_TIMEOUT;
    }

    (void)osDelay(poll_ticks);
  }
}

static void w5500_start_tcp_server(void)
{
  const uint8_t tcp_mode = W5500_SOCKET0_TCP_MODE;
  const uint8_t server_port[2] = {
    (uint8_t)(W5500_TCP_SERVER_PORT >> 8),
    (uint8_t)(W5500_TCP_SERVER_PORT & 0xffu)
  };
  const uint8_t open_command = W5500_SOCKET0_OPEN_COMMAND;
  const uint8_t listen_command = W5500_SOCKET0_LISTEN_COMMAND;

  HAL_StatusTypeDef status = w5500_socket0_write_registers(
    W5500_SOCKET0_MR_ADDRESS, &tcp_mode, 1u);
  g_w5500_socket0_spi_status = (int32_t)status;

  if (status == HAL_OK)
  {
    status = w5500_socket0_write_registers(
      W5500_SOCKET0_PORT_ADDRESS, server_port, (uint16_t)sizeof(server_port));
    g_w5500_socket0_spi_status = (int32_t)status;
  }
  if (status == HAL_OK)
  {
    status = w5500_socket0_write_registers(
      W5500_SOCKET0_CR_ADDRESS, &open_command, 1u);
    g_w5500_socket0_spi_status = (int32_t)status;
  }
  if (status == HAL_OK)
  {
    status = w5500_wait_for_socket0_status(
      W5500_SOCKET0_STATUS_INIT, W5500_SOCKET_STATE_TIMEOUT_MS);
  }
  if (status != HAL_OK)
  {
    return;
  }

  g_w5500_tcp_init_ok = 1u;
  status = w5500_socket0_write_registers(
    W5500_SOCKET0_CR_ADDRESS, &listen_command, 1u);
  g_w5500_socket0_spi_status = (int32_t)status;

  if (status == HAL_OK)
  {
    status = w5500_wait_for_socket0_status(
      W5500_SOCKET0_STATUS_LISTEN, W5500_SOCKET_STATE_TIMEOUT_MS);
  }
  if (status == HAL_OK)
  {
    g_w5500_tcp_listen_ok = 1u;
  }
}

static HAL_StatusTypeDef w5500_socket1_write_registers(
  uint16_t address,
  const uint8_t *data,
  uint16_t length)
{
  uint8_t tx[W5500_COMMON_HEADER_LENGTH + W5500_SOCKET1_MAX_DATA_LENGTH];

  if ((data == NULL) || (length == 0u) ||
      (length > W5500_SOCKET1_MAX_DATA_LENGTH))
  {
    return HAL_ERROR;
  }

  tx[0] = (uint8_t)(address >> 8);
  tx[1] = (uint8_t)(address & 0xffu);
  tx[2] = W5500_SOCKET1_WRITE_VDM;
  for (uint16_t index = 0u; index < length; index++)
  {
    tx[W5500_COMMON_HEADER_LENGTH + index] = data[index];
  }

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_Transmit(
    &hspi2,
    tx,
    (uint16_t)(W5500_COMMON_HEADER_LENGTH + length),
    W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  return status;
}

static HAL_StatusTypeDef w5500_socket1_read_register(
  uint16_t address,
  uint8_t *value)
{
  const uint8_t tx[4] = {
    (uint8_t)(address >> 8),
    (uint8_t)(address & 0xffu),
    W5500_SOCKET1_READ_VDM,
    0u
  };
  uint8_t rx[4] = {0u};

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2, tx, rx, (uint16_t)sizeof(tx), W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  if (status == HAL_OK)
  {
    *value = rx[3];
  }

  return status;
}

static HAL_StatusTypeDef w5500_socket1_read_registers(
  uint16_t address,
  uint8_t *data,
  uint16_t length)
{
  uint8_t tx[W5500_COMMON_HEADER_LENGTH + W5500_SOCKET1_MAX_DATA_LENGTH] = {0u};
  uint8_t rx[W5500_COMMON_HEADER_LENGTH + W5500_SOCKET1_MAX_DATA_LENGTH] = {0u};

  if ((data == NULL) || (length == 0u) ||
      (length > W5500_SOCKET1_MAX_DATA_LENGTH))
  {
    return HAL_ERROR;
  }

  tx[0] = (uint8_t)(address >> 8);
  tx[1] = (uint8_t)(address & 0xffu);
  tx[2] = W5500_SOCKET1_READ_VDM;

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  const HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2,
    tx,
    rx,
    (uint16_t)(W5500_COMMON_HEADER_LENGTH + length),
    W5500_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  if (status == HAL_OK)
  {
    for (uint16_t index = 0u; index < length; index++)
    {
      data[index] = rx[W5500_COMMON_HEADER_LENGTH + index];
    }
  }

  return status;
}

static HAL_StatusTypeDef w5500_socket1_read_u16(
  uint16_t address,
  uint16_t *value)
{
  uint8_t data[2] = {0u};

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  const HAL_StatusTypeDef status = w5500_socket1_read_registers(
    address, data, (uint16_t)sizeof(data));
  if (status == HAL_OK)
  {
    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
  }

  return status;
}

static HAL_StatusTypeDef w5500_socket1_read_stable_u16(
  uint16_t address,
  uint16_t *value)
{
  uint16_t previous_value = 0u;
  uint16_t current_value = 0u;

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  for (uint32_t attempt = 0u;
       attempt < W5500_UDP_STABLE_READ_ATTEMPTS;
       attempt++)
  {
    const HAL_StatusTypeDef status =
      w5500_socket1_read_u16(address, &current_value);
    if (status != HAL_OK)
    {
      return status;
    }

    if ((attempt != 0u) && (current_value == previous_value))
    {
      *value = current_value;
      return HAL_OK;
    }

    previous_value = current_value;
  }

  /* Leave the caller's value unchanged if no consecutive samples agree. */
  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef w5500_socket1_write_tx_buffer(
  uint16_t address,
  const uint8_t *data,
  uint16_t length)
{
  const uint8_t header[W5500_COMMON_HEADER_LENGTH] = {
    (uint8_t)(address >> 8),
    (uint8_t)(address & 0xffu),
    W5500_SOCKET1_TX_BUFFER_WRITE_VDM
  };
  uint8_t header_rx[W5500_COMMON_HEADER_LENGTH] = {0u};

  if ((data == NULL) || (length == 0u) ||
      (length > PROTOCOL_MAX_FRAME_SIZE))
  {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
    &hspi2,
    header,
    header_rx,
    (uint16_t)sizeof(header),
    W5500_SPI_TIMEOUT_MS);
  uint16_t offset = 0u;
  while ((status == HAL_OK) && (offset < length))
  {
    const uint16_t remaining = (uint16_t)(length - offset);
    const uint16_t chunk_length = (remaining > W5500_UDP_SPI_CHUNK_SIZE) ?
      W5500_UDP_SPI_CHUNK_SIZE : remaining;
    status = HAL_SPI_TransmitReceive(
      &hspi2,
      &data[offset],
      g_w5500_udp_spi_discard,
      chunk_length,
      W5500_SPI_TIMEOUT_MS);
    offset = (uint16_t)(offset + chunk_length);
  }
  HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);

  return status;
}

static HAL_StatusTypeDef w5500_socket1_execute_command(uint8_t command)
{
  HAL_StatusTypeDef status = w5500_socket1_write_registers(
    W5500_SOCKET1_CR_ADDRESS, &command, 1u);
  if (status != HAL_OK)
  {
    return status;
  }

  const uint32_t timeout_ticks =
    rs422_ms_to_kernel_ticks(W5500_SOCKET_COMMAND_TIMEOUT_MS);
  const uint32_t poll_ticks =
    rs422_ms_to_kernel_ticks(W5500_SOCKET_COMMAND_POLL_MS);
  const uint32_t start_tick = osKernelGetTickCount();

  for (;;)
  {
    uint8_t command_register = command;
    status = w5500_socket1_read_register(
      W5500_SOCKET1_CR_ADDRESS, &command_register);
    if (status != HAL_OK)
    {
      return status;
    }
    if (command_register == 0u)
    {
      return HAL_OK;
    }
    if ((uint32_t)(osKernelGetTickCount() - start_tick) >= timeout_ticks)
    {
      return HAL_TIMEOUT;
    }

    (void)osDelay(poll_ticks);
  }
}

static HAL_StatusTypeDef w5500_wait_for_socket1_status(
  uint8_t expected_status,
  uint32_t timeout_ms)
{
  const uint32_t timeout_ticks = rs422_ms_to_kernel_ticks(timeout_ms);
  const uint32_t poll_ticks =
    rs422_ms_to_kernel_ticks(W5500_SOCKET_STATE_POLL_MS);
  const uint32_t start_tick = osKernelGetTickCount();

  for (;;)
  {
    uint8_t socket_status = W5500_SOCKET1_STATUS_CLOSED;
    const HAL_StatusTypeDef spi_status = w5500_socket1_read_register(
      W5500_SOCKET1_SR_ADDRESS, &socket_status);
    g_w5500_udp_socket1_spi_status = (int32_t)spi_status;

    if (spi_status != HAL_OK)
    {
      return spi_status;
    }

    g_w5500_udp_socket1_status = socket_status;
    if (socket_status == expected_status)
    {
      return HAL_OK;
    }
    if ((uint32_t)(osKernelGetTickCount() - start_tick) >= timeout_ticks)
    {
      return HAL_TIMEOUT;
    }

    (void)osDelay(poll_ticks);
  }
}

static HAL_StatusTypeDef w5500_udp_wait_for_tx_free(
  uint16_t required_size)
{
  const uint32_t timeout_ticks =
    rs422_ms_to_kernel_ticks(W5500_UDP_TX_FREE_TIMEOUT_MS);
  const uint32_t poll_ticks =
    rs422_ms_to_kernel_ticks(W5500_UDP_IO_POLL_MS);
  const uint32_t start_tick = osKernelGetTickCount();

  for (;;)
  {
    uint16_t free_size = 0u;
    const HAL_StatusTypeDef status = w5500_socket1_read_stable_u16(
      W5500_SOCKET1_TX_FSR_ADDRESS, &free_size);
    if (status != HAL_OK)
    {
      return status;
    }
    if (free_size >= required_size)
    {
      return HAL_OK;
    }
    if ((uint32_t)(osKernelGetTickCount() - start_tick) >= timeout_ticks)
    {
      return HAL_TIMEOUT;
    }

    (void)osDelay(poll_ticks);
  }
}

static HAL_StatusTypeDef w5500_udp_wait_for_send_result(void)
{
  const uint32_t timeout_ticks =
    rs422_ms_to_kernel_ticks(W5500_UDP_SEND_TIMEOUT_MS);
  const uint32_t poll_ticks =
    rs422_ms_to_kernel_ticks(W5500_UDP_IO_POLL_MS);
  const uint32_t start_tick = osKernelGetTickCount();

  for (;;)
  {
    uint8_t socket_interrupt = 0u;
    HAL_StatusTypeDef status = w5500_socket1_read_register(
      W5500_SOCKET1_IR_ADDRESS, &socket_interrupt);
    if (status != HAL_OK)
    {
      return status;
    }

    const uint8_t send_result = (uint8_t)(socket_interrupt &
      (W5500_SOCKET1_IR_SEND_OK | W5500_SOCKET1_IR_TIMEOUT));
    if (send_result != 0u)
    {
      status = w5500_socket1_write_registers(
        W5500_SOCKET1_IR_ADDRESS, &send_result, 1u);
      if (status != HAL_OK)
      {
        return status;
      }

      if ((send_result & W5500_SOCKET1_IR_TIMEOUT) != 0u)
      {
        return HAL_TIMEOUT;
      }
      return HAL_OK;
    }

    if ((uint32_t)(osKernelGetTickCount() - start_tick) >= timeout_ticks)
    {
      return HAL_TIMEOUT;
    }

    (void)osDelay(poll_ticks);
  }
}

static HAL_StatusTypeDef w5500_udp_send_frame(
  const uint8_t *frame,
  uint16_t frame_length)
{
  if ((frame == NULL) || (frame_length == 0u) ||
      (frame_length > PROTOCOL_MAX_FRAME_SIZE))
  {
    return HAL_ERROR;
  }

  HAL_StatusTypeDef status = w5500_udp_wait_for_tx_free(frame_length);
  if (status != HAL_OK)
  {
    return status;
  }

  uint16_t tx_write_pointer = 0u;
  status = w5500_socket1_read_u16(
    W5500_SOCKET1_TX_WR_ADDRESS, &tx_write_pointer);
  if (status != HAL_OK)
  {
    return status;
  }

  status = w5500_socket1_write_tx_buffer(
    tx_write_pointer, frame, frame_length);
  if (status != HAL_OK)
  {
    return status;
  }

  const uint16_t new_tx_write_pointer =
    (uint16_t)(tx_write_pointer + frame_length);
  const uint8_t tx_write_pointer_bytes[2] = {
    (uint8_t)(new_tx_write_pointer >> 8),
    (uint8_t)(new_tx_write_pointer & 0xffu)
  };
  status = w5500_socket1_write_registers(
    W5500_SOCKET1_TX_WR_ADDRESS,
    tx_write_pointer_bytes,
    (uint16_t)sizeof(tx_write_pointer_bytes));
  if (status != HAL_OK)
  {
    return status;
  }

  /* Sn_IR is write-one-to-clear. Remove stale SEND result before SEND. */
  const uint8_t stale_send_bits =
    W5500_SOCKET1_IR_SEND_OK | W5500_SOCKET1_IR_TIMEOUT;
  status = w5500_socket1_write_registers(
    W5500_SOCKET1_IR_ADDRESS, &stale_send_bits, 1u);
  if (status != HAL_OK)
  {
    return status;
  }

  status = w5500_socket1_execute_command(W5500_SOCKET1_SEND_COMMAND);
  if (status != HAL_OK)
  {
    return status;
  }

  return w5500_udp_wait_for_send_result();
}

static HAL_StatusTypeDef w5500_start_udp_socket(void)
{
  const uint8_t udp_mode = W5500_SOCKET1_UDP_MODE;
  const uint8_t local_port[2] = {
    (uint8_t)(W5500_UDP_LOCAL_PORT >> 8),
    (uint8_t)(W5500_UDP_LOCAL_PORT & 0xffu)
  };
  const uint8_t destination_port[2] = {
    (uint8_t)(W5500_UDP_DESTINATION_PORT >> 8),
    (uint8_t)(W5500_UDP_DESTINATION_PORT & 0xffu)
  };

  HAL_StatusTypeDef status = w5500_socket1_write_registers(
    W5500_SOCKET1_MR_ADDRESS, &udp_mode, 1u);
  if (status == HAL_OK)
  {
    status = w5500_socket1_write_registers(
      W5500_SOCKET1_PORT_ADDRESS,
      local_port,
      (uint16_t)sizeof(local_port));
  }
  if (status == HAL_OK)
  {
    status = w5500_socket1_execute_command(W5500_SOCKET1_OPEN_COMMAND);
  }
  if (status == HAL_OK)
  {
    status = w5500_wait_for_socket1_status(
      W5500_SOCKET1_STATUS_UDP, W5500_SOCKET_STATE_TIMEOUT_MS);
  }
  if (status == HAL_OK)
  {
    status = w5500_socket1_write_registers(
      W5500_SOCKET1_DIPR_ADDRESS,
      g_w5500_udp_destination_ip,
      (uint16_t)sizeof(g_w5500_udp_destination_ip));
  }
  if (status == HAL_OK)
  {
    status = w5500_socket1_write_registers(
      W5500_SOCKET1_DPORT_ADDRESS,
      destination_port,
      (uint16_t)sizeof(destination_port));
  }

  return status;
}

static uint32_t w5500_kernel_uptime_ms(void)
{
  const uint32_t tick_frequency = osKernelGetTickFreq();
  if (tick_frequency == 0u)
  {
    return 0u;
  }

  return (uint32_t)(((uint64_t)osKernelGetTickCount() * 1000u) /
                    tick_frequency);
}

static void w5500_udp_send_telemetry(void)
{
  const uint16_t sequence = g_w5500_udp_next_seq;
  const uint32_t uptime_ms = w5500_kernel_uptime_ms();

  g_udp_telemetry_packet.version = PROTOCOL_VERSION;
  g_udp_telemetry_packet.msg_id = MSG_TELEMETRY;
  g_udp_telemetry_packet.seq = sequence;
  g_udp_telemetry_packet.length = W5500_UDP_PAYLOAD_LENGTH;
  g_udp_telemetry_packet.payload[0] = (uint8_t)(uptime_ms >> 24);
  g_udp_telemetry_packet.payload[1] = (uint8_t)(uptime_ms >> 16);
  g_udp_telemetry_packet.payload[2] = (uint8_t)(uptime_ms >> 8);
  g_udp_telemetry_packet.payload[3] = (uint8_t)(uptime_ms & 0xffu);
  g_udp_telemetry_packet.payload[4] = 0u;
  g_udp_telemetry_packet.payload[5] = 0u;

  size_t encoded_length = 0u;
  const protocol_result_t encode_status = protocol_encode(
    &g_udp_telemetry_packet,
    g_w5500_udp_tx_frame,
    sizeof(g_w5500_udp_tx_frame),
    &encoded_length);
  if ((encode_status != PROTO_OK) || (encoded_length == 0u) ||
      (encoded_length > UINT16_MAX))
  {
    g_w5500_udp_last_tx_status = (int32_t)encode_status;
    g_w5500_udp_tx_error_count++;
    return;
  }

  const HAL_StatusTypeDef send_status = w5500_udp_send_frame(
    g_w5500_udp_tx_frame, (uint16_t)encoded_length);
  g_w5500_udp_last_tx_status = (int32_t)send_status;
  g_w5500_udp_socket1_spi_status = (int32_t)send_status;

  if (send_status == HAL_OK)
  {
    g_w5500_udp_tx_packet_count++;
    g_w5500_udp_tx_bytes += (uint32_t)encoded_length;
    g_w5500_udp_last_tx_seq = sequence;
    g_w5500_udp_next_seq = (uint16_t)(sequence + 1u);
  }
  else
  {
    g_w5500_udp_tx_error_count++;
  }
}

static void rs422_rx_ring_push_from_isr(uint8_t byte)
{
  const uint16_t head = g_rs422_rx_head;
  const uint16_t tail = g_rs422_rx_tail;

  if ((uint16_t)(head - tail) >= RS422_RX_RING_CAPACITY)
  {
    g_rs422_rx_overflow_count++;
    return;
  }

  g_rs422_rx_ring[head & RS422_RX_RING_MASK] = byte;
  __DMB();
  g_rs422_rx_head = (uint16_t)(head + 1u);
}

static uint32_t rs422_rx_ring_pop(uint8_t *byte)
{
  const uint16_t tail = g_rs422_rx_tail;

  if (tail == g_rs422_rx_head)
  {
    return 0u;
  }

  __DMB();
  *byte = g_rs422_rx_ring[tail & RS422_RX_RING_MASK];
  __DMB();
  g_rs422_rx_tail = (uint16_t)(tail + 1u);
  return 1u;
}

static uint32_t rs422_ms_to_kernel_ticks(uint32_t time_ms)
{
  const uint32_t tick_frequency = osKernelGetTickFreq();
  uint32_t ticks = (uint32_t)((((uint64_t)time_ms * tick_frequency) + 999u) /
                              1000u);

  if (ticks == 0u)
  {
    ticks = 1u;
  }

  return ticks;
}

static uint32_t rs422_process_rx_buffer(
  uint16_t expected_pong_sequence,
  uint32_t match_pong)
{
  uint8_t rx_byte;
  uint32_t matching_pong_received = 0u;

  while (rs422_rx_ring_pop(&rx_byte) != 0u)
  {
    const stream_parser_event_t parser_event = stream_parser_feed_byte(
      &g_rs422_parser,
      rx_byte,
      &g_rs422_received_packet);

    if (parser_event == STREAM_EVENT_FRAME)
    {
      g_rs422_rx_frame_count++;
      g_rs422_last_msg_id = g_rs422_received_packet.msg_id;
      g_rs422_last_valid_frame_tick = osKernelGetTickCount();
      __DMB();
      g_rs422_has_valid_frame = 1u;

      if ((match_pong != 0u) &&
          (g_rs422_received_packet.msg_id == MSG_PONG) &&
          (g_rs422_received_packet.seq == expected_pong_sequence))
      {
        matching_pong_received = 1u;
      }
    }
  }

  return matching_pong_received;
}

static void rs422_handle_tx_done_event(uint32_t flags)
{
  if ((flags & RS422_EVENT_TX_DONE) != 0u)
  {
    if (g_rs422_tx_busy != 0u)
    {
      g_rs422_tx_busy = 0u;
      g_rs422_tx_complete_count++;
    }
    else
    {
      g_rs422_tx_error_count++;
    }
  }
}

static int32_t rs422_wait_for_tx_complete(
  uint16_t expected_pong_sequence,
  uint32_t *matching_pong_received)
{
  const uint32_t timeout_ticks =
    rs422_ms_to_kernel_ticks(RS422_BRINGUP_TX_TIMEOUT_MS);
  const uint32_t wait_start_ticks = osKernelGetTickCount();

  *matching_pong_received = 0u;

  while (g_rs422_tx_busy != 0u)
  {
    *matching_pong_received |=
      rs422_process_rx_buffer(expected_pong_sequence, 1u);

    const uint32_t pending_flags = osThreadFlagsWait(
      RS422_EVENT_MASK,
      osFlagsWaitAny,
      0u);

    if ((pending_flags & osFlagsError) == 0u)
    {
      rs422_handle_tx_done_event(pending_flags);
    }
    else if (pending_flags != osFlagsErrorResource)
    {
      g_rs422_tx_error_count++;
      return RS422_PING_TEST_TX_FAILURE;
    }

    if (g_rs422_tx_busy == 0u)
    {
      *matching_pong_received |=
        rs422_process_rx_buffer(expected_pong_sequence, 1u);
      return RS422_PING_TEST_PASS;
    }

    const uint32_t elapsed_ticks =
      (uint32_t)(osKernelGetTickCount() - wait_start_ticks);

    if (elapsed_ticks >= timeout_ticks)
    {
      g_rs422_tx_timeout_count++;
      return RS422_PING_TEST_TX_FAILURE;
    }

    const uint32_t flags = osThreadFlagsWait(
      RS422_EVENT_MASK,
      osFlagsWaitAny,
      timeout_ticks - elapsed_ticks);

    if (flags == osFlagsErrorTimeout)
    {
      g_rs422_tx_timeout_count++;
      return RS422_PING_TEST_TX_FAILURE;
    }

    if ((flags & osFlagsError) != 0u)
    {
      g_rs422_tx_error_count++;
      return RS422_PING_TEST_TX_FAILURE;
    }

    rs422_handle_tx_done_event(flags);
  }

  return RS422_PING_TEST_PASS;
}

static int32_t rs422_ping_bringup_test(uint16_t sequence)
{
  HAL_StatusTypeDef tx_status;
  size_t tx_frame_length = 0u;
  uint32_t matching_pong_received = 0u;
  uint32_t wait_start_ticks;
  uint32_t pong_timeout_ticks;

  g_rs422_last_seq = sequence;

  if (g_rs422_tx_busy != 0u)
  {
    g_rs422_tx_busy_count++;
    return RS422_PING_TEST_TX_FAILURE;
  }

  g_rs422_ping_packet.version = PROTOCOL_VERSION;
  g_rs422_ping_packet.msg_id = MSG_PING;
  g_rs422_ping_packet.seq = sequence;
  g_rs422_ping_packet.length = 0u;

  if (protocol_encode(
        &g_rs422_ping_packet,
        g_rs422_tx_frame,
        sizeof(g_rs422_tx_frame),
        &tx_frame_length) != PROTO_OK)
  {
    return RS422_PING_TEST_ENCODE_FAILURE;
  }

  if ((osThreadFlagsClear(RS422_EVENT_TX_DONE) & osFlagsError) != 0u)
  {
    g_rs422_tx_error_count++;
    return RS422_PING_TEST_TX_FAILURE;
  }

  g_rs422_tx_busy = 1u;
  tx_status = HAL_UART_Transmit_IT(
    &huart4,
    g_rs422_tx_frame,
    (uint16_t)tx_frame_length);

  if (tx_status != HAL_OK)
  {
    g_rs422_tx_busy = 0u;

    if (tx_status == HAL_BUSY)
    {
      g_rs422_tx_busy_count++;
    }
    else
    {
      g_rs422_tx_error_count++;
    }

    return RS422_PING_TEST_TX_FAILURE;
  }

  if (rs422_wait_for_tx_complete(sequence, &matching_pong_received) !=
      RS422_PING_TEST_PASS)
  {
    return RS422_PING_TEST_TX_FAILURE;
  }

  if (matching_pong_received != 0u)
  {
    return RS422_PING_TEST_PASS;
  }

  pong_timeout_ticks =
    rs422_ms_to_kernel_ticks(RS422_BRINGUP_PONG_TIMEOUT_MS);
  wait_start_ticks = osKernelGetTickCount();

  while ((uint32_t)(osKernelGetTickCount() - wait_start_ticks) <
         pong_timeout_ticks)
  {
    if (rs422_process_rx_buffer(sequence, 1u) != 0u)
    {
      return RS422_PING_TEST_PASS;
    }

    const uint32_t elapsed_ticks =
      (uint32_t)(osKernelGetTickCount() - wait_start_ticks);

    if (elapsed_ticks >= pong_timeout_ticks)
    {
      break;
    }

    const uint32_t remaining_ticks = pong_timeout_ticks - elapsed_ticks;

    const uint32_t flags = osThreadFlagsWait(
      RS422_EVENT_MASK,
      osFlagsWaitAny,
      remaining_ticks);

    if (flags == osFlagsErrorTimeout)
    {
      break;
    }

    if ((flags & osFlagsError) != 0u)
    {
      return RS422_PING_TEST_RX_FAILURE;
    }

    rs422_handle_tx_done_event(flags);
  }

  return RS422_PING_TEST_PONG_TIMEOUT;
}

static int32_t rs422_ping_bringup_run(void)
{
  int32_t last_failure = RS422_PING_TEST_PASS;

  g_rs422_ping_test_result = RS422_PING_TEST_NOT_RUN;
  g_rs422_last_msg_id = 0u;
  g_rs422_last_seq = 0u;
  g_rs422_rx_frame_count = 0u;
  g_rs422_test_total = 0u;
  g_rs422_test_success = 0u;
  g_rs422_test_fail = 0u;

  for (uint32_t test_index = 0u;
       test_index < RS422_BRINGUP_TEST_COUNT;
       test_index++)
  {
    const uint16_t sequence = (uint16_t)(test_index + 1u);
    const int32_t test_result = rs422_ping_bringup_test(sequence);

    g_rs422_test_total++;
    g_rs422_ping_test_result = test_result;

    if (test_result == RS422_PING_TEST_PASS)
    {
      g_rs422_test_success++;
    }
    else
    {
      g_rs422_test_fail++;
      last_failure = test_result;
    }

    if ((test_index + 1u) < RS422_BRINGUP_TEST_COUNT)
    {
      HAL_Delay(RS422_BRINGUP_INTER_TEST_DELAY_MS);
    }
  }

  if (g_rs422_test_fail == 0u)
  {
    return RS422_PING_TEST_PASS;
  }

  return last_failure;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI2_Init();
  MX_UART4_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  g_gateway_queue_init_ok = 0u;
  rs422_cmd_queue = osMessageQueueNew(
    RS422_CMD_QUEUE_DEPTH,
    sizeof(protocol_packet_t),
    &rs422_cmd_queue_attributes);
  tcp_response_queue = osMessageQueueNew(
    TCP_RESPONSE_QUEUE_DEPTH,
    sizeof(protocol_packet_t),
    &tcp_response_queue_attributes);
  udp_telemetry_queue = osMessageQueueNew(
    UDP_TELEMETRY_QUEUE_DEPTH,
    sizeof(protocol_packet_t),
    &udp_telemetry_queue_attributes);

  if ((rs422_cmd_queue != NULL) &&
      (tcp_response_queue != NULL) &&
      (udp_telemetry_queue != NULL))
  {
    g_gateway_queue_init_ok = 1u;
  }
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of RS422Task */
  RS422TaskHandle = osThreadNew(StartRS422Task, NULL, &RS422Task_attributes);

  /* creation of EthernetTask */
  EthernetTaskHandle = osThreadNew(StartEthernetTask, NULL, &EthernetTask_attributes);

  /* creation of HealthTask */
  HealthTaskHandle = osThreadNew(StartHealthTask, NULL, &HealthTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart4, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart4, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, W5500_CS_Pin|W5500_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LPUART1_TX_Pin LPUART1_RX_Pin */
  GPIO_InitStruct.Pin = LPUART1_TX_Pin|LPUART1_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF12_LPUART1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : W5500_CS_Pin */
  GPIO_InitStruct.Pin = W5500_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(W5500_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : W5500_RST_Pin */
  GPIO_InitStruct.Pin = W5500_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(W5500_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : W5500_INT_Pin */
  GPIO_InitStruct.Pin = W5500_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(W5500_INT_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((huart != NULL) &&
      (huart->Instance == UART4) &&
      (RS422TaskHandle != NULL))
  {
    (void)osThreadFlagsSet(RS422TaskHandle, RS422_EVENT_TX_DONE);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((huart != NULL) && (huart->Instance == UART4))
  {
    rs422_rx_ring_push_from_isr(g_uart4_rx_byte);

    if (HAL_UART_Receive_IT(&huart4, &g_uart4_rx_byte, 1u) != HAL_OK)
    {
      g_rs422_uart_rx_error_count++;
    }

    if (RS422TaskHandle != NULL)
    {
      (void)osThreadFlagsSet(RS422TaskHandle, RS422_EVENT_RX);
    }
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((huart != NULL) && (huart->Instance == UART4))
  {
    g_rs422_uart_rx_error_count++;

    if ((huart->RxState == HAL_UART_STATE_READY) &&
        (HAL_UART_Receive_IT(&huart4, &g_uart4_rx_byte, 1u) != HAL_OK))
    {
      g_rs422_uart_rx_error_count++;
    }

    if (RS422TaskHandle != NULL)
    {
      (void)osThreadFlagsSet(RS422TaskHandle, RS422_EVENT_RX);
    }
  }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartRS422Task */
/**
* @brief Function implementing the RS422Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartRS422Task */
void StartRS422Task(void *argument)
{
  /* USER CODE BEGIN StartRS422Task */
  g_rs422_rx_head = 0u;
  g_rs422_rx_tail = 0u;
  g_rs422_rx_overflow_count = 0u;
  g_rs422_uart_rx_error_count = 0u;
  g_rs422_tx_busy = 0u;
  g_rs422_tx_complete_count = 0u;
  g_rs422_tx_busy_count = 0u;
  g_rs422_tx_error_count = 0u;
  g_rs422_tx_timeout_count = 0u;
  g_rs422_last_valid_frame_tick = 0u;
  g_rs422_has_valid_frame = 0u;
  stream_parser_init(&g_rs422_parser);

  if (HAL_UART_Receive_IT(&huart4, &g_uart4_rx_byte, 1u) == HAL_OK)
  {
    g_rs422_ping_test_result = rs422_ping_bringup_run();
  }
  else
  {
    g_rs422_uart_rx_error_count++;
    g_rs422_ping_test_result = RS422_PING_TEST_RX_FAILURE;
  }

  /* Infinite loop */
  for(;;)
  {
    (void)rs422_process_rx_buffer(0u, 0u);

    const uint32_t flags = osThreadFlagsWait(
      RS422_EVENT_MASK,
      osFlagsWaitAny,
      osWaitForever);

    if ((flags & osFlagsError) == 0u)
    {
      rs422_handle_tx_done_event(flags);
    }
    else
    {
      g_rs422_tx_error_count++;
    }
  }
  /* USER CODE END StartRS422Task */
}

/* USER CODE BEGIN Header_StartEthernetTask */
/**
* @brief Function implementing the EthernetTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartEthernetTask */
void StartEthernetTask(void *argument)
{
  /* USER CODE BEGIN StartEthernetTask */
  uint8_t version = 0u;

  g_w5500_net_config_ok = 0u;
  g_w5500_net_readback_ok = 0u;
  g_w5500_net_write_status = -1;
  g_w5500_net_read_status = -1;
  g_w5500_tcp_init_ok = 0u;
  g_w5500_tcp_listen_ok = 0u;
  g_w5500_socket0_status = W5500_SOCKET0_STATUS_CLOSED;
  g_w5500_socket0_spi_status = -1;
  g_w5500_tcp_rx_bytes = 0u;
  g_w5500_tcp_tx_bytes = 0u;
  g_w5500_tcp_valid_frame_count = 0u;
  g_w5500_tcp_ping_count = 0u;
  g_w5500_tcp_pong_count = 0u;
  g_w5500_tcp_last_rx_msg_id = 0u;
  g_w5500_tcp_last_rx_seq = 0u;
  g_w5500_tcp_last_tx_seq = 0u;
  g_w5500_tcp_rx_error_count = 0u;
  g_w5500_tcp_tx_error_count = 0u;
  g_w5500_tcp_last_rx_status = -1;
  g_w5500_tcp_last_tx_status = -1;
  g_w5500_udp_open_ok = 0u;
  g_w5500_udp_socket1_status = W5500_SOCKET1_STATUS_CLOSED;
  g_w5500_udp_socket1_spi_status = -1;
  g_w5500_udp_tx_packet_count = 0u;
  g_w5500_udp_tx_bytes = 0u;
  g_w5500_udp_tx_error_count = 0u;
  g_w5500_udp_last_tx_seq = 0u;
  g_w5500_udp_last_tx_status = -1;
  g_w5500_udp_next_seq = 1u;
  stream_parser_init(&g_tcp_parser);
  for (uint16_t index = 0u; index < W5500_GAR_LENGTH; index++)
  {
    g_w5500_gar_readback[index] = 0u;
    g_w5500_subr_readback[index] = 0u;
    g_w5500_sipr_readback[index] = 0u;
  }
  for (uint16_t index = 0u; index < W5500_SHAR_LENGTH; index++)
  {
    g_w5500_shar_readback[index] = 0u;
  }

  w5500_hardware_reset();
  const HAL_StatusTypeDef status = w5500_read_version(&version);
  g_w5500_version = version;
  g_w5500_spi_status = (int32_t)status;

  if ((status == HAL_OK) && (version == 0x04u))
  {
    const HAL_StatusTypeDef write_status = w5500_write_network_config();
    g_w5500_net_write_status = (int32_t)write_status;

    if (write_status == HAL_OK)
    {
      g_w5500_net_config_ok = 1u;
      const HAL_StatusTypeDef read_status = w5500_read_network_config();
      g_w5500_net_read_status = (int32_t)read_status;

      if ((read_status == HAL_OK) &&
          (w5500_network_readback_matches() != 0u))
      {
        g_w5500_net_readback_ok = 1u;
      }
    }
  }

  if ((g_w5500_net_config_ok != 0u) &&
      (g_w5500_net_readback_ok != 0u))
  {
    w5500_start_tcp_server();
  }

  if ((g_w5500_net_config_ok != 0u) &&
      (g_w5500_net_readback_ok != 0u))
  {
    const HAL_StatusTypeDef udp_open_status = w5500_start_udp_socket();
    g_w5500_udp_socket1_spi_status = (int32_t)udp_open_status;
    if (udp_open_status == HAL_OK)
    {
      g_w5500_udp_open_ok = 1u;
    }
  }

  const uint32_t phy_period_ticks =
    rs422_ms_to_kernel_ticks(W5500_PHY_POLL_PERIOD_MS);
  uint32_t next_phy_tick = osKernelGetTickCount();

  /* Infinite loop */
  for(;;)
  {
    uint8_t phycfgr = 0u;
    const HAL_StatusTypeDef phy_status = w5500_read_common_register(
      W5500_PHYCFGR_ADDRESS, &phycfgr);

    if (phy_status == HAL_OK)
    {
      g_w5500_phycfgr = phycfgr;
      g_w5500_link_up = ((phycfgr & W5500_PHYCFGR_LNK) != 0u) ? 1u : 0u;
    }
    g_w5500_phy_spi_status = (int32_t)phy_status;

    if (g_w5500_tcp_listen_ok != 0u)
    {
      uint8_t socket_status = W5500_SOCKET0_STATUS_CLOSED;
      const HAL_StatusTypeDef socket_spi_status =
        w5500_socket0_read_register(
          W5500_SOCKET0_SR_ADDRESS, &socket_status);
      g_w5500_socket0_spi_status = (int32_t)socket_spi_status;

      if (socket_spi_status == HAL_OK)
      {
        g_w5500_socket0_status = socket_status;
        if (socket_status == W5500_SOCKET0_STATUS_ESTABLISHED)
        {
          const HAL_StatusTypeDef rx_status = w5500_tcp_process_rx();
          g_w5500_tcp_last_rx_status = (int32_t)rx_status;
          if (rx_status != HAL_OK)
          {
            g_w5500_tcp_rx_error_count++;
          }
        }
      }
    }

    if (g_w5500_udp_open_ok != 0u)
    {
      uint8_t udp_socket_status = W5500_SOCKET1_STATUS_CLOSED;
      const HAL_StatusTypeDef udp_socket_spi_status =
        w5500_socket1_read_register(
          W5500_SOCKET1_SR_ADDRESS, &udp_socket_status);
      g_w5500_udp_socket1_spi_status = (int32_t)udp_socket_spi_status;

      if (udp_socket_spi_status == HAL_OK)
      {
        g_w5500_udp_socket1_status = udp_socket_status;
        if ((udp_socket_status == W5500_SOCKET1_STATUS_UDP) &&
            (g_w5500_link_up != 0u))
        {
          w5500_udp_send_telemetry();
        }
      }
    }

    next_phy_tick += phy_period_ticks;
    if (osDelayUntil(next_phy_tick) != osOK)
    {
      /* A slow/failed SPI read may miss the deadline. Block before retrying. */
      (void)osDelay(phy_period_ticks);
      next_phy_tick = osKernelGetTickCount();
    }
  }
  /* USER CODE END StartEthernetTask */
}

/* USER CODE BEGIN Header_StartHealthTask */
/**
* @brief Function implementing the HealthTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartHealthTask */
void StartHealthTask(void *argument)
{
  /* USER CODE BEGIN StartHealthTask */
  const uint32_t health_period_ticks =
    rs422_ms_to_kernel_ticks(HEALTH_TASK_PERIOD_MS);
  const uint32_t comm_timeout_ticks =
    rs422_ms_to_kernel_ticks(RS422_COMM_TIMEOUT_MS);
  const uint32_t health_start_tick = osKernelGetTickCount();
  uint32_t next_wake_tick = health_start_tick;

  g_rs422_comm_state = COMM_INIT;
  g_rs422_comm_timeout_count = 0u;
  g_rs422_comm_recovery_count = 0u;

  /* Infinite loop */
  for(;;)
  {
    next_wake_tick += health_period_ticks;
    (void)osDelayUntil(next_wake_tick);

    const uint32_t current_tick = osKernelGetTickCount();
    const uint32_t has_valid_frame = g_rs422_has_valid_frame;
    const uint32_t last_valid_frame_tick =
      g_rs422_last_valid_frame_tick;

    if (has_valid_frame != 0u)
    {
      if ((uint32_t)(current_tick - last_valid_frame_tick) >=
          comm_timeout_ticks)
      {
        if (g_rs422_comm_state != COMM_TIMEOUT)
        {
          g_rs422_comm_timeout_count++;
          g_rs422_comm_state = COMM_TIMEOUT;
        }
      }
      else if (g_rs422_comm_state != COMM_OK)
      {
        if (g_rs422_comm_state == COMM_TIMEOUT)
        {
          g_rs422_comm_recovery_count++;
        }

        g_rs422_comm_state = COMM_OK;
      }
    }
    else if ((uint32_t)(current_tick - health_start_tick) >=
             comm_timeout_ticks)
    {
      if (g_rs422_comm_state != COMM_TIMEOUT)
      {
        g_rs422_comm_timeout_count++;
        g_rs422_comm_state = COMM_TIMEOUT;
      }
    }

    g_gateway_free_heap_bytes = (uint32_t)xPortGetFreeHeapSize();
    g_gateway_min_ever_free_heap_bytes =
      (uint32_t)xPortGetMinimumEverFreeHeapSize();
  }
  /* USER CODE END StartHealthTask */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
