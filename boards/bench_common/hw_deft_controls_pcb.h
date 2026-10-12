/*
 * hw_deft_controls_pcb.h - hardware profile: Deft Robotics Controls PCB (STM32G474RET6).
 *
 * Source: DeftRoboticsControlsPCB.ioc + Core/Src/{fdcan,gpio}.c + App sources of the
 * Deft_Robo_Controls firmware. Include this FIRST in a bench board_config.h.
 *
 * Clock: HSE 25 MHz -> PLL (M=5, N=68) -> SYSCLK 170 MHz; APB1 /2 -> PCLK1 85 MHz.
 *        FDCAN kernel clock = PCLK1 = 85 MHz (crystal-derived, OK for CAN).
 *        auvCAN timing at 85 MHz: prescaler 5, seg1 14, seg2 2 = 17 tq, 88.2 % sample
 *        point -- identical to the existing Deft firmware's FDCAN setup.
 *
 * CAN:   CH1 = FDCAN1  PB8 (BOOT0!) = RX, PB9 = TX   <- used by the bench apps
 *        CH2 = FDCAN3  PA8 = RX, PA15 = TX
 *        CH3 = FDCAN2  PB12 = RX, PB13 = TX
 *        The existing firmware never drives a transceiver standby pin, so standby is
 *        assumed hard-wired to normal mode (no BOARD_CAN_STBY_*). Confirm on the schematic.
 *
 * !! PB8 is BOOT0. With the factory option bytes the chip samples PB8 at reset, and an
 * !! idle CAN transceiver holds RX HIGH -> the MCU boots the ROM bootloader, not your
 * !! firmware. In STM32CubeProgrammer > Option bytes set nSWBOOT0 = 0 (unchecked) and
 * !! nBOOT0 = 1 (checked) on EVERY board before testing with the bus connected.
 *
 * LED:   PC7 = CH1 activity LED ("CAN1_ACT", active-high).
 * Input: PB7 = hard-ESTOP sense from the PDU, internal pull-up, asserted LOW.
 *        On the boss it's the kill/arm toggle: briefly short PB7 to GND = one press.
 */
#ifndef HW_DEFT_CONTROLS_PCB_H
#define HW_DEFT_CONTROLS_PCB_H

#define BOARD_FDCAN_CLOCK_HZ   85000000u
#define BOARD_FDCAN_INSTANCE   FDCAN1
#define BOARD_FDCAN_HANDLE     hfdcan1

#define BOARD_LED_PORT         GPIOC
#define BOARD_LED_PIN          GPIO_PIN_7
#define BOARD_LED_ACTIVE_HIGH  1

#define BOARD_BTN_PORT         GPIOB
#define BOARD_BTN_PIN          GPIO_PIN_7
#define BOARD_BTN_ACTIVE_HIGH  0

#endif
