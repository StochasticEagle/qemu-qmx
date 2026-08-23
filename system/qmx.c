/*
 * QMX machine configuration support
 *
 * QMX is intentionally translated into QEMU's existing command-line option
 * machinery.  This keeps validation and object creation in the QEMU
 * subsystems that already own those semantics instead of duplicating them in
 * the QMX parser.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/qmx.h"

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
        if (s[i] != '\\') {
            g_string_append_c(out, s[i]);
            continue;
        }
        i++;
        if (i + 1 > len) {
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

    if (quoted) {
        error_setg(errp, "unterminated quoted string");
        g_ptr_array_free(fields, true);
        return NULL;
    }
    return fields;
}

static char *qmx_escape_qemu_value(const char *value)
{
    GString *out = g_string_new(NULL);
    const char *p;

    /* QemuOpts uses doubled commas for a literal comma in a value. */
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

static char *qmx_build_properties(const char *value, const char *id,
                                  bool inject_id, bool resolve_file,
                                  const char *qmx_dir, Error **errp)
{
    g_autoptr(GPtrArray) fields = qmx_split_fields(value, errp);
    GString *out;
    bool have_id = false;
    guint i;

    if (!fields) {
        return NULL;
    }

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
        decoded = qmx_unquote(eq + 1, errp);
        if (!decoded) {
            goto fail;
        }
        if (!strcmp(field, "id")) {
            have_id = true;
        }
        if (resolve_file && !strcmp(field, "file")) {
            g_autofree char *resolved = qmx_resolve_path(qmx_dir, decoded);
            g_free(g_steal_pointer(&decoded));
            decoded = g_strdup(resolved);
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

static bool qmx_translate_assignment(GPtrArray *args, const char *key,
                                     const char *value, const char *qmx_dir,
                                     bool *version_seen, Error **errp)
{
    g_auto(GStrv) parts = g_strsplit(key, ".", 3);
    g_autofree char *scalar = NULL;
    g_autofree char *props = NULL;
    const char *family = parts[0];
    const char *id = parts[1];

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

    if (!id) {
        scalar = qmx_unquote(value, errp);
        if (!scalar) {
            return false;
        }
        if (!strcmp(family, "name")) {
            qmx_add_option(args, "-name", scalar);
        } else if (!strcmp(family, "machine")) {
            qmx_add_option(args, "-machine", scalar);
        } else if (!strcmp(family, "memory")) {
            qmx_add_option(args, "-m", scalar);
        } else if (!strcmp(family, "accel")) {
            qmx_add_option(args, "-accel", scalar);
        } else if (!strcmp(family, "cpu")) {
            qmx_add_option(args, "-cpu", scalar);
        } else if (!strcmp(family, "display")) {
            qmx_add_option(args, "-display", scalar);
        } else if (!strcmp(family, "vga")) {
            qmx_add_option(args, "-vga", scalar);
        } else if (!strcmp(family, "boot")) {
            qmx_add_option(args, "-boot", scalar);
        } else if (!strcmp(family, "nvram")) {
            /*
             * The text grammar is implemented now; the cmos128 backend is a
             * separate RTC persistence change.  Keep the development example
             * runnable, but make the missing persistence impossible to miss.
             */
            fprintf(stderr,
                    "qemu: warning: QMX nvram persistence is not implemented "
                    "yet; using volatile CMOS\n");
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

    if (!strcmp(family, "audiodev")) {
        props = qmx_build_properties(value, id, true, false, qmx_dir, errp);
        if (props) {
            qmx_add_option(args, "-audiodev", props);
        }
    } else if (!strcmp(family, "device")) {
        props = qmx_build_properties(value, id, true, false, qmx_dir, errp);
        if (props) {
            qmx_add_option(args, "-device", props);
        }
    } else if (!strcmp(family, "drive")) {
        props = qmx_build_properties(value, id, true, true, qmx_dir, errp);
        if (props) {
            qmx_add_option(args, "-drive", props);
        }
    } else if (!strcmp(family, "fw_cfg")) {
        props = qmx_build_properties(value, id, false, false, qmx_dir, errp);
        if (props) {
            qmx_add_option(args, "-fw_cfg", props);
        }
    } else {
        error_setg(errp, "unsupported QMX object family '%s'", family);
        return false;
    }

    return props != NULL;
}

static GPtrArray *qmx_parse_file(const char *filename, Error **errp)
{
    g_autofree char *absolute = g_canonicalize_filename(filename, NULL);
    g_autofree char *dir = g_path_get_dirname(absolute);
    g_autofree char *contents = NULL;
    g_auto(GStrv) lines = NULL;
    g_autoptr(GHashTable) seen = NULL;
    GPtrArray *args;
    gsize len;
    bool version_seen = false;
    int i;

    if (!g_file_get_contents(absolute, &contents, &len, NULL)) {
        error_setg(errp, "cannot open QMX file '%s': %s",
                   absolute, g_strerror(errno));
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
            error_setg(errp, "%s:%d: expected 'key = value'", absolute, i + 1);
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

        if (!qmx_translate_assignment(args, key, value, dir,
                                      &version_seen, errp)) {
            error_prepend(errp, "%s:%d: ", absolute, i + 1);
            goto fail;
        }
    }

    if (!version_seen) {
        error_setg(errp, "%s: missing required 'qmx = 1' declaration", absolute);
        goto fail;
    }
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
    GPtrArray *newv;
    int i;

    for (i = 1; i < *argc; i++) {
        if (!strcmp(oldv[i], "-qmx")) {
            if (i + 1 >= *argc) {
                error_setg(errp, "-qmx requires a QMX filename");
                return false;
            }
            qmx_file = oldv[i + 1];
            qmx_index = i;
            qmx_count++;
            explicit_qmx = true;
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

    qmx_args = qmx_parse_file(qmx_file, errp);
    if (!qmx_args) {
        return false;
    }

    newv = g_ptr_array_new();
    qmx_add_arg(newv, oldv[0]);
    for (i = 1; i < *argc; i++) {
        guint j;

        if (i == qmx_index) {
            for (j = 0; j < qmx_args->len; j++) {
                qmx_add_arg(newv, g_ptr_array_index(qmx_args, j));
            }
            if (explicit_qmx) {
                i++;
            }
            continue;
        }
        qmx_add_arg(newv, oldv[i]);
    }
    g_ptr_array_add(newv, NULL);

    *argc = newv->len - 1;
    *argv = (char **)g_ptr_array_free(newv, false);
    return true;
}
