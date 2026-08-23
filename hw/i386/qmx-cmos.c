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
#include "system/rtc.h"
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
    if ((value & 0x0f) > 9 || ((value >> 4) & 0x0f) > 9) {
        return -1;
    }
    return ((value >> 4) * 10) + (value & 0x0f);
}

static int qmx_cmos_to_bcd(MC146818RtcState *s, int value)
{
    if (s->cmos_data[RTC_REG_B] & REG_B_DM) {
        return value;
    }
    return ((value / 10) << 4) | (value % 10);
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
            if (hour < 1 || hour > 12) {
                hour = -1;
            } else {
                hour %= 12;
                if (s->cmos_data[RTC_HOURS] & 0x80) {
                    hour += 12;
                }
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
        tm.tm_wday < 0 || tm.tm_wday > 6 ||
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

static void qmx_cmos_set_current_time(MC146818RtcState *s)
{
    struct tm tm;
    int year;

    qemu_get_timedate(&tm, 0);
    s->cmos_data[RTC_SECONDS] = qmx_cmos_to_bcd(s, tm.tm_sec);
    s->cmos_data[RTC_MINUTES] = qmx_cmos_to_bcd(s, tm.tm_min);
    if (s->cmos_data[RTC_REG_B] & REG_B_24H) {
        s->cmos_data[RTC_HOURS] = qmx_cmos_to_bcd(s, tm.tm_hour);
    } else {
        int hour = (tm.tm_hour % 12) ? tm.tm_hour % 12 : 12;

        s->cmos_data[RTC_HOURS] = qmx_cmos_to_bcd(s, hour);
        if (tm.tm_hour >= 12) {
            s->cmos_data[RTC_HOURS] |= 0x80;
        }
    }
    s->cmos_data[RTC_DAY_OF_WEEK] = qmx_cmos_to_bcd(s, tm.tm_wday + 1);
    s->cmos_data[RTC_DAY_OF_MONTH] = qmx_cmos_to_bcd(s, tm.tm_mday);
    s->cmos_data[RTC_MONTH] = qmx_cmos_to_bcd(s, tm.tm_mon + 1);
    year = tm.tm_year + 1900 - s->base_year;
    s->cmos_data[RTC_YEAR] = qmx_cmos_to_bcd(s, year % 100);
    s->cmos_data[RTC_CENTURY] = qmx_cmos_to_bcd(s, year / 100);

    s->base_rtc = mktimegm(&tm);
    s->last_update = qemu_clock_get_ns(rtc_clock);
    s->offset = 0;
}

static void qmx_cmos_snapshot(QmxCmosState *state,
                              uint8_t image[QMX_CMOS_SIZE])
{
    static const uint8_t time_regs[] = {
        RTC_SECONDS, RTC_MINUTES, RTC_HOURS, RTC_DAY_OF_WEEK,
        RTC_DAY_OF_MONTH, RTC_MONTH, RTC_YEAR, RTC_CENTURY,
    };
    size_t i;

    memcpy(image, state->rtc->cmos_data, QMX_CMOS_SIZE);

    /* Register C is interrupt state, not persistent configuration. */
    image[RTC_REG_C] = state->last[RTC_REG_C];

    /*
     * With rtc_init=time0 the image provides persistent CMOS configuration,
     * while the wall clock is initialized from QEMU's normal time source.
     * Preserve the previously persisted RTC date/time bytes when saving so a
     * configuration change (for example SeaBIOS boot order or F2 delay) does
     * not incidentally turn the image into a time snapshot.
     */
    if (!state->rtc_from_image) {
        for (i = 0; i < ARRAY_SIZE(time_regs); i++) {
            image[time_regs[i]] = state->last[time_regs[i]];
        }
    }
}

static bool qmx_cmos_write(QmxCmosState *state, bool force)
{
    g_autoptr(GError) gerr = NULL;
    uint8_t image[QMX_CMOS_SIZE];

    if (!state->active) {
        return true;
    }

    qmx_cmos_snapshot(state, image);
    if (!force && memcmp(state->last, image, QMX_CMOS_SIZE) == 0) {
        return true;
    }

    if (!g_file_set_contents(state->file,
                             (const char *)image,
                             QMX_CMOS_SIZE, &gerr)) {
        warn_report("QMX nvram: cannot write '%s': %s; disabling persistent CMOS",
                    state->file, gerr->message);
        state->active = false;
        return false;
    }
    memcpy(state->last, image, QMX_CMOS_SIZE);
    return true;
}

static void qmx_cmos_flush_timer(void *opaque)
{
    QmxCmosState *state = opaque;

    qmx_cmos_write(state, false);
    if (state->active) {
        timer_mod(state->flush_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
                  QMX_CMOS_FLUSH_INTERVAL_MS);
    }
}

static void qmx_cmos_exit(Notifier *notifier, void *opaque)
{
    QmxCmosState *state = container_of(notifier, QmxCmosState, exit_notifier);

    (void)opaque;
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
        qmx_cmos_set_current_time(rtc);
    } else if (!qmx_cmos_set_image_time(rtc)) {
        memcpy(rtc->cmos_data, current, QMX_CMOS_SIZE);
        warn_report("QMX nvram: '%s' contains an invalid RTC date/time; using volatile CMOS",
                    file);
        return true;
    }

    /* Register C is interrupt state, not persistent configuration. */
    rtc->cmos_data[RTC_REG_C] = current[RTC_REG_C];

    if (qmx_cmos.active) {
        timer_free(qmx_cmos.flush_timer);
        qemu_remove_exit_notifier(&qmx_cmos.exit_notifier);
        g_free(qmx_cmos.file);
        memset(&qmx_cmos, 0, sizeof(qmx_cmos));
    }

    qmx_cmos.rtc = rtc;
    qmx_cmos.file = g_strdup(file);
    qmx_cmos.rtc_from_image = !strcmp(rtc_init, "image");
    memcpy(qmx_cmos.last, contents, QMX_CMOS_SIZE);
    qmx_cmos.last[RTC_REG_C] = current[RTC_REG_C];
    if (qmx_cmos.rtc_from_image) {
        memcpy(qmx_cmos.last, rtc->cmos_data, QMX_CMOS_SIZE);
    }
    qmx_cmos.active = true;

    qmx_cmos.exit_notifier.notify = qmx_cmos_exit;
    qemu_add_exit_notifier(&qmx_cmos.exit_notifier);
    qmx_cmos.flush_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                        qmx_cmos_flush_timer, &qmx_cmos);
    timer_mod(qmx_cmos.flush_timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
              QMX_CMOS_FLUSH_INTERVAL_MS);
    return true;
}

static void qmx_cmos_register(void)
{
    qmx_register_nvram_backend(qmx_cmos_init);
}

type_init(qmx_cmos_register)
