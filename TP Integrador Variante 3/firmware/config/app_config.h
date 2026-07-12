#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * TP Integrador - Variante 3: Caja de comandos fisica
 * ---------------------------------------------------
 * Configuracion central de la aplicacion. Todos los numeros "magicos"
 * viven aca para que el informe pueda citarlos una sola vez.
 */

/* ------------------------------------------------------------------ */
/* UART (Etapa 1)                                                      */
/* ------------------------------------------------------------------ */

/* Baudrate pedido por el PDF: 115200 8N1 en PA9/PA10. */
#define UART_BAUDRATE                 115200U

/* Bytes que puede guardar la cola que escribe la ISR de USART1. */
#define UART_RX_ISR_QUEUE_LENGTH      64U

/* Mensajes logicos que pueden esperar a ser transmitidos por UART. */
#define UART_TX_QUEUE_LENGTH          8U

/* Mensajes completos que pueden esperar a la tarea de aplicacion. */
#define APP_MESSAGE_QUEUE_LENGTH      8U

/* Eventos de boton pendientes hacia la aplicacion. */
#define BUTTON_EVENT_QUEUE_LENGTH     4U

/* ------------------------------------------------------------------ */
/* Protocolo (Etapa 1)                                                 */
/* ------------------------------------------------------------------ */

/* Payload maximo pedido por la consigna general del protocolo. */
#define PROTOCOL_MAX_PAYLOAD_LENGTH   48U

/* Frame maximo suficiente para @LL:TTT:PAYLOAD:CC\n. */
#define PROTOCOL_MAX_FRAME_LENGTH     64U

/* ------------------------------------------------------------------ */
/* Nucleo de perifericos (Etapa 2)                                     */
/* ------------------------------------------------------------------ */

/* Periodo de muestreo del potenciometro en PA0 (base temporal TIM3). */
#define ADC_SAMPLE_PERIOD_MS          500U

/* Anti-rebote del pulsador: la presion se confirma tras este tiempo. */
#define BUTTON_DEBOUNCE_MS            50U

/* Frecuencia del PWM del LED (TIM2 CH2, PA1). */
#define LED_PWM_FREQUENCY_HZ          1000U

/* Frecuencia del tono del buzzer pasivo (TIM4 CH1, PB6). */
#define BUZZER_TONE_FREQUENCY_HZ      2000U

/* ------------------------------------------------------------------ */
/* Variante 3: caja de comandos (Etapa 3)                              */
/* ------------------------------------------------------------------ */

/* Resolucion del motor de patrones de LED y buzzer. */
#define SIGNALS_TICK_MS               10U

/* Si el bridge no manda ACK:ok, reenviamos el EVT cada 500 ms. */
#define ACK_RETRY_PERIOD_MS           500U

/* Reintentos antes de declarar ERR:timeout y desistir. */
#define ACK_MAX_RETRIES               5U

/* Duracion de cada pitido del codigo de modo y del silencio entre pitidos. */
#define BEEP_ON_MS                    120U
#define BEEP_OFF_MS                   120U

/* Patron PATROL: destello lento 1 Hz (500 ms encendido / 500 ms apagado). */
#define PATTERN_PATROL_HALF_MS        500U

/* Patron DEPLOY: destello rapido 4 Hz (125 ms encendido / 125 ms apagado). */
#define PATTERN_DEPLOY_HALF_MS        125U

/* Patron RETURN: doble destello (on/off/on/pausa larga). */
#define PATTERN_RETURN_FLASH_MS       100U
#define PATTERN_RETURN_PAUSE_MS       700U

/* Brillo del LED cuando esta "encendido" dentro de un patron (0-100). */
#define LED_ON_DUTY_PERCENT           100U

/* ------------------------------------------------------------------ */
/* Evidencia de Etapa 2                                                */
/* ------------------------------------------------------------------ */

/*
 * Con 1, el firmware ademas emite DAT:adc=NNNN cada ADC_SAMPLE_PERIOD_MS
 * para capturar las lecturas del potenciometro en el monitor serie
 * (evidencia minima de la Etapa 2). Para la Variante 3 final dejar en 0:
 * la consigna solo pide DAT:param=NNN junto con cada cambio de modo.
 */
#define ETAPA2_ADC_REPORT             0

#endif
