#include "../firmware/config/FreeRTOSConfig.h"

#if (configUSE_MALLOC_FAILED_HOOK != 1)
#error "El hook de fallo de heap debe permanecer habilitado"
#endif

#if (configCHECK_FOR_STACK_OVERFLOW < 2)
#error "La deteccion reforzada de desborde de stack debe permanecer habilitada"
#endif

#include <stdio.h>

int main(void)
{
    puts("[OK]   FreeRTOS: hooks de heap y stack habilitados");
    return 0;
}
