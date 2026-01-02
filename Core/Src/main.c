/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
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
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "console.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct {
	led_mode_t mode;
	uint32_t last_toggle_ms;
	uint32_t interval_ms;
	uint8_t led_on;
} led_ctrl_t;

typedef struct {
	uint8_t pending;
	uint32_t timestamp_ms;
} button_db_t;

volatile button_db_t button = { 0 };

typedef void (*task_fn_t)(void);

typedef struct {
	task_fn_t fn;
	uint32_t period_ms;
	uint32_t last_run_ms;
} task_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEBOUNCE_MS 50
#define TASK_COUNT 4

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint32_t system_tick_ms = 0;

led_ctrl_t led = { .mode = LED_MODE_SLOW, .last_toggle_ms = 0, .interval_ms =
		500, .led_on = 0 };

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void led_update(led_ctrl_t *ctrl, uint32_t now_ms);
uint8_t button_debounce_update(void);
void task_led(void);
void task_button(void);
void task_idle(void);
void task_console(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
task_t tasks[TASK_COUNT] = { { task_button, 10, 0 },   // 10 ms
		{ task_led, 1, 0 },   // 1 ms
		{ task_console, 5, 0 },   // 5 ms
		{ task_idle, 0, 0 }    // always
};

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

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
	MX_DMA_Init();
	MX_TIM2_Init();
	MX_USART2_UART_Init();
	/* USER CODE BEGIN 2 */

	console_init();

	HAL_TIM_Base_Start_IT(&htim2);

	/* USER CODE END 2 */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1) {
		/* USER CODE END WHILE */

		/* USER CODE BEGIN 3 */
		uint32_t now = system_tick_ms;

		for (int i = 0; i < TASK_COUNT; i++) {
			if (tasks[i].period_ms == 0
					|| (now - tasks[i].last_run_ms) >= tasks[i].period_ms) {
				tasks[i].last_run_ms = now;
				tasks[i].fn();
			}
		}

	}
	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

	/** Configure the main internal regulator output voltage
	 */
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = 16;
	RCC_OscInitStruct.PLL.PLLN = 336;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
	RCC_OscInitStruct.PLL.PLLQ = 4;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
		Error_Handler();
	}
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == B1_Pin) {
		if (!button.pending) {
			button.pending = 1;
			button.timestamp_ms = system_tick_ms;
		}
	}
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM2) {
		system_tick_ms++;
	}
}

void led_set_mode(led_mode_t mode)
{
    led.mode = mode;
}

led_mode_t led_get_mode(void)
{
    return led.mode;
}

void led_update(led_ctrl_t *ctrl, uint32_t now_ms) {
	switch (ctrl->mode) {
	case LED_MODE_OFF:
		HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
		ctrl->led_on = 0;
		break;

	case LED_MODE_SLOW:
		ctrl->interval_ms = 500;
		break;

	case LED_MODE_FAST:
		ctrl->interval_ms = 100;
		break;
	}

	if (ctrl->mode != LED_MODE_OFF) {
		if ((now_ms - ctrl->last_toggle_ms) >= ctrl->interval_ms) {
			ctrl->last_toggle_ms = now_ms;
			ctrl->led_on ^= 1;
			HAL_GPIO_WritePin(
			LD2_GPIO_Port,
			LD2_Pin, ctrl->led_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
		}
	}
}

uint8_t button_debounce_update(void) {
	if (button.pending) {
		if ((system_tick_ms - button.timestamp_ms) >= DEBOUNCE_MS) {
			button.pending = 0;

			if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET) {
				return 1;  // Valid press
			}
		}
	}
	return 0;
}

void task_led(void) {
	led_update(&led, system_tick_ms);
}

void task_button(void) {
	if (button_debounce_update()) {
		led.mode++;
		if (led.mode > LED_MODE_FAST) {
			led.mode = LED_MODE_OFF;
		}
	}
}

void task_idle(void) {
	__WFI();  // Sleep until next interrupt
}

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1) {
	}
	/* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
