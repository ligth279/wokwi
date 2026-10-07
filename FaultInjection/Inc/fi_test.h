#ifndef FI_TEST_H
#define FI_TEST_H

#include <stdint.h>

/* FI-TEST: harmless framework self-test fault. The target is a dedicated
 * variable that nothing else in the application uses; the injection flips
 * bit 0, the observation checks the variable differs from its nominal
 * value, the cleanup restores it. GDB-assisted runs perform the same flip
 * from the debugger (Tests/gdb/fi_test_inject.gdb). */

#define FI_TEST_NOMINAL 0xC0FFEE00u

extern volatile uint32_t fi_test_target;

void fi_test_inject(uint32_t *before, uint32_t *after);
int  fi_test_observe(void);
void fi_test_cleanup(void);
uint32_t fi_test_read(void);

#endif /* FI_TEST_H */
