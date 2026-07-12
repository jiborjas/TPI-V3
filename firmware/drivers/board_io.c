#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "../config/app_config.h"
#include "board_io.h"

/*
 * TP Integrador - Variante 3: nucleo de perifericos
 * -------------------------------------------------
 *
 * Calculos de timers (SYSCLK = 72 MHz, APB1 x2 para timers = 72 MHz):
 *
 * TIM2 (PWM LED, PA1 = TIM2_CH2):
 *     f_pwm = 72 MHz / (PSC+1) / (ARR+1)
 *     PSC = 71  -> contador a 1 MHz
 *     ARR = 999 -> 1 MHz / 1000 = 1 kHz
 *     duty% -> CCR2 = duty * 10  (0..1000)
 *
 * TIM4 (PWM buzzer, PB6 = TIM4_CH1):
 *     PSC = 71  -> contador a 1 MHz
 *     ARR = 499 -> 1 MHz / 500 = 2 kHz (tono audible para buzzer pasivo)
 *     encendido: CCR1 = 250 (50 % duty); apagado: CCR1 = 0
 *
 * TIM3 (base temporal de 1 ms):
 *     PSC = 7199 -> contador a 10 kHz
 *     ARR = 9    -> update cada 10 ticks = 1 ms
 *     En cada update: uptime++, ventana de anti-rebote y periodo de ADC.
 */

#define LED_PWM_PSC        71U
#define LED_PWM_ARR        999U

#define BUZZER_PWM_PSC     71U
#define BUZZER_PWM_ARR     499U
#define BUZZER_ON_CCR      ((BUZZER_PWM_ARR + 1U) / 2U)   /* 50 % duty */

#define TIMEBASE_PSC       7199U   /* 72 MHz / 7200 = 10 kHz */
#define TIMEBASE_ARR       9U      /* 10 kHz / 10 = 1 kHz -> 1 ms */

#define ADC_MAX_VALUE      4095U

/* Cola hacia la aplicacion con presiones de boton confirmadas. */
static QueueHandle_t g_button_queue = NULL;

/* Milisegundos desde el arranque, mantenidos por la ISR de TIM3. */
static volatile uint32_t g_uptime_ms = 0U;

/* Ultimo valor crudo leido del ADC (0-4095). */
static volatile uint16_t g_adc_raw = 0U;

/* Contador hacia la proxima muestra de ADC. */
static volatile uint16_t g_adc_elapsed_ms = 0U;

/* Anti-rebote: cuenta regresiva iniciada por EXTI1. */
static volatile bool g_debounce_active = false;
static volatile uint16_t g_debounce_remaining_ms = 0U;

/* ------------------------------------------------------------------ */
/* Inicializacion                                                      */
/* ------------------------------------------------------------------ */

static void adc_setup(void)
{
    /* Prescaler del ADC: 72 MHz / 6 = 12 MHz (limite del ADC: 14 MHz). */
    rcc_set_adcpre(RCC_CFGR_ADCPRE_PCLK2_DIV6);

    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_ADC1);

    /* PA0 como entrada analogica (canal 0 del ADC1). */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO0);

    adc_power_off(ADC1);
    adc_disable_scan_mode(ADC1);
    adc_set_single_conversion_mode(ADC1);

    /* 55.5 ciclos de muestreo: lectura estable para un pote de 10k. */
    adc_set_sample_time(ADC1, ADC_CHANNEL0, ADC_SMPR_SMP_55DOT5CYC);

    adc_power_on(ADC1);

    /* Espera breve y calibracion recomendada por el manual de referencia. */
    for (volatile uint32_t i = 0U; i < 800000U; i++) {
        __asm__("nop");
    }
    adc_reset_calibration(ADC1);
    adc_calibrate(ADC1);
}

static void led_pwm_setup(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_TIM2);

    /* PA1 = TIM2_CH2 en funcion alternativa push-pull. */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO1);

    timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM2, LED_PWM_PSC);
    timer_set_period(TIM2, LED_PWM_ARR);

    timer_set_oc_mode(TIM2, TIM_OC2, TIM_OCM_PWM1);
    timer_set_oc_value(TIM2, TIM_OC2, 0U);
    timer_enable_oc_output(TIM2, TIM_OC2);

    timer_enable_preload(TIM2);
    timer_continuous_mode(TIM2);
    timer_enable_counter(TIM2);
}

static void buzzer_pwm_setup(void)
{
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_TIM4);

    /* PB6 = TIM4_CH1 en funcion alternativa push-pull. */
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO6);

    timer_set_mode(TIM4, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM4, BUZZER_PWM_PSC);
    timer_set_period(TIM4, BUZZER_PWM_ARR);

    timer_set_oc_mode(TIM4, TIM_OC1, TIM_OCM_PWM1);
    timer_set_oc_value(TIM4, TIM_OC1, 0U);   /* Arranca en silencio. */
    timer_enable_oc_output(TIM4, TIM_OC1);

    timer_enable_preload(TIM4);
    timer_continuous_mode(TIM4);
    timer_enable_counter(TIM4);
}

static void timebase_setup(void)
{
    rcc_periph_clock_enable(RCC_TIM3);

    timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM3, TIMEBASE_PSC);
    timer_set_period(TIM3, TIMEBASE_ARR);

    /* Prioridad compatible con FreeRTOS (numericamente >= 0x50). */
    nvic_set_priority(NVIC_TIM3_IRQ, 0x60);
    nvic_enable_irq(NVIC_TIM3_IRQ);

    timer_enable_irq(TIM3, TIM_DIER_UIE);
    timer_continuous_mode(TIM3);
    timer_enable_counter(TIM3);
}

static void button_setup(void)
{
    rcc_periph_clock_enable(RCC_GPIOB);
    /* AFIO ya habilitado en system_init_board(); EXTI necesita AFIO. */

    /* PB1 entrada flotante: el pull-down de 10k es externo (consigna). */
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO1);

    exti_select_source(EXTI1, GPIOB);
    exti_set_trigger(EXTI1, EXTI_TRIGGER_RISING);   /* Presion: 0 -> 3.3 V. */
    exti_enable_request(EXTI1);

    nvic_set_priority(NVIC_EXTI1_IRQ, 0x70);
    nvic_enable_irq(NVIC_EXTI1_IRQ);
}

void board_io_init(void)
{
    adc_setup();
    led_pwm_setup();
    buzzer_pwm_setup();
    timebase_setup();
    button_setup();
}

void board_io_set_button_queue(QueueHandle_t queue)
{
    g_button_queue = queue;
}

/* ------------------------------------------------------------------ */
/* API para la aplicacion                                              */
/* ------------------------------------------------------------------ */

uint16_t board_io_get_adc_raw(void)
{
    return g_adc_raw;
}

uint8_t board_io_get_param(void)
{
    /* Mapeo lineal 0-4095 -> 0-255 (division entera por 16.06 ~ >>4). */
    return (uint8_t) (((uint32_t) g_adc_raw * 255U) / ADC_MAX_VALUE);
}

void board_io_set_led_duty(uint8_t duty_percent)
{
    uint32_t ccr;

    if (duty_percent > 100U) {
        duty_percent = 100U;
    }

    /* duty% -> CCR2 en 0..(ARR+1). */
    ccr = ((uint32_t) duty_percent * (LED_PWM_ARR + 1U)) / 100U;
    timer_set_oc_value(TIM2, TIM_OC2, ccr);
}

void board_io_set_buzzer(bool on)
{
    timer_set_oc_value(TIM4, TIM_OC1, on ? BUZZER_ON_CCR : 0U);
}

uint32_t board_io_get_uptime_ms(void)
{
    return g_uptime_ms;
}

/* ------------------------------------------------------------------ */
/* Interrupciones                                                      */
/* ------------------------------------------------------------------ */

/* EXTI1: flanco ascendente en PB1. Solo arma la ventana de anti-rebote. */
void exti1_isr(void)
{
    exti_reset_request(EXTI1);

    /* Si ya hay una ventana corriendo, este flanco es rebote: se ignora. */
    if (!g_debounce_active) {
        g_debounce_active = true;
        g_debounce_remaining_ms = BUTTON_DEBOUNCE_MS;
    }
}

/* TIM3: base temporal de 1 ms. ADC periodico + anti-rebote por timer. */
void tim3_isr(void)
{
    BaseType_t woken = pdFALSE;

    if (!timer_get_flag(TIM3, TIM_SR_UIF)) {
        return;
    }
    timer_clear_flag(TIM3, TIM_SR_UIF);

    g_uptime_ms++;

    /* --- Muestreo periodico del ADC (lectura corta por polling) --- */
    g_adc_elapsed_ms++;
    if (g_adc_elapsed_ms >= ADC_SAMPLE_PERIOD_MS) {
        g_adc_elapsed_ms = 0U;

        adc_start_conversion_direct(ADC1);
        /* ~(55.5+12.5) ciclos a 12 MHz ~ 6 us: espera acotada y segura. */
        while (!adc_eoc(ADC1)) {
        }
        g_adc_raw = (uint16_t) adc_read_regular(ADC1);
    }

    /* --- Anti-rebote del pulsador --- */
    if (g_debounce_active) {
        if (g_debounce_remaining_ms > 0U) {
            g_debounce_remaining_ms--;
        }

        if (g_debounce_remaining_ms == 0U) {
            g_debounce_active = false;

            /* Presion valida solo si PB1 sigue en alto tras 50 ms. */
            if (gpio_get(GPIOB, GPIO1) != 0U) {
                button_event_t event = { .timestamp_ms = g_uptime_ms };

                if (g_button_queue != NULL) {
                    xQueueSendFromISR(g_button_queue, &event, &woken);
                }
            }
        }
    }

    portYIELD_FROM_ISR(woken);
}
