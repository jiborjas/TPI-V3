#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "../config/app_config.h"
#include "../drivers/board_io.h"
#include "../drivers/uart_comm.h"
#include "../protocol/parser.h"
#include "../protocol/protocol.h"
#include "command_box.h"
#include "signals.h"
#include "tasks.h"

/*
 * TP Integrador - Variante 3: integracion FreeRTOS
 * ------------------------------------------------
 *
 * Flujo de datos:
 *
 *   USART1 ISR --> g_uart_rx_queue --> task_rx_parser --> g_app_queue
 *   EXTI1/TIM3 ISR --> g_button_queue ------------------> task_app
 *   task_app --(cbox callback)--> g_uart_tx_queue --> task_uart_tx --> USART1
 *   task_signals: cada 10 ms actualiza LED (TIM2) y buzzer (TIM4)
 *
 * Corre por interrupcion: recepcion UART (byte a byte), base temporal de
 * 1 ms (TIM3: ADC periodico + anti-rebote) y flanco del pulsador (EXTI1).
 * Todo lo demas corre en tareas.
 */

/* Colas. */
static QueueHandle_t g_uart_rx_queue;     /* bytes crudos desde la ISR */
static QueueHandle_t g_app_queue;         /* mensajes validos hacia la app */
static QueueHandle_t g_uart_tx_queue;     /* mensajes logicos hacia UART */
static QueueHandle_t g_button_queue;      /* presiones confirmadas */

/* Estado de la variante y del motor de senales. */
static cbox_t g_cbox;
static signals_t g_signals;

/* Contadores de diagnostico del parser. */
static volatile uint32_t g_parser_error_count = 0U;

/* ------------------------------------------------------------------ */
/* Callbacks que conectan la logica pura con el mundo real             */
/* ------------------------------------------------------------------ */

/* La caja de comandos emite mensajes: se encolan hacia task_uart_tx. */
static void cbox_send_to_tx_queue(void *context, protocol_type_t type,
                                  const char *payload)
{
    protocol_message_t message;

    (void) context;

    if (protocol_message_set(&message, type, payload)) {
        xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
    }
}

/* El motor de senales escribe el duty del LED en TIM2. */
static void signals_led_out(void *context, uint8_t duty_percent)
{
    (void) context;
    board_io_set_led_duty(duty_percent);
}

/* El motor de senales enciende o calla el buzzer en TIM4. */
static void signals_buzzer_out(void *context, bool on)
{
    (void) context;
    board_io_set_buzzer(on);
}

/* ------------------------------------------------------------------ */
/* Tareas                                                              */
/* ------------------------------------------------------------------ */

/* Consume bytes de la ISR, corre la FSM y entrega mensajes validos. */
static void task_rx_parser(void *args)
{
    static parser_t parser;
    protocol_message_t message;
    uint8_t byte;
    parser_result_t result;

    (void) args;

    parser_init(&parser);

    for (;;) {
        if (xQueueReceive(g_uart_rx_queue, &byte, portMAX_DELAY) != pdPASS) {
            continue;
        }

        result = parser_consume_byte(&parser, byte, &message);

        if (result == PARSER_RESULT_MESSAGE_READY) {
            xQueueSend(g_app_queue, &message, portMAX_DELAY);
        } else if (result == PARSER_RESULT_ERROR) {
            /*
             * Trama invalida (checksum, longitud o caracteres): ademas de
             * resincronizar, respondemos ERR:bounds (lazo de Etapa 1).
             */
            g_parser_error_count++;
            cbox_send_to_tx_queue(NULL, PROTOCOL_TYPE_ERR, "bounds");
        }
    }
}

/* Logica principal: boton -> cambio de modo; mensajes -> ACK; reintentos. */
static void task_app(void *args)
{
    protocol_message_t message;
    button_event_t button;
#if ETAPA2_ADC_REPORT
    uint32_t next_adc_report_ms = 0U;
    char adc_payload[13];
#endif  // ETAPA2_ADC_REPORT

    (void) args;

    for (;;) {
#if ETAPA2_ADC_REPORT
        /* Evidencia Etapa 2: DAT:adc=NNNN periodico con ancho fijo. */
        if ((int32_t) (board_io_get_uptime_ms() - next_adc_report_ms) >= 0) {
            next_adc_report_ms = board_io_get_uptime_ms() + ADC_SAMPLE_PERIOD_MS;
            (void) snprintf(adc_payload, sizeof(adc_payload), "adc=%04u",
                            (unsigned) board_io_get_adc_raw());
            cbox_send_to_tx_queue(NULL, PROTOCOL_TYPE_DAT, adc_payload);
        }
#endif  // ETAPA2_ADC_REPORT

        /* Boton confirmado por el anti-rebote de TIM3. */
        if (xQueueReceive(g_button_queue, &button, 0) == pdPASS) {
            if (cbox_on_button(&g_cbox, board_io_get_param(),
                               button.timestamp_ms)) {
                signals_enter_mode(&g_signals, g_cbox.mode,
                                   button.timestamp_ms);
            }
        }

        /* Mensajes ya validados por el parser (ACK del bridge, etc.). */
        if (xQueueReceive(g_app_queue, &message, 0) == pdPASS) {
            cbox_on_message(&g_cbox, &message, board_io_get_uptime_ms());
        }

        /* Reenvio de EVT sin ACK. */
        cbox_on_tick(&g_cbox, board_io_get_uptime_ms());

        /* 10 ms de resolucion es suficiente para 500 ms de timeout. */
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

/* Patrones de LED y codigo de pitidos, cada SIGNALS_TICK_MS. */
static void task_signals(void *args)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void) args;

    for (;;) {
        signals_tick(&g_signals, board_io_get_uptime_ms());
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SIGNALS_TICK_MS));
    }
}

/* Serializa mensajes logicos como @LL:TTT:PAYLOAD:CC\n y los transmite. */
static void task_uart_tx(void *args)
{
    protocol_message_t message;
    char frame[PROTOCOL_MAX_FRAME_LENGTH];
    size_t frame_length;

    (void) args;

    for (;;) {
        if (xQueueReceive(g_uart_tx_queue, &message, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (protocol_encode_frame(&message, frame, sizeof(frame), &frame_length)) {
            uart_comm_send_bytes((const uint8_t *) frame, frame_length);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Arranque                                                            */
/* ------------------------------------------------------------------ */

void tasks_start(void)
{
    g_uart_rx_queue = xQueueCreate(UART_RX_ISR_QUEUE_LENGTH, sizeof(uint8_t));
    g_app_queue     = xQueueCreate(APP_MESSAGE_QUEUE_LENGTH, sizeof(protocol_message_t));
    g_uart_tx_queue = xQueueCreate(UART_TX_QUEUE_LENGTH, sizeof(protocol_message_t));
    g_button_queue  = xQueueCreate(BUTTON_EVENT_QUEUE_LENGTH, sizeof(button_event_t));

    /* No es seguro iniciar ISR o tareas con una cola sin reservar. */
    configASSERT(g_uart_rx_queue != NULL);
    configASSERT(g_app_queue != NULL);
    configASSERT(g_uart_tx_queue != NULL);
    configASSERT(g_button_queue != NULL);

    /* Las ISR necesitan conocer sus colas antes de habilitarse. */
    uart_comm_set_rx_queue(g_uart_rx_queue);
    board_io_set_button_queue(g_button_queue);

    /* Logica pura: caja de comandos y motor de senales. */
    cbox_init(&g_cbox, cbox_send_to_tx_queue, NULL);
    signals_init(&g_signals, signals_led_out, signals_buzzer_out, NULL);
    /* Al energizar: modo STOP (LED fijo), sin pitidos hasta la primera presion. */

    configASSERT(xTaskCreate(task_rx_parser, "rxparse", 256, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(task_app, "app", 256, NULL, 2, NULL) == pdPASS);
    configASSERT(xTaskCreate(task_signals, "signals", 128, NULL, 2, NULL) == pdPASS);
    configASSERT(xTaskCreate(task_uart_tx, "uarttx", 256, NULL, 1, NULL) == pdPASS);

    vTaskStartScheduler();
}
