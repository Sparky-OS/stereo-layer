/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#define _POSIX_C_SOURCE 200809L
#include "stereo-content.h"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef STEREO_CONTENT_WORKER
#define STEREO_CONTENT_WORKER "/usr/libexec/stereo-content-worker"
#endif

static const char introspection[] =
    "<node><interface name='org.stereo.Content1'><method name='Refine'>"
    "<arg type='h' direction='in'/><arg type='s' direction='in'/><arg type='x' direction='in'/>"
    "<arg type='b' direction='in'/><arg type='s' direction='out'/></method></interface></node>";
static GHashTable *cache;
static GQueue order = G_QUEUE_INIT;
static unsigned running;
typedef struct Pending {
    GDBusMethodInvocation *invocation;
    GSubprocess *process;
    char *key, *base;
    struct stat identity;
    int fd;
    gint64 deadline;
    guint timeout;
} Pending;

static char *identity_key(int fd, const char *base, struct stat *st)
{
    if (fstat(fd, st) || !S_ISREG(st->st_mode))
        return NULL;
    return g_strdup_printf("1:%llu:%llu:%llu:%lld:%ld:%lld:%ld:%s",
        (unsigned long long)st->st_dev, (unsigned long long)st->st_ino, (unsigned long long)st->st_size,
        (long long)st->st_mtim.tv_sec, st->st_mtim.tv_nsec,
        (long long)st->st_ctim.tv_sec, st->st_ctim.tv_nsec, base);
}

static gboolean expire(gpointer data)
{
    Pending *p = data;
    p->timeout = 0;
    g_subprocess_force_exit(p->process);
    return G_SOURCE_REMOVE;
}

static void completed(GObject *source, GAsyncResult *async, gpointer data)
{
    Pending *p = data;
    GBytes *bytes = NULL;
    GError *error = NULL;
    const char *mime = p->base;
    StereoContentResult result;
    gboolean ok = g_subprocess_communicate_finish(G_SUBPROCESS(source), async, &bytes, NULL, &error);
    if (p->timeout)
        g_source_remove(p->timeout);
    if (ok && g_subprocess_get_successful(p->process) && g_get_monotonic_time() < p->deadline) {
        gsize size;
        const void *buffer = g_bytes_get_data(bytes, &size);
        if (size == sizeof(result)) {
            memcpy(&result, buffer, sizeof(result));
            struct stat current;
            char *key = identity_key(p->fd, p->base, &current);
            if (key && !strcmp(key, p->key) && result.abi == STEREO_CONTENT_ABI && result.size == sizeof(result)
                && result.status == STEREO_CONTENT_OK && memchr(result.refined_mime, 0, sizeof(result.refined_mime))) {
                mime = result.refined_mime;
                if (!g_hash_table_contains(cache, key)) {
                    while (g_queue_get_length(&order) >= 1024) {
                        char *old = g_queue_pop_head(&order);
                        g_hash_table_remove(cache, old);
                        g_free(old);
                    }
                    g_queue_push_tail(&order, g_strdup(key));
                }
                g_hash_table_replace(cache, g_strdup(key), g_strdup(mime));
            }
            g_free(key);
        }
    }
    g_dbus_method_invocation_return_value(p->invocation, g_variant_new("(s)", mime));
    g_clear_pointer(&bytes, g_bytes_unref);
    g_clear_error(&error);
    g_object_unref(p->invocation);
    g_object_unref(p->process);
    close(p->fd);
    g_free(p->key);
    g_free(p->base);
    g_free(p);
    --running;
}

static void method_call(GDBusConnection *connection, const gchar *sender, const gchar *object,
    const gchar *interface, const gchar *method, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer data)
{
    (void)connection; (void)sender; (void)object; (void)interface; (void)method; (void)data;
    gint handle;
    const char *base;
    gint64 deadline;
    gboolean cached_only;
    g_variant_get(parameters, "(h&sxb)", &handle, &base, &deadline, &cached_only);
    GUnixFDList *fds = g_dbus_message_get_unix_fd_list(g_dbus_method_invocation_get_message(invocation));
    int fd = fds ? g_unix_fd_list_get(fds, handle, NULL) : -1;
    struct stat st;
    char *key = fd < 0 ? NULL : identity_key(fd, base, &st);
    const char *cached = key ? g_hash_table_lookup(cache, key) : NULL;
    gint64 now = g_get_monotonic_time();
    deadline = MIN(deadline, now + 400000);
    if (!key || cached || cached_only || running >= 2 || deadline <= now || strlen(base) >= 128) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", cached ? cached : base));
        g_free(key);
        if (fd >= 0)
            close(fd);
        return;
    }
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_subprocess_launcher_take_fd(launcher, dup(fd), 3);
    char time[32];
    g_snprintf(time, sizeof(time), "%lld", (long long)deadline);
    GError *error = NULL;
    GSubprocess *process = g_subprocess_launcher_spawn(launcher, &error, STEREO_CONTENT_WORKER, base, time, NULL);
    g_object_unref(launcher);
    if (!process) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", base));
        g_clear_error(&error);
        g_free(key);
        close(fd);
        return;
    }
    Pending *p = g_new0(Pending, 1);
    p->invocation = g_object_ref(invocation);
    p->process = process;
    p->key = key;
    p->base = g_strdup(base);
    p->identity = st;
    p->fd = fd;
    p->deadline = deadline;
    ++running;
    p->timeout = g_timeout_add(MAX(1, (deadline - now) / 1000), expire, p);
    g_subprocess_communicate_async(process, NULL, NULL, completed, p);
}

static void acquired(GDBusConnection *connection, const gchar *name, gpointer data)
{
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(introspection, NULL);
    static const GDBusInterfaceVTable vtable = {.method_call = method_call};
    g_dbus_connection_register_object(connection, "/org/stereo/Content1", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

static void lost(GDBusConnection *connection, const gchar *name, gpointer data)
{
    (void)connection; (void)name;
    g_main_loop_quit(data);
}

int main(void)
{
    cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    guint owner = g_bus_own_name(G_BUS_TYPE_SESSION, "org.stereo.Content1", G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
        acquired, NULL, lost, loop, NULL);
    g_main_loop_run(loop);
    g_bus_unown_name(owner);
    g_main_loop_unref(loop);
    g_hash_table_unref(cache);
    g_queue_clear_full(&order, g_free);
    return 0;
}
