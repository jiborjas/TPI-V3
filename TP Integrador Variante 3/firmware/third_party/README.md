# Dependencias de terceros

El firmware necesita `libopencm3` y `FreeRTOS-Kernel` en esta carpeta.
Para no duplicar ~100 MB, no se incluyen en la entrega. Dos opciones:

## Opción A: copiar desde el TP5 compartido (recomendado)

```bash
cp -r "../../SE TPs/TPs compartidos/TP5/firmware/third_party/libopencm3" .
cp -r "../../SE TPs/TPs compartidos/TP5/firmware/third_party/FreeRTOS-Kernel" .
```

## Opción B: clonar desde los repositorios oficiales

```bash
git clone https://github.com/libopencm3/libopencm3.git
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
```

Luego, desde `firmware/`:

```bash
make          # compila libopencm3 la primera vez y después el firmware
```

`common/linker.ld` (script de linker para STM32F103C8T6, 64K flash / 20K RAM)
ya está incluido en esta carpeta.
