/*
 * QMX persistent legacy PC CMOS backing
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/qmx.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "system/system.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/rtc/mc146818rtc_regs.h"

#define QMX_CMOS_SIZE 128
#define QMX_CMOS_FLUSH_INTERVAL_MS 1000

typedef struct QmxCmosState {
    MC146818RtcState *rtc;
    char *file;
    bool rtc_from_image;
    uint8_t last[QMX_CMOS_SIZE];
    QEMUTimer *flush_timer;
    Notifier exit_notifier;
    bool active;
} QmxCmosState;

static QmxCmosState qmx_cmos;

static int qmx_cmos_from_bcd(MC146818RtcState *s, int value)
{
    if ((value & 0xc0) == 0xc0) {
        return -1;
    }
    if (s->cmos_data[RTC_REG_B] & REG_B_DM) {
        return value;
    }
    return ((value >> 4) * 10) + (value & 0x0f);
}

static bool qmx_cmos_set_image_time(MC146818RtcState *s)
{
    struct tm tm = { 0 };
    int hour;
    int century;
    int year;

    tm.tm_sec = qmx_cmos_from_bcd(s, s->cmos_data[RTC_SECONDS]);
    tm.tm_min = qmx_cmos_from_bcd(s, s->cmos_data[RTC_MINUTES]);
    hour = qmx_cmos_from_bcd(s, s->cmos_data[RTC_HOURS] & 0x7f);
    if (!(s->cmos_data[RTC_REG_B] & REG_B_24H)) {
        if (hour >= 0) {
            hour %= 12;
            if (s->cmos_data[RTC_HOURS] & 0x80) {
                hour += 12;
            }
        }
    }
    tm.tm_hour = hour;
    tm.tm_wday = qmx_cmos_from_bcd(s, s->cmos_data[RTC_DAY_OF_WEEK]) - 1;
    tm.tm_mday = qmx_cmos_from_bcd(s, s->cmos_data[RTC_DAY_OF_MONTH]);
    tm.tm_mon = qmx_cmos_from_bcd(s, s->cmos_data[RTC_MONTH]) - 1;
    year = qmx_cmos_from_bcd(s, s->cmos_data[RTC_YEAR]);
    century = qmx_cmos_from_bcd(s, s->cmos_data[RTC_CENTURY]);

    if (tm.tm_sec < 0 || tm.tm_sec > 59 ||
        tm.tm_min < 0 || tm.tm_min > 59 ||
        tm.tm_hour < 0 || tm.tm_hour > 23 ||
        tm.tm_mday < 1 || tm.tm_mday > 31 ||
        tm.tm_mon < 0 || tm.tm_mon > 11 ||
        year < 0 || year > 99 || century < 0 || century > 99) {
        return false;
    }

    tm.tm_year = year + s->base_year + century * 100 - 1900;
    s->base_rtc = mktimegm(&tm);
    s->last_update = qemu_clock_get_ns(rtc_clock);
    s->offset = 0;
    return true;
}

static void qmx_cmos_restore_current_time(uint8_t current[QMX_CMOS_SIZE],
                                          MC146818RtcState *s)
{
    static const uint8_t time_regs[] = {
        RTC_SECONDS, RTC_MINUTES, RTC_HOURS, RTC_DAY_OF_WEEK,
        RTC_DAY_OF_MONTH, RTC_MONTH, RTC_YEAR, RTC_CENTURY,
    };
    size_t i;

    for (i = 0; i < ARRAY_SIZE(time_regs); i++) {
        s->cmos_data[time_regs[i]] = current[time_regs[i]];
    }
    /* Register C is interrupt state, not persistent configuration. */
    s->cmos_data[RTC_REG_C] = current[RTC_REG_C];
}

static bool qmx_cmos_write(QmxCmosState *state, bool force)
{
    g_autoptr(GError) gerr = NULL;

    if (!state->active) {
        return true;
    }
    if (!force && !memcmp(state->last, state->rtc->cmos_data,
                          QMX_CMOS_SIZE)) {
        return true;
    }

    if (!g_file_set_contents(state->file,
                             (const char *)state->rtc->cmos_data,
                             QMX_CMOS_SIZE, &gerr)) {
        warn_report("QMX nvram: cannot write '%s': %s; disabling persistent CMOS",
                    state->file, gerr->message);
        state->active = false;
        return false;
    }
    memcpy(state->last, state->rtc->cmos_data, QMX_CMOS_SIZE);
    return true;
}

static void qmx_cmos_flush_timer(void *opaque)
{
    QmxCmosState *state = opaque;

    qmx_cmos_write(state, false);
    if (state->active) {
        timer_mod(state->flush_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_REALTIME) /
                  NANOSECONDS_PER_MILLISECOND + QMX_CMOS_FLUSH_INTERVAL_MS);
    }
}

static void qmx_cmos_exit(Notifier *notifier, void *opaque)
{
    QmxCmosState *state = container_of(notifier, QmxCmosState, exit_notifier);

    qmx_cmos_write(state, true);
}

static bool qmx_cmos_init(const char *file, const char *rtc_init, Error **errp)
{
    bool ambiguous = false;
    Object *obj;
    MC146818RtcState *rtc;
    g_autofree char *contents = NULL;
    g_autoptr(GError) gerr = NULL;
    gsize len = 0;
    uint8_t current[QMX_CMOS_SIZE];
    bool existed;

    obj = object_resolve_path_type("", TYPE_MC146818_RTC, &ambiguous);
    if (!obj || ambiguous) {
        error_setg(errp,
                   "QMX nvram cmos128 requires exactly one MC146818-compatible RTC");
        return false;
    }
    rtc = MC146818_RTC(obj);
    memcpy(current, rtc->cmos_data, QMX_CMOS_SIZE);

    existed = g_file_get_contents(file, &contents, &len, &gerr);
    if (!existed) {
        if (!g_error_matches(gerr, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            warn_report("QMX nvram: cannot open '%s': %s; using volatile CMOS",
                        file, gerr->message);
            return true;
        }
        g_clear_error(&gerr);
        if (!g_file_set_contents(file, (const char *)current,
                                 QMX_CMOS_SIZE, &gerr)) {
            warn_report("QMX nvram: cannot create '%s': %s; using volatile CMOS",
                        file, gerr->message);
            return true;
        }
        contents = g_memdup2(current, QMX_CMOS_SIZE);
        len = QMX_CMOS_SIZE;
    }

    if (len != QMX_CMOS_SIZE) {
        warn_report("QMX nvram: '%s' is %" G_GSIZE_FORMAT
                    " bytes, expected %d; using volatile CMOS",
                    file, len, QMX_CMOS_SIZE);
        return true;
    }

    memcpy(rtc->cmos_data, contents, QMX_CMOS_SIZE);
    if (!strcmp(rtc_init, "time0")) {
        qmx_cmos_restore_current_time(current, rtc);
    } else if (!qmx_cmos_set_image_time(rtc)) {
        memcpy(rtc->cmos_data, current, QMX_CMOS_SIZE);
        warn_report("QMX nvram: '%s' contains an invalid RTC date/time; using volatile CMOS",
                    file);
        return true;
    }

    if (qmx_cmos.active) {
        timer_free(qmx_cmos.flush_timer);
        qemu_remove_exit_notifier(&qmx_cmos.exit_notifier);
        g_free(qmx_cmos.file);
        memset(&qmx_cmos, 0, sizeof(qmx_cmos));
    }

    qmx_cmos.rtc = rtc;
    qmx_cmos.file = g_strdup(file);
    qmx_cmos.rtc_from_image = !strcmp(rtc_init, "image");
    memcpy(qmx_cmos.last, rtc->cmos_data, QMX_CMOS_SIZE);
    qmx_cmos.active = true;

    qmx_cmos.exit_notifier.notify = qmx_cmos_exit;
    qemu_add_exit_notifier(&qmx_cmos.exit_notifier);
    qmx_cmos.flush_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                        qmx_cmos_flush_timer, &qmx_cmos);
    timer_mod(qmx_cmos.flush_timer,
              qemu_clock_get_ns(QEMU_CLOCK_REALTIME) /
              NANOSECONDS_PER_MILLISECOND + QMX_CMOS_FLUSH_INTERVAL_MS);
    return true;
}

static void qmx_cmos_register(void)
{
    qmx_register_nvram_backend(qmx_cmos_init);
}

type_init(qmx_cmos_register)
