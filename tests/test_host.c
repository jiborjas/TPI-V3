/*
 * TP Integrador - Variante 3: tests de host
 * -----------------------------------------
 * Compila protocol.c, parser.c, command_box.c y signals.c en la PC y
 * verifica contra los ejemplos del PDF de la consigna.
 *
 * Uso:
 *   ./test_host          -> corre todos los tests
 *   ./test_host --sim    -> imprime una sesion simulada de ~25 s (evidencia)
 *   ./test_host --fsm    -> imprime el recorrido de la FSM para una trama
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/app/command_box.h"
#include "../firmware/app/signals.h"
#include "../firmware/protocol/parser.h"
#include "../firmware/protocol/protocol.h"

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define CHECK(cond, name)                                                  \
    do {                                                                   \
        g_tests_run++;                                                     \
        if (cond) {                                                        \
            printf("[OK]   %s\n", name);                                   \
        } else {                                                           \
            g_tests_failed++;                                              \
            printf("[FAIL] %s (linea %d)\n", name, __LINE__);              \
        }                                                                  \
    } while (0)

/* ------------------------------------------------------------------ */
/* Ayudantes                                                           */
/* ------------------------------------------------------------------ */

/* Codifica tipo+payload y devuelve la trama como string C. */
static const char *encode(protocol_type_t type, const char *payload)
{
    static char frame[PROTOCOL_MAX_FRAME_LENGTH + 1U];
    protocol_message_t msg;
    size_t len = 0U;

    if (!protocol_message_set(&msg, type, payload)) {
        return NULL;
    }
    if (!protocol_encode_frame(&msg, frame, sizeof(frame) - 1U, &len)) {
        return NULL;
    }
    frame[len] = '\0';
    return frame;
}

/* Alimenta un string completo al parser y devuelve el ultimo resultado. */
static parser_result_t feed(parser_t *p, const char *bytes, protocol_message_t *out,
                            int *messages, int *errors)
{
    parser_result_t last = PARSER_RESULT_IN_PROGRESS;

    for (size_t i = 0U; i < strlen(bytes); i++) {
        last = parser_consume_byte(p, (uint8_t) bytes[i], out);
        if ((messages != NULL) && (last == PARSER_RESULT_MESSAGE_READY)) {
            (*messages)++;
        }
        if ((errors != NULL) && (last == PARSER_RESULT_ERROR)) {
            (*errors)++;
        }
    }
    return last;
}

/* Captura de mensajes emitidos por la caja de comandos. */
#define CAPTURE_MAX 32
static struct {
    protocol_type_t type;
    char payload[PROTOCOL_MAX_PAYLOAD_LENGTH + 1U];
} g_captured[CAPTURE_MAX];
static int g_captured_count = 0;

static void capture_send(void *ctx, protocol_type_t type, const char *payload)
{
    (void) ctx;
    if (g_captured_count < CAPTURE_MAX) {
        g_captured[g_captured_count].type = type;
        snprintf(g_captured[g_captured_count].payload,
                 sizeof(g_captured[g_captured_count].payload), "%s", payload);
        g_captured_count++;
    }
}

static void capture_reset(void)
{
    g_captured_count = 0;
}

/* Salidas simuladas del motor de senales. */
static uint8_t g_led_duty = 0U;
static bool g_buzzer_on = false;

static void fake_led(void *ctx, uint8_t duty) { (void) ctx; g_led_duty = duty; }
static void fake_buzzer(void *ctx, bool on)   { (void) ctx; g_buzzer_on = on; }

/* ------------------------------------------------------------------ */
/* Tests de protocolo (ejemplos 4.4.1 a 4.4.4 del PDF)                 */
/* ------------------------------------------------------------------ */

static void test_encode_examples(void)
{
    const char *f;

    f = encode(PROTOCOL_TYPE_DAT, "adc=2048");
    CHECK((f != NULL) && (strcmp(f, "@0C:DAT:adc=2048:77\n") == 0),
          "4.4.1 DAT:adc=2048 -> @0C:DAT:adc=2048:77");

    f = encode(PROTOCOL_TYPE_EVT, "e_stop");
    CHECK((f != NULL) && (strcmp(f, "@0A:EVT:e_stop:14\n") == 0),
          "4.4.2 EVT:e_stop -> @0A:EVT:e_stop:14");

    f = encode(PROTOCOL_TYPE_STS, "OK");
    CHECK((f != NULL) && (strcmp(f, "@06:STS:OK:56\n") == 0),
          "4.4.3 STS:OK -> @06:STS:OK:56");

    f = encode(PROTOCOL_TYPE_CMD, "mode=ESTOP");
    CHECK((f != NULL) && (strcmp(f, "@0E:CMD:mode=ESTOP:5C\n") == 0),
          "4.4.4 CMD:mode=ESTOP -> @0E:CMD:mode=ESTOP:5C");
}

/* Tramas de la Variante 3 (para citar en el informe). */
static void test_encode_variant3(void)
{
    printf("\nTramas de la Variante 3 (calculadas con protocol_encode_frame):\n");
    const struct { protocol_type_t t; const char *p; } frames[] = {
        { PROTOCOL_TYPE_EVT, "cmd_patrol" }, { PROTOCOL_TYPE_STS, "mode=PATROL" },
        { PROTOCOL_TYPE_EVT, "cmd_stop" },   { PROTOCOL_TYPE_STS, "mode=STOP" },
        { PROTOCOL_TYPE_EVT, "cmd_deploy" }, { PROTOCOL_TYPE_STS, "mode=DEPLOY" },
        { PROTOCOL_TYPE_EVT, "cmd_return" }, { PROTOCOL_TYPE_DAT, "param=128" },
        { PROTOCOL_TYPE_DAT, "param=007" },  { PROTOCOL_TYPE_ACK, "ok" },
        { PROTOCOL_TYPE_ERR, "timeout" },    { PROTOCOL_TYPE_ERR, "bounds" },
    };

    for (size_t i = 0U; i < sizeof(frames) / sizeof(frames[0]); i++) {
        const char *f = encode(frames[i].t, frames[i].p);
        CHECK(f != NULL, "encode variante 3");
        if (f != NULL) {
            printf("       %s", f);
        }
    }
}

static void test_parser_accepts_all_types(void)
{
    const char *valid[] = {
        "@0C:DAT:adc=2048:77\n", "@0A:EVT:e_stop:14\n", "@06:STS:OK:56\n",
        "@0E:CMD:mode=ESTOP:5C\n",
    };
    parser_t p;
    protocol_message_t msg;

    parser_init(&p);

    for (size_t i = 0U; i < sizeof(valid) / sizeof(valid[0]); i++) {
        int messages = 0;
        feed(&p, valid[i], &msg, &messages, NULL);
        CHECK(messages == 1, "parser acepta trama valida");
    }

    /* ACK y ERR tambien deben decodificar (checksum autogenerado). */
    int messages = 0;
    feed(&p, encode(PROTOCOL_TYPE_ACK, "ok"), &msg, &messages, NULL);
    feed(&p, encode(PROTOCOL_TYPE_ERR, "timeout"), &msg, &messages, NULL);
    CHECK(messages == 2, "parser acepta ACK:ok y ERR:timeout");
}

static void test_parser_rejects_invalid(void)
{
    parser_t p;
    protocol_message_t msg;
    int errors;

    parser_init(&p);

    /* Checksum incorrecto. */
    errors = 0;
    feed(&p, "@0C:DAT:adc=2048:78\n", &msg, NULL, &errors);
    CHECK(errors == 1, "rechaza checksum incorrecto");

    /* Pregunta 7.3.4: LL=0A pero CMD:ping mide 8 (08). El parser junta 10
     * bytes de body: 'CMD:ping:5' y al esperar ':' encuentra '2' -> error. */
    errors = 0;
    feed(&p, "@0A:CMD:ping:52\n", &msg, NULL, &errors);
    CHECK(errors == 1, "rechaza LL inconsistente (@0A:CMD:ping:52)");

    /* Tipo desconocido. */
    errors = 0;
    feed(&p, "@08:XYZ:ping:6A\n", &msg, NULL, &errors);
    CHECK(errors >= 1, "rechaza tipo desconocido");

    /* Caracter no imprimible en el body. */
    errors = 0;
    feed(&p, "@08:CMD:pi\x01g:52\n", &msg, NULL, &errors);
    CHECK(errors >= 1, "rechaza caracter no imprimible");
}

static void test_parser_resync(void)
{
    parser_t p;
    protocol_message_t msg;
    int messages = 0;
    int errors = 0;

    parser_init(&p);

    /* Ruido + trama valida: el ruido se ignora en WAIT_START. */
    feed(&p, "ruido$$##@06:STS:OK:56\n", &msg, &messages, &errors);
    CHECK(messages == 1, "resincroniza tras ruido inicial");

    /* Dos tramas pegadas sin separacion: ambas deben salir. */
    messages = 0;
    feed(&p, "@06:STS:OK:56\n@0A:EVT:e_stop:14\n", &msg, &messages, NULL);
    CHECK(messages == 2, "distingue dos tramas pegadas");

    /* Trama truncada seguida de '@': el '@' se reutiliza como inicio. */
    messages = 0;
    errors = 0;
    feed(&p, "@0C:DAT:adc=20@06:STS:OK:56\n", &msg, &messages, &errors);
    CHECK((messages == 1) && (errors == 1),
          "reutiliza '@' que corto una trama incompleta");
}

/* La API textual de compatibilidad debe usar exactamente la misma FSM. */
static void test_protocol_api_and_boundaries(void)
{
    char frame[PROTOCOL_MAX_FRAME_LENGTH + 1U];
    char payload[PROTOCOL_MAX_PAYLOAD_LENGTH + 1U];
    protocol_message_t msg;
    parser_t p;
    protocol_message_t out;
    int errors = 0;
    int messages = 0;
    int encoded_length;
    size_t frame_length = 0U;

    encoded_length = protocol_encode("DAT", "adc=2048", frame, sizeof(frame));
    CHECK((encoded_length == 20) &&
          (strcmp(frame, "@0C:DAT:adc=2048:77\n") == 0),
          "API textual codifica el ejemplo del protocolo");
    CHECK(protocol_validate("0C:DAT:adc=2048:77", 18U) == 0,
          "protocol_validate acepta una trama valida mediante la FSM");
    CHECK(protocol_validate("0C:DAT:adc=2048:78", 18U) == -1,
          "protocol_validate rechaza checksum incorrecto mediante la FSM");

    memset(payload, 'x', PROTOCOL_MAX_PAYLOAD_LENGTH);
    payload[PROTOCOL_MAX_PAYLOAD_LENGTH] = '\0';
    CHECK(protocol_message_set(&msg, PROTOCOL_TYPE_DAT, payload) &&
          protocol_encode_frame(&msg, frame, sizeof(frame), &frame_length) &&
          (protocol_validate(&frame[1], frame_length - 2U) == 0),
          "acepta payload maximo de 48 bytes");

    parser_init(&p);
    feed(&p, "@06:STS:OK:56\r\n", &out, &messages, &errors);
    CHECK((messages == 1) && (errors == 0), "acepta CRLF solo al final de la trama");

    parser_init(&p);
    errors = 0;
    feed(&p, "@06:DAT:a\r", &out, NULL, &errors);
    CHECK((errors == 1) && (p.last_error == PARSER_ERROR_BODY),
          "rechaza CR dentro del body y conserva el diagnostico");

    parser_init(&p);
    errors = 0;
    feed(&p, "@0C:DAT:adc=2048:78\n", &out, NULL, &errors);
    CHECK((errors == 1) && (p.last_error == PARSER_ERROR_CHECKSUM),
          "expone error de checksum para diagnostico");
}

/* ------------------------------------------------------------------ */
/* Tests de la caja de comandos (Variante 3)                           */
/* ------------------------------------------------------------------ */

static void test_cbox_cycle_and_frames(void)
{
    cbox_t box;
    protocol_message_t ack;

    cbox_init(&box, capture_send, NULL);
    CHECK(box.mode == CBOX_MODE_STOP, "arranca en STOP (estado seguro)");

    /* Presion 1: STOP -> DEPLOY, param chico con relleno de ceros. */
    capture_reset();
    cbox_on_button(&box, 7U, 1000U);
    CHECK(box.mode == CBOX_MODE_DEPLOY, "STOP -> DEPLOY");
    CHECK(g_captured_count == 3, "emite EVT + STS + DAT");
    CHECK((g_captured[0].type == PROTOCOL_TYPE_EVT) &&
          (strcmp(g_captured[0].payload, "cmd_deploy") == 0), "EVT:cmd_deploy");
    CHECK((g_captured[1].type == PROTOCOL_TYPE_STS) &&
          (strcmp(g_captured[1].payload, "mode=DEPLOY") == 0), "STS:mode=DEPLOY");
    CHECK((g_captured[2].type == PROTOCOL_TYPE_DAT) &&
          (strcmp(g_captured[2].payload, "param=007") == 0),
          "DAT:param=007 (ancho fijo)");
    protocol_message_set(&ack, PROTOCOL_TYPE_ACK, "ok");
    cbox_on_message(&box, &ack, 1100U);

    /* Presion 2: DEPLOY -> RETURN; STS de RETURN es mode=STOP (consigna). */
    capture_reset();
    cbox_on_button(&box, 255U, 2000U);
    CHECK(box.mode == CBOX_MODE_RETURN, "DEPLOY -> RETURN");
    CHECK(strcmp(g_captured[0].payload, "cmd_return") == 0, "EVT:cmd_return");
    CHECK(strcmp(g_captured[1].payload, "mode=STOP") == 0,
          "STS de RETURN es mode=STOP");
    CHECK(strcmp(g_captured[2].payload, "param=255") == 0, "DAT:param=255");
    cbox_on_message(&box, &ack, 2100U);

    /* Presiones 3 y 4: RETURN -> PATROL -> STOP (ciclo completo). */
    cbox_on_button(&box, 0U, 3000U);
    CHECK(box.mode == CBOX_MODE_PATROL, "RETURN -> PATROL");
    cbox_on_message(&box, &ack, 3100U);
    cbox_on_button(&box, 0U, 4000U);
    CHECK(box.mode == CBOX_MODE_STOP, "PATROL -> STOP (cierra el ciclo)");
}

static void test_cbox_serializes_pending_commands(void)
{
    cbox_t box;
    protocol_message_t ack;

    cbox_init(&box, capture_send, NULL);
    capture_reset();
    CHECK(cbox_on_button(&box, 42U, 0U), "acepta el primer comando");
    CHECK(box.ack_pending && (strcmp(box.pending_evt, "cmd_deploy") == 0),
          "mantiene el EVT pendiente hasta su ACK");

    capture_reset();
    CHECK(!cbox_on_button(&box, 99U, 10U),
          "rechaza una pulsacion mientras espera ACK");
    CHECK((g_captured_count == 0) && (box.mode == CBOX_MODE_DEPLOY) &&
          (box.ignored_button_presses == 1U),
          "no pisa modo ni trama pendiente");

    protocol_message_set(&ack, PROTOCOL_TYPE_ACK, "ok");
    cbox_on_message(&box, &ack, 20U);
    capture_reset();
    CHECK(cbox_on_button(&box, 99U, 30U),
          "acepta el siguiente comando despues del ACK");
    CHECK((box.mode == CBOX_MODE_RETURN) && (g_captured_count == 3) &&
          (strcmp(g_captured[0].payload, "cmd_return") == 0),
          "conserva el ciclo de modos al serializar comandos");

    cbox_on_message(&box, &ack, 40U);
    CHECK(!box.ack_pending, "ACK sin transacciones paralelas cierra la espera");
}

static void test_cbox_ack_and_retry(void)
{
    cbox_t box;
    protocol_message_t ack;

    /* Caso 1: ACK:ok llega a tiempo -> no hay reenvio. */
    cbox_init(&box, capture_send, NULL);
    capture_reset();
    cbox_on_button(&box, 100U, 0U);
    protocol_message_set(&ack, PROTOCOL_TYPE_ACK, "ok");
    cbox_on_message(&box, &ack, 100U);
    capture_reset();
    cbox_on_tick(&box, 600U);
    CHECK(g_captured_count == 0, "con ACK:ok no reenvia");

    /* Caso 2: sin ACK -> reenvia cada 500 ms, 5 veces, y ERR:timeout. */
    cbox_init(&box, capture_send, NULL);
    capture_reset();
    cbox_on_button(&box, 100U, 0U);       /* EVT original en t=0 */
    capture_reset();

    uint32_t t = 0U;
    int resends = 0;
    int err_timeout = 0;
    for (t = 0U; t <= 4000U; t += 10U) {
        cbox_on_tick(&box, t);
    }
    for (int i = 0; i < g_captured_count; i++) {
        if (g_captured[i].type == PROTOCOL_TYPE_EVT) {
            resends++;
        }
        if ((g_captured[i].type == PROTOCOL_TYPE_ERR) &&
            (strcmp(g_captured[i].payload, "timeout") == 0)) {
            err_timeout++;
        }
    }
    CHECK(resends == 5, "reenvia el EVT 5 veces (cada 500 ms)");
    CHECK(err_timeout == 1, "agotados los reenvios emite ERR:timeout");
    CHECK(!box.ack_pending, "desiste tras ERR:timeout");

    /* Caso 3: trama valida no-ACK recibida -> responde ACK:ok (eco Etapa 1). */
    capture_reset();
    protocol_message_set(&ack, PROTOCOL_TYPE_CMD, "mode=RUN");
    cbox_on_message(&box, &ack, 5000U);
    CHECK((g_captured_count == 1) && (g_captured[0].type == PROTOCOL_TYPE_ACK) &&
          (strcmp(g_captured[0].payload, "ok") == 0),
          "trama valida recibida -> ACK:ok");
}

/* ------------------------------------------------------------------ */
/* Tests del motor de senales                                          */
/* ------------------------------------------------------------------ */

static int count_buzzer_pulses(signals_t *sig, uint32_t from_ms, uint32_t to_ms)
{
    int pulses = 0;
    bool prev = false;

    for (uint32_t t = from_ms; t < to_ms; t += 10U) {
        signals_tick(sig, t);
        if (g_buzzer_on && !prev) {
            pulses++;
        }
        prev = g_buzzer_on;
    }
    return pulses;
}

static void test_signals_patterns(void)
{
    signals_t sig;

    signals_init(&sig, fake_led, fake_buzzer, NULL);

    /* STOP: LED fijo encendido. */
    signals_enter_mode(&sig, CBOX_MODE_STOP, 0U);
    count_buzzer_pulses(&sig, 0U, 1000U);   /* consume los pitidos */
    signals_tick(&sig, 1000U);
    uint8_t d1 = g_led_duty;
    signals_tick(&sig, 1600U);
    CHECK((d1 == 100U) && (g_led_duty == 100U), "STOP: LED fijo");

    /* PATROL: 1 Hz -> en 2 s el LED pasa por on y off. */
    signals_enter_mode(&sig, CBOX_MODE_PATROL, 2000U);
    signals_tick(&sig, 2100U);
    bool on_phase = (g_led_duty > 0U);
    signals_tick(&sig, 2700U);
    bool off_phase = (g_led_duty == 0U);
    CHECK(on_phase && off_phase, "PATROL: destello 1 Hz (on a 100 ms, off a 700 ms)");

    /* DEPLOY: 4 Hz -> a 60 ms esta on y a 190 ms esta off. */
    signals_enter_mode(&sig, CBOX_MODE_DEPLOY, 10000U);
    signals_tick(&sig, 10060U);
    on_phase = (g_led_duty > 0U);
    signals_tick(&sig, 10190U);
    off_phase = (g_led_duty == 0U);
    CHECK(on_phase && off_phase, "DEPLOY: destello 4 Hz");

    /* RETURN: doble destello -> 2 pulsos de LED por periodo de 1 s. */
    signals_enter_mode(&sig, CBOX_MODE_RETURN, 20000U);
    int led_pulses = 0;
    bool prev = false;
    for (uint32_t t = 20000U; t < 21000U; t += 10U) {
        signals_tick(&sig, t);
        bool now_on = (g_led_duty > 0U);
        if (now_on && !prev) {
            led_pulses++;
        }
        prev = now_on;
    }
    CHECK(led_pulses == 2, "RETURN: doble destello por periodo");

    /* Codigos de pitidos: 1=PATROL ... 4=RETURN. */
    signals_enter_mode(&sig, CBOX_MODE_PATROL, 30000U);
    CHECK(count_buzzer_pulses(&sig, 30000U, 32000U) == 1, "PATROL: 1 pitido");
    signals_enter_mode(&sig, CBOX_MODE_STOP, 40000U);
    CHECK(count_buzzer_pulses(&sig, 40000U, 42000U) == 2, "STOP: 2 pitidos");
    signals_enter_mode(&sig, CBOX_MODE_DEPLOY, 50000U);
    CHECK(count_buzzer_pulses(&sig, 50000U, 52000U) == 3, "DEPLOY: 3 pitidos");
    signals_enter_mode(&sig, CBOX_MODE_RETURN, 60000U);
    CHECK(count_buzzer_pulses(&sig, 60000U, 62000U) == 4, "RETURN: 4 pitidos");
}

/* ------------------------------------------------------------------ */
/* Modo --fsm: recorrido estado por estado (evidencia Etapa 1)         */
/* ------------------------------------------------------------------ */

static const char *state_name(parser_state_t s)
{
    switch (s) {
    case PARSER_STATE_WAIT_START:             return "WAIT_START";
    case PARSER_STATE_READ_LEN_HI:            return "READ_LEN_HI";
    case PARSER_STATE_READ_LEN_LO:            return "READ_LEN_LO";
    case PARSER_STATE_EXPECT_LEN_SEPARATOR:   return "EXPECT_LEN_SEP";
    case PARSER_STATE_READ_BODY:              return "READ_BODY";
    case PARSER_STATE_EXPECT_CHECK_SEPARATOR: return "EXPECT_CHK_SEP";
    case PARSER_STATE_READ_CHECK_HI:          return "READ_CHECK_HI";
    case PARSER_STATE_READ_CHECK_LO:          return "READ_CHECK_LO";
    case PARSER_STATE_EXPECT_END:             return "EXPECT_END";
    default:                                  return "?";
    }
}

static void print_fsm_trace(const char *frame)
{
    parser_t p;
    protocol_message_t msg;

    parser_init(&p);
    printf("Recorrido de la FSM para la trama: %s\n", frame);
    printf("%-6s %-8s %-16s -> %-16s %s\n",
           "byte#", "char", "estado previo", "estado nuevo", "resultado");

    for (size_t i = 0U; i < strlen(frame); i++) {
        char c = frame[i];
        parser_state_t before = p.state;
        parser_result_t r = parser_consume_byte(&p, (uint8_t) c, &msg);
        printf("%-6zu %-8s %-16s -> %-16s %s\n", i,
               (c == '\n') ? "\\n" : (char[]){c, '\0'},
               state_name(before), state_name(p.state),
               (r == PARSER_RESULT_MESSAGE_READY) ? "MESSAGE_READY" :
               (r == PARSER_RESULT_ERROR) ? "ERROR" : "IN_PROGRESS");
    }
    if (msg.type == PROTOCOL_TYPE_DAT) {
        printf("Mensaje decodificado: DAT payload='%s'\n", msg.payload);
    }
}

/* ------------------------------------------------------------------ */
/* Modo --sim: sesion simulada de ~25 s (evidencia Etapa 3)            */
/* ------------------------------------------------------------------ */

static uint32_t g_sim_now = 0U;
static uint16_t g_sim_adc = 1523U;

static void sim_send(void *ctx, protocol_type_t type, const char *payload)
{
    protocol_message_t msg;
    char frame[PROTOCOL_MAX_FRAME_LENGTH + 1U];
    size_t len = 0U;

    (void) ctx;

    if (protocol_message_set(&msg, type, payload) &&
        protocol_encode_frame(&msg, frame, sizeof(frame) - 1U, &len)) {
        frame[len] = '\0';
        printf("[%6u.%03u] PILL->BRIDGE  %s", g_sim_now / 1000U,
               g_sim_now % 1000U, frame);
    }
}

static void sim_bridge_ack(cbox_t *box, uint32_t delay_ms)
{
    protocol_message_t ack;

    char frame[PROTOCOL_MAX_FRAME_LENGTH + 1U];
    size_t len = 0U;

    g_sim_now += delay_ms;
    protocol_message_set(&ack, PROTOCOL_TYPE_ACK, "ok");
    if (protocol_encode_frame(&ack, frame, sizeof(frame) - 1U, &len)) {
        frame[len] = '\0';
        printf("[%6u.%03u] BRIDGE->PILL  %s", g_sim_now / 1000U,
               g_sim_now % 1000U, frame);
    }
    cbox_on_message(box, &ack, g_sim_now);
}

static void sim_advance(cbox_t *box, uint32_t ms)
{
    uint32_t target = g_sim_now + ms;

    while (g_sim_now < target) {
        g_sim_now += 10U;
        cbox_on_tick(box, g_sim_now);
    }
}

static void run_simulation(void)
{
    cbox_t box;
    uint8_t param;

    printf("Sesion simulada - Variante 3: Caja de comandos fisica\n");
    printf("Firmware Blue Pill (logica real) + bridge simulado\n");
    printf("Boot: modo STOP, LED fijo, sin tramas hasta la primera presion\n\n");

    cbox_init(&box, sim_send, NULL);

    /* t=2.0 s: presion 1 -> DEPLOY, pote en 1523/4095 -> param=094 */
    g_sim_now = 2000U;
    param = (uint8_t) (((uint32_t) g_sim_adc * 255U) / 4095U);
    printf("[%6u.%03u] BOTON presionado (anti-rebote OK), pote=%u\n",
           g_sim_now / 1000U, g_sim_now % 1000U, g_sim_adc);
    cbox_on_button(&box, param, g_sim_now);
    sim_bridge_ack(&box, 40U);
    sim_advance(&box, 4000U);

    /* t=6.0 s: presion 2 -> RETURN, pote subio a 3800 -> param=236 */
    g_sim_adc = 3800U;
    param = (uint8_t) (((uint32_t) g_sim_adc * 255U) / 4095U);
    printf("[%6u.%03u] BOTON presionado (anti-rebote OK), pote=%u\n",
           g_sim_now / 1000U, g_sim_now % 1000U, g_sim_adc);
    cbox_on_button(&box, param, g_sim_now);
    sim_bridge_ack(&box, 35U);
    sim_advance(&box, 4000U);

    /* t=10.1 s: presion 3 -> PATROL; el bridge no responde: reintentos. */
    g_sim_adc = 512U;
    param = (uint8_t) (((uint32_t) g_sim_adc * 255U) / 4095U);
    printf("[%6u.%03u] BOTON presionado (anti-rebote OK), pote=%u\n",
           g_sim_now / 1000U, g_sim_now % 1000U, g_sim_adc);
    printf("[%6u.%03u] (bridge desconectado a proposito: escenario de falla)\n",
           g_sim_now / 1000U, g_sim_now % 1000U);
    cbox_on_button(&box, param, g_sim_now);
    sim_advance(&box, 1200U);   /* 2 reintentos a 500 y 1000 ms */

    /* El bridge vuelve y confirma. */
    printf("[%6u.%03u] (bridge reconectado)\n", g_sim_now / 1000U, g_sim_now % 1000U);
    sim_bridge_ack(&box, 20U);
    sim_advance(&box, 3000U);

    /* t=14.4 s: presion 4 -> STOP. */
    g_sim_adc = 2047U;
    param = (uint8_t) (((uint32_t) g_sim_adc * 255U) / 4095U);
    printf("[%6u.%03u] BOTON presionado (anti-rebote OK), pote=%u\n",
           g_sim_now / 1000U, g_sim_now % 1000U, g_sim_adc);
    cbox_on_button(&box, param, g_sim_now);
    sim_bridge_ack(&box, 38U);
    sim_advance(&box, 4000U);

    /* t=18.4 s: presion 5 -> DEPLOY de nuevo; ahora sin ACK hasta timeout. */
    printf("[%6u.%03u] BOTON presionado (anti-rebote OK), pote=%u\n",
           g_sim_now / 1000U, g_sim_now % 1000U, g_sim_adc);
    printf("[%6u.%03u] (bridge desconectado: se agotan los 5 reintentos)\n",
           g_sim_now / 1000U, g_sim_now % 1000U);
    cbox_on_button(&box, param, g_sim_now);
    sim_advance(&box, 3600U);   /* 5 reintentos + ERR:timeout */

    printf("\nFin de la sesion: %u cambios de modo, %u timeout de ACK\n",
           (unsigned) box.mode_changes, (unsigned) box.ack_timeouts);
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    if ((argc > 1) && (strcmp(argv[1], "--sim") == 0)) {
        run_simulation();
        return 0;
    }
    if ((argc > 1) && (strcmp(argv[1], "--fsm") == 0)) {
        print_fsm_trace("@0C:DAT:adc=2048:77\n");
        return 0;
    }

    printf("== Protocolo: ejemplos del PDF ==\n");
    test_encode_examples();
    test_encode_variant3();

    printf("\n== Parser: aceptacion ==\n");
    test_parser_accepts_all_types();

    printf("\n== Parser: rechazo ==\n");
    test_parser_rejects_invalid();

    printf("\n== Parser: resincronizacion ==\n");
    test_parser_resync();

    printf("\n== API y limites de protocolo ==\n");
    test_protocol_api_and_boundaries();

    printf("\n== Variante 3: ciclo de modos ==\n");
    test_cbox_cycle_and_frames();
    test_cbox_serializes_pending_commands();

    printf("\n== Variante 3: ACK y reintentos ==\n");
    test_cbox_ack_and_retry();

    printf("\n== Variante 3: patrones de LED y buzzer ==\n");
    test_signals_patterns();

    printf("\nResultado: %d tests, %d fallas\n", g_tests_run, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
