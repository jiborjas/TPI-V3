# TP Integrador — Variante 3: Caja de comandos física

**Sistemas Embebidos (3.3.127) — UADE**
Integrantes: Borda, Rojas y Castiglioni

La Blue Pill (STM32F103C8T6) implementa una caja de comandos física para un
robot Unitree Go2/G1 a través del bridge ROS 2 de la cátedra. Un pulsador
cicla los modos PATROL → STOP → DEPLOY → RETURN; un potenciómetro define el
parámetro auxiliar (0–255) que acompaña cada comando. LED y buzzer señalizan
el modo activo.

## Estructura

```
firmware/            Código fuente (FreeRTOS + libopencm3)
  main.c             Punto de entrada
  config/            app_config.h (parámetros) + FreeRTOSConfig.h
  drivers/           uart_comm (USART1) + board_io (ADC, PWM, EXTI, TIM3)
  protocol/          protocol.c (framing/checksum) + parser.c (FSM)
  app/               command_box (lógica variante) + signals (patrones) + tasks
  third_party/       linker.ld (incluido) + libopencm3/FreeRTOS (ver README)
tests/               Tests de host (gcc): 53 casos + simulador de sesión
tools/bridge_sim.py  Bridge simulado por puerto serie (responde ACK:ok)
evidencia/           Capturas de tests, FSM, ADC y sesión simulada
diagramas/           Estados, parser, tareas, conexión, patrones (PNG)
informe/             Informe técnico (docx + pdf)
```

## Compilar el firmware

1. Obtener dependencias (una sola vez): ver `firmware/third_party/README.md`.
2. `cd firmware && make` → genera `bin/main.elf`, `.hex` y `.bin`.
3. Flasheo con ST-Link: `make flash` (usa OpenOCD).

## Correr los tests de host (sin hardware)

```
cd tests
make run    # 53 tests de protocolo, parser, variante y señales
make fsm    # recorrido estado por estado de la FSM (evidencia Etapa 1)
make sim    # sesión simulada de ~25 s con reintentos de ACK (Etapa 3)
```

## Probar contra un bridge simulado (con hardware, sin robot)

```
pip install pyserial
python3 tools/bridge_sim.py COM5            # responde ACK:ok a cada EVT
python3 tools/bridge_sim.py COM5 --no-ack   # escenario de falla (timeout)
```

## Conexionado

| Pin Blue Pill | Función | Periférico |
|---|---|---|
| PA9  | USART1 TX | USB-UART del bridge (115200 8N1) |
| PA10 | USART1 RX | USB-UART del bridge |
| PA0  | ADC1 IN0 | Potenciómetro 10 kΩ (3.3 V — cursor — GND) |
| PA1  | TIM2 CH2 (PWM 1 kHz) | LED + R 220 Ω a GND |
| PB6  | TIM4 CH1 (PWM 2 kHz) | Buzzer pasivo a GND |
| PB1  | EXTI1 | Pulsador a 3.3 V + pull-down externo 10 kΩ |

Todas las masas comparten GND. La Blue Pill opera a 3.3 V: ningún pin
recibe 5 V. Alimentación por USB desde la PC del bridge.
