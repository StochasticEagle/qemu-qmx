/*
 * QMX machine configuration support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_QMX_H
#define QEMU_QMX_H

#include "qapi/error.h"

bool qmx_expand_argv(int *argc, char ***argv, Error **errp);

/* Runtime bookkeeping used only for QMX-defined tolerant media. */
void qmx_mark_drive_failed(const char *id);
bool qmx_drive_failed(const char *id);
const char *qmx_drive_path(const char *id);
bool qmx_should_skip_device(const char *device_id, const char **drive_id);

#endif /* QEMU_QMX_H */
