#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/*
 * Fallos de infraestructura FreeRTOS.
 *
 * No hay un canal de log confiable cuando falta heap o una pila ya se
 * corrompio. Conservamos una causa visible desde GDB y detenemos el sistema
 * con las interrupciones deshabilitadas para no propagar la corrupcion.
 */
typedef enum {
    RTOS_FATAL_NONE = 0U,
    RTOS_FATAL_MALLOC,
    RTOS_FATAL_STACK_OVERFLOW
} rtos_fatal_reason_t;

volatile rtos_fatal_reason_t g_rtos_fatal_reason = RTOS_FATAL_NONE;
volatile const char *g_rtos_overflow_task = NULL;

static void rtos_halt(void)
{
    taskDISABLE_INTERRUPTS();

    for (;;) {
        /* Punto estable para inspeccionar g_rtos_* mediante ST-Link/GDB. */
    }
}

void vApplicationMallocFailedHook(void)
{
    g_rtos_fatal_reason = RTOS_FATAL_MALLOC;
    rtos_halt();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void) task;
    g_rtos_fatal_reason = RTOS_FATAL_STACK_OVERFLOW;
    g_rtos_overflow_task = task_name;
    rtos_halt();
}
