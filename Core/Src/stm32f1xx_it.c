#include "stm32f1xx_hal.h"

#ifdef USE_FREERTOS
/* SysTick_Handler is provided by the FreeRTOS port (RTOS/port_wokwi_cm3),
 * which calls this hook once per elapsed tick, before and after the
 * scheduler starts. */
void vPortOnTick(void)
{
    HAL_IncTick();
}
#else
void SysTick_Handler(void)
{
    HAL_IncTick();
}
#endif
