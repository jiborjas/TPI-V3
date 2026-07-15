#ifndef COMMAND_BOX_H
#define COMMAND_BOX_H

#include <stdbool.h>
#include <stdint.h>

#include "../protocol/protocol.h"

/*
 * TP Integrador - Variante 3: Caja de comandos fisica
 * ---------------------------------------------------
 * Logica pura de la variante. No toca hardware ni FreeRTOS: recibe
 * eventos (boton, mensaje, tick de tiempo) y emite mensajes de protocolo
 * a traves de un callback. Eso permite compilarla y testearla en la PC.
 *
 * Modos y ciclo (consigna):  PATROL -> STOP -> DEPLOY -> RETURN -> PATROL
 *
 * En cada presion valida del boton, si no hay una transaccion pendiente, la
 * caja:
 *   1. avanza al modo siguiente,
 *   2. envia EVT:cmd_<modo> + STS:mode=<MODO> + DAT:param=NNN,
 *   3. queda esperando ACK:ok; si no llega en ACK_RETRY_PERIOD_MS
 *      reenvia el EVT hasta ACK_MAX_RETRIES veces y luego ERR:timeout.
 *
 * Mientras espera ese ACK no acepta otra presion: el protocolo no contiene un
 * identificador de transaccion, por lo que permitir dos EVT pendientes haria
 * imposible asociar ACK:ok con el evento correcto.
 *
 * Nota de la consigna: el STS de RETURN es mode=STOP (el robot vuelve a
 * base y queda detenido), por eso RETURN no tiene un STS propio.
 */

typedef enum {
    CBOX_MODE_PATROL = 0,
    CBOX_MODE_STOP,
    CBOX_MODE_DEPLOY,
    CBOX_MODE_RETURN,
    CBOX_MODE_COUNT
} cbox_mode_t;

/* Callback con el que la caja publica mensajes hacia la cola TX. */
typedef void (*cbox_send_fn)(void *context, protocol_type_t type, const char *payload);


// Estructura de control de la caja de comandos: modo, estado de ACK y estadisticas
typedef struct {
    cbox_mode_t mode;             /* Modo actual de la caja. */

    bool ack_pending;             /* Hay un EVT esperando ACK:ok. */
    char pending_evt[13];         /* Payload del EVT pendiente de ACK. */
    uint32_t ack_deadline_ms;     /* Momento del proximo reenvio. */
    uint8_t retries_left;         /* Reenvios que quedan antes de ERR. */

    uint32_t mode_changes;        /* Diagnostico: presiones validas. */
    uint32_t ack_timeouts;        /* Diagnostico: EVT que agotaron reenvios. */
    uint32_t ignored_button_presses; /* Pulsaciones durante una espera de ACK. */

    cbox_send_fn send;            /* Como se emiten los mensajes. */
    void *send_context;           /* Contexto opaco para el callback. */
} cbox_t;

/* Arranca en STOP (estado seguro) sin emitir tramas hasta la primera presion. */
void cbox_init(cbox_t *box, cbox_send_fn send, void *send_context);

/*
 * Presion valida de boton: cicla modo y emite EVT + STS + DAT.
 * Devuelve false si ya existe un EVT pendiente de ACK:ok.
 */
bool cbox_on_button(cbox_t *box, uint8_t param_value, uint32_t now_ms);

/* Mensaje valido recibido del bridge (tipicamente ACK:ok). */
void cbox_on_message(cbox_t *box, const protocol_message_t *message, uint32_t now_ms);

/* Tick periodico: administra el reenvio del EVT pendiente. */
void cbox_on_tick(cbox_t *box, uint32_t now_ms);

/* Textos asociados al modo (para payloads y para el motor de senales). */
const char *cbox_mode_to_evt_payload(cbox_mode_t mode);
const char *cbox_mode_to_sts_payload(cbox_mode_t mode);

/* Cantidad de pitidos del codigo acustico: PATROL=1 ... RETURN=4. */
uint8_t cbox_mode_beep_count(cbox_mode_t mode);

#endif
