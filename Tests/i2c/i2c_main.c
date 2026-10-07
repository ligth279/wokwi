/*
 * Step 1 - I2C sensor chip + stuck-low fault chip verification.
 *
 *   I2C-SCAN        exactly one device, at 0x48
 *   I2C-WHOAMI      WHO_AM_I == 0x5A
 *   I2C-TEMP        5 reads over 800 ms: in base +-3.5 C, sample counter advances
 *   I2C-IDLE-LINE   SDA reads HIGH at the MCU pin on an idle bus
 *   I2C-STUCK-LINE  TRIG pulse -> SDA physically reads LOW at the MCU pin
 *   I2C-STUCK-ERR   sensor transaction fails while SDA is stuck (reports status
 *                   and DWT cycles until the failure was returned)
 *   I2C-RECOVER     9-clock bus clear frees SDA; sensor works again
 *   I2C-PERM-FAIL   TRIG held HIGH -> bus clear cannot free SDA (expected)
 *   I2C-PERM-CLEAR  TRIG released -> bus clear frees SDA; sensor works again
 */
#include "board.h"
#include "dwt.h"
#include "i2c_recovery.h"
#include "log.h"
#include "sensor.h"
#include <stdio.h>

static uint32_t failures;

static void result(const char *id, int pass, const char *detail)
{
    LOG("I2CTEST", "id=%s result=%s %s", id, pass ? "PASS" : "FAIL", detail);
    if (!pass) failures++;
}

static void trig(int level)
{
    HAL_GPIO_WritePin(FAULT_I2C_TRIG_PORT, FAULT_I2C_TRIG_PIN, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static int sensor_alive(void)
{
    uint8_t id = 0;
    return sensor_read_whoami(&id) == SENSOR_OK;
}

static void recover_and_report(const char *id, int expect_success)
{
    char d[128];
    i2c_recovery_report_t rep;
    uint32_t c0 = dwt_cycles();
    int ok = i2c_bus_recover(&rep);
    uint32_t cyc = dwt_cycles() - c0;
    int alive = ok && sensor_alive();
    snprintf(d, sizeof d, "sda_low_before=%d clocks=%lu sda_high_after=%d reinit=%d sensor_ok=%d cycles=%lu",
             rep.sda_low_before, (unsigned long)rep.clocks_sent, rep.sda_high_after, rep.reinit_ok,
             alive, (unsigned long)cyc);
    result(id, expect_success ? alive : !rep.sda_high_after, d);
}

int main(void)
{
    char d[128];
    HAL_Init();
    board_clock_init();
    board_gpio_init();
    board_uart_init();
    dwt_init();
    LOG("BOOT", "system_start build=i2ctest");
    board_i2c_init();

    /* Scan */
    int found = 0, found48 = 0;
    for (uint16_t a = 0x08; a < 0x78; a++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(a << 1), 1, 2) == HAL_OK) {
            found++;
            if (a == SENSOR_I2C_ADDR) found48 = 1;
            LOG("I2CTEST", "device addr=0x%02X", a);
        }
    }
    snprintf(d, sizeof d, "devices=%d has_0x48=%d", found, found48);
    result("I2C-SCAN", found == 1 && found48, d);

    /* WHO_AM_I */
    uint8_t id = 0;
    sensor_status_t st = sensor_read_whoami(&id);
    snprintf(d, sizeof d, "status=%s id=0x%02X", sensor_status_str(st), id);
    result("I2C-WHOAMI", st == SENSOR_OK, d);

    /* Temperature stream */
    int ok_reads = 0, in_range = 0;
    uint8_t s_first = 0, s_last = 0;
    sensor_read_sample(&s_first);
    for (int i = 0; i < 5; i++) {
        int16_t t = 0;
        st = sensor_read_temp(&t);
        if (st == SENSOR_OK) ok_reads++;
        if (t >= 2500 - 350 && t <= 2500 + 350) in_range++;
        LOG("SENSOR", "value=%d status=%s", t, sensor_status_str(st));
        HAL_Delay(200);
    }
    sensor_read_sample(&s_last);
    snprintf(d, sizeof d, "ok_reads=%d in_range=%d sample_first=%u sample_last=%u", ok_reads, in_range,
             s_first, s_last);
    result("I2C-TEMP", ok_reads == 5 && in_range == 5 && (uint8_t)(s_last - s_first) == 5, d);

    /* Line level must read HIGH when idle, or the stuck check below is meaningless. */
    int sda_idle_low = i2c_sda_is_low();
    snprintf(d, sizeof d, "sda_low=%d", sda_idle_low);
    result("I2C-IDLE-LINE", !sda_idle_low, d);

    /* Transient stuck-low: TRIG pulse */
    trig(1);
    HAL_Delay(1);
    trig(0);
    HAL_Delay(1);
    int sda_low = i2c_sda_is_low();
    snprintf(d, sizeof d, "sda_low=%d", sda_low);
    result("I2C-STUCK-LINE", sda_low, d);

    int16_t t = 0;
    uint32_t c0 = dwt_cycles();
    st = sensor_read_temp(&t);
    uint32_t cyc = dwt_cycles() - c0;
    snprintf(d, sizeof d, "status=%s cycles_to_error=%lu", sensor_status_str(st), (unsigned long)cyc);
    result("I2C-STUCK-ERR", st != SENSOR_OK, d);

    recover_and_report("I2C-RECOVER", 1);

    /* Permanent stuck-low: TRIG held high */
    trig(1);
    HAL_Delay(1);
    st = sensor_read_temp(&t);
    LOG("I2CTEST", "perm_stuck read_status=%s", sensor_status_str(st));
    recover_and_report("I2C-PERM-FAIL", 0);
    trig(0);
    HAL_Delay(1);
    recover_and_report("I2C-PERM-CLEAR", 1);

    LOG("I2CTEST", "summary failures=%lu", (unsigned long)failures);
    LOG("I2CTEST", "I2CTEST_DONE");
    while (1) {
        HAL_Delay(500);
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}
