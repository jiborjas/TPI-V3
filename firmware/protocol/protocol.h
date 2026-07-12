#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../config/app_config.h"

/* Caracter que marca "aca empieza una trama nueva". */
#define PROTOCOL_START_CHAR      '@'

/* Caracter que marca "aca termina la trama". */
#define PROTOCOL_END_CHAR        '\n'

/* Separador usado entre LL, TTT, PAYLOAD y CC. */
#define PROTOCOL_SEPARATOR_CHAR  ':'

/* Todos los tipos del protocolo son de tres letras: CMD, ACK, STS, etc. */
#define PROTOCOL_TYPE_LENGTH     3U

/* El body es exactamente TTT:PAYLOAD. */
#define PROTOCOL_MAX_BODY_SIZE   (PROTOCOL_TYPE_LENGTH + 1U + PROTOCOL_MAX_PAYLOAD_LENGTH)

/* Alias local para que el codigo lea igual que el PDF. */
#define PROTOCOL_MAX_FRAME_SIZE  PROTOCOL_MAX_FRAME_LENGTH

/* Tipos logicos que viajan dentro del campo TTT. */
typedef enum {
    PROTOCOL_TYPE_CMD = 0,
    PROTOCOL_TYPE_DAT,
    PROTOCOL_TYPE_EVT,
    PROTOCOL_TYPE_STS,
    PROTOCOL_TYPE_ACK,
    PROTOCOL_TYPE_ERR,
    PROTOCOL_TYPE_INVALID
} protocol_type_t;

/* Mensaje ya decodificado: la aplicacion no ve bytes crudos, ve tipo + payload. */
typedef struct {
    protocol_type_t type;
    char payload[PROTOCOL_MAX_PAYLOAD_LENGTH + 1U];
    uint8_t payload_length;
} protocol_message_t;

/* Helpers con nombres de la consigna del PDF. */
int hex_char_to_nibble(char c);
uint8_t protocol_checksum(const char *input, size_t len);
int protocol_encode(const char *type_text, const char *payload, char *buf, size_t buf_size);
int protocol_validate(const char *frame, size_t frame_len);

/* API original del esqueleto: las tareas FreeRTOS usan estas funciones. */
bool protocol_message_set(protocol_message_t *message, protocol_type_t type, const char *payload);
bool protocol_encode_frame(const protocol_message_t *message, char *frame, size_t frame_size, size_t *frame_length);
bool protocol_decode_body(const char *body, uint8_t body_length, protocol_message_t *message);
uint8_t protocol_compute_checksum(const char *data, size_t length);
const char *protocol_type_to_text(protocol_type_t type);
protocol_type_t protocol_type_from_text(const char *text);

#endif
