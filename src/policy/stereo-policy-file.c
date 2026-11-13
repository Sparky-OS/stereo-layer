/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The person's per-file viewing choice, kept in the programs' own state and never in the file (stereo-policy.h). */

#define _GNU_SOURCE
#include "stereo-policy.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEADER "stereo-policy-file-choice 1\n"

static uint64_t fnv1a(const char *text, uint64_t hash)
{
    for (const unsigned char *c = (const unsigned char *)text; *c; ++c) {
        hash ^= *c;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/* The file as the store names it: its real path when it resolves, else the path as given, made absolute. */
static int key_path(const char *path, char *key, size_t size)
{
    char resolved[PATH_MAX];
    if (!path || !path[0] || strchr(path, '\n')) {
        errno = EINVAL;
        return -1;
    }
    if (realpath(path, resolved)) {
        path = resolved;
    } else if (path[0] != '/') {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd)))
            return -1;
        if ((size_t)snprintf(key, size, "%s/%s", cwd, path) >= size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        return 0;
    }
    if (strlen(path) >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(key, path);
    return 0;
}

static int store_directory(char *directory, size_t size)
{
    const char *state = getenv("XDG_STATE_HOME");
    int length;
    if (state && state[0] == '/') {
        length = snprintf(directory, size, "%s/stereo-policy/files", state);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] != '/') {
            errno = ENOENT;
            return -1;
        }
        length = snprintf(directory, size, "%s/.local/state/stereo-policy/files", home);
    }
    if (length < 0 || (size_t)length >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int entry_path(const char *key, char *entry, size_t size)
{
    char directory[PATH_MAX];
    if (store_directory(directory, sizeof(directory)) < 0)
        return -1;
    const int length = snprintf(entry, size, "%s/%016llx%016llx", directory, (unsigned long long)fnv1a(key, 0xcbf29ce484222325ULL),
        (unsigned long long)fnv1a(key, 0x84222325cbf29ce4ULL));
    if (length < 0 || (size_t)length >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int make_directories(char *path)
{
    for (char *slash = path + 1; *slash; ++slash) {
        if (*slash != '/')
            continue;
        *slash = 0;
        const int made = mkdir(path, 0700);
        *slash = '/';
        if (made < 0 && errno != EEXIST)
            return -1;
    }
    return mkdir(path, 0700) < 0 && errno != EEXIST ? -1 : 0;
}

static int in_range(int value, int low, int high) { return value >= low && value <= high; }

static int valid(const struct stereo_policy_file_choice *choice)
{
    return in_range(choice->layout, STEREO_POLICY_LAYOUT_NONE, STEREO_POLICY_LAYOUT_DEPTH_SEPARATE) && in_range(choice->order, 0, 1)
        && in_range(choice->anaglyph, STEREO_POLICY_ANAGLYPH_NONE, STEREO_POLICY_ANAGLYPH_AMBER_BLUE)
        && in_range(choice->view, STEREO_POLICY_VIEW_STEREO, STEREO_POLICY_VIEW_MONO) && in_range(choice->swap_eyes, 0, 1);
}

static int is_default(const struct stereo_policy_file_choice *choice)
{
    return choice->layout == STEREO_POLICY_LAYOUT_NONE && choice->anaglyph == STEREO_POLICY_ANAGLYPH_NONE && choice->view == STEREO_POLICY_VIEW_STEREO
        && !choice->swap_eyes;
}

int stereo_policy_file_choice_entry(const char *path, char *entry, size_t size)
{
    char key[PATH_MAX];
    if (!entry || key_path(path, key, sizeof(key)) < 0)
        return -1;
    return entry_path(key, entry, size);
}

int stereo_policy_file_choice(const char *path, struct stereo_policy_file_choice *choice)
{
    char key[PATH_MAX], entry[PATH_MAX], line[PATH_MAX + 16];
    struct stereo_policy_file_choice read = {0, 0, 0, 0, 0};
    if (choice)
        *choice = read;
    if (key_path(path, key, sizeof(key)) < 0 || entry_path(key, entry, sizeof(entry)) < 0)
        return -1;
    FILE *file = fopen(entry, "re");
    if (!file)
        return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
    int found = 0, header = 0;
    while (fgets(line, sizeof(line), file)) {
        const size_t length = strlen(line);
        if (!header) {
            if (strcmp(line, HEADER) != 0)
                break;
            header = 1;
            continue;
        }
        if (length && line[length - 1] == '\n')
            line[length - 1] = 0;
        if (strncmp(line, "path=", 5) == 0)
            found = strcmp(line + 5, key) == 0;
        else if (sscanf(line, "layout=%d", &read.layout) == 1 || sscanf(line, "order=%d", &read.order) == 1
            || sscanf(line, "anaglyph=%d", &read.anaglyph) == 1 || sscanf(line, "view=%d", &read.view) == 1
            || sscanf(line, "swap-eyes=%d", &read.swap_eyes) == 1)
            continue;
    }
    const int failed = ferror(file);
    fclose(file);
    if (failed) {
        errno = EIO;
        return -1;
    }
    /* an entry of another version, of another file with the same name, or with a value outside the lists says nothing */
    if (!header || !found || !valid(&read) || is_default(&read))
        return 0;
    if (choice)
        *choice = read;
    return 1;
}

int stereo_policy_set_file_choice(const char *path, const struct stereo_policy_file_choice *choice)
{
    char key[PATH_MAX], entry[PATH_MAX], directory[PATH_MAX], temporary[PATH_MAX + 16];
    if (key_path(path, key, sizeof(key)) < 0 || entry_path(key, entry, sizeof(entry)) < 0)
        return -1;
    if (!choice || is_default(choice))
        return unlink(entry) < 0 && errno != ENOENT && errno != ENOTDIR ? -1 : 0;
    if (!valid(choice)) {
        errno = EINVAL;
        return -1;
    }
    if (store_directory(directory, sizeof(directory)) < 0 || make_directories(directory) < 0)
        return -1;
    snprintf(temporary, sizeof(temporary), "%s.XXXXXX", entry);
    const int descriptor = mkostemp(temporary, O_CLOEXEC);
    if (descriptor < 0)
        return -1;
    FILE *file = fdopen(descriptor, "w");
    if (!file) {
        const int error = errno;
        close(descriptor);
        unlink(temporary);
        errno = error;
        return -1;
    }
    const int written = fprintf(file, HEADER "path=%s\nlayout=%d\norder=%d\nanaglyph=%d\nview=%d\nswap-eyes=%d\n", key, choice->layout, choice->order,
        choice->anaglyph, choice->view, choice->swap_eyes);
    int error = written < 0 || fflush(file) != 0 || fsync(descriptor) != 0 ? (errno ? errno : EIO) : 0;
    if (fclose(file) != 0 && !error)
        error = errno ? errno : EIO;
    if (!error && rename(temporary, entry) < 0)
        error = errno;
    if (error) {
        unlink(temporary);
        errno = error;
        return -1;
    }
    return 0;
}
