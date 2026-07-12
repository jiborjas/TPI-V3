#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdbool.h>
#include <stdint.h>

#include "command_box.h"

/*
 * TP Integrador - Variante 3: motor de senales
 * --------------------------------------------
 * Traduce el modo actual a patrones de LED y el cambio de modo a un
 * codigo de pitidos. Es logica pura basada en tiempo: recibe now_ms y
 * escribe sus salidas por callbacks, asi tambien se puede testear en PC.
 *
 * Patrones de LED (consigna):
 *   PATROL -> destello lento 1 Hz
 *   STOP   -> fijo encendido
 *   DEPLOY -> destello rapido 4 Hz
 *   RETURN -> doble destello (flash-flash-pausa)
 *
 * Codigo acustico al entrar a un modo: 1/2/3/4 pitidos.
 */

typedef void (*signals_led_fn)(void *context, uint8_t duty_percent);
typedef void (*signals_buzzer_fn)(void *context, bool on);

typedef struct {
    cbox_mode_t mode;            /* Modo cuyo patron se esta mostrando. */
    uint32_t pattern_start_ms;   /* Comienzo de la fase del patron. */

    uint8_t beeps_remaining;     /* Pitidos que faltan del codigo actual. */
    bool beep_on;                /* El buzzer esta en la mitad "on" del pitido. */
    uint32_t beep_edge_ms;       /* Momento del proximo flanco del pitido. */

    signals_led_fn set_led;
    signals_buzzer_fn set_buzzer;
    void *context;
} signals_t;

void signals_init(signals_t *signals, signals_led_fn set_led,
                  signals_buzzer_fn set_buzzer, void *context);

/* Cambia el patron de LED y dispara el codigo de pitidos del modo. */
void signals_enter_mode(signals_t *signals, cbox_mode_t mode, uint32_t now_ms);

/* Debe llamarse cada SIGNALS_TICK_MS; actualiza LED y buzzer. */
void signals_tick(signals_t *signals, uint32_t now_ms);

#endif
