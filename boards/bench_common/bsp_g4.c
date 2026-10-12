/*
 * bsp_g4.c - bench BSP for STM32G4 boards (Nucleo-64 or custom).
 *
 * Everything board-specific is a macro you can override in board_config.h:
 *
 *   BOARD_FDCAN_INSTANCE      FDCAN1 / FDCAN2 / FDCAN3           (default FDCAN1)
 *   BOARD_FDCAN_HANDLE        CubeMX handle: hfdcan1 / 2 / 3      (default hfdcan1)
 *   BOARD_LED_PORT/_PIN       armed/killed LED                   (default PA5, Nucleo LD2)
 *   BOARD_LED_ACTIVE_HIGH     1 = pin high turns LED on          (default 1)
 *   BOARD_BTN_PORT/_PIN       kill/arm button or jumper pin      (default PC13, Nucleo B1)
 *   BOARD_BTN_ACTIVE_HIGH     1 = reads high when pressed        (default 1)
 *   BOARD_CAN_STBY_PORT/_PIN  transceiver standby/silent/enable pin, if an MCU pin drives it
 *   BOARD_CAN_STBY_NORMAL     level that puts the transceiver in NORMAL mode (default 0:
 *                             TCAN1042 STB, MCP2562 STBY, TJA1051 S, SN65HVD230 Rs are all
 *                             active-low-for-normal; transceivers with an EN pin need 1)
 *
 * LED and button GPIOs are configured here, so CubeMX only needs FDCAN.
 */
#include "bsp.h"

#include "main.h"                 /* CubeMX: HAL, hfdcanN, Error_Handler */
#include "board_config.h"
#include "auvcan_port_g4.h"

#ifndef BOARD_FDCAN_INSTANCE
#define BOARD_FDCAN_INSTANCE  FDCAN1
#endif
#ifndef BOARD_FDCAN_HANDLE
#define BOARD_FDCAN_HANDLE    hfdcan1
#endif
#ifndef BOARD_LED_PORT
#define BOARD_LED_PORT        GPIOA
#define BOARD_LED_PIN         GPIO_PIN_5      /* Nucleo-64 LD2 */
#endif
#ifndef BOARD_LED_ACTIVE_HIGH
#define BOARD_LED_ACTIVE_HIGH 1
#endif
#ifndef BOARD_BTN_PORT
#define BOARD_BTN_PORT        GPIOC
#define BOARD_BTN_PIN         GPIO_PIN_13     /* Nucleo-64 B1 */
#endif
#ifndef BOARD_BTN_ACTIVE_HIGH
#define BOARD_BTN_ACTIVE_HIGH 1
#endif
#ifndef BOARD_CAN_STBY_NORMAL
#define BOARD_CAN_STBY_NORMAL 0
#endif

extern FDCAN_HandleTypeDef BOARD_FDCAN_HANDLE;

static auvcan_g4_t g4;

static void enable_all_gpio_clocks(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
}

const auvcan_port_t *bsp_init(void)
{
    enable_all_gpio_clocks();

    GPIO_InitTypeDef io = {0};
    io.Mode  = GPIO_MODE_OUTPUT_PP;
    io.Pull  = GPIO_NOPULL;
    io.Speed = GPIO_SPEED_FREQ_LOW;

    io.Pin = BOARD_LED_PIN;
    HAL_GPIO_Init(BOARD_LED_PORT, &io);
    bsp_led(false);

#ifdef BOARD_CAN_STBY_PORT
    /* Take the transceiver out of standby/silent BEFORE the controller joins the bus. */
    HAL_GPIO_WritePin(BOARD_CAN_STBY_PORT, BOARD_CAN_STBY_PIN,
                      BOARD_CAN_STBY_NORMAL ? GPIO_PIN_SET : GPIO_PIN_RESET);
    io.Pin = BOARD_CAN_STBY_PIN;
    HAL_GPIO_Init(BOARD_CAN_STBY_PORT, &io);
#endif

    io.Pin  = BOARD_BTN_PIN;
    io.Mode = GPIO_MODE_INPUT;
    io.Pull = BOARD_BTN_ACTIVE_HIGH ? GPIO_PULLDOWN : GPIO_PULLUP;
    HAL_GPIO_Init(BOARD_BTN_PORT, &io);

    if (auvcan_g4_init(&g4, &BOARD_FDCAN_HANDLE, BOARD_FDCAN_INSTANCE, BOARD_FDCAN_CLOCK_HZ) != 0) {
        return NULL;    /* -1: no exact 1 Mbps timing for BOARD_FDCAN_CLOCK_HZ; -2: HAL error */
    }

    /* The RX path is interrupt-driven. If CubeMX didn't enable "FDCANx interrupt 0"
     * (and so didn't generate FDCANx_IT0_IRQHandler), nothing would ever be received
     * and the failure would be silent. Stop here instead. */
    const IRQn_Type it0 = (BOARD_FDCAN_INSTANCE == FDCAN1) ? FDCAN1_IT0_IRQn
#if defined(FDCAN2)
                        : (BOARD_FDCAN_INSTANCE == FDCAN2) ? FDCAN2_IT0_IRQn
#endif
#if defined(FDCAN3)
                        : (BOARD_FDCAN_INSTANCE == FDCAN3) ? FDCAN3_IT0_IRQn
#endif
                        : FDCAN1_IT0_IRQn;
    if (NVIC_GetEnableIRQ(it0) == 0u) {
        bsp_fatal("FDCAN interrupt 0 not enabled: tick it in CubeMX > NVIC and regenerate");
    }
    return auvcan_g4_port(&g4);
}

void bsp_led(bool on)
{
    const bool level = BOARD_LED_ACTIVE_HIGH ? on : !on;
    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool bsp_button(void)
{
    const bool high = HAL_GPIO_ReadPin(BOARD_BTN_PORT, BOARD_BTN_PIN) == GPIO_PIN_SET;
    return BOARD_BTN_ACTIVE_HIGH ? high : !high;
}

void bsp_fatal(const char *why)
{
    (void)why;          /* set a breakpoint here; `why` says which step failed */
    Error_Handler();
    for (;;) {}
}
