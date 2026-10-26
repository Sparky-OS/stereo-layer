/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <sys/stat.h>
#include <string.h>

int stereo_content_refine_fd(int fd, const char *base, unsigned timeout_ms, char *mime, unsigned capacity)
{
    struct stat before, after;
    if (!base || !mime || !capacity || fstat(fd, &before) || !S_ISREG(before.st_mode))
        return 0;
    g_strlcpy(mime, base, capacity);
    gint64 deadline = g_get_monotonic_time() + MIN(timeout_ms, 400) * 1000;
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!bus) {
        g_clear_error(&error);
        return 0;
    }
    GUnixFDList *fds = g_unix_fd_list_new();
    int index = g_unix_fd_list_append(fds, fd, &error);
    GVariant *reply = NULL;
    gint64 remaining = deadline - g_get_monotonic_time();
    if (index >= 0 && (remaining > 0 || timeout_ms == 0))
        reply = g_dbus_connection_call_with_unix_fd_list_sync(bus, "org.stereo.Content1", "/org/stereo/Content1",
            "org.stereo.Content1", "Refine", g_variant_new("(hsxb)", index, base, deadline, timeout_ms == 0),
            G_VARIANT_TYPE("(s)"), timeout_ms ? G_DBUS_CALL_FLAGS_NONE : G_DBUS_CALL_FLAGS_NO_AUTO_START,
            timeout_ms ? MAX(1, remaining / 1000) : 1, fds, NULL, NULL, &error);
    int changed = 0;
    if (reply && !fstat(fd, &after) && before.st_dev == after.st_dev && before.st_ino == after.st_ino
        && before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec
        && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec && before.st_ctim.tv_sec == after.st_ctim.tv_sec
        && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec) {
        const char *refined;
        g_variant_get(reply, "(&s)", &refined);
        if (strlen(refined) < capacity && (!timeout_ms || g_get_monotonic_time() < deadline)) {
            g_strlcpy(mime, refined, capacity);
            changed = strcmp(mime, base) != 0;
        }
    }
    g_clear_pointer(&reply, g_variant_unref);
    g_clear_error(&error);
    g_object_unref(fds);
    g_object_unref(bus);
    return changed;
}
