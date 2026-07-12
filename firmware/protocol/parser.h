#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>

#include "protocol.h"

typedef enum {
    PARSER_STATE_WAIT_START = 0,
    PARSER_STATE_READ_LEN_HI,
    PARSER_STATE_READ_LEN_LO,
    PARSER_STATE_EXPECT_LEN_SEPARATOR,
    PARSER_STATE_READ_BODY,
    PARSER_STATE_EXPECT_CHECK_SEPARATOR,
    PARSER_STATE_READ_CHECK_HI,
    PARSER_STATE_READ_CHECK_LO,
    PARSER_STATE_EXPECT_END
} parser_state_t;

typedef enum {
    PARSER_RESULT_IN_PROGRESS = 0,
    PARSER_RESULT_MESSAGE_READY,
    PARSER_RESULT_ERROR
} parser_result_t;

typedef struct {
    parser_state_t state;
    char length_field[3];
    char body[PROTOCOL_MAX_BODY_SIZE + 1U];
    char checksum_field[3];
    uint8_t expected_body_length;
    uint8_t body_index;
} parser_t;

void parser_init(parser_t *parser);
void parser_reset(parser_t *parser);
parser_result_t parser_consume_byte(parser_t *parser, uint8_t byte, protocol_message_t *message);

#endif
