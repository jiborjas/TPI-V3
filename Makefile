# Punto de entrada unico para el TP Integrador - Variante 3.
# Los Makefiles especificos conservan sus responsabilidades; este archivo solo
# permite invocarlos desde la raiz del proyecto.

.DEFAULT_GOAL := all

.PHONY: all clean flash size check-deps openocd gdb firmware-help \
        test test-fsm test-sim test-clean stage4 help

all:
	$(MAKE) -C firmware all

clean:
	$(MAKE) -C firmware clean
	$(MAKE) -C tests clean

flash:
	$(MAKE) -C firmware flash

size:
	$(MAKE) -C firmware size

check-deps:
	$(MAKE) -C firmware check-deps

openocd:
	$(MAKE) -C firmware openocd

gdb:
	$(MAKE) -C firmware gdb

firmware-help:
	$(MAKE) -C firmware help

test:
	$(MAKE) -C tests run

test-fsm:
	$(MAKE) -C tests fsm

test-sim:
	$(MAKE) -C tests sim

test-clean:
	$(MAKE) -C tests clean

# Prepara el binario y las pruebas antes de conectar el bridge ROS 2 real.
# La ejecucion con robot requiere el puerto y topicos provistos en laboratorio.
stage4: all test
	@echo "Etapa 4 preparada: firmware compilado y tests aprobados."
	@echo "En laboratorio: make flash; ejecutar bridge ROS 2; observar topicos y robot."
	@echo "Guia: docs/etapa4_integracion.md"

help:
	@echo "Comandos desde la raiz de TPI-V3:"
	@echo "  make / make all  -> compila el firmware"
	@echo "  make flash       -> flashea con ST-Link/OpenOCD"
	@echo "  make openocd     -> inicia el servidor OpenOCD"
	@echo "  make gdb         -> conecta gdb-multiarch al servidor"
	@echo "  make size        -> muestra el uso de memoria"
	@echo "  make test        -> ejecuta tests de host"
	@echo "  make test-fsm    -> muestra el recorrido de la FSM"
	@echo "  make test-sim    -> corre la sesion simulada"
	@echo "  make stage4      -> prepara firmware y checklist de integracion"
	@echo "  make clean       -> limpia firmware y tests"
