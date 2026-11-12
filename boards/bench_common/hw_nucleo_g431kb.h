/*
 * hw_nucleo_g431kb.h - hardware profile: ST NUCLEO-G431KB (STM32G431KBT6, Nucleo-32).
 *
 * Select it by defining BOARD_HW_NUCLEO_G431KB (CubeIDE: Project > Properties >
 * C/C++ Build > Settings > MCU GCC Compiler > Preprocessor > Define symbols).
 *
 * CAN:   FDCAN1  PA11 = RX (Arduino D10), PA12 = TX (Arduino D2), AF9.
 *        The board has NO CAN transceiver: wire an external 3.3 V one
 *        (SN65HVD230, TCAN1042V/TCAN332, MCP2562 with VIO = 3.3 V ...):
 *          PA12 -> TXD (D),  PA11 <- RXD (R),  3V3, GND,
 *          standby / S / Rs pin -> GND (normal mode), 120 ohm across CANH-CANL
 *          at each end of the bus (many breakout modules already have one).
 *        LQFP32 has no PB9, so FDCAN1 cannot go on PB8/PB9 on this chip -> no
 *        BOOT0/option-byte step like the Deft PCB.
 *
 * Clock: whatever CubeMX shows in "To FDCAN (MHz)". The default below assumes the
 *        same tree as the Deft bench: PLL from HSE -> SYSCLK 170 MHz, APB1 /2,
 *        FDCAN = PCLK1 = 85 MHz. Use HSE (crystal or ST-LINK MCO bypass, as the
 *        Board Selector sets it up), not HSI, as the PLL source.
 *        Any value that is a whole number of MHz with an exact 1 Mbps split works
 *        (24, 48, 80, 85, 160, 170 ...); otherwise bsp_init() stops in bsp_fatal().
 *
 * LED:   LD2 (green) on PB8, active-high. Only one user LED: it shows the
 *        armed/killed/bus-trouble status; there is no separate heartbeat LED.
 * Input: no user button on Nucleo-32. Kill/arm input = PA0 (Arduino A0), internal
 *        pull-up: touch A0 to GND = one press. Or toggle `hard_kill` in the debugger.
 *
 * Pin <-> Arduino-label mapping: check UM2397 or the CubeMX Board Selector pinout.
 */
#ifndef HW_NUCLEO_G431KB_H
#define HW_NUCLEO_G431KB_H

#ifndef BOARD_FDCAN_CLOCK_HZ
#define BOARD_FDCAN_CLOCK_HZ   85000000u
#endif
#define BOARD_FDCAN_INSTANCE   FDCAN1
#define BOARD_FDCAN_HANDLE     hfdcan1

#define BOARD_LED_PORT         GPIOB
#define BOARD_LED_PIN          GPIO_PIN_8
#define BOARD_LED_ACTIVE_HIGH  1

#define BOARD_BTN_PORT         GPIOA
#define BOARD_BTN_PIN          GPIO_PIN_0
#define BOARD_BTN_ACTIVE_HIGH  0

#endif
