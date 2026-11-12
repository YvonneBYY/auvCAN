/* Minimal stand-in for ST's usbd_def.h + usbd_cdc.h, for tests/test_usbcdc.c only. */
#ifndef FAKE_USBD_CDC_H
#define FAKE_USBD_CDC_H
#include <stdint.h>
typedef enum { USBD_OK = 0U, USBD_BUSY, USBD_EMEM, USBD_FAIL } USBD_StatusTypeDef;
#define USBD_STATE_DEFAULT     0x01U
#define USBD_STATE_CONFIGURED  0x03U
#define CDC_SET_LINE_CODING        0x20U
#define CDC_GET_LINE_CODING        0x21U
#define CDC_SET_CONTROL_LINE_STATE 0x22U
typedef struct {
    int8_t (*Init)(void);
    int8_t (*DeInit)(void);
    int8_t (*Control)(uint8_t cmd, uint8_t *pbuf, uint16_t length);
    int8_t (*Receive)(uint8_t *Buf, uint32_t *Len);
    int8_t (*TransmitCplt)(uint8_t *Buf, uint32_t *Len, uint8_t epnum);
} USBD_CDC_ItfTypeDef;
typedef struct { volatile uint8_t dev_state; } USBD_HandleTypeDef;
uint8_t USBD_CDC_RegisterInterface(USBD_HandleTypeDef *pdev, USBD_CDC_ItfTypeDef *fops);
uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length);
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff);
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev);
#endif
