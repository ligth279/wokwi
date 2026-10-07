#include "fi_test.h"

volatile uint32_t fi_test_target = FI_TEST_NOMINAL;

void fi_test_inject(uint32_t *before, uint32_t *after)
{
    *before = fi_test_target;
    fi_test_target ^= 1u;
    *after = fi_test_target;
}

int fi_test_observe(void)
{
    return fi_test_target != FI_TEST_NOMINAL;
}

void fi_test_cleanup(void)
{
    fi_test_target = FI_TEST_NOMINAL;
}

uint32_t fi_test_read(void)
{
    return fi_test_target;
}
