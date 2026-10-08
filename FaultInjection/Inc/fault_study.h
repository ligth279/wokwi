#ifndef FAULT_STUDY_H
#define FAULT_STUDY_H

#include <stdint.h>

#ifndef FI_STUDY_FAULTS
#define FI_STUDY_FAULTS 1
#endif
#ifndef RECOVERY
#define RECOVERY 0
#endif

/* Real injection routines of the nine study faults (CLAUDE.md section 35).
 * Each follows the fault_desc_t contract of fault_catalog.h.
 *
 * Injection sites in the application (all no-ops while no fault is armed):
 *   fi_study_sensor_hook()  control task, between receiving a sample and
 *                           computing the control output   (DATA-01)
 *   fi_study_site_sensor()  sensor task, after each period wake-up (TIM-02)
 *
 * Fault summary (target, how it is corrupted, expected raw impact):
 *   MEM-01   g_config.setpoint_centi   bit 10 flipped          wrong output
 *   MEM-02   sensor task saved LR      bit 29 flipped (stack)  crash on resume
 *   CPU-01   program counter           bx to (pc^2^29)|1       fetch fault
 *   CPU-02   stack pointer             sp = sp ^ 2^28          stack access fault
 *   TIM-01   control task              for(;;){}               hang
 *   TIM-02   sensor task               blocks forever          sensor/control stall
 *   DATA-01  one control input sample  replaced by 8500        wrong output (1 cycle)
 *   DATA-02  g_config.kp_pct_per_c     overwritten by 100      wrong output
 *   PERIPH-01 I2C SDA                  i2c-stuck chip TRIG=1   sensor reads fail
 */

#if FI_STUDY_FAULTS

#define FS_DATA01_VALUE 8500
#define FS_DATA02_KP    100
#define FS_MEM01_BIT    10u

/* Sensor task blocks this long without a heartbeat before TIM-02 counts as
 * observed (three sensor periods). */
#define FS_TIM02_STALL_MS 300u

int16_t fi_study_sensor_hook(int16_t centi);
void    fi_study_site_sensor(void);

void fs_mem01_inject(uint32_t *b, uint32_t *a);
int  fs_mem01_observe(void);
void fs_mem01_cleanup(void);
uint32_t fs_mem01_read(void);

void fs_mem02_plan(uint32_t *b, uint32_t *a);
void fs_mem02_inject(uint32_t *b, uint32_t *a);
void fs_cpu01_plan(uint32_t *b, uint32_t *a);
void fs_cpu01_inject(uint32_t *b, uint32_t *a);
void fs_cpu02_plan(uint32_t *b, uint32_t *a);
void fs_cpu02_inject(uint32_t *b, uint32_t *a);
void fs_tim01_plan(uint32_t *b, uint32_t *a);
void fs_tim01_inject(uint32_t *b, uint32_t *a);

void fs_tim02_inject(uint32_t *b, uint32_t *a);
int  fs_tim02_observe(void);
uint32_t fs_tim02_read(void);

void fs_data01_inject(uint32_t *b, uint32_t *a);
int  fs_data01_observe(void);
void fs_data01_cleanup(void);
uint32_t fs_data01_read(void);

void fs_data02_inject(uint32_t *b, uint32_t *a);
int  fs_data02_observe(void);
void fs_data02_cleanup(void);
uint32_t fs_data02_read(void);

void fs_periph01_inject(uint32_t *b, uint32_t *a);
int  fs_periph01_observe(void);
uint32_t fs_periph01_read(void);

#define FS_MEM01_INJECT fs_mem01_inject
#define FS_MEM01_OBSERVE fs_mem01_observe
#define FS_MEM01_CLEANUP fs_mem01_cleanup
#define FS_MEM01_READ fs_mem01_read
#if RECOVERY
int fs_mem02_observe(void); /* with recovery MEM-02 no longer crashes: observed when detected */
#define FS_MEM02_OBSERVE fs_mem02_observe
#else
#define FS_MEM02_OBSERVE 0
#endif
#define FS_MEM02_PLAN fs_mem02_plan
#define FS_MEM02_INJECT fs_mem02_inject
#define FS_CPU01_PLAN fs_cpu01_plan
#define FS_CPU01_INJECT fs_cpu01_inject
#define FS_CPU02_PLAN fs_cpu02_plan
#define FS_CPU02_INJECT fs_cpu02_inject
#define FS_TIM01_PLAN fs_tim01_plan
#define FS_TIM01_INJECT fs_tim01_inject
#define FS_TIM02_INJECT fs_tim02_inject
#define FS_TIM02_OBSERVE fs_tim02_observe
#define FS_TIM02_READ fs_tim02_read
#define FS_DATA01_INJECT fs_data01_inject
#define FS_DATA01_OBSERVE fs_data01_observe
#define FS_DATA01_CLEANUP fs_data01_cleanup
#define FS_DATA01_READ fs_data01_read
#define FS_DATA02_INJECT fs_data02_inject
#define FS_DATA02_OBSERVE fs_data02_observe
#define FS_DATA02_CLEANUP fs_data02_cleanup
#define FS_DATA02_READ fs_data02_read
#define FS_PERIPH01_INJECT fs_periph01_inject
#define FS_PERIPH01_OBSERVE fs_periph01_observe
#define FS_PERIPH01_READ fs_periph01_read

#else /* study faults not compiled in: the application sites are pass-throughs */

static inline int16_t fi_study_sensor_hook(int16_t centi) { return centi; }
static inline void fi_study_site_sensor(void) {}

#endif

#endif /* FAULT_STUDY_H */
