# Etapa 4 — Integración con bridge ROS 2 y robot

Esta etapa no se puede completar sin el bridge ROS 2 y el robot de la cátedra.
El firmware ya genera para la Variante 3 los eventos `cmd_deploy`,
`cmd_return`, `cmd_patrol` y `cmd_stop`, más sus tramas `STS` y
`DAT:param=NNN`.

## Preparación

1. Desde la raíz ejecutar `make stage4` y luego `make flash`.
2. Conectar GND común y UART cruzada: PA9 de la Blue Pill a RX del adaptador,
   y PA10 a TX. Configuración: 115200, 8N1.
3. Ejecutar el bridge ROS 2 entregado por la cátedra con el puerto asignado.

## Verificación requerida

1. Ejecutar `ros2 topic list` y anotar los tópicos publicados por el bridge.
2. Abrir los tópicos relevantes con `ros2 topic echo <topico>`.
3. Presionar el botón cuatro veces, esperando `ACK:ok` entre cada presión.
   Verificar los cuatro cambios de modo y la reacción del robot.
4. Medir cinco veces la latencia entre presión y reacción (video o timestamps
   de terminal/tópicos), e informar mínimo, máximo y promedio.
5. Desconectar UART o apagar el bridge durante un evento: deben observarse los
   cinco reintentos de EVT y `ERR:timeout`. Al reconectar, una nueva pulsación
   debe iniciar una transacción nueva.

## Evidencia para entregar

- Video del robot reaccionando en los cuatro modos.
- Captura simultánea del monitor serie y de los tópicos ROS 2.
- Log del caso de falla y una breve descripción de la recuperación observada.
