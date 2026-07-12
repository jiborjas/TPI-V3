#include <stddef.h>
#include <stdint.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "../config/app_config.h"
#include "uart_comm.h"

/*
 * TP5 - Driver UART minimo
 * ------------------------
 *
 * Configuración de USART1:
 *
 * - PA9  = TX de la Blue Pill
 * - PA10 = RX de la Blue Pill
 * - 115200 baud
 * - 8 bits
 * - sin paridad
 * - 1 stop bit
 * - sin control de flujo
 *
 * Importante: la ISR solo mete bytes en una cola. No parsea tramas. Eso evita
 * hacer trabajo pesado dentro de la interrupcion.
 */

/* Cola donde la ISR deposita cada byte recibido. */
static QueueHandle_t g_uart_rx_queue = NULL;

/* irq del status: cantidad de interrupciones RXNE atendidas. */
static volatile uint32_t g_uart_rx_irq_count = 0U;

/* qd del status: bytes descartados porque la cola estaba llena. */
static volatile uint32_t g_uart_rx_drop_count = 0U;

/* Permite que tasks.c le entregue al driver la cola RX. */
void uart_comm_set_rx_queue(QueueHandle_t queue)
{
    /* Guardamos el handle para usarlo desde usart1_isr(). */
    g_uart_rx_queue = queue;
}

/* Configura GPIOA y USART1. */
void uart_comm_init(void)
{
    /* Habilitamos clock del puerto GPIOA porque PA9/PA10 viven ahi. */
    rcc_periph_clock_enable(RCC_GPIOA);

    /* Habilitamos clock del periferico USART1. */
    rcc_periph_clock_enable(RCC_USART1);

    /* PA9 como salida alternativa push-pull: USART1_TX. */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO9);

    /* PA10 como entrada con pull-up/pull-down: USART1_RX. */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN, GPIO10);

    /* Dejamos PA10 con pull-up para que no flote si el cable no esta conectado. */
    gpio_set(GPIOA, GPIO10);

    /* Baudrate pedido por el TP. */
    usart_set_baudrate(USART1, UART_BAUDRATE);

    /* 8 bits de datos. */
    usart_set_databits(USART1, 8);

    /* 1 bit de stop. */
    usart_set_stopbits(USART1, USART_STOPBITS_1);

    /* Habilitamos transmision y recepcion. */
    usart_set_mode(USART1, USART_MODE_TX_RX);

    /* Sin paridad. */
    usart_set_parity(USART1, USART_PARITY_NONE);

    /* Sin RTS/CTS. */
    usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

    /* Prioridad de interrupcion compatible con FreeRTOS. */
    nvic_set_priority(NVIC_USART1_IRQ, 0x50);

    /* Habilitamos la interrupcion USART1 en el NVIC. */
    nvic_enable_irq(NVIC_USART1_IRQ);

    /* RXNE dispara interrupcion cuando llega un byte. */
    usart_enable_rx_interrupt(USART1);

    /* Encendemos USART1. */
    usart_enable(USART1);
}

/* Envia bytes por polling bloqueante. */
void uart_comm_send_bytes(const uint8_t *data, size_t length)
{
    /* Indice del byte a transmitir. */
    size_t i;

    /* Si no hay buffer, no hacemos nada. */
    if (data == NULL) {
        return;
    }

    /* Mandamos byte por byte. */
    for (i = 0U; i < length; i++) {
        /*
         * usart_send_blocking espera a que TXE permita cargar el registro DR.
         * Es simple y suficiente para el TP; la cola TX ya desacopla la app.
         */
        usart_send_blocking(USART1, data[i]);
    }
}

/* Getter de irq para status?. */
uint32_t uart_comm_get_rx_irq_count(void)
{
    /* Devuelve cuantos bytes dispararon RXNE. */
    return g_uart_rx_irq_count;
}

/* Getter de qd para status?. */
uint32_t uart_comm_get_rx_drop_count(void)
{
    /* Devuelve cuantos bytes no entraron en la cola RX. */
    return g_uart_rx_drop_count;
}

/* ISR real asociada a USART1. */
void usart1_isr(void)
{
    /* FreeRTOS usa esto para saber si debe cambiar de tarea al salir de la ISR. */
    BaseType_t higher_priority_task_woken = pdFALSE;

    /* Byte recibido desde el registro DR. */
    uint8_t byte;

    /*
     * RXNE significa "Receive data register not empty".
     * Si no esta activo, esta interrupcion no era por un byte recibido.
     */
    if (usart_get_flag(USART1, USART_SR_RXNE) == 0) {
        return;
    }

    /* Leer DR con usart_recv() tambien limpia RXNE. */
    byte = (uint8_t) usart_recv(USART1);

    /* Contamos el byte recibido por interrupcion. */
    g_uart_rx_irq_count++;

    /* Solo encolamos si tasks_start() ya configuro la cola. */
    if (g_uart_rx_queue != NULL) {
        /* En ISR se usa xQueueSendFromISR(), no xQueueSend(). */
        if (xQueueSendFromISR(g_uart_rx_queue, &byte, &higher_priority_task_woken) != pdPASS) {
            /* Si la cola estaba llena, registramos descarte. */
            g_uart_rx_drop_count++;
        }
    }

    /* Si una tarea de mayor prioridad se desperto, FreeRTOS puede conmutar ya. */
    portYIELD_FROM_ISR(higher_priority_task_woken);
}
