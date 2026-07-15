# Diagramas de flujo mantenibles

Estos diagramas describen la implementación actual del firmware. Están
escritos en [Mermaid](https://mermaid.js.org/), para que sean revisables junto
con el código y se puedan renderizar en GitHub, VS Code o mermaid.live.

| Archivo | Describe | Referencia de código |
|---|---|---|
| `01_arquitectura_tareas.mmd` | tareas, ISR y colas FreeRTOS | `firmware/app/tasks.c` |
| `02_parser_fsm.mmd` | parser incremental y recuperación de errores | `firmware/protocol/parser.c` |
| `03_transaccion_command_box.mmd` | pulsación, ACK, reintentos y timeout | `firmware/app/command_box.c` |
| `04_perifericos_eventos.mmd` | ADC, botón, PWM y señales | `firmware/drivers/board_io.c`, `firmware/app/signals.c` |

No reemplazan el esquema físico de conexionado: ese debe conservarse como un
diagrama eléctrico independiente. Los cuatro archivos son fuentes; si se
necesitan imágenes para el informe, conviene exportarlas a SVG o PNG desde el
renderer elegido, sin editar las imágenes a mano.
