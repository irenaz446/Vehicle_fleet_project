/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Vehicle Fleet Management System — STM32 Sensor Simulator
  *
  * This firmware simulates 100 virtual vehicles (CAR-001 to CAR-100).
  * It cycles through all 100 cars every second, sending one 84-byte
  * telemetry_frame_t per car to the BBG over I2C (slave mode).
  *
  * Simulation (fleet_data.c):
  *   - Each car has independent GPS position, speed, accel, gyro state
  *   - Trip state machine per car: IDLE → ACTIVE → ENDING → IDLE
  *   - Random events: harsh braking, acceleration, sharp turns, overspeed
  *
  * Timing:
  *   TIM2 fires every 1 second → fleet_data_update() advances all 100 cars
  *   Main loop cycles through cars: prepares frame → arms I2C → waits ACK
  *   100 cars × ~10ms per I2C transaction ≈ 1 second per full sweep
  *
  * Peripherals:
  *   I2C2   slave address 0x08, 84-byte frame per transaction
  *   TIM2   1 Hz tick (Prescaler=15999, Period=999, HSI 16MHz)
  *   USART3 115200 baud debug output
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "string.h"
#include "fleet_data.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define STM32_SLAVE_ADDR    0x08     /* our own slave address */
#define I2C_FRAME_BYTES     84     /* sizeof(telemetry_frame_t) */
#define NUM_CARS            100
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

I2C_HandleTypeDef hi2c2;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
/** @brief The frame buffer currently armed on the I2C slave. */
static telemetry_frame_t g_frame;

/**
 * @brief Which car (0-99) is currently being served.
 *        Advanced in the main loop after each successful I2C TX.
 */
static int g_current_car = 0;

/**
 * @brief 1-second tick flag set by TIM2 IRQ.
 *        When set: call fleet_data_update() then start new sweep.
 */
static volatile uint8_t g_tick = 0;

/**
 * @brief I2C TX-complete flag set by HAL_I2C_SlaveTxCpltCallback.
 *        When set: advance to next car in the sweep.
 */
static volatile uint8_t g_tx_done = 0;

/**
 * @brief Counts how many cars have been sent in the current 1-second sweep.
 *        When it reaches NUM_CARS, the sweep is complete.
 */
static int g_sweep_count = 0;

/** @brief Total frames sent since startup — for statistics. */
static uint32_t g_total_frames = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_I2C2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief Arm the I2C slave for one transmit transaction.
 *        Called from main (never from IRQ).
 */
static void i2c_arm(void)
{
    HAL_I2C_Slave_Transmit_IT(&hi2c2,
                               (uint8_t *)&g_frame,
							   I2C_FRAME_BYTES);
}

/**
 * @brief Prepare g_frame for the given car and arm the I2C slave.
 * @param car_idx  Car index 0-99.
 */
static void send_car(int car_idx)
{
    fleet_get_frame(car_idx, &g_frame);
    i2c_arm();
}

/**
 * @brief I2C slave TX callback — Called when BBG master has read all I2C_FRAME_BYTES.
 *        Sets g_tx_done so main loop can advance to next car.
 *        Re-arms immediately so slave stays responsive.
 */
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C2) return;

    /* Re-arm immediately for the next read from BBG */
    g_tx_done = 1;              /* tell main loop a TX just completed */
    /* Re-arm with same frame — BBG might read again before main loop acts */
    HAL_I2C_Slave_Transmit_IT(&hi2c2, (uint8_t *)&g_frame, I2C_FRAME_BYTES);
}

/**
 * @brief I2C error callback — re-arm the slave so it stays responsive.
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C2) return;

    /* Re-arm after any error (e.g. master sent NACK at end of read) */
    HAL_I2C_Slave_Transmit_IT(&hi2c2, (uint8_t *)&g_frame, I2C_FRAME_BYTES);
}

/* ── Timer callback: fires every 1 second ────────── */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2) return;
    g_tick = 1;   /* signal main loop */
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
  MX_TIM2_Init();
  MX_USART3_UART_Init();
  MX_I2C2_Init();
  /* USER CODE BEGIN 2 */

  /* Initialise simulation for all 100 cars */
  /* Use HAL_GetTick() / 1000 as the base timestamp.
   * This gives seconds since reset — unique and ever-increasing.
   * Replace with HAL_RTC_GetTime() for real UTC if an RTC is configured. */
  uint32_t base_ts = HAL_GetTick() / 1000UL;
  fleet_data_init(base_ts);

  /* Prepare and arm the first car */
  send_car(0);

  /* Start 1-second timer */
  HAL_TIM_Base_Start_IT(&htim2);

  printf("=== Vehicle Fleet STM32 Simulator started ===\r\n");
  printf("Cars        : %d virtual vehicles\r\n", NUM_CARS);
  printf("Frame size  : %d bytes\r\n", I2C_FRAME_BYTES);
  printf("Slave addr  : 0x%02X\r\n", STM32_SLAVE_ADDR);
  printf("Sweep rate  : ~1 second per full cycle\r\n\n");
  printf("sizeof(telemetry_frame_t) = %d\r\n", (int)sizeof(telemetry_frame_t));
  printf("offset msg_type    = %d\r\n", (int)offsetof(telemetry_frame_t, msg_type));
  printf("offset timestamp   = %d\r\n", (int)offsetof(telemetry_frame_t, timestamp_sec));
  printf("sizeof frame       = %d\r\n", (int)sizeof(telemetry_frame_t));

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

      /* ── 1-second tick: advance simulation state for all cars ─────── */
      if (g_tick) {
          g_tick = 0;
          fleet_data_update();
          g_sweep_count = 0;   /* reset sweep counter for new second     */

          /* Print statistics every 10 seconds */
          static uint32_t stat_count = 0;
          stat_count++;
          if (stat_count % 10 == 0) {
              printf("[STATS] Total frames sent: %lu | Cars simulated: %d\r\n",
                     (unsigned long)g_total_frames, NUM_CARS);
          }
      }

      /* ── I2C TX complete: advance to next car ──────────────────────── */
      if (g_tx_done) {
          g_tx_done = 0;
          g_total_frames++;

          /*
           * If we haven't finished the current sweep (100 cars),
           * move to the next car immediately.
           * If sweep is done, wait for the next 1-second tick before
           * starting a new sweep (so we stay in sync with simulation).
           */
          g_sweep_count++;

          if (g_sweep_count < NUM_CARS) {
              /* Advance to next car */
              g_current_car = (g_current_car + 1) % NUM_CARS;
              send_car(g_current_car);
          }
          /* else: sweep complete — main loop waits for g_tick */
      }

  /* USER CODE END 3 */
  }
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x00303D5B;
  hi2c2.Init.OwnAddress1 = 16;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 15999;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

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
