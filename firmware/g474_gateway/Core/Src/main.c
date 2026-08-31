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

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
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
