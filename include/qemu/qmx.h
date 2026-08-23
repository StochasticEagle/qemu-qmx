/*
 * QMX machine configuration support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_QMX_H
#define QEMU_QMX_H

#include "qapi/error.h"

bool qmx_expand_argv(int *argc, char ***argv, Error **errp);

#endif /* QEMU_QMX_H */
