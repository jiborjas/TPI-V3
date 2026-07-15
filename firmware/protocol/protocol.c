#include <string.h>

#include "parser.h"
#include "protocol.h"

/*
 * TP5 - Etapa 1: framing y checksum
 * ----------------------------------
 *
 * UART manda bytes sueltos. El protocolo convierte esos bytes en mensajes
 * reconocibles usando esta forma:
 *
 *     @LL:TTT:PAYLOAD:CC\n
 *
 * Donde:
 *
 * - '@' permite encontrar el comienzo.
 * - LL es la longitud hexadecimal del body "TTT:PAYLOAD".
 * - TTT es el tipo de mensaje: CMD, DAT, EVT, STS, ACK o ERR.
 * - PAYLOAD es texto ASCII legible.
 * - CC es el checksum XOR de "LL:TTT:PAYLOAD".
 * - '\n' cierra la trama.
 *
 * Este archivo no toca hardware. Solo trabaja con texto y buffers. Eso es muy
 * importante: lo podemos probar en la PC antes de flashear la Blue Pill.
 */

/* Convierte un valor de 0 a 15 en un caracter hexadecimal mayuscula. */
static char nibble_to_hex(uint8_t value)
{
    /* Para 0..9 alcanza con sumarle el valor al caracter '0'. */
    if (value < 10U) {
        return (char) ('0' + value);
    }

    /* Para 10..15 usamos A..F. */
    return (char) ('A' + (value - 10U));
}

/* Pregunta si un enum protocol_type_t representa un tipo real del protocolo. */
static bool protocol_type_is_valid(protocol_type_t type)
{
    /* El switch deja explicita la lista cerrada de tipos aceptados. */
    switch (type) {
    case PROTOCOL_TYPE_CMD:
    case PROTOCOL_TYPE_DAT:
    case PROTOCOL_TYPE_EVT:
    case PROTOCOL_TYPE_STS:
    case PROTOCOL_TYPE_ACK:
    case PROTOCOL_TYPE_ERR:
        return true;
    default:
        return false;
    }
}

/* Valida el payload: texto imprimible, hasta 48 bytes, y sin '@'. */
static bool payload_is_valid(const char *payload, uint8_t payload_length)
{
    /* Indice simple para recorrer byte por byte. */
    uint8_t i;

    /* Sin puntero no hay texto para validar. */
    if (payload == NULL) {
        return false;
    }

    /* Recorremos solo payload_length, no dependemos de strlen(). */
    for (i = 0U; i < payload_length; i++) {
        /* Cada caracter se mira como byte ASCII. */
        const uint8_t c = (uint8_t) payload[i];

        /*
         * 0x20..0x7E son caracteres ASCII imprimibles.
         * '@' se reserva para resincronizar el parser cuando hay ruido.
         * ':' sí está permitido porque LL dice exactamente donde termina el body.
         */
        if ((c < 0x20U) || (c > 0x7EU) || (c == (uint8_t) PROTOCOL_START_CHAR)) {
            return false;
        }
    }

    /* Si ningun byte rompio las reglas, el payload es valido. */
    return true;
}

/* Convierte un caracter hexadecimal de la consigna en su valor 0..15. */
int hex_char_to_nibble(char c)
{
    /* Caso numerico: '0'..'9'. */
    if ((c >= '0') && (c <= '9')) {
        return (int) (c - '0');
    }

    /* Caso alfabetico permitido por el TP: 'A'..'F'. */
    if ((c >= 'A') && (c <= 'F')) {
        return (int) (c - 'A' + 10);
    }

    /* Minusculas y otros caracteres no son validos. */
    return -1;
}

/* Calcula XOR byte a byte sobre el texto indicado por el protocolo. */
uint8_t protocol_checksum(const char *input, size_t len)
{
    /* Indice para recorrer el buffer. */
    size_t i;

    /* El XOR arranca en cero, como pide el algoritmo. */
    uint8_t checksum = 0U;

    /* Si no hay buffer, devolvemos cero para no leer memoria invalida. */
    if (input == NULL) {
        return 0U;
    }

    /* XOR es acumulativo: cada byte modifica el resultado anterior. */
    for (i = 0U; i < len; i++) {
        checksum ^= (uint8_t) input[i];
    }

    /* Este byte es el CC de la trama. */
    return checksum;
}

/* Alias usado por la API original del esqueleto. */
uint8_t protocol_compute_checksum(const char *data, size_t length)
{
    /* Mantenemos un solo algoritmo real para evitar inconsistencias. */
    return protocol_checksum(data, length);
}

/* Traduce el enum interno al texto de tres letras que viaja por UART. */
const char *protocol_type_to_text(protocol_type_t type)
{
    /* Cada caso devuelve un literal constante. */
    switch (type) {
    case PROTOCOL_TYPE_CMD:
        return "CMD";
    case PROTOCOL_TYPE_DAT:
        return "DAT";
    case PROTOCOL_TYPE_EVT:
        return "EVT";
    case PROTOCOL_TYPE_STS:
        return "STS";
    case PROTOCOL_TYPE_ACK:
        return "ACK";
    case PROTOCOL_TYPE_ERR:
        return "ERR";
    default:
        return "INV";
    }
}

/* Traduce el texto TTT recibido a un enum interno. */
protocol_type_t protocol_type_from_text(const char *text)
{
    /* Sin texto no se puede reconocer ningun tipo. */
    if (text == NULL) {
        return PROTOCOL_TYPE_INVALID;
    }

    /* Como es una API de strings, exigimos exactamente tres letras y '\0'. */
    if (strlen(text) != PROTOCOL_TYPE_LENGTH) {
        return PROTOCOL_TYPE_INVALID;
    }

    /* strncmp mira exactamente las tres letras del tipo. */
    if (strncmp(text, "CMD", PROTOCOL_TYPE_LENGTH) == 0) {
        return PROTOCOL_TYPE_CMD;
    }
    if (strncmp(text, "DAT", PROTOCOL_TYPE_LENGTH) == 0) {
        return PROTOCOL_TYPE_DAT;
    }
    if (strncmp(text, "EVT", PROTOCOL_TYPE_LENGTH) == 0) {
        return PROTOCOL_TYPE_EVT;
    }
    if (strncmp(text, "STS", PROTOCOL_TYPE_LENGTH) == 0) {
        return PROTOCOL_TYPE_STS;
    }
    if (strncmp(text, "ACK", PROTOCOL_TYPE_LENGTH) == 0) {
        return PROTOCOL_TYPE_ACK;
    }
    if (strncmp(text, "ERR", PROTOCOL_TYPE_LENGTH) == 0) {
        return PROTOCOL_TYPE_ERR;
    }

    /* Si no coincide con la lista cerrada, es invalido. */
    return PROTOCOL_TYPE_INVALID;
}

/* Carga un protocol_message_t con validaciones comunes. */
bool protocol_message_set(protocol_message_t *message, protocol_type_t type, const char *payload)
{
    /* strlen devuelve size_t, por eso usamos size_t primero. */
    size_t payload_length;

    /* Los punteros de salida y entrada son obligatorios. */
    if ((message == NULL) || (payload == NULL)) {
        return false;
    }

    /* Evita generar mensajes "INV". */
    if (!protocol_type_is_valid(type)) {
        return false;
    }

    /* Medimos el payload como string C. */
    payload_length = strlen(payload);

    /* El PDF fija un maximo para que los buffers sean chicos y predecibles. */
    if (payload_length > PROTOCOL_MAX_PAYLOAD_LENGTH) {
        return false;
    }

    /* Tambien validamos caracteres, no solo longitud. */
    if (!payload_is_valid(payload, (uint8_t) payload_length)) {
        return false;
    }

    /* Guardamos el tipo ya validado. */
    message->type = type;

    /* Guardamos la longitud para no recalcularla en transmision. */
    message->payload_length = (uint8_t) payload_length;

    /* Copiamos payload + '\0' para que tambien sea un string legible. */
    memcpy(message->payload, payload, payload_length + 1U);

    /* El mensaje quedo listo para app o UART TX. */
    return true;
}

/* Arma una trama completa desde un mensaje ya separado en tipo + payload. */
bool protocol_encode_frame(const protocol_message_t *message,
                           char *frame,
                           size_t frame_size,
                           size_t *frame_length)    //pasar frame_length es redundante a propósito
{
    /* Texto de tres letras asociado al enum. */
    const char *type_text;

    /* Body temporal: "TTT:PAYLOAD". */
    char body[PROTOCOL_MAX_BODY_SIZE + 1U];

    /* Texto exacto sobre el que se calcula XOR: "LL:TTT:PAYLOAD". */
    char checksum_input[3U + PROTOCOL_MAX_BODY_SIZE];

    /* Longitud del body, que entra en LL. */
    uint8_t body_length;

    /* Resultado CC del XOR. */
    uint8_t checksum;

    /* Posicion de escritura dentro del buffer final. */
    size_t index = 0U;

    /* Cantidad minima de bytes que debe poder guardar frame. */
    size_t required_frame_length;

    /* Por defecto avisamos "no escribi nada". */
    if (frame_length != NULL) {
        *frame_length = 0U;
    }

    /* Sin mensaje o sin buffer de salida no hay forma segura de continuar. */
    if ((message == NULL) || (frame == NULL)) {
        return false;
    }

    /* Validamos tipo antes de convertirlo a texto. */
    if (!protocol_type_is_valid(message->type)) {
        return false;
    }

    /* Validamos longitud declarada en el mensaje. */
    if (message->payload_length > PROTOCOL_MAX_PAYLOAD_LENGTH) {
        return false;
    }

    /* Validamos caracteres reales del payload. */
    if (!payload_is_valid(message->payload, message->payload_length)) {
        return false;
    }

    /* Convertimos enum a "CMD", "ACK", etc. */
    type_text = protocol_type_to_text(message->type);

    /* LL cuenta TTT + ':' + PAYLOAD. */
    body_length = (uint8_t) (PROTOCOL_TYPE_LENGTH + 1U + message->payload_length);

    /* Frame: @ + LL + : + BODY + : + CC + \n. */
    required_frame_length = (size_t) body_length + 8U;

    /* Si el body o el frame no entran, abortamos sin escribir parcial. */
    if ((body_length > PROTOCOL_MAX_BODY_SIZE) || (frame_size < required_frame_length)) {
        return false;
    }

    /* Copiamos las tres letras del tipo al body. */
    memcpy(&body[0], type_text, PROTOCOL_TYPE_LENGTH);

    /* Agregamos el separador interno TTT:PAYLOAD. */
    body[PROTOCOL_TYPE_LENGTH] = PROTOCOL_SEPARATOR_CHAR;

    /* Copiamos el payload justo despues del separador. */
    memcpy(&body[PROTOCOL_TYPE_LENGTH + 1U], message->payload, message->payload_length);

    /* Terminador solo para debug; el largo real sigue siendo body_length. */
    body[body_length] = '\0';

    /* LL alto: parte superior de la longitud. */
    checksum_input[0] = nibble_to_hex((uint8_t) (body_length >> 4U));

    /* LL bajo: parte inferior de la longitud. */
    checksum_input[1] = nibble_to_hex((uint8_t) (body_length & 0x0FU));

    /* El ':' despues de LL tambien entra al checksum. */
    checksum_input[2] = PROTOCOL_SEPARATOR_CHAR;

    /* Sumamos el body al texto protegido. */
    memcpy(&checksum_input[3], body, body_length);

    /* Calculamos CC sobre "LL:TTT:PAYLOAD". */
    checksum = protocol_compute_checksum(checksum_input, (size_t) body_length + 3U);

    /* Escribimos '@'. */
    frame[index++] = PROTOCOL_START_CHAR;

    /* Escribimos LL alto. */
    frame[index++] = checksum_input[0];

    /* Escribimos LL bajo. */
    frame[index++] = checksum_input[1];

    /* Escribimos ':' despues de LL. */
    frame[index++] = PROTOCOL_SEPARATOR_CHAR;

    /* Escribimos TTT:PAYLOAD. */
    memcpy(&frame[index], body, body_length);

    /* Avanzamos la posicion despues del body. */
    index += body_length;

    /* Escribimos ':' antes de CC. */
    frame[index++] = PROTOCOL_SEPARATOR_CHAR;

    /* Escribimos nibble alto de CC. */
    frame[index++] = nibble_to_hex((uint8_t) (checksum >> 4U));

    /* Escribimos nibble bajo de CC. */
    frame[index++] = nibble_to_hex((uint8_t) (checksum & 0x0FU));

    /* Cerramos la trama con salto de linea. */
    frame[index++] = PROTOCOL_END_CHAR;

    /* Si sobra espacio, dejamos '\0' para poder imprimir como string. */
    if (frame_size > index) {
        frame[index] = '\0';
    }

    /* Devolvemos la longitud real enviada por UART, sin contar '\0'. */
    if (frame_length != NULL) {
        *frame_length = index;
    }

    /* Codificacion exitosa. */
    return true;
}

/* Wrapper con la firma textual del PDF: type + payload -> frame. */
int protocol_encode(const char *type_text, const char *payload, char *buf, size_t buf_size)
{
    /* Mensaje intermedio usado por la API original. */
    protocol_message_t message;

    /* Longitud final de la trama. */
    size_t frame_length = 0U;

    /* Tipo ya convertido desde texto. */
    protocol_type_t type;

    /* Validacion de punteros de entrada y salida. */
    if ((type_text == NULL) || (payload == NULL) || (buf == NULL)) {
        return -1;
    }

    /* Convertimos "CMD" a PROTOCOL_TYPE_CMD, etc. */
    type = protocol_type_from_text(type_text);

    /* Armamos el mensaje con las mismas validaciones que usa el firmware. */
    if (!protocol_message_set(&message, type, payload)) {
        return -1;
    }

    /* Reutilizamos el encoder real para no duplicar reglas. */
    if (!protocol_encode_frame(&message, buf, buf_size, &frame_length)) {
        return -1;
    }

    /* La consigna de tests suele esperar int con cantidad de caracteres. */
    return (int) frame_length;
}

/* Decodifica "TTT:PAYLOAD" a protocol_message_t. */
bool protocol_decode_body(const char *body, uint8_t body_length, protocol_message_t *message)
{
    /* Buffer local para copiar solo las tres letras TTT. */
    char type_text[PROTOCOL_TYPE_LENGTH + 1U];

    /* Longitud del payload, calculada a partir de LL. */
    uint8_t payload_length;

    /* Tipo interno despues de convertir TTT. */
    protocol_type_t type;

    /* Punteros obligatorios. */
    if ((body == NULL) || (message == NULL)) {
        return false;
    }

    /* Minimo "TTT:"; maximo definido por app_config.h. */
    if ((body_length < (PROTOCOL_TYPE_LENGTH + 1U)) || (body_length > PROTOCOL_MAX_BODY_SIZE)) {
        return false;
    }

    /* El cuarto caracter del body debe separar tipo y payload. */
    if (body[PROTOCOL_TYPE_LENGTH] != PROTOCOL_SEPARATOR_CHAR) {
        return false;
    }

    /* Copiamos TTT. */
    memcpy(type_text, body, PROTOCOL_TYPE_LENGTH);

    /* Agregamos terminador para usar funciones de string sin riesgo. */
    type_text[PROTOCOL_TYPE_LENGTH] = '\0';

    /* Convertimos TTT a enum. */
    type = protocol_type_from_text(type_text);

    /* Rechazamos tipos desconocidos. */
    if (!protocol_type_is_valid(type)) {
        return false;
    }

    /* El payload es todo lo que queda despues de "TTT:". */
    payload_length = (uint8_t) (body_length - PROTOCOL_TYPE_LENGTH - 1U);

    /* Validamos caracteres del payload antes de copiarlo. */
    if (!payload_is_valid(&body[PROTOCOL_TYPE_LENGTH + 1U], payload_length)) {
        return false;
    }

    /* Guardamos el tipo ya validado. */
    message->type = type;

    /* Guardamos la longitud del payload. */
    message->payload_length = payload_length;

    /* Copiamos el payload exacto, no usamos strcpy porque no sabemos si venia terminado. */
    memcpy(message->payload, &body[PROTOCOL_TYPE_LENGTH + 1U], payload_length);

    /* Agregamos '\0' para que app.c pueda usar strcmp de forma simple. */
    message->payload[payload_length] = '\0';

    /* El body era valido y el mensaje quedo listo. */
    return true;
}

/*
 * Valida "LL:TTT:PAYLOAD:CC" sin '@' inicial ni '\n' final.
 *
 * La FSM incremental es la unica implementacion que decide si una trama es
 * valida. Este wrapper conserva la API de pruebas sin duplicar reglas de
 * longitud, checksum y payload.
 */
int protocol_validate(const char *frame, size_t frame_len)
{
    parser_t parser;
    protocol_message_t message;
    parser_result_t result;
    size_t i;

    if ((frame == NULL) || (frame_len == 0U) ||
        (frame_len > (PROTOCOL_MAX_FRAME_SIZE - 2U))) {
        return -1;
    }

    parser_init(&parser);
    (void) parser_consume_byte(&parser, (uint8_t) PROTOCOL_START_CHAR, &message);

    for (i = 0U; i < frame_len; i++) {
        result = parser_consume_byte(&parser, (uint8_t) frame[i], &message);
        if (result == PARSER_RESULT_ERROR) {
            return -1;
        }
    }

    result = parser_consume_byte(&parser, (uint8_t) PROTOCOL_END_CHAR, &message);
    return (result == PARSER_RESULT_MESSAGE_READY) ? 0 : -1;
}
