#ifndef UART_COMM_H
#define UART_COMM_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

/* Configura USART1 PA9/PA10 a 115200 8N1. */
void uart_comm_init(void);

/* Entrega al driver la cola donde la ISR debe dejar bytes recibidos. */
void uart_comm_set_rx_queue(QueueHandle_t queue);

/* Envia length bytes por USART1 usando polling bloqueante. */
void uart_comm_send_bytes(const uint8_t *data, size_t length);

/* Devuelve irq: bytes recibidos por interrupcion RXNE. */
uint32_t uart_comm_get_rx_irq_count(void);

/* Devuelve qd: bytes descartados por cola RX llena. */
uint32_t uart_comm_get_rx_drop_count(void);

#endif
