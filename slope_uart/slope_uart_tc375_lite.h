#ifndef SLOPE_UART_TC375_LITE_H
#define SLOPE_UART_TC375_LITE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void slopeUartTc375TxInit(void);
void slopeUartTc375TxPoll(void);

void slopeUartTc375RequestNone(void);
void slopeUartTc375RequestOpen(void);
void slopeUartTc375RequestClose(void);

uint8_t slopeUartTc375TxIsBusy(void);
uint8_t slopeUartTc375ConsumeTxSuccess(uint8_t* out_cmd);

#ifdef __cplusplus
}
#endif

#endif