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
#include "qemu/cutils.h"
#include "qemu/datadir.h"
#include "qemu/qmx.h"

typedef struct QmxQemuOption {
    const char *name;
    bool has_arg;
} QmxQemuOption;

/*
 * Use QEMU's generated option definitions directly.  This is deliberately
 * not a QMX option list: when QEMU gains an option, QMX learns its spelling,
 * and arity from the same source as the command line.
 */
#define HAS_ARG 1
#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask) \
    { option, (opt_arg) != 0 },
#define DEFHEADING(text)
#define ARCHHEADING(text, arch_mask)
static const QmxQemuOption qmx_qemu_options[] = {
#include "qemu-options.def"
    { NULL, false },
};
#undef ARCHHEADING
#undef DEFHEADING
#undef DEF
#undef HAS_ARG

static GHashTable *qmx_drive_paths;
static GHashTable *qmx_device_drives;
static GHashTable *qmx_failed_drives;
static char *qmx_nvram_file;
static char *qmx_nvram_rtc_init;
static bool qmx_check_mode;
static QmxNvramBackendInit qmx_nvram_backend;

typedef struct QmxManagedProcess {
    GPid pid;
    char *socket_path;
} QmxManagedProcess;

static GPtrArray *qmx_managed_processes;
static bool qmx_cleanup_registered;

static void qmx_managed_process_free(gpointer opaque)
{
    QmxManagedProcess *process = opaque;

    g_free(process->socket_path);
    g_free(process);
}

static void qmx_stop_managed_process(QmxManagedProcess *process)
{
    GPid pid = process->pid;

    if (!process->pid) {
        return;
    }

#ifdef _WIN32
    TerminateProcess(process->pid, 0);
    WaitForSingleObject(process->pid, 1000);
#else
    int status;
    unsigned int i;

    if (kill(process->pid, SIGTERM) == 0 || errno == EPERM) {
        for (i = 0; i < 100; i++) {
            if (waitpid(process->pid, &status, WNOHANG) == process->pid) {
                process->pid = 0;
                break;
            }
            g_usleep(10000);
        }
    }
    if (process->pid) {
        kill(process->pid, SIGKILL);
        while (waitpid(process->pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
#endif

    g_spawn_close_pid(pid);
    process->pid = 0;
    if (process->socket_path) {
        g_unlink(process->socket_path);
    }
}

static void qmx_stop_managed_processes(void)
{
    guint i;

    if (!qmx_managed_processes) {
        return;
    }
    for (i = 0; i < qmx_managed_processes->len; i++) {
        qmx_stop_managed_process(g_ptr_array_index(qmx_managed_processes, i));
    }
    g_clear_pointer(&qmx_managed_processes, g_ptr_array_unref);
}

static void qmx_runtime_reset(void)
{
    qmx_stop_managed_processes();
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

static const QmxQemuOption *qmx_find_qemu_option(const char *name)
{
    const QmxQemuOption *option;

    for (option = qmx_qemu_options; option->name; option++) {
        if (!strcmp(option->name, name)) {
            return option;
        }
    }
    return NULL;
}

static const char *qmx_qemu_option_name(const char *family)
{
    /* QMX uses the descriptive name instead of QEMU's abbreviated -m. */
    return !strcmp(family, "memory") ? "m" : family;
}

static bool qmx_family_injects_id(const char *family)
{
    return !strcmp(family, "audiodev") || !strcmp(family, "chardev") ||
           !strcmp(family, "device") || !strcmp(family, "drive") ||
           !strcmp(family, "fsdev") || !strcmp(family, "netdev") ||
           !strcmp(family, "object") || !strcmp(family, "tpmdev");
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

static bool qmx_field_equals(GPtrArray *fields, const char *property,
                             const char *expected)
{
    guint i;

    for (i = 0; i < fields->len; i++) {
        const char *field = g_ptr_array_index(fields, i);
        const char *eq = strchr(field, '=');
        g_autofree char *name = NULL;
        g_autofree char *decoded = NULL;
        Error *local_err = NULL;

        if (!eq) {
            continue;
        }
        name = g_strndup(field, eq - field);
        g_strstrip(name);
        if (strcmp(name, property)) {
            continue;
        }
        decoded = qmx_unquote(eq + 1, &local_err);
        if (local_err) {
            error_free(local_err);
            return false;
        }
        return !strcmp(decoded, expected);
    }
    return false;
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

static char *qmx_find_system_firmware(const char *name)
{
    static const char * const system_firmware_dirs[] = {
        "/usr/share/edk2/x64",
        "/usr/share/edk2/ovmf",
        "/usr/share/OVMF",
        "/usr/share/qemu",
        NULL,
    };
    char *resolved = qemu_find_file_in_data_dirs(QEMU_FILE_TYPE_BIOS, name);
    size_t i;

    if (resolved) {
        return resolved;
    }

    for (i = 0; system_firmware_dirs[i]; i++) {
        resolved = g_build_filename(system_firmware_dirs[i], name, NULL);
        if (g_access(resolved, R_OK) == 0) {
            return resolved;
        }
        g_free(resolved);
    }
    return NULL;
}

static char *qmx_resolve_drive_file(const char *value, const char *qmx_dir,
                                    bool system_firmware, Error **errp)
{
    static const char fat_rw_prefix[] = "fat:rw:";
    static const char fat_prefix[] = "fat:";
    const char *prefix = NULL;
    char *resolved;

    if (g_str_has_prefix(value, fat_rw_prefix)) {
        prefix = fat_rw_prefix;
    } else if (g_str_has_prefix(value, fat_prefix)) {
        prefix = fat_prefix;
    }
    if (prefix) {
        g_autofree char *fat_path =
            qmx_resolve_path(qmx_dir, value + strlen(prefix));

        return g_strdup_printf("%s%s", prefix, fat_path);
    }

    if (!system_firmware) {
        return qmx_resolve_path(qmx_dir, value);
    }
    if (g_path_is_absolute(value)) {
        return g_canonicalize_filename(value, NULL);
    }
    if (g_str_has_prefix(value, "./")) {
        return g_canonicalize_filename(value, qmx_dir);
    }

    resolved = qmx_find_system_firmware(value);

    if (!resolved) {
        error_setg(errp,
                   "system firmware '%s' was not found in QEMU's firmware search path",
                   value);
    }
    return resolved;
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
                                  char **template_out,
                                  Error **errp)
{
    g_autoptr(GPtrArray) fields = qmx_split_fields(value, errp);
    g_autoptr(GHashTable) seen = NULL;
    GString *out;
    bool have_id = false;
    bool have_bare = false;
    bool pflash;
    bool system_firmware;
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
    if (template_out) {
        *template_out = NULL;
    }
    if (!fields) {
        return NULL;
    }

    pflash = !strcmp(family, "drive") &&
             qmx_field_equals(fields, "if", "pflash");
    system_firmware = pflash &&
                      qmx_field_equals(fields, "readonly", "on");

    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    out = g_string_new(NULL);
    for (i = 0; i < fields->len; i++) {
        char *field = g_ptr_array_index(fields, i);
        char *eq = strchr(field, '=');
        g_autofree char *decoded = NULL;
        g_autofree char *escaped = NULL;

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
            if (out->len) {
                g_string_append_c(out, ',');
            }
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
        if (!strcmp(field, "template")) {
            if (!template_out || !pflash || system_firmware) {
                error_setg(errp,
                           "template is valid only for writable pflash drives");
                goto fail;
            }
            *template_out = g_strdup(decoded);
            continue;
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
        if (qmx_property_is_path(family, field) ||
            g_str_has_prefix(decoded, "./")) {
            g_autofree char *resolved = NULL;

            if (!strcmp(family, "drive") && !strcmp(field, "file")) {
                resolved = qmx_resolve_drive_file(decoded, qmx_dir,
                                                  system_firmware, errp);
            } else {
                resolved = qmx_resolve_path(qmx_dir, decoded);
            }
            if (!resolved) {
                goto fail;
            }
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
        if (out->len) {
            g_string_append_c(out, ',');
        }
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
    if (template_out) {
        g_clear_pointer(template_out, g_free);
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

static bool qmx_cli_backend_is(const char *value, const char *backend)
{
    size_t len = strlen(backend);

    return !strncmp(value, backend, len) &&
           (value[len] == '\0' || value[len] == ',');
}

static bool qmx_copy_file_once(const char *source, const char *destination,
                               Error **errp)
{
    g_autofree char *directory = g_path_get_dirname(destination);
    uint8_t buffer[64 * 1024];
    int src = -1;
    int dst = -1;
    bool created = false;
    bool ok = false;

    if (g_file_test(destination, G_FILE_TEST_EXISTS)) {
        return true;
    }
    if (g_mkdir_with_parents(directory, 0700) < 0) {
        error_setg_errno(errp, errno,
                         "cannot create firmware state directory '%s'",
                         directory);
        return false;
    }

    src = qemu_open_old(source, O_RDONLY | O_BINARY);
    if (src < 0) {
        error_setg_errno(errp, errno,
                         "cannot open firmware variables template '%s'",
                         source);
        goto out;
    }
    dst = qemu_open_old(destination,
                        O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
    if (dst < 0) {
        if (errno == EEXIST) {
            ok = true;
            goto out;
        }
        error_setg_errno(errp, errno,
                         "cannot create firmware variables file '%s'",
                         destination);
        goto out;
    }
    created = true;

    while (true) {
        ssize_t count = read(src, buffer, sizeof(buffer));

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            error_setg_errno(errp, errno,
                             "cannot read firmware variables template '%s'",
                             source);
            goto out;
        }
        if (!count) {
            ok = true;
            break;
        }
        if (qemu_write_full(dst, buffer, count) != count) {
            error_setg_errno(errp, errno,
                             "cannot write firmware variables file '%s'",
                             destination);
            goto out;
        }
    }

out:
    if (src >= 0) {
        close(src);
    }
    if (dst >= 0 && close(dst) < 0 && ok) {
        error_setg_errno(errp, errno,
                         "cannot close firmware variables file '%s'",
                         destination);
        ok = false;
    }
    if (!ok && created) {
        g_unlink(destination);
    }
    return ok;
}

static bool qmx_managed_process_running(QmxManagedProcess *process)
{
#ifdef _WIN32
    DWORD code;

    return GetExitCodeProcess(process->pid, &code) && code == STILL_ACTIVE;
#else
    int status;
    pid_t result = waitpid(process->pid, &status, WNOHANG);

    if (result == 0) {
        return true;
    }
    if (result == process->pid) {
        process->pid = 0;
    }
    return false;
#endif
}

static bool qmx_start_swtpm(const char *socket_path, Error **errp)
{
    g_autofree char *state_dir = g_path_get_dirname(socket_path);
    g_autofree char *state_arg = g_strdup_printf("dir=%s", state_dir);
    g_autofree char *ctrl_arg =
        g_strdup_printf("type=unixio,path=%s", socket_path);
    char *child_argv[] = {
        (char *)"swtpm", (char *)"socket", (char *)"--tpm2",
        (char *)"--tpmstate", state_arg,
        (char *)"--ctrl", ctrl_arg,
        NULL,
    };
    g_autoptr(GError) gerr = NULL;
    QmxManagedProcess *process;
    unsigned int i;

    /* An existing socket belongs to an explicitly managed external swtpm. */
    if (g_file_test(socket_path, G_FILE_TEST_EXISTS)) {
        return true;
    }
    if (g_mkdir_with_parents(state_dir, 0700) < 0) {
        error_setg_errno(errp, errno,
                         "cannot create software TPM state directory '%s'",
                         state_dir);
        return false;
    }

    process = g_new0(QmxManagedProcess, 1);
    process->socket_path = g_strdup(socket_path);
    if (!g_spawn_async(NULL, child_argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                       NULL, NULL, &process->pid, &gerr)) {
        error_setg(errp, "cannot start software TPM: %s", gerr->message);
        qmx_managed_process_free(process);
        return false;
    }

    if (!qmx_managed_processes) {
        qmx_managed_processes =
            g_ptr_array_new_with_free_func(qmx_managed_process_free);
    }
    g_ptr_array_add(qmx_managed_processes, process);
    if (!qmx_cleanup_registered) {
        atexit(qmx_stop_managed_processes);
        qmx_cleanup_registered = true;
    }

    for (i = 0; i < 500; i++) {
        if (g_file_test(socket_path, G_FILE_TEST_EXISTS)) {
            return true;
        }
        if (!qmx_managed_process_running(process)) {
            error_setg(errp,
                       "software TPM exited before creating socket '%s'",
                       socket_path);
            return false;
        }
        g_usleep(10000);
    }

    error_setg(errp, "software TPM did not create socket '%s' within 5 seconds",
               socket_path);
    return false;
}

static bool qmx_start_managed_tpm_helpers(GPtrArray *args, Error **errp)
{
    g_autoptr(GHashTable) sockets =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    guint i;

    for (i = 0; i + 1 < args->len; ) {
        const char *option = g_ptr_array_index(args, i);
        const QmxQemuOption *definition =
            option[0] == '-' ? qmx_find_qemu_option(option + 1) : NULL;

        if (!definition || !definition->has_arg) {
            i++;
            continue;
        }
        if (!strcmp(option, "-chardev")) {
            const char *value = g_ptr_array_index(args, i + 1);
            g_autofree char *id = qmx_cli_get_property(value, "id");
            g_autofree char *path = qmx_cli_get_property(value, "path");
            g_autofree char *server = qmx_cli_get_property(value, "server");

            if (qmx_cli_backend_is(value, "socket") && id && path &&
                (!server || strcmp(server, "on"))) {
                g_hash_table_replace(sockets, g_strdup(id), g_strdup(path));
            }
        }
        i += 2;
    }

    for (i = 0; i + 1 < args->len; ) {
        const char *option = g_ptr_array_index(args, i);
        const QmxQemuOption *definition =
            option[0] == '-' ? qmx_find_qemu_option(option + 1) : NULL;

        if (!definition || !definition->has_arg) {
            i++;
            continue;
        }
        if (!strcmp(option, "-tpmdev")) {
            const char *value = g_ptr_array_index(args, i + 1);
            g_autofree char *chardev =
                qmx_cli_get_property(value, "chardev");
            const char *socket_path = chardev ?
                g_hash_table_lookup(sockets, chardev) : NULL;

            if (qmx_cli_backend_is(value, "emulator") && socket_path &&
                !qmx_start_swtpm(socket_path, errp)) {
                return false;
            }
        }
        i += 2;
    }
    return true;
}

static void qmx_add_override(GHashTable *overrides, const char *key)
{
    g_hash_table_add(overrides, g_strdup(key));
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
        const QmxQemuOption *definition;

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

        definition = qmx_find_qemu_option(opt);
        if (!definition) {
            continue;
        }
        if (!definition->has_arg) {
            continue;
        }
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
        } else if (qmx_family_injects_id(opt)) {
            g_autofree char *id = qmx_cli_get_id(arg);
            if (id) {
                g_autofree char *override =
                    g_strdup_printf("%s.%s", opt, id);
                qmx_add_override(overrides, override);
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
    static const char fat_rw_prefix[] = "fat:rw:";
    static const char fat_prefix[] = "fat:";
    const char *path = file;

    if (!file) {
        return true;
    }
    if (g_str_has_prefix(path, fat_rw_prefix)) {
        path += strlen(fat_rw_prefix);
    } else if (g_str_has_prefix(path, fat_prefix)) {
        path += strlen(fat_prefix);
    }
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        warn_report("QMX drive '%s': media '%s' not found; "
                    "omitting drive and dependent device(s)", id, path);
        qmx_mark_drive_failed(id);
        return false;
    }
    if (g_access(path, R_OK) != 0) {
        warn_report("QMX drive '%s': media '%s' is not readable: %s; "
                    "omitting drive and dependent device(s)",
                    id, path, strerror(errno));
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
        const QmxQemuOption *definition =
            option[0] == '-' ? qmx_find_qemu_option(option + 1) : NULL;

        if (definition && !definition->has_arg) {
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
                warn_report("QMX device '%s': drive '%s' is unavailable; "
                            "omitting device", id ? id : "<unnamed>", drive);
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
    g_auto(GStrv) parts = g_strsplit(key, ".", 2);
    g_autofree char *scalar = NULL;
    g_autofree char *props = NULL;
    g_autofree char *file = NULL;
    g_autofree char *drive = NULL;
    g_autofree char *name = NULL;
    g_autofree char *template = NULL;
    g_autofree char *option = NULL;
    const char *family = parts[0];
    const char *id = parts[1];
    const char *qemu_name = qmx_qemu_option_name(family);
    const QmxQemuOption *definition;
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

    if (!strcmp(family, "m")) {
        error_setg(errp,
                   "QMX uses 'memory', not the command-line abbreviation 'm'");
        return false;
    }

    if (!id && !strcmp(family, "description")) {
        scalar = qmx_unquote(value, errp);
        return scalar != NULL;
    }
    if (!id && !strcmp(family, "nvram")) {
        return qmx_translate_nvram(value, qmx_dir, !overridden, errp);
    }

    if (id && strchr(id, '.')) {
        error_setg(errp,
                   "QMX parameter keys have at most one occurrence identifier: '%s'",
                   key);
        return false;
    }

    definition = qmx_find_qemu_option(qemu_name);
    if (!definition) {
        error_setg(errp, "QEMU parameter '%s' is not supported by this build",
                   family);
        return false;
    }
    option = g_strdup_printf("-%s", qemu_name);

    if (!definition->has_arg) {
        if (id) {
            error_setg(errp,
                       "argumentless QEMU parameter '%s' cannot have an occurrence identifier",
                       family);
            return false;
        }
        scalar = qmx_unquote(value, errp);
        if (!scalar) {
            return false;
        }
        if (strcmp(scalar, "on") && strcmp(scalar, "off")) {
            error_setg(errp, "%s must be 'on' or 'off'", family);
            return false;
        }
        if (!overridden && !strcmp(scalar, "on")) {
            qmx_add_arg(args, option);
        }
        return true;
    }

    if (!id) {
        if (strchr(value, '"') && value[strspn(value, " \t")] != '"') {
            scalar = qmx_build_properties(value, family, NULL, false, qmx_dir,
                                          NULL, NULL, NULL, NULL, errp);
        } else {
            scalar = qmx_unquote(value, errp);
        }
        if (!scalar) {
            return false;
        }
        if (g_str_has_prefix(scalar, "./")) {
            g_autofree char *resolved = qmx_resolve_path(qmx_dir, scalar);

            g_free(g_steal_pointer(&scalar));
            scalar = g_strdup(resolved);
        } else if (!strcmp(family, "serial") ||
                   !strcmp(family, "parallel") ||
                   !strcmp(family, "monitor")) {
            g_autofree char *rebased =
                qmx_rebase_chardev_scalar(scalar, qmx_dir);

            g_free(g_steal_pointer(&scalar));
            scalar = g_strdup(rebased);
        }
        if (!overridden) {
            qmx_add_option(args, option, scalar);
        }
        if (!strcmp(family, "L") && !overridden) {
            qemu_add_data_dir(g_strdup(scalar));
        }
        return true;
    }

    props = qmx_build_properties(value, family, id,
                                 qmx_family_injects_id(family), qmx_dir,
                                 !strcmp(family, "drive") ? &file : NULL,
                                 !strcmp(family, "device") ? &drive : NULL,
                                 !strcmp(family, "fw_cfg") ? &name : NULL,
                                 !strcmp(family, "drive") ? &template : NULL,
                                 errp);
    if (!props) {
        return false;
    }

    if (!strcmp(family, "fw_cfg") && name) {
        g_autofree char *override = g_strdup_printf("@fw_cfg:%s", name);

        overridden |= g_hash_table_contains(overrides, override);
    }
    if (overridden) {
        return true;
    }

    if (!strcmp(family, "device") && drive) {
        g_hash_table_replace(qmx_device_drives, g_strdup(id),
                             g_strdup(drive));
    }
    if (!strcmp(family, "drive")) {
        g_autofree char *interface = qmx_cli_get_property(props, "if");
        g_autofree char *readonly = qmx_cli_get_property(props, "readonly");
        bool system_firmware = interface && readonly &&
                               !strcmp(interface, "pflash") &&
                               !strcmp(readonly, "on");
        bool template_will_create = false;

        if (file) {
            g_hash_table_replace(qmx_drive_paths, g_strdup(id),
                                 g_strdup(file));
        }
        if (template && !file) {
            error_setg(errp, "QMX pflash template requires file=...");
            return false;
        }
        if (template && !g_file_test(file, G_FILE_TEST_EXISTS)) {
            g_autofree char *resolved_template =
                qmx_resolve_drive_file(template, qmx_dir, true, errp);

            if (!resolved_template) {
                return false;
            }
            if (g_access(resolved_template, R_OK) != 0) {
                error_setg_errno(errp, errno,
                                 "QMX firmware variables template '%s' is not readable",
                                 resolved_template);
                return false;
            }
            template_will_create = true;
            if (!qmx_check_mode &&
                !qmx_copy_file_once(resolved_template, file, errp)) {
                return false;
            }
        }
        if (system_firmware &&
            (!file || !g_file_test(file, G_FILE_TEST_EXISTS) ||
             g_access(file, R_OK) != 0)) {
            error_setg(errp, "QMX system firmware '%s' is unavailable",
                       file ? file : "<unspecified>");
            return false;
        }
        if (!system_firmware && !template_will_create &&
            !qmx_drive_media_available(id, file)) {
            return true;
        }
    }

    qmx_add_option(args, option, props);
    return true;
}

static bool qmx_prepare_declared_data_dirs(char **lines, const char *qmx_dir,
                                           Error **errp)
{
    int i;

    for (i = 0; lines[i]; i++) {
        g_autofree char *copy = g_strdup(lines[i]);
        char *line = g_strstrip(copy);
        char *eq;
        char *key;
        char *value;
        g_autofree char *path = NULL;

        if (!line[0] || line[0] == '#') {
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        key = g_strstrip(line);
        if (strcmp(key, "L") && !g_str_has_prefix(key, "L.")) {
            continue;
        }
        value = g_strstrip(eq + 1);
        path = qmx_unquote(value, errp);
        if (!path) {
            error_prepend(errp, "line %d: ", i + 1);
            return false;
        }
        if (g_str_has_prefix(path, "./")) {
            g_autofree char *resolved = qmx_resolve_path(qmx_dir, path);

            g_free(g_steal_pointer(&path));
            path = g_strdup(resolved);
        }
        qemu_add_data_dir(g_strdup(path));
    }
    return true;
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

    if (!qmx_prepare_declared_data_dirs(lines, dir, errp)) {
        error_prepend(errp, "%s: ", absolute);
        goto fail;
    }

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

static void qmx_prepare_firmware_paths(int argc, char **argv)
{
    int i;

    qemu_init_exec_dir(argv[0]);
    qemu_add_default_firmwarepath();
    for (i = 1; i < argc; i++) {
        const char *option = argv[i];
        const QmxQemuOption *definition;

        if (!strcmp(option, "-qmx") || !strcmp(option, "-qmx-check")) {
            i++;
            continue;
        }
        if (option[0] != '-') {
            continue;
        }
        while (*option == '-') {
            option++;
        }
        definition = qmx_find_qemu_option(option);
        if (!definition || !definition->has_arg || i + 1 >= argc) {
            continue;
        }
        if (!strcmp(option, "L")) {
            qemu_add_data_dir(g_strdup(argv[i + 1]));
        }
        i++;
    }
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

    qmx_prepare_firmware_paths(*argc, oldv);
    qmx_runtime_reset();
    overrides = qmx_collect_cli_overrides(*argc, oldv, qmx_index);
    qmx_args = qmx_parse_file(qmx_file, overrides, errp);
    if (!qmx_args) {
        return false;
    }

    if (qmx_check_mode) {
        return true;
    }

    if (!qmx_start_managed_tpm_helpers(qmx_args, errp)) {
        qmx_stop_managed_processes();
        return false;
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
