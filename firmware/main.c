#include <libopencm3/stm32/rcc.h>

#include "app/tasks.h"
#include "drivers/board_io.h"
#include "drivers/uart_comm.h"
#include "platform/system_init.h"

/*
 * TP Integrador - Variante 3: Caja de comandos fisica
 * ---------------------------------------------------
 * Blue Pill (STM32F103C8T6) como caja de comandos de un Unitree Go2/G1
 * a traves del bridge ROS 2 de la catedra.
 *
 * Conexiones:
 *   PA9  -> TX hacia el USB-UART del bridge (115200 8N1)
 *   PA10 <- RX desde el USB-UART del bridge
 *   PA0  <- potenciometro 10k (parametro auxiliar 0-255)
 *   PA1  -> R 220 ohm -> LED de modo (PWM TIM2_CH2, 1 kHz)
 *   PB6  -> buzzer pasivo (PWM TIM4_CH1, 2 kHz) [con driver si hace falta]
 *   PB1  <- pulsador a 3.3 V con pull-down externo de 10k (EXTI1)
 */
int main(void)
{
    /* Blue Pill a 72 MHz con cristal externo de 8 MHz. */
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

    /* AFIO para funciones alternativas (USART1, EXTI). */
    system_init_board();

    /* Nucleo de perifericos: ADC, PWM LED, PWM buzzer, TIM3, EXTI1. */
    board_io_init();

    /* USART1 PA9/PA10 a 115200 8N1 con RX por interrupcion. */
    uart_comm_init();

    /* Colas + tareas + scheduler de FreeRTOS. No retorna. */
    tasks_start();

    /* Si el scheduler devolvio el control, quedamos en un loop seguro. */
    for (;;) {
    }
}
