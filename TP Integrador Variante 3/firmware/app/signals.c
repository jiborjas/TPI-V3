#include <stddef.h>
#include <string.h>

#include "../config/app_config.h"
#include "signals.h"

/*
 * Motor de senales de la Variante 3. Ver signals.h.
 *
 * Todos los patrones se calculan por fase: (now - pattern_start) % periodo.
 * Asi el patron es deterministico y no acumula error aunque un tick llegue
 * tarde.
 */

void signals_init(signals_t *signals, signals_led_fn set_led,
                  signals_buzzer_fn set_buzzer, void *context)
{
    if (signals == NULL) {
        return;
    }

    memset(signals, 0, sizeof(*signals));
    signals->mode = CBOX_MODE_STOP;
    signals->set_led = set_led;
    signals->set_buzzer = set_buzzer;
    signals->context = context;
}

void signals_enter_mode(signals_t *signals, cbox_mode_t mode, uint32_t now_ms)
{
    if (signals == NULL) {
        return;
    }

    signals->mode = mode;
    signals->pattern_start_ms = now_ms;

    /* Codigo de pitidos del modo: arranca con el buzzer sonando. */
    signals->beeps_remaining = cbox_mode_beep_count(mode);
    signals->beep_on = (signals->beeps_remaining > 0U);
    signals->beep_edge_ms = now_ms + BEEP_ON_MS;
}

/* Devuelve true si el LED debe estar encendido en esta fase del patron. */
static bool led_pattern_is_on(cbox_mode_t mode, uint32_t phase_ms)
{
    uint32_t period;

    switch (mode) {
    case CBOX_MODE_PATROL:
        /* 1 Hz: 500 ms on / 500 ms off. */
        period = 2U * PATTERN_PATROL_HALF_MS;
        return (phase_ms % period) < PATTERN_PATROL_HALF_MS;

    case CBOX_MODE_STOP:
        /* Fijo encendido. */
        return true;

    case CBOX_MODE_DEPLOY:
        /* 4 Hz: 125 ms on / 125 ms off. */
        period = 2U * PATTERN_DEPLOY_HALF_MS;
        return (phase_ms % period) < PATTERN_DEPLOY_HALF_MS;

    case CBOX_MODE_RETURN: {
        /* Doble destello: on(100) off(100) on(100) pausa(700). */
        const uint32_t f = PATTERN_RETURN_FLASH_MS;

        period = (3U * f) + PATTERN_RETURN_PAUSE_MS;
        phase_ms %= period;

        if (phase_ms < f) {
            return true;            /* Primer flash. */
        }
        if (phase_ms < (2U * f)) {
            return false;           /* Silencio entre flashes. */
        }
        if (phase_ms < (3U * f)) {
            return true;            /* Segundo flash. */
        }
        return false;               /* Pausa larga. */
    }

    default:
        return false;
    }
}

void signals_tick(signals_t *signals, uint32_t now_ms)
{
    uint32_t phase;
    bool led_on;

    if (signals == NULL) {
        return;
    }

    /* --- LED: patron del modo actual --- */
    phase = now_ms - signals->pattern_start_ms;
    led_on = led_pattern_is_on(signals->mode, phase);

    if (signals->set_led != NULL) {
        signals->set_led(signals->context,
                         led_on ? (uint8_t) LED_ON_DUTY_PERCENT : 0U);
    }

    /* --- Buzzer: codigo de pitidos del ultimo cambio de modo --- */
    if (signals->beeps_remaining > 0U) {
        if ((int32_t) (now_ms - signals->beep_edge_ms) >= 0) {
            if (signals->beep_on) {
                /* Termina la mitad sonora: pasa al silencio entre pitidos. */
                signals->beep_on = false;
                signals->beeps_remaining--;
                signals->beep_edge_ms = now_ms + BEEP_OFF_MS;
            } else if (signals->beeps_remaining > 0U) {
                /* Comienza el siguiente pitido. */
                signals->beep_on = true;
                signals->beep_edge_ms = now_ms + BEEP_ON_MS;
            }
        }
    }

    if (signals->set_buzzer != NULL) {
        signals->set_buzzer(signals->context,
                            (signals->beeps_remaining > 0U) && signals->beep_on);
    }
}
