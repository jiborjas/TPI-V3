#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "parser.h"

/*
 * TP5 - Etapa 2: parser incremental
 * ----------------------------------
 *
 * UART entrega un flujo de bytes. Puede llegar una trama completa, media
 * trama, ruido antes de la trama o dos tramas pegadas. Por eso no alcanza con
 * hacer strcmp() sobre un buffer grande: necesitamos una FSM.
 *
 * FSM significa "Finite State Machine", o maquina de estados finitos. En
 * castellano llano: el parser recuerda que esta esperando ahora mismo.
 *
 * Formato esperado:
 *
 *     @LL:TTT:PAYLOAD:CC\n
 *
 * Estados:
 *
 * - WAIT_START: espera '@'.
 * - READ_LEN_HI: lee primer hex de LL.
 * - READ_LEN_LO: lee segundo hex de LL.
 * - EXPECT_LEN_SEPARATOR: espera ':' despues de LL.
 * - READ_BODY: junta exactamente LL bytes de TTT:PAYLOAD.
 * - EXPECT_CHECK_SEPARATOR: espera ':' antes de CC.
 * - READ_CHECK_HI: lee primer hex de CC.
 * - READ_CHECK_LO: lee segundo hex de CC.
 * - EXPECT_END: espera '\n', valida checksum y entrega mensaje.
 */

/* Pregunta si el byte es un digito hexadecimal aceptado por el TP. */
static bool is_hex_digit(uint8_t byte)
{
    /* '0'..'9' son validos. */
    if ((byte >= (uint8_t) '0') && (byte <= (uint8_t) '9')) {
        return true;
    }

    /* 'A'..'F' son validos; usamos mayusculas para tener una unica forma. */
    if ((byte >= (uint8_t) 'A') && (byte <= (uint8_t) 'F')) {
        return true;
    }

    /* Todo lo demas, incluidas minusculas, se rechaza. */
    return false;
}

/* Convierte dos caracteres hex ya validados en un byte. */
static uint8_t hex_pair_to_value(const char *text)
{
    /* Nibble alto: primer caracter de la pareja. */
    const int hi = hex_char_to_nibble(text[0]);

    /* Nibble bajo: segundo caracter de la pareja. */
    const int lo = hex_char_to_nibble(text[1]);

    /* Un byte se arma desplazando el alto 4 bits y combinando con OR. */
    return (uint8_t) (((uint8_t) hi << 4U) | (uint8_t) lo);
}

/* Decide si un byte puede aparecer dentro de TTT:PAYLOAD. */
static bool is_body_byte_allowed(uint8_t byte)
{
    /* El body debe ser ASCII imprimible. */
    if ((byte < 0x20U) || (byte > 0x7EU)) {
        return false;
    }

    /* '@' se reserva como marca de inicio para poder resincronizar. */
    if (byte == (uint8_t) PROTOCOL_START_CHAR) {
        return false;
    }

    /* ':' esta permitido porque LL indica donde termina el body. */
    return true;
}

/* Manejo comun de error: resetear y reutilizar '@' si justo aparecio uno. */
static parser_result_t parser_fail(parser_t *parser, uint8_t byte)
{
    /* Borramos todo lo acumulado de la trama rota. */
    parser_reset(parser);

    /*
     * Regla de resincronizacion pedida por el PDF:
     * si el byte que rompio la trama es '@', no lo tiramos; lo tomamos como
     * comienzo de una trama nueva.
     */
    if (byte == (uint8_t) PROTOCOL_START_CHAR) {
        parser->state = PARSER_STATE_READ_LEN_HI;
    }

    /* La tarea FreeRTOS incrementa el contador pe cuando ve este resultado. */
    return PARSER_RESULT_ERROR;
}

/* Devuelve el parser al estado inicial. */
void parser_reset(parser_t *parser)
{
    /* Si el puntero no existe, no hay nada seguro para tocar. */
    if (parser == NULL) {
        return;
    }

    /* El parser vuelve a esperar el proximo '@'. */
    parser->state = PARSER_STATE_WAIT_START;

    /* Limpiamos los dos caracteres de LL mas terminador. */
    parser->length_field[0] = '\0';
    parser->length_field[1] = '\0';
    parser->length_field[2] = '\0';

    /* Limpiamos el body TTT:PAYLOAD. */
    parser->body[0] = '\0';

    /* Limpiamos los dos caracteres de CC mas terminador. */
    parser->checksum_field[0] = '\0';
    parser->checksum_field[1] = '\0';
    parser->checksum_field[2] = '\0';

    /* Todavia no sabemos cuantos bytes de body esperar. */
    parser->expected_body_length = 0U;

    /* Todavia no recibimos ningun byte del body actual. */
    parser->body_index = 0U;
}

/* Inicializar es resetear desde cero. */
void parser_init(parser_t *parser)
{
    /* Dejamos la estructura en un estado conocido. */
    parser_reset(parser);
}

/* Consume exactamente un byte y devuelve el estado del parseo. */
parser_result_t parser_consume_byte(parser_t *parser, uint8_t byte, protocol_message_t *message)
{
    /* Texto usado para recalcular CC: "LL:TTT:PAYLOAD". */
    char checksum_input[3U + PROTOCOL_MAX_BODY_SIZE];

    /* Checksum que venia en la trama. */
    uint8_t received_checksum;

    /* Checksum recalculado localmente. */
    uint8_t computed_checksum;

    /* Sin parser o sin mensaje de salida no podemos trabajar. */
    if ((parser == NULL) || (message == NULL)) {
        return PARSER_RESULT_ERROR;
    }

    /*
     * Algunas terminales mandan CRLF. El protocolo termina con LF, asi que
     * ignoramos '\r' para que la prueba manual sea mas amable.
     */
    if (byte == (uint8_t) '\r') {
        return PARSER_RESULT_IN_PROGRESS;
    }

    /* La accion depende totalmente del estado actual de la FSM. */
    switch (parser->state) {
    case PARSER_STATE_WAIT_START:
        /* Antes de '@', todo se considera ruido de linea. */
        if (byte == (uint8_t) PROTOCOL_START_CHAR) {
            parser_reset(parser);
            parser->state = PARSER_STATE_READ_LEN_HI;
        }
        return PARSER_RESULT_IN_PROGRESS;

    case PARSER_STATE_READ_LEN_HI:
        /* Primer caracter de LL: debe ser hexadecimal. */
        if (!is_hex_digit(byte)) {
            return parser_fail(parser, byte);
        }

        /* Guardamos el primer digito de longitud. */
        parser->length_field[0] = (char) byte;

        /* Pasamos a leer el segundo digito. */
        parser->state = PARSER_STATE_READ_LEN_LO;

        /* Todavia falta el resto de la trama. */
        return PARSER_RESULT_IN_PROGRESS;

    case PARSER_STATE_READ_LEN_LO:
        /* Segundo caracter de LL: tambien debe ser hexadecimal. */
        if (!is_hex_digit(byte)) {
            return parser_fail(parser, byte);
        }

        /* Guardamos el segundo digito. */
        parser->length_field[1] = (char) byte;

        /* Terminamos el string "LL" para debug y conversion. */
        parser->length_field[2] = '\0';

        /* Convertimos LL desde hexadecimal ASCII a numero. */
        parser->expected_body_length = hex_pair_to_value(parser->length_field);

        /* Minimo body permitido: "TTT:"; el payload puede ser vacio. */
        if (parser->expected_body_length < (PROTOCOL_TYPE_LENGTH + 1U)) {
            return parser_fail(parser, byte);
        }

        /* Maximo body permitido: "TTT:" + payload maximo. */
        if (parser->expected_body_length > PROTOCOL_MAX_BODY_SIZE) {
            return parser_fail(parser, byte);
        }

        /* Despues de LL debe venir ':'. */
        parser->state = PARSER_STATE_EXPECT_LEN_SEPARATOR;

        /* La trama sigue en progreso. */
        return PARSER_RESULT_IN_PROGRESS;

    case PARSER_STATE_EXPECT_LEN_SEPARATOR:
        /* El separador despues de LL es obligatorio. */
        if (byte != (uint8_t) PROTOCOL_SEPARATOR_CHAR) {
            return parser_fail(parser, byte);
        }

        /* Arrancamos a llenar el body desde la posicion cero. */
        parser->body_index = 0U;

        /* El proximo estado junta TTT:PAYLOAD. */
        parser->state = PARSER_STATE_READ_BODY;

        /* Todavia no hay mensaje completo. */
        return PARSER_RESULT_IN_PROGRESS;

    case PARSER_STATE_READ_BODY:
        /* Cada byte del body debe ser imprimible y no puede ser '@'. */
        if (!is_body_byte_allowed(byte)) {
            return parser_fail(parser, byte);
        }

        /* Proteccion extra contra escrituras fuera del buffer. */
        if (parser->body_index >= PROTOCOL_MAX_BODY_SIZE) {
            return parser_fail(parser, byte);
        }

        /* Guardamos este byte del body. */
        parser->body[parser->body_index] = (char) byte;

        /* Avanzamos la posicion para el proximo byte. */
        parser->body_index++;

        /* Si ya juntamos LL bytes, el body esta completo. */
        if (parser->body_index == parser->expected_body_length) {
            parser->body[parser->body_index] = '\0';
            parser->state = PARSER_STATE_EXPECT_CHECK_SEPARATOR;
        }

        /* Mientras no llegue '\n' validado, seguimos en progreso. */
        return PARSER_RESULT_IN_PROGRESS;

    case PARSER_STATE_EXPECT_CHECK_SEPARATOR:
        /* Antes del checksum debe venir ':'. */
        if (byte != (uint8_t) PROTOCOL_SEPARATOR_CHAR) {
            return parser_fail(parser, byte);
        }

        /* Pasamos al primer caracter de CC. */
        parser->state = PARSER_STATE_READ_CHECK_HI;

        /* La trama sigue incompleta. */
        return PARSER_RESULT_IN_PROGRESS;

    case PARSER_STATE_READ_CHECK_HI:
        /* Primer digito de CC: hexadecimal mayuscula. */
        if (!is_hex_digit(byte)) {
            return parser_fail(parser, byte);
        }

        /* Guardamos nibble alto de CC. */
        parser->checksum_field[0] = (char) byte;

        /* Pasamos al nibble bajo. */
        parser->state = PARSER_STATE_READ_CHECK_LO;

        /* Todavia falta el segundo digito y el fin de linea. */
        return PARSER_RESULT_IN_PROGRESS;

    case PARSER_STATE_READ_CHECK_LO:
        /* Segundo digito de CC: tambien hexadecimal. */
        if (!is_hex_digit(byte)) {
            return parser_fail(parser, byte);
        }

        /* Guardamos nibble bajo de CC. */
        parser->checksum_field[1] = (char) byte;

        /* Terminamos el string "CC" para poder convertirlo. */
        parser->checksum_field[2] = '\0';

        /* Solo falta el '\n' final. */
        parser->state = PARSER_STATE_EXPECT_END;

        /* La trama aun no se entrega. */
        return PARSER_RESULT_IN_PROGRESS;

    case PARSER_STATE_EXPECT_END:
        /* La trama bien formada termina con '\n'. */
        if (byte != (uint8_t) PROTOCOL_END_CHAR) {
            return parser_fail(parser, byte);
        }

        /*
         * Para validar CC reconstruimos exactamente el texto que uso el emisor:
         * "LL:TTT:PAYLOAD".
         */
        checksum_input[0] = parser->length_field[0];
        checksum_input[1] = parser->length_field[1];
        checksum_input[2] = PROTOCOL_SEPARATOR_CHAR;
        memcpy(&checksum_input[3], parser->body, parser->expected_body_length);

        /* Convertimos CC recibido desde texto hexadecimal a byte. */
        received_checksum = hex_pair_to_value(parser->checksum_field);

        /* Calculamos el CC local sobre LL:body. */
        computed_checksum = protocol_compute_checksum(checksum_input,
                                                      (size_t) parser->expected_body_length + 3U);

        /* Si no coincide, la trama se descarta. */
        if (received_checksum != computed_checksum) {
            parser_reset(parser);
            return PARSER_RESULT_ERROR;
        }

        /* Checksum OK: ahora validamos y separamos TTT:PAYLOAD. */
        if (!protocol_decode_body(parser->body, parser->expected_body_length, message)) {
            parser_reset(parser);
            return PARSER_RESULT_ERROR;
        }

        /* Dejamos el parser listo para la siguiente trama. */
        parser_reset(parser);

        /* Avisamos a task_parser que message ya contiene un mensaje completo. */
        return PARSER_RESULT_MESSAGE_READY;

    default:
        /* Si el estado se corrompe, volvemos a un punto conocido. */
        parser_reset(parser);
        return PARSER_RESULT_ERROR;
    }
}
