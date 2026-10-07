#include "soft_uart_rx.h"
#include "stm32f1xx.h"

#define RX_PIN      10u
#define RING_SIZE   64u

static volatile uint8_t ring[RING_SIZE];
static volatile uint8_t head, tail;
static volatile soft_uart_stats_t stats;

static volatile uint32_t half_ticks; /* half-bit periods since the start edge */
static volatile uint8_t  shift;

static inline uint32_t rx_level(void)
{
    return (GPIOA->IDR >> RX_PIN) & 1u;
}

static void arm_start_edge(void)
{
    EXTI->PR  = 1u << RX_PIN;
    EXTI->IMR |= 1u << RX_PIN;
}

void soft_uart_rx_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    /* PA10: input with pull-up (CNF=10, MODE=00; ODR=1 selects pull-up). */
    GPIOA->CRH = (GPIOA->CRH & ~(0xFu << ((RX_PIN - 8u) * 4u))) | (0x8u << ((RX_PIN - 8u) * 4u));
    GPIOA->ODR |= 1u << RX_PIN;

    /* EXTI10 <- port A, falling edge. */
    AFIO->EXTICR[2] &= ~AFIO_EXTICR3_EXTI10;
    EXTI->FTSR |= 1u << RX_PIN;
    EXTI->RTSR &= ~(1u << RX_PIN);

    /* TIM3: 72 MHz timer clock (APB1 x2), update every half bit. */
    TIM3->CR1  = 0;
    TIM3->PSC  = 0;
    TIM3->ARR  = (SystemCoreClock / SOFT_UART_RX_BAUD / 2u) - 1u;
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->EGR  = TIM_EGR_UG;
    TIM3->SR   = 0;

    NVIC_SetPriority(EXTI15_10_IRQn, 6);
    NVIC_SetPriority(TIM3_IRQn, 6);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);
    arm_start_edge();
}

void EXTI15_10_IRQHandler(void)
{
    if (EXTI->PR & (1u << RX_PIN)) {
        EXTI->PR = 1u << RX_PIN;
        EXTI->IMR &= ~(1u << RX_PIN); /* ignore data-bit edges until the byte ends */
        half_ticks = 0;
        shift = 0;
        TIM3->CNT = 0;
        TIM3->SR = 0;
        TIM3->CR1 = TIM_CR1_CEN;
    }
}

void TIM3_IRQHandler(void)
{
    if (!(TIM3->SR & TIM_SR_UIF)) {
        return;
    }
    TIM3->SR = ~TIM_SR_UIF;
    uint32_t k = ++half_ticks; /* sample point k = time k/2 bits after the edge */
    uint32_t level = rx_level();

    if (k == 1u) { /* middle of start bit */
        if (level != 0u) {
            stats.false_starts++;
            TIM3->CR1 = 0;
            arm_start_edge();
        }
    } else if (k >= 3u && k <= 17u && (k & 1u)) { /* data bits 0..7, LSB first */
        shift = (uint8_t)((shift >> 1) | (level << 7));
    } else if (k == 19u) { /* middle of stop bit */
        TIM3->CR1 = 0;
        if (level == 0u) {
            stats.framing_errors++;
        } else {
            uint8_t next = (uint8_t)((head + 1u) % RING_SIZE);
            if (next != tail) {
                ring[head] = shift;
                head = next;
                stats.bytes++;
            } else {
                stats.overflows++;
            }
        }
        arm_start_edge();
    }
}

int soft_uart_getc(void)
{
    if (tail == head) {
        return -1;
    }
    uint8_t c = ring[tail];
    tail = (uint8_t)((tail + 1u) % RING_SIZE);
    return c;
}

void soft_uart_get_stats(soft_uart_stats_t *out)
{
    out->bytes = stats.bytes;
    out->framing_errors = stats.framing_errors;
    out->false_starts = stats.false_starts;
    out->overflows = stats.overflows;
}
