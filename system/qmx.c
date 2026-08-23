/*
 * QMX machine configuration support
 *
 * QMX is intentionally translated into QEMU's existing command-line option
 * machinery. This keeps validation and object creation in the QEMU
 * subsystems that already own those semantics instead of duplicating them in
 * the QMX parser.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/qmx.h"

static GHashTable *qmx_drive_paths;
static GHashTable *qmx_device_drives;
static GHashTable *qmx_failed_drives;
static char *qmx_nvram_file;
static char *qmx_nvram_rtc_init;
static bool qmx_check_mode;
static QmxNvramBackendInit qmx_nvram_backend;

static void qmx_runtime_reset(void)
{
    g_clear_pointer(&qmx_drive_paths, g_hash_table_destroy);
    g_clear_pointer(&qmx_device_drives, g_hash_table_destroy);
    g_clear_pointer(&qmx_failed_drives, g_hash_table_destroy);
    g_clear_pointer(&qmx_nvram_file, g_free);
    g_clear_pointer(&qmx_nvram_rtc_init, g_free);

    qmx_drive_paths = g_hash_table_new_full(g_str_hash, g_str_equal,
                                            g_free, g_free);
    qmx_device_drives = g_hash_table_new_full(g_str_hash, g_str_equal,
                                              g_free, g_free);
    qmx_failed_drives = g_hash_table_new_full(g_str_hash, g_str_equal,
                                              g_free, NULL);
}

bool qmx_check_requested(void)
{
    return qmx_check_mode;
}

void qmx_register_nvram_backend(QmxNvramBackendInit initfn)
{
    qmx_nvram_backend = initfn;
}

bool qmx_runtime_init(Error **errp)
{
    if (!qmx_nvram_file) {
        return true;
    }
    if (!qmx_nvram_backend) {
        error_setg(errp,
                   "QMX nvram format cmos128 is not supported by this machine/target");
        return false;
    }
    return qmx_nvram_backend(qmx_nvram_file, qmx_nvram_rtc_init, errp);
}

void qmx_mark_drive_failed(const char *id)
{
    if (qmx_failed_drives && id) {
        g_hash_table_add(qmx_failed_drives, g_strdup(id));
    }
}

bool qmx_drive_failed(const char *id)
{
    return qmx_failed_drives && id &&
           g_hash_table_contains(qmx_failed_drives, id);
}

const char *qmx_drive_path(const char *id)
{
    if (!qmx_drive_paths || !id) {
        return NULL;
    }
    return g_hash_table_lookup(qmx_drive_paths, id);
}

bool qmx_should_skip_device(const char *device_id, const char **drive_id)
{
    const char *drive;

    if (!qmx_device_drives || !device_id) {
        return false;
    }
    drive = g_hash_table_lookup(qmx_device_drives, device_id);
    if (!drive || !qmx_drive_failed(drive)) {
        return false;
    }
    if (drive_id) {
        *drive_id = drive;
    }
    return true;
}

static bool qmx_is_ident(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    if (!(*p == '_' || g_ascii_isalpha(*p))) {
        return false;
    }
    for (p++; *p; p++) {
        if (!(*p == '_' || *p == '-' || g_ascii_isalnum(*p))) {
            return false;
        }
    }
    return true;
}

static bool qmx_is_key(const char *key)
{
    g_auto(GStrv) parts = g_strsplit(key, ".", -1);
    int i;

    for (i = 0; parts[i]; i++) {
        if (!parts[i][0] || !qmx_is_ident(parts[i])) {
            return false;
        }
    }
    return i != 0;
}

static char *qmx_unquote(const char *text, Error **errp)
{
    g_autofree char *tmp = g_strdup(text);
    char *s = g_strstrip(tmp);
    GString *out;
    size_t len = strlen(s);
    size_t i;

    if (!len || s[0] != '"') {
        if (strchr(s, '"')) {
            error_setg(errp, "unexpected quote in scalar '%s'", s);
            return NULL;
        }
        return g_strdup(s);
    }
    if (len < 2 || s[len - 1] != '"') {
        error_setg(errp, "unterminated quoted string");
        return NULL;
    }

    out = g_string_new(NULL);
    for (i = 1; i + 1 < len; i++) {
        if (s[i] == '"') {
            error_setg(errp, "unescaped quote in quoted string");
            g_string_free(out, true);
            return NULL;
        }
        if (s[i] != '\\') {
            g_string_append_c(out, s[i]);
            continue;
        }
        i++;
        if (i >= len - 1) {
            error_setg(errp, "unterminated escape sequence");
            g_string_free(out, true);
            return NULL;
        }
        switch (s[i]) {
        case '\\':
            g_string_append_c(out, '\\');
            break;
        case '"':
            g_string_append_c(out, '"');
            break;
        case 'n':
            g_string_append_c(out, '\n');
            break;
        case 't':
            g_string_append_c(out, '\t');
            break;
        default:
            error_setg(errp, "unsupported escape sequence \\%c", s[i]);
            g_string_free(out, true);
            return NULL;
        }
    }
    return g_string_free(out, false);
}

static GPtrArray *qmx_split_fields(const char *value, Error **errp)
{
    GPtrArray *fields = g_ptr_array_new_with_free_func(g_free);
    GString *field = g_string_new(NULL);
    bool quoted = false;
    bool escaped = false;
    const char *p;

    for (p = value; ; p++) {
        char c = *p;

        if (escaped) {
            if (c == '\0') {
                error_setg(errp, "unterminated escape sequence");
                g_string_free(field, true);
                g_ptr_array_free(fields, true);
                return NULL;
            }
            g_string_append_c(field, c);
            escaped = false;
            continue;
        }
        if (quoted && c == '\\') {
            g_string_append_c(field, c);
            escaped = true;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
            g_string_append_c(field, c);
            continue;
        }
        if (c == '\0' && quoted) {
            error_setg(errp, "unterminated quoted string");
            g_string_free(field, true);
            g_ptr_array_free(fields, true);
            return NULL;
        }
        if ((!quoted && c == ',') || c == '\0') {
            char *item = g_string_free(field, false);
            g_strstrip(item);
            if (!item[0]) {
                error_setg(errp, "empty item in property list");
                g_free(item);
                g_ptr_array_free(fields, true);
                return NULL;
            }
            g_ptr_array_add(fields, item);
            if (c == '\0') {
                break;
            }
            field = g_string_new(NULL);
            continue;
        }
        g_string_append_c(field, c);
    }

    return fields;
}

static char *qmx_escape_qemu_value(const char *value)
{
    GString *out = g_string_new(NULL);
    const char *p;

    for (p = value; *p; p++) {
        if (*p == ',') {
            g_string_append_c(out, ',');
        }
        g_string_append_c(out, *p);
    }
    return g_string_free(out, false);
}

static char *qmx_resolve_path(const char *dir, const char *path)
{
    if (g_path_is_absolute(path)) {
        return g_canonicalize_filename(path, NULL);
    }
    return g_canonicalize_filename(path, dir);
}

static bool qmx_property_is_path(const char *family, const char *property)
{
    if (!strcmp(family, "drive")) {
        return !strcmp(property, "file");
    }
    if (!strcmp(family, "fw_cfg")) {
        return !strcmp(property, "file");
    }
    if (!strcmp(family, "chardev")) {
        return !strcmp(property, "path") || !strcmp(property, "logfile");
    }
    if (!strcmp(family, "netdev")) {
        return !strcmp(property, "script") ||
               !strcmp(property, "downscript") ||
               !strcmp(property, "vhostdev");
    }
    if (!strcmp(family, "object")) {
        return !strcmp(property, "file") ||
               !strcmp(property, "filename") ||
               !strcmp(property, "mem-path");
    }
    return false;
}

static char *qmx_rebase_chardev_scalar(const char *value, const char *qmx_dir)
{
    static const char *prefixes[] = { "file:", "pipe:", "unix:" };
    size_t i;

    for (i = 0; i < ARRAY_SIZE(prefixes); i++) {
        const char *prefix = prefixes[i];
        const char *rest;
        const char *comma = NULL;
        g_autofree char *path = NULL;
        g_autofree char *resolved = NULL;

        if (!g_str_has_prefix(value, prefix)) {
            continue;
        }
        rest = value + strlen(prefix);
        if (!strcmp(prefix, "unix:")) {
            comma = strchr(rest, ',');
        }
        path = comma ? g_strndup(rest, comma - rest) : g_strdup(rest);
        resolved = qmx_resolve_path(qmx_dir, path);
        return g_strdup_printf("%s%s%s", prefix, resolved, comma ? comma : "");
    }
    return g_strdup(value);
}

static char *qmx_build_properties(const char *value, const char *family,
                                  const char *id, bool inject_id,
                                  const char *qmx_dir, char **file_out,
                                  char **drive_out, char **name_out,
                                  Error **errp)
{
    g_autoptr(GPtrArray) fields = qmx_split_fields(value, errp);
    g_autoptr(GHashTable) seen = NULL;
    GString *out;
    bool have_id = false;
    bool have_bare = false;
    guint i;

    if (file_out) {
        *file_out = NULL;
    }
    if (drive_out) {
        *drive_out = NULL;
    }
    if (name_out) {
        *name_out = NULL;
    }
    if (!fields) {
        return NULL;
    }

    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    out = g_string_new(NULL);
    for (i = 0; i < fields->len; i++) {
        char *field = g_ptr_array_index(fields, i);
        char *eq = strchr(field, '=');
        g_autofree char *decoded = NULL;
        g_autofree char *escaped = NULL;

        if (i) {
            g_string_append_c(out, ',');
        }
        if (!eq) {
            if (have_bare || i != 0) {
                error_setg(errp, "property '%s' is missing '='", field);
                goto fail;
            }
            have_bare = true;
            decoded = qmx_unquote(field, errp);
            if (!decoded) {
                goto fail;
            }
            escaped = qmx_escape_qemu_value(decoded);
            g_string_append(out, escaped);
            continue;
        }

        *eq = '\0';
        g_strstrip(field);
        g_strstrip(eq + 1);
        if (!qmx_is_ident(field)) {
            error_setg(errp, "invalid property name '%s'", field);
            goto fail;
        }
        if (!eq[1]) {
            error_setg(errp, "empty value for property '%s'", field);
            goto fail;
        }
        if (g_hash_table_contains(seen, field)) {
            error_setg(errp, "duplicate property '%s'", field);
            goto fail;
        }
        g_hash_table_add(seen, g_strdup(field));

        decoded = qmx_unquote(eq + 1, errp);
        if (!decoded) {
            goto fail;
        }
        if (!strcmp(field, "id")) {
            have_id = true;
            if (id && strcmp(decoded, id)) {
                error_setg(errp,
                           "explicit id '%s' does not match QMX object id '%s'",
                           decoded, id);
                goto fail;
            }
        }
        if (qmx_property_is_path(family, field)) {
            g_autofree char *resolved = qmx_resolve_path(qmx_dir, decoded);
            g_free(g_steal_pointer(&decoded));
            decoded = g_strdup(resolved);
        }
        if (file_out && !strcmp(field, "file")) {
            *file_out = g_strdup(decoded);
        }
        if (drive_out && !strcmp(field, "drive")) {
            *drive_out = g_strdup(decoded);
        }
        if (name_out && !strcmp(field, "name")) {
            *name_out = g_strdup(decoded);
        }
        escaped = qmx_escape_qemu_value(decoded);
        g_string_append_printf(out, "%s=%s", field, escaped);
    }

    if (inject_id && !have_id) {
        g_autofree char *escaped_id = qmx_escape_qemu_value(id);
        if (out->len) {
            g_string_append_c(out, ',');
        }
        g_string_append_printf(out, "id=%s", escaped_id);
    }
    return g_string_free(out, false);

fail:
    if (file_out) {
        g_clear_pointer(file_out, g_free);
    }
    if (drive_out) {
        g_clear_pointer(drive_out, g_free);
    }
    if (name_out) {
        g_clear_pointer(name_out, g_free);
    }
    g_string_free(out, true);
    return NULL;
}

static void qmx_add_arg(GPtrArray *args, const char *arg)
{
    g_ptr_array_add(args, g_strdup(arg));
}

static void qmx_add_option(GPtrArray *args, const char *option,
                           const char *value)
{
    qmx_add_arg(args, option);
    qmx_add_arg(args, value);
}

static char *qmx_cli_get_property(const char *value, const char *property)
{
    g_auto(GStrv) fields = g_strsplit(value, ",", -1);
    g_autofree char *prefix = g_strdup_printf("%s=", property);
    int i;

    for (i = 0; fields[i]; i++) {
        char *field = g_strstrip(fields[i]);
        if (g_str_has_prefix(field, prefix) && field[strlen(prefix)]) {
            return g_strdup(field + strlen(prefix));
        }
    }
    return NULL;
}

static char *qmx_cli_get_id(const char *value)
{
    return qmx_cli_get_property(value, "id");
}

static char *qmx_cli_get_name(const char *value)
{
    return qmx_cli_get_property(value, "name");
}

static void qmx_add_override(GHashTable *overrides, const char *key)
{
    g_hash_table_add(overrides, g_strdup(key));
}

static bool qmx_scalar_cli_option(const char *opt, const char **key,
                                  bool *takes_arg)
{
    *key = NULL;
    *takes_arg = true;
    if (!strcmp(opt, "name")) {
        *key = "name";
    } else if (!strcmp(opt, "machine") || !strcmp(opt, "M")) {
        *key = "machine";
    } else if (!strcmp(opt, "m")) {
        *key = "memory";
    } else if (!strcmp(opt, "accel")) {
        *key = "accel";
    } else if (!strcmp(opt, "enable-kvm")) {
        *key = "accel";
        *takes_arg = false;
    } else if (!strcmp(opt, "cpu")) {
        *key = "cpu";
    } else if (!strcmp(opt, "display")) {
        *key = "display";
    } else if (!strcmp(opt, "nographic")) {
        *key = "display";
        *takes_arg = false;
    } else if (!strcmp(opt, "vga")) {
        *key = "vga";
    } else if (!strcmp(opt, "bios")) {
        *key = "bios";
    } else if (!strcmp(opt, "boot")) {
        *key = "boot";
    } else if (!strcmp(opt, "serial")) {
        *key = "serial";
    } else if (!strcmp(opt, "parallel")) {
        *key = "parallel";
    } else if (!strcmp(opt, "monitor")) {
        *key = "monitor";
    } else if (!strcmp(opt, "smp")) {
        *key = "smp";
    } else if (!strcmp(opt, "rtc")) {
        *key = "rtc";
    } else if (!strcmp(opt, "usb")) {
        *key = "usb";
        *takes_arg = false;
    }
    return *key != NULL;
}

static bool qmx_object_cli_option(const char *opt)
{
    return !strcmp(opt, "drive") || !strcmp(opt, "device") ||
           !strcmp(opt, "audiodev") || !strcmp(opt, "netdev") ||
           !strcmp(opt, "chardev") || !strcmp(opt, "object") ||
           !strcmp(opt, "fw_cfg");
}

static GHashTable *qmx_collect_cli_overrides(int argc, char **argv,
                                             int qmx_index)
{
    GHashTable *overrides = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  g_free, NULL);
    int i;

    for (i = 1; i < argc; i++) {
        const char *opt = argv[i];
        const char *arg = NULL;
        const char *key = NULL;
        bool takes_arg = false;

        if (i == qmx_index) {
            i++;
            continue;
        }
        if (opt[0] != '-') {
            continue;
        }
        while (*opt == '-') {
            opt++;
        }

        if (qmx_scalar_cli_option(opt, &key, &takes_arg)) {
            qmx_add_override(overrides, key);
            if (takes_arg && i + 1 < argc) {
                i++;
            }
            continue;
        }

        if (qmx_object_cli_option(opt)) {
            if (i + 1 >= argc) {
                continue;
            }
            arg = argv[++i];
            if (!strcmp(opt, "fw_cfg")) {
                g_autofree char *name = qmx_cli_get_name(arg);
                if (name) {
                    g_autofree char *override =
                        g_strdup_printf("@fw_cfg:%s", name);
                    qmx_add_override(overrides, override);
                }
            } else {
                g_autofree char *id = qmx_cli_get_id(arg);
                if (id) {
                    g_autofree char *override =
                        g_strdup_printf("%s.%s", opt, id);
                    qmx_add_override(overrides, override);
                }
            }
        }
    }
    return overrides;
}

static bool qmx_translate_nvram(const char *value, const char *qmx_dir,
                                bool emit, Error **errp)
{
    g_autoptr(GPtrArray) fields = qmx_split_fields(value, errp);
    g_autoptr(GHashTable) seen = NULL;
    g_autofree char *file = NULL;
    g_autofree char *format = NULL;
    g_autofree char *rtc_init = NULL;
    guint i;

    if (!fields) {
        return false;
    }
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (i = 0; i < fields->len; i++) {
        char *field = g_ptr_array_index(fields, i);
        char *eq = strchr(field, '=');
        g_autofree char *decoded = NULL;

        if (!eq) {
            error_setg(errp, "nvram requires named properties");
            return false;
        }
        *eq = '\0';
        g_strstrip(field);
        g_strstrip(eq + 1);
        if (!qmx_is_ident(field)) {
            error_setg(errp, "invalid nvram property '%s'", field);
            return false;
        }
        if (g_hash_table_contains(seen, field)) {
            error_setg(errp, "duplicate nvram property '%s'", field);
            return false;
        }
        g_hash_table_add(seen, g_strdup(field));
        decoded = qmx_unquote(eq + 1, errp);
        if (!decoded) {
            return false;
        }

        if (!strcmp(field, "file")) {
            if (!decoded[0]) {
                error_setg(errp, "nvram file must not be empty");
                return false;
            }
            file = qmx_resolve_path(qmx_dir, decoded);
        } else if (!strcmp(field, "format")) {
            format = g_strdup(decoded);
        } else if (!strcmp(field, "rtc_init")) {
            rtc_init = g_strdup(decoded);
        } else {
            error_setg(errp, "unsupported nvram property '%s'", field);
            return false;
        }
    }

    if (!file) {
        error_setg(errp, "nvram requires file=...");
        return false;
    }
    if (!format || strcmp(format, "cmos128")) {
        error_setg(errp, "nvram format must be 'cmos128'");
        return false;
    }
    if (!rtc_init) {
        rtc_init = g_strdup("time0");
    }
    if (strcmp(rtc_init, "time0") && strcmp(rtc_init, "image")) {
        error_setg(errp, "nvram rtc_init must be 'time0' or 'image'");
        return false;
    }

    if (emit) {
        g_free(qmx_nvram_file);
        g_free(qmx_nvram_rtc_init);
        qmx_nvram_file = g_strdup(file);
        qmx_nvram_rtc_init = g_strdup(rtc_init);
    }
    return true;
}

static bool qmx_drive_media_available(const char *id, const char *file)
{
    if (!file) {
        return true;
    }
    if (!g_file_test(file, G_FILE_TEST_EXISTS)) {
        warn_report("QMX drive '%s': media '%s' not found; omitting drive and dependent device(s)",
                    id, file);
        qmx_mark_drive_failed(id);
        return false;
    }
    if (g_access(file, R_OK) != 0) {
        warn_report("QMX drive '%s': media '%s' is not readable: %s; omitting drive and dependent device(s)",
                    id, file, strerror(errno));
        qmx_mark_drive_failed(id);
        return false;
    }
    return true;
}

static void qmx_filter_failed_devices(GPtrArray *args)
{
    guint i = 0;

    while (i < args->len) {
        const char *option = g_ptr_array_index(args, i);

        if (!strcmp(option, "-usb")) {
            i++;
            continue;
        }
        if (i + 1 >= args->len) {
            break;
        }
        if (!strcmp(option, "-device")) {
            const char *value = g_ptr_array_index(args, i + 1);
            g_autofree char *drive = qmx_cli_get_property(value, "drive");

            if (drive && qmx_drive_failed(drive)) {
                g_autofree char *id = qmx_cli_get_id(value);
                warn_report("QMX device '%s': drive '%s' is unavailable; omitting device",
                            id ? id : "<unnamed>", drive);
                g_ptr_array_remove_range(args, i, 2);
                continue;
            }
        }
        i += 2;
    }
}

static bool qmx_translate_assignment(GPtrArray *args, const char *key,
                                     const char *value, const char *qmx_dir,
                                     GHashTable *overrides,
                                     bool *version_seen, Error **errp)
{
    g_auto(GStrv) parts = g_strsplit(key, ".", 3);
    g_autofree char *scalar = NULL;
    g_autofree char *props = NULL;
    g_autofree char *file = NULL;
    g_autofree char *drive = NULL;
    g_autofree char *name = NULL;
    const char *family = parts[0];
    const char *id = parts[1];
    bool overridden = g_hash_table_contains(overrides, key);

    if (!strcmp(family, "qmx") && !id) {
        scalar = qmx_unquote(value, errp);
        if (!scalar) {
            return false;
        }
        if (strcmp(scalar, "1")) {
            error_setg(errp, "unsupported QMX major version '%s'", scalar);
            return false;
        }
        *version_seen = true;
        return true;
    }

    if (!id && !strcmp(family, "nvram")) {
        return qmx_translate_nvram(value, qmx_dir, !overridden, errp);
    }

    if (!id) {
        scalar = qmx_unquote(value, errp);
        if (!scalar) {
            return false;
        }

        if (!strcmp(family, "description")) {
            return true;
        } else if (!strcmp(family, "name")) {
            if (!overridden) {
                qmx_add_option(args, "-name", scalar);
            }
        } else if (!strcmp(family, "machine")) {
            if (!overridden) {
                qmx_add_option(args, "-machine", scalar);
            }
        } else if (!strcmp(family, "memory")) {
            if (!overridden) {
                qmx_add_option(args, "-m", scalar);
            }
        } else if (!strcmp(family, "accel")) {
            if (!overridden) {
                qmx_add_option(args, "-accel", scalar);
            }
        } else if (!strcmp(family, "cpu")) {
            if (!overridden) {
                qmx_add_option(args, "-cpu", scalar);
            }
        } else if (!strcmp(family, "display")) {
            if (!overridden) {
                qmx_add_option(args, "-display", scalar);
            }
        } else if (!strcmp(family, "vga")) {
            if (!overridden) {
                qmx_add_option(args, "-vga", scalar);
            }
        } else if (!strcmp(family, "bios")) {
            if (!overridden) {
                g_autofree char *resolved = qmx_resolve_path(qmx_dir, scalar);
                qmx_add_option(args, "-bios", resolved);
            }
        } else if (!strcmp(family, "boot")) {
            if (!overridden) {
                qmx_add_option(args, "-boot", scalar);
            }
        } else if (!strcmp(family, "serial") ||
                   !strcmp(family, "parallel") ||
                   !strcmp(family, "monitor")) {
            if (!overridden) {
                g_autofree char *rebased =
                    qmx_rebase_chardev_scalar(scalar, qmx_dir);
                g_autofree char *option = g_strdup_printf("-%s", family);
                qmx_add_option(args, option, rebased);
            }
        } else if (!strcmp(family, "smp")) {
            if (!overridden) {
                qmx_add_option(args, "-smp", scalar);
            }
        } else if (!strcmp(family, "rtc")) {
            if (!overridden) {
                qmx_add_option(args, "-rtc", scalar);
            }
        } else if (!strcmp(family, "usb")) {
            if (strcmp(scalar, "on") && strcmp(scalar, "off")) {
                error_setg(errp, "usb must be 'on' or 'off'");
                return false;
            }
            if (!overridden && !strcmp(scalar, "on")) {
                qmx_add_arg(args, "-usb");
            }
        } else {
            error_setg(errp, "unsupported QMX directive '%s'", key);
            return false;
        }
        return true;
    }

    if (parts[2]) {
        error_setg(errp, "QMX object keys have exactly one object id: '%s'", key);
        return false;
    }

    if (!strcmp(family, "audiodev") || !strcmp(family, "netdev") ||
        !strcmp(family, "chardev") || !strcmp(family, "object")) {
        const char *option = !strcmp(family, "audiodev") ? "-audiodev" :
                             !strcmp(family, "netdev") ? "-netdev" :
                             !strcmp(family, "chardev") ? "-chardev" : "-object";
        props = qmx_build_properties(value, family, id, true, qmx_dir,
                                     NULL, NULL, NULL, errp);
        if (props && !overridden) {
            qmx_add_option(args, option, props);
        }
    } else if (!strcmp(family, "device")) {
        props = qmx_build_properties(value, family, id, true, qmx_dir,
                                     NULL, &drive, NULL, errp);
        if (props && !overridden) {
            qmx_add_option(args, "-device", props);
            if (drive) {
                g_hash_table_replace(qmx_device_drives, g_strdup(id),
                                     g_strdup(drive));
            }
        }
    } else if (!strcmp(family, "drive")) {
        props = qmx_build_properties(value, family, id, true, qmx_dir,
                                     &file, NULL, NULL, errp);
        if (props && !overridden) {
            if (file) {
                g_hash_table_replace(qmx_drive_paths, g_strdup(id),
                                     g_strdup(file));
            }
            if (qmx_drive_media_available(id, file)) {
                qmx_add_option(args, "-drive", props);
            }
        }
    } else if (!strcmp(family, "fw_cfg")) {
        props = qmx_build_properties(value, family, id, false, qmx_dir,
                                     NULL, NULL, &name, errp);
        if (props) {
            bool name_overridden = false;
            if (name) {
                g_autofree char *override =
                    g_strdup_printf("@fw_cfg:%s", name);
                name_overridden = g_hash_table_contains(overrides, override);
            }
            if (!overridden && !name_overridden) {
                qmx_add_option(args, "-fw_cfg", props);
            }
        }
    } else {
        error_setg(errp, "unsupported QMX object family '%s'", family);
        return false;
    }

    return props != NULL;
}

static GPtrArray *qmx_parse_file(const char *filename, GHashTable *overrides,
                                 Error **errp)
{
    g_autofree char *absolute = g_canonicalize_filename(filename, NULL);
    g_autofree char *dir = g_path_get_dirname(absolute);
    g_autofree char *contents = NULL;
    g_auto(GStrv) lines = NULL;
    g_autoptr(GHashTable) seen = NULL;
    g_autoptr(GError) gerr = NULL;
    GPtrArray *args;
    gsize len;
    bool version_seen = false;
    int i;

    if (!g_file_get_contents(absolute, &contents, &len, &gerr)) {
        error_setg(errp, "cannot open QMX file '%s': %s",
                   absolute, gerr->message);
        return NULL;
    }

    args = g_ptr_array_new_with_free_func(g_free);
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    lines = g_strsplit(contents, "\n", -1);

    for (i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        char *eq;
        char *key;
        char *value;

        if (!line[0] || line[0] == '#') {
            continue;
        }

        eq = strchr(line, '=');
        if (!eq) {
            error_setg(errp, "%s:%d: missing '='; expected 'key = value'",
                       absolute, i + 1);
            goto fail;
        }
        *eq = '\0';
        key = g_strstrip(line);
        value = g_strstrip(eq + 1);

        if (!qmx_is_key(key)) {
            error_setg(errp, "%s:%d: invalid QMX key '%s'",
                       absolute, i + 1, key);
            goto fail;
        }
        if (!value[0]) {
            error_setg(errp, "%s:%d: empty value for '%s'",
                       absolute, i + 1, key);
            goto fail;
        }
        if (g_hash_table_contains(seen, key)) {
            error_setg(errp, "%s:%d: duplicate QMX assignment '%s'",
                       absolute, i + 1, key);
            goto fail;
        }
        g_hash_table_add(seen, g_strdup(key));

        if (!qmx_translate_assignment(args, key, value, dir, overrides,
                                      &version_seen, errp)) {
            error_prepend(errp, "%s:%d: ", absolute, i + 1);
            goto fail;
        }
    }

    if (!version_seen) {
        error_setg(errp, "%s: missing required 'qmx = 1' declaration",
                   absolute);
        goto fail;
    }

    qmx_filter_failed_devices(args);
    return args;

fail:
    g_ptr_array_free(args, true);
    return NULL;
}

static bool qmx_has_suffix(const char *arg)
{
    return g_str_has_suffix(arg, ".qmx");
}

bool qmx_expand_argv(int *argc, char ***argv, Error **errp)
{
    char **oldv = *argv;
    const char *qmx_file = NULL;
    int qmx_index = -1;
    int qmx_count = 0;
    bool explicit_qmx = false;
    g_autoptr(GPtrArray) qmx_args = NULL;
    g_autoptr(GHashTable) overrides = NULL;
    GPtrArray *newv;
    int i;
    guint j;

    qmx_check_mode = false;
    for (i = 1; i < *argc; i++) {
        if (!strcmp(oldv[i], "-qmx") || !strcmp(oldv[i], "-qmx-check")) {
            bool check = !strcmp(oldv[i], "-qmx-check");

            if (i + 1 >= *argc) {
                error_setg(errp, "%s requires a QMX filename", oldv[i]);
                return false;
            }
            qmx_file = oldv[i + 1];
            qmx_index = i;
            qmx_count++;
            explicit_qmx = true;
            qmx_check_mode = check;
            i++;
        }
    }

    if (!explicit_qmx && *argc == 2 && oldv[1][0] != '-' &&
        qmx_has_suffix(oldv[1])) {
        qmx_file = oldv[1];
        qmx_index = 1;
        qmx_count = 1;
    }

    if (!qmx_count) {
        return true;
    }
    if (qmx_count != 1) {
        error_setg(errp, "exactly one QMX file may be specified");
        return false;
    }

    qmx_runtime_reset();
    overrides = qmx_collect_cli_overrides(*argc, oldv, qmx_index);
    qmx_args = qmx_parse_file(qmx_file, overrides, errp);
    if (!qmx_args) {
        return false;
    }

    if (qmx_check_mode) {
        return true;
    }

    newv = g_ptr_array_new();
    qmx_add_arg(newv, oldv[0]);
    for (j = 0; j < qmx_args->len; j++) {
        qmx_add_arg(newv, g_ptr_array_index(qmx_args, j));
    }
    for (i = 1; i < *argc; i++) {
        if (i == qmx_index) {
            i++;
            continue;
        }
        qmx_add_arg(newv, oldv[i]);
    }
    g_ptr_array_add(newv, NULL);

    *argc = newv->len - 1;
    *argv = (char **)g_ptr_array_free(newv, false);
    return true;
}
