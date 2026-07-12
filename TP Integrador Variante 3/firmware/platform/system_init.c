#include <libopencm3/stm32/rcc.h>

#include "system_init.h"

/* Inicializacion comun de la placa. */
void system_init_board(void)
{
    /*
     * AFIO maneja funciones alternativas en STM32F1.
     * USART1 usa funcion alternativa en PA9/PA10, por eso lo habilitamos.
     */
    rcc_periph_clock_enable(RCC_AFIO);
}
