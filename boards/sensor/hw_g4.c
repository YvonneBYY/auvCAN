/*
 * hw_g4.c - sensor board I/O on an STM32G4 (HAL). Pins and handles: board_config.h.
 *
 * I2C transfers are blocking with a short timeout (a few hundred µs at 400 kHz);
 * CAN reception is interrupt-driven, so nothing is lost meanwhile. The MS5837's
 * 9 ms conversions are waited for by sensor.c's state machine, not here.
 */
#include "sensor_hw.h"

#include "main.h"                      /* CubeMX: HAL + hi2cN / hspiN */
#include "board_config.h"

#if !defined(HAL_I2C_MODULE_ENABLED) || !defined(HAL_SPI_MODULE_ENABLED)
#error "Sensor board needs I2C and SPI enabled in CubeMX (see board_config.h: I2C3 + SPI1 by default) - regenerate"
#endif

#define I2C_TIMEOUT_MS  5u
#define SPI_TIMEOUT_MS  5u

extern I2C_HandleTypeDef BOARD_SENSOR_I2C;
extern SPI_HandleTypeDef BOARD_SENSOR_SPI;

typedef struct { GPIO_TypeDef *port; uint16_t pin; } pin_t;
static const pin_t s_leak[SENSOR_LEAKS] = BOARD_LEAK_PINS;
static sensor_hw_t s_hw;

static int g_i2c_write(void *ctx, uint8_t addr7, const uint8_t *data, uint8_t n)
{
    (void)ctx;
    return HAL_I2C_Master_Transmit(&BOARD_SENSOR_I2C, (uint16_t)(addr7 << 1), (uint8_t *)data, n,
                                   I2C_TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

static int g_i2c_read(void *ctx, uint8_t addr7, uint8_t *data, uint8_t n)
{
    (void)ctx;
    return HAL_I2C_Master_Receive(&BOARD_SENSOR_I2C, (uint16_t)(addr7 << 1), data, n,
                                  I2C_TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

static void short_delay(void)               /* ~100 µs after CS, as Orion waited; generous */
{
    for (volatile uint32_t i = 0; i < SystemCoreClock / 40000u; i++) {}
}

static int g_spi_read(void *ctx, uint8_t *rx, uint8_t n)
{
    (void)ctx;
    HAL_GPIO_WritePin(BOARD_ADC_CS_PORT, BOARD_ADC_CS_PIN, GPIO_PIN_RESET);
    short_delay();
    const HAL_StatusTypeDef r = HAL_SPI_Receive(&BOARD_SENSOR_SPI, rx, n, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(BOARD_ADC_CS_PORT, BOARD_ADC_CS_PIN, GPIO_PIN_SET);
    return r == HAL_OK ? 0 : -1;
}

static uint8_t g_leak_raw(void *ctx)
{
    (void)ctx;
    uint8_t bits = 0;
    for (uint8_t i = 0; i < SENSOR_LEAKS; i++) {
        const bool high = HAL_GPIO_ReadPin(s_leak[i].port, s_leak[i].pin) == GPIO_PIN_SET;
        if (high == (BOARD_LEAK_ACTIVE_HIGH != 0)) bits |= (uint8_t)(1u << i);
    }
    return bits;
}

const sensor_hw_t *sensor_hw_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef io = {0};
    HAL_GPIO_WritePin(BOARD_ADC_CS_PORT, BOARD_ADC_CS_PIN, GPIO_PIN_SET);   /* deselected */
    io.Pin = BOARD_ADC_CS_PIN;
    io.Mode = GPIO_MODE_OUTPUT_PP;
    io.Pull = GPIO_NOPULL;
    io.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_ADC_CS_PORT, &io);

    io.Mode = GPIO_MODE_INPUT;                     /* unconnected input reads "no leak" */
    io.Pull = BOARD_LEAK_ACTIVE_HIGH ? GPIO_PULLDOWN : GPIO_PULLUP;
    for (uint8_t i = 0; i < SENSOR_LEAKS; i++) {
        io.Pin = s_leak[i].pin;
        HAL_GPIO_Init(s_leak[i].port, &io);
    }

    s_hw = (sensor_hw_t){ .ctx = NULL, .i2c_write = g_i2c_write, .i2c_read = g_i2c_read,
                          .spi_read = g_spi_read, .leak_raw = g_leak_raw };
    return &s_hw;
}
