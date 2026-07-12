#ifndef BOARD_IO_H
#define BOARD_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

/*
 * TP Integrador - Variante 3
 * --------------------------
 * board_io concentra los cuatro perifericos del nucleo comun:
 *
 *  - ADC1 canal 0 (PA0): potenciometro, muestreado cada ADC_SAMPLE_PERIOD_MS
 *    usando TIM3 como base temporal de 1 ms.
 *  - TIM2 CH2 (PA1): PWM de 1 kHz para el brillo del LED de modo.
 *  - TIM4 CH1 (PB6): PWM de 2 kHz para el tono del buzzer pasivo.
 *  - EXTI1 (PB1): pulsador con pull-down externo y anti-rebote por TIM3.
 *
 * Ningun modulo de aplicacion toca registros: todo pasa por esta API.
 */

/* Evento que la ISR de TIM3 encola cuando confirma una presion valida. */
typedef struct {
    uint32_t timestamp_ms;   /* Tick de 1 ms en que se confirmo la presion. */
} button_event_t;

/* Inicializa GPIO, ADC1, TIM2 (PWM LED), TIM4 (PWM buzzer), TIM3 y EXTI1. */
void board_io_init(void);

/* Cola donde la ISR deposita eventos de boton ya anti-rebotados. */
void board_io_set_button_queue(QueueHandle_t queue);

/* Ultimo valor crudo del ADC (0-4095), actualizado cada 500 ms por TIM3. */
uint16_t board_io_get_adc_raw(void);

/* Valor del potenciometro mapeado a 0-255 (parametro auxiliar del comando). */
uint8_t board_io_get_param(void);

/* Duty del LED en porcentaje 0-100 (0 apaga, 100 brillo maximo). */
void board_io_set_led_duty(uint8_t duty_percent);

/* Enciende o apaga el tono del buzzer (PWM 50% o 0%). */
void board_io_set_buzzer(bool on);

/* Milisegundos desde el arranque segun TIM3 (para diagnostico). */
uint32_t board_io_get_uptime_ms(void);

#endif
