#include <stdio.h>
#include <string.h>

#include "../config/app_config.h"
#include "command_box.h"

/*
 * Logica de la Variante 3. Ver command_box.h para el contrato completo.
 */

/* Tabla de payloads EVT por modo (consigna, tabla de mensajes). */
static const char *const k_evt_payload[CBOX_MODE_COUNT] = {
    [CBOX_MODE_PATROL] = "cmd_patrol",
    [CBOX_MODE_STOP]   = "cmd_stop",
    [CBOX_MODE_DEPLOY] = "cmd_deploy",
    [CBOX_MODE_RETURN] = "cmd_return",
};

/* Tabla de payloads STS. RETURN informa mode=STOP segun la consigna. */
static const char *const k_sts_payload[CBOX_MODE_COUNT] = {
    [CBOX_MODE_PATROL] = "mode=PATROL",
    [CBOX_MODE_STOP]   = "mode=STOP",
    [CBOX_MODE_DEPLOY] = "mode=DEPLOY",
    [CBOX_MODE_RETURN] = "mode=STOP",
};


/* Codigo de pitidos: 1=PATROL, 2=STOP, 3=DEPLOY, 4=RETURN. */
static const uint8_t k_beep_count[CBOX_MODE_COUNT] = { 1U, 2U, 3U, 4U };


//Traducen el modo actual (PATROL, STOP, etc.) a las cadenas de texto ASCII que
// requiere el protocolo para los mensajes de tipo EVT (eventos) y STS (estado)
const char *cbox_mode_to_evt_payload(cbox_mode_t mode)
{    return (mode < CBOX_MODE_COUNT) ? k_evt_payload[mode] : "";}

const char *cbox_mode_to_sts_payload(cbox_mode_t mode)
{    return (mode < CBOX_MODE_COUNT) ? k_sts_payload[mode] : "";}

//Asigna la cantidad de pitidos del buzzer según el modo: 
// 1 para PATROL, 2 para STOP, 3 para DEPLOY y 4 para RETURN
uint8_t cbox_mode_beep_count(cbox_mode_t mode)
{    return (mode < CBOX_MODE_COUNT) ? k_beep_count[mode] : 0U;}

//OBS: Si el modo es mayor a CBOX_MODE_COUNT, es un modo inválido o desconocido.


//Inicializa la estructura de control, configurando por seguridad el modo inicial
// en STOP y guardando la función de envío para la comunicación con el robot
void cbox_init(cbox_t *box, cbox_send_fn send, void *send_context)
{
    if (box == NULL) {
        return;
    }

    memset(box, 0, sizeof(*box));

    /* Estado seguro al energizar: el robot queda detenido. */
    box->mode = CBOX_MODE_STOP;
    box->send = send;
    box->send_context = send_context;
}

/* Emite un mensaje a traves del callback configurado. */
static void cbox_emit(cbox_t *box, protocol_type_t type, const char *payload)
{
    if (box->send != NULL) {
        box->send(box->send_context, type, payload);
    }
}


/* Inicia una unica transaccion de cambio de modo. */
bool cbox_on_button(cbox_t *box, uint8_t param_value, uint32_t now_ms)
{
    char dat_payload[13];

    if (box == NULL) {
        return false;
    }

    /* ACK:ok no tiene ID; nunca puede haber dos EVT en vuelo. */
    if (box->ack_pending) {
        box->ignored_button_presses++;
        return false;
    }

    /* Ciclo cerrado de la consigna: PATROL -> STOP -> DEPLOY -> RETURN. */
    box->mode = (cbox_mode_t) ((box->mode + 1U) % CBOX_MODE_COUNT);
    box->mode_changes++;    //cicla el estado (con el resto de la división) y registra un cambio.

    /*
     * Ancho fijo del payload numerico: param=NNN con relleno de ceros.
     * %03u garantiza tres digitos aunque el valor sea chico (param=007).
     */
    (void) snprintf(dat_payload, sizeof(dat_payload), "param=%03u",
                    (unsigned) param_value);

    /* Terna de tramas de la consigna: EVT + STS + DAT. */
    cbox_emit(box, PROTOCOL_TYPE_EVT, cbox_mode_to_evt_payload(box->mode));
    cbox_emit(box, PROTOCOL_TYPE_STS, cbox_mode_to_sts_payload(box->mode));
    cbox_emit(box, PROTOCOL_TYPE_DAT, dat_payload);

//  lógica de reintentos automáticos para asegurar que sean confirmados con un ACK:ok
    (void) snprintf(box->pending_evt, sizeof(box->pending_evt), "%s",
                    cbox_mode_to_evt_payload(box->mode));
    box->ack_pending = true;
    box->retries_left = ACK_MAX_RETRIES;
    box->ack_deadline_ms = now_ms + ACK_RETRY_PERIOD_MS;
    return true;
}


// se encarga de procesar los mensajes que llegan a la Caja de Comandos
void cbox_on_message(cbox_t *box, const protocol_message_t *message, uint32_t now_ms)
{
    (void) now_ms;

    if ((box == NULL) || (message == NULL)) {
        return;
    }

    switch (message->type) {
    case PROTOCOL_TYPE_ACK:
        /* Solo un ACK:ok mientras hay transaccion puede confirmarla. */
        if (box->ack_pending && (strcmp(message->payload, "ok") == 0)) {
            box->ack_pending = false;
        }
        break;

    case PROTOCOL_TYPE_CMD:
    case PROTOCOL_TYPE_DAT:
    case PROTOCOL_TYPE_EVT:
    case PROTOCOL_TYPE_STS:
        /*
         * La Variante 3 no consume estos tipos, pero el protocolo pide
         * confirmar recepcion de tramas validas (lazo de eco de Etapa 1).
         */
        cbox_emit(box, PROTOCOL_TYPE_ACK, "ok");
        break;

    case PROTOCOL_TYPE_ERR:
    default:
        /* Un ERR del bridge se registra pero no cambia el modo local. */
        break;
    }
}


// Gestión de reintentos por tiempo, tipo watchdog, para el EVT pendiente de ACK:ok
void cbox_on_tick(cbox_t *box, uint32_t now_ms)
{
    if ((box == NULL) || (!box->ack_pending)) {
        return;
    }

    /* Comparacion robusta ante desborde del contador de ms. */
    if ((int32_t) (now_ms - box->ack_deadline_ms) < 0) {
        return;
    }

    if (box->retries_left > 0U) {
        /* Reenvio del EVT pendiente, mismo payload. */
        cbox_emit(box, PROTOCOL_TYPE_EVT, box->pending_evt);
        box->retries_left--;
        box->ack_deadline_ms = now_ms + ACK_RETRY_PERIOD_MS;
    } else {
        /* Se agotaron los reenvios: reportamos timeout y desistimos. */
        cbox_emit(box, PROTOCOL_TYPE_ERR, "timeout");
        box->ack_pending = false;
        box->ack_timeouts++;
    }
}
