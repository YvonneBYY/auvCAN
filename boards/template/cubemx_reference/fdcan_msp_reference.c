/*
 * fdcan_msp_reference.c - what CubeMX must generate for an auvCAN board.
 *
 * Reference only: CubeMX normally writes this into stm32g4xx_hal_msp.c and
 * stm32g4xx_it.c. Compare your generated code against it. Shown for FDCAN1 on
 * PA11 (RX) / PA12 (TX), kernel clock = 16 MHz HSE. Used by the ARM test build.
 *
 * CubeMX settings:
 *   Connectivity > FDCAN1: Activated, mode = Classic (the auvCAN port overrides
 *       the bit-timing fields, so their CubeMX values don't matter)
 *   Project Manager > Advanced Settings: untick "Generate Code" for MX_FDCAN1_Init
 *       (optional: if it runs anyway, auvcan_g4_init() simply re-initialises)
 *   NVIC: FDCAN1 interrupt 0 enabled. Interrupt 1 is not used (auvCAN keeps
 *         every FDCAN interrupt on line 0 so one ISR feeds both RX rings).
 *   Clock Configuration: FDCAN clock mux = HSE (or PLLQ from HSE). Never HSI.
 *
 * Using FDCAN2 or FDCAN3 instead (custom boards): same pattern with
 *   FDCANx_IT0_IRQn / FDCANx_IT0_IRQHandler() -> HAL_FDCAN_IRQHandler(&hfdcanx),
 *   the pins/AF CubeMX picks for that instance, and in board_config.h:
 *   #define BOARD_FDCAN_INSTANCE FDCANx   and   #define BOARD_FDCAN_HANDLE hfdcanx
 *   (__HAL_RCC_FDCAN_CLK_ENABLE() is one clock shared by all three instances.)
 */
#include "stm32g4xx_hal.h"

void Error_Handler(void);

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef *hfdcan)
{
    if (hfdcan->Instance != FDCAN1) {
        return;
    }

    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    clk.FdcanClockSelection  = RCC_FDCANCLKSOURCE_HSE;
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) {
        Error_Handler();
    }

    __HAL_RCC_FDCAN_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_11 | GPIO_PIN_12;     /* PA11 = FDCAN1_RX, PA12 = FDCAN1_TX */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 1, 0);    /* all FDCAN interrupts (line 0) */
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
}

/* stm32g4xx_it.c */
extern FDCAN_HandleTypeDef hfdcan1;

void FDCAN1_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&hfdcan1); }
