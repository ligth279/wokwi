/* STM32F103 implementation of the fault-injection port (fi_port.h):
 * DWT cycle counter, HAL tick and TIM4 as the one-shot injection timer. */
#include "fi_port.h"
#include "dwt.h"
#include "stm32f1xx.h"
#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static SemaphoreHandle_t fi_mutex;

void fi_port_init(void)
{
    fi_mutex = xSemaphoreCreateMutex();
}

int fi_port_log_lock(void)
{
    if (fi_mutex == NULL || __get_IPSR() != 0u || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return 0;
    }
    xSemaphoreTake(fi_mutex, portMAX_DELAY);
    return 1;
}

void fi_port_log_unlock(int locked)
{
    if (locked) {
        xSemaphoreGive(fi_mutex);
    }
}

uint32_t fi_port_cycles(void)
{
    return dwt_cycles();
}

uint32_t fi_port_ms(void)
{
    return HAL_GetTick();
}

void fi_port_timer_start(uint32_t delay_ms)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    TIM4->CR1  = 0;
    TIM4->DIER = 0;
    TIM4->PSC  = 7200u - 1u;          /* 72 MHz / 7200 = 10 kHz (0.1 ms) */
    TIM4->ARR  = delay_ms * 10u - 1u; /* <= 59999 for 6000 ms */
    TIM4->CNT  = 0;
    TIM4->CR1  = TIM_CR1_OPM | TIM_CR1_URS;
    TIM4->EGR  = TIM_EGR_UG;          /* load PSC */
    TIM4->SR   = 0;
    TIM4->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM4_IRQn, 6);
    NVIC_EnableIRQ(TIM4_IRQn);
    TIM4->CR1 |= TIM_CR1_CEN;
}

void fi_port_timer_stop(void)
{
    TIM4->CR1 = 0;
    TIM4->DIER = 0;
}

void TIM4_IRQHandler(void)
{
    if (!(TIM4->SR & TIM_SR_UIF)) {
        return;
    }
    TIM4->SR = ~TIM_SR_UIF;
    fi_port_timer_stop(); /* one-pulse: stop for good */
    fi_timer_expired();
}
