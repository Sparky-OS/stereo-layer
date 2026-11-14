/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The person's per-file viewing choice, kept in the programs' own state and never in the file (stereo-policy.h). */

#define _GNU_SOURCE
#include "stereo-policy.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEADER "stereo-policy-file-choice 1\n"
#define LOCK_NAME ".lock"
#define CURSOR_NAME ".cursor"
#define ENTRY_NAME_LENGTH 32

/* Two autotests build this file with their own values: a mask that makes every path share one entry file, and no lock. */
#ifndef STEREO_POLICY_FILE_HASH_MASK
#define STEREO_POLICY_FILE_HASH_MASK 0xffffffffffffffffULL
#endif

/* One file's choice inside an entry file. An entry file is named from a hash of the path and holds every path of that
 * hash, so two paths with one hash keep one record each. */
struct record {
    char *path;
    struct stereo_policy_file_choice choice;
    int identified; /* device and inode are the file's when the record was written */
    unsigned long long device, inode;
};

struct records {
    struct record *list;
    size_t count;
};

static uint64_t fnv1a(const char *text, uint64_t hash)
{
    for (const unsigned char *c = (const unsigned char *)text; *c; ++c) {
        hash ^= *c;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/* path without "." and ".." parts and doubled slashes; it starts with a slash. */
static void clean_path(char *path)
{
    char *out = path, *in = path;
    while (*in) {
        while (*in == '/')
            ++in;
        if (!*in)
            break;
        const size_t length = strcspn(in, "/");
        if (length == 1 && in[0] == '.') {
            in += length;
            continue;
        }
        if (length == 2 && in[0] == '.' && in[1] == '.') {
            while (out > path && *--out != '/') { }
            in += length;
            continue;
        }
        *out++ = '/';
        memmove(out, in, length);
        out += length;
        in += length;
    }
    if (out == path)
        *out++ = '/';
    *out = 0;
}

/* The file as the store names it: its real path when it resolves. A file that is no longer there (the old name of a
 * file that was renamed or moved) is named by the real path of its folder and its own name, which is what its real path
 * was unless the file itself was a link; when the folder is gone too, by the path as given, made absolute and cleaned. */
static int key_path(const char *path, char *key, size_t size)
{
    char resolved[PATH_MAX], absolute[PATH_MAX];
    if (!path || !path[0] || strchr(path, '\n')) {
        errno = EINVAL;
        return -1;
    }
    if (realpath(path, resolved)) {
        if (strlen(resolved) >= size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(key, resolved);
        return 0;
    }
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(absolute)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(absolute, path);
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd)))
            return -1;
        if ((size_t)snprintf(absolute, sizeof(absolute), "%s/%s", cwd, path) >= sizeof(absolute)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    clean_path(absolute);
    char *slash = strrchr(absolute, '/');
    if (slash && slash != absolute && slash[1]) {
        *slash = 0;
        if (realpath(absolute, resolved)) {
            const int length = snprintf(key, size, "%s/%s", strcmp(resolved, "/") == 0 ? "" : resolved, slash + 1);
            if (length < 0 || (size_t)length >= size) {
                errno = ENAMETOOLONG;
                return -1;
            }
            return 0;
        }
        *slash = '/';
    }
    if (strlen(absolute) >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(key, absolute);
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
    const int length = snprintf(entry, size, "%s/%016llx%016llx", directory,
        (unsigned long long)(fnv1a(key, 0xcbf29ce484222325ULL) & STEREO_POLICY_FILE_HASH_MASK),
        (unsigned long long)(fnv1a(key, 0x84222325cbf29ce4ULL) & STEREO_POLICY_FILE_HASH_MASK));
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

static void free_records(struct records *records)
{
    for (size_t i = 0; i < records->count; ++i)
        free(records->list[i].path);
    free(records->list);
    records->list = NULL;
    records->count = 0;
}

static struct record *add_record(struct records *records, const char *path)
{
    struct record *grown = realloc(records->list, (records->count + 1) * sizeof(*grown));
    if (!grown)
        return NULL;
    records->list = grown;
    struct record *record = &grown[records->count];
    memset(record, 0, sizeof(*record));
    record->path = strdup(path);
    if (!record->path)
        return NULL;
    ++records->count;
    return record;
}

static struct record *find_record(struct records *records, const char *path)
{
    for (size_t i = 0; i < records->count; ++i) {
        if (strcmp(records->list[i].path, path) == 0)
            return &records->list[i];
    }
    return NULL;
}

static void drop_record(struct records *records, struct record *record)
{
    free(record->path);
    const size_t index = (size_t)(record - records->list);
    memmove(record, record + 1, (records->count - index - 1) * sizeof(*record));
    --records->count;
}

/* Every record of an entry file. A file that is not there holds none; one of another version says nothing. Returns 0,
 * or -1 with errno set when it is there and cannot be read. */
static int read_records(const char *entry, struct records *records)
{
    char line[PATH_MAX + 16];
    records->list = NULL;
    records->count = 0;
    FILE *file = fopen(entry, "re");
    if (!file)
        return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
    struct record *record = NULL;
    int header = 0, failed = 0;
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
        if (strncmp(line, "path=", 5) == 0) {
            record = add_record(records, line + 5);
            if (!record) {
                failed = ENOMEM;
                break;
            }
            continue;
        }
        if (!record)
            continue;
        if (sscanf(line, "device=%llu", &record->device) == 1 || sscanf(line, "inode=%llu", &record->inode) == 1) {
            record->identified = 1;
            continue;
        }
        if (sscanf(line, "layout=%d", &record->choice.layout) == 1 || sscanf(line, "order=%d", &record->choice.order) == 1
            || sscanf(line, "anaglyph=%d", &record->choice.anaglyph) == 1 || sscanf(line, "view=%d", &record->choice.view) == 1
            || sscanf(line, "swap-eyes=%d", &record->choice.swap_eyes) == 1)
            continue;
    }
    if (!failed && ferror(file))
        failed = EIO;
    fclose(file);
    if (failed) {
        free_records(records);
        errno = failed;
        return -1;
    }
    return 0;
}

/* Writes the records as the whole entry file, through a temporary file; with none left the entry file is removed. */
static int write_records(const char *entry, const struct records *records)
{
    char temporary[PATH_MAX + 16];
    if (records->count == 0)
        return unlink(entry) < 0 && errno != ENOENT && errno != ENOTDIR ? -1 : 0;
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
    int error = fputs(HEADER, file) < 0 ? (errno ? errno : EIO) : 0;
    for (size_t i = 0; !error && i < records->count; ++i) {
        const struct record *record = &records->list[i];
        if (fprintf(file, "path=%s\nlayout=%d\norder=%d\nanaglyph=%d\nview=%d\nswap-eyes=%d\n", record->path, record->choice.layout,
                record->choice.order, record->choice.anaglyph, record->choice.view, record->choice.swap_eyes)
                < 0
            || (record->identified && fprintf(file, "device=%llu\ninode=%llu\n", record->device, record->inode) < 0))
            error = errno ? errno : EIO;
    }
    if (!error && (fflush(file) != 0 || fsync(descriptor) != 0))
        error = errno ? errno : EIO;
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

/* One writer at a time: a changed entry file is read, changed and written back as a whole, and a prune or a move reads
 * and writes several. Readers take no lock, since an entry file is replaced by a rename. Returns the lock's descriptor,
 * -1 with errno set, or -2 when the store has no folder yet (create is 0): nothing is kept, nothing to lock. */
static int lock_store(int create)
{
    char directory[PATH_MAX], name[PATH_MAX + 16];
    if (store_directory(directory, sizeof(directory)) < 0)
        return -1;
    if (create && make_directories(directory) < 0)
        return -1;
    snprintf(name, sizeof(name), "%s/" LOCK_NAME, directory);
    const int descriptor = open(name, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor < 0)
        return !create && (errno == ENOENT || errno == ENOTDIR) ? -2 : -1;
#ifndef STEREO_POLICY_FILE_TEST_NO_LOCK
    while (flock(descriptor, LOCK_EX) < 0) {
        if (errno != EINTR) {
            const int error = errno;
            close(descriptor);
            errno = error;
            return -1;
        }
    }
#endif
    return descriptor;
}

static void unlock_store(int descriptor)
{
    const int error = errno;
    close(descriptor);
    errno = error;
}

static void identify(struct record *record)
{
    struct stat file;
    record->identified = stat(record->path, &file) == 0;
    record->device = record->identified ? (unsigned long long)file.st_dev : 0;
    record->inode = record->identified ? (unsigned long long)file.st_ino : 0;
}

/* Whether the file of a record is gone for good: nothing at its path, and the place it was is still there. When the
 * record knows the file's device, the nearest folder that still exists must be on that device: a drive that is not
 * connected, or a share that is not mounted, shows another device there, and its files keep their choice. A record
 * without a device goes only when the file's own folder is still there. Anything that cannot be told keeps the record. */
static int record_is_gone(const struct record *record)
{
    struct stat place;
    char folder[PATH_MAX];
    if (lstat(record->path, &place) == 0 || (errno != ENOENT && errno != ENOTDIR))
        return 0;
    if (strlen(record->path) >= sizeof(folder))
        return 0;
    strcpy(folder, record->path);
    for (int first = 1;; first = 0) {
        char *slash = strrchr(folder, '/');
        if (!slash)
            return 0;
        if (slash == folder)
            slash[1] = 0;
        else
            *slash = 0;
        if (stat(folder, &place) == 0)
            return record->identified ? (unsigned long long)place.st_dev == record->device : first;
        if ((errno != ENOENT && errno != ENOTDIR) || strcmp(folder, "/") == 0)
            return 0;
    }
}

static int entry_name(const struct dirent *item)
{
    return strlen(item->d_name) == ENTRY_NAME_LENGTH && strspn(item->d_name, "0123456789abcdef") == ENTRY_NAME_LENGTH;
}

/* Looks at up to limit entry files, after the one the last call stopped at, and drops the records of files that are
 * gone. The work is bounded by limit and by the store's own folder: only paths the store holds are looked at, never a
 * folder of the person's. The store is locked by the caller. Returns the records dropped, or -1 with errno set. */
static int prune_locked(int limit)
{
    char directory[PATH_MAX], name[PATH_MAX + 64], cursor[ENTRY_NAME_LENGTH + 2] = "";
    struct dirent **items = NULL;
    if (limit <= 0)
        return 0;
    if (store_directory(directory, sizeof(directory)) < 0)
        return -1;
    const int count = scandir(directory, &items, entry_name, alphasort);
    if (count < 0)
        return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
    snprintf(name, sizeof(name), "%s/" CURSOR_NAME, directory);
    FILE *file = fopen(name, "re");
    if (file) {
        if (!fgets(cursor, sizeof(cursor), file))
            cursor[0] = 0;
        cursor[strcspn(cursor, "\n")] = 0;
        fclose(file);
    }
    int first = 0, dropped = 0, error = 0;
    while (first < count && strcmp(items[first]->d_name, cursor) <= 0)
        ++first;
    if (first >= count)
        first = 0;
    const int looked = count < limit ? count : limit;
    const char *last = "";
    for (int step = 0; step < looked && !error; ++step) {
        const int index = (first + step) % count;
        struct records records;
        last = index == count - 1 ? "" : items[index]->d_name;
        snprintf(name, sizeof(name), "%s/%s", directory, items[index]->d_name);
        if (read_records(name, &records) < 0)
            continue;
        int changed = 0;
        for (size_t i = records.count; i-- > 0;) {
            if (record_is_gone(&records.list[i])) {
                drop_record(&records, &records.list[i]);
                ++changed;
            }
        }
        if (changed) {
            if (write_records(name, &records) < 0)
                error = errno;
            else
                dropped += changed;
        }
        free_records(&records);
    }
    snprintf(name, sizeof(name), "%s/" CURSOR_NAME, directory);
    file = fopen(name, "we");
    if (file) {
        fprintf(file, "%s\n", last);
        fclose(file);
    }
    for (int i = 0; i < count; ++i)
        free(items[i]);
    free(items);
    if (error) {
        errno = error;
        return -1;
    }
    return dropped;
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
    char key[PATH_MAX], entry[PATH_MAX];
    const struct stereo_policy_file_choice none = {0, 0, 0, 0, 0};
    struct records records;
    if (choice)
        *choice = none;
    if (key_path(path, key, sizeof(key)) < 0 || entry_path(key, entry, sizeof(entry)) < 0)
        return -1;
    if (read_records(entry, &records) < 0)
        return -1;
    /* an entry of another version, of another file with the same hash, or with a value outside the lists says nothing */
    const struct record *record = find_record(&records, key);
    const int found = record && valid(&record->choice) && !is_default(&record->choice);
    if (found && choice)
        *choice = record->choice;
    free_records(&records);
    return found;
}

int stereo_policy_set_file_choice(const char *path, const struct stereo_policy_file_choice *choice)
{
    char key[PATH_MAX], entry[PATH_MAX];
    struct records records;
    if (key_path(path, key, sizeof(key)) < 0 || entry_path(key, entry, sizeof(entry)) < 0)
        return -1;
    const int forget = !choice || is_default(choice);
    if (!forget && !valid(choice)) {
        errno = EINVAL;
        return -1;
    }
    const int lock = lock_store(!forget);
    if (lock == -2)
        return 0;
    if (lock < 0)
        return -1;
    if (read_records(entry, &records) < 0) {
        unlock_store(lock);
        return -1;
    }
    struct record *record = find_record(&records, key);
    int result = 0;
    if (forget) {
        if (record) {
            drop_record(&records, record);
            result = write_records(entry, &records);
        }
    } else {
        if (!record)
            record = add_record(&records, key);
        if (!record) {
            errno = ENOMEM;
            result = -1;
        } else {
            record->choice = *choice;
            identify(record);
            result = write_records(entry, &records);
        }
    }
    const int error = errno;
    free_records(&records);
    /* The store tidies itself where it is written: a few entries a time, so a write stays a small, bounded step. A
     * prune that fails keeps its entries for the next write and is not this choice's failure. */
    if (result == 0 && !forget)
        prune_locked(STEREO_POLICY_FILE_PRUNE_STEP);
    unlock_store(lock);
    errno = error;
    return result;
}

int stereo_policy_prune_file_choices(int limit)
{
    const int lock = lock_store(0);
    if (lock == -2)
        return 0;
    if (lock < 0)
        return -1;
    const int dropped = prune_locked(limit);
    unlock_store(lock);
    return dropped;
}

/* Puts moved into the entry file of its path, in place of a record already there. */
static int place_record(const struct record *moved)
{
    char entry[PATH_MAX];
    struct records records;
    if (entry_path(moved->path, entry, sizeof(entry)) < 0 || read_records(entry, &records) < 0)
        return -1;
    struct record *record = find_record(&records, moved->path);
    if (!record)
        record = add_record(&records, moved->path);
    int result = -1;
    if (record) {
        record->choice = moved->choice;
        identify(record);
        result = write_records(entry, &records);
    } else {
        errno = ENOMEM;
    }
    const int error = errno;
    free_records(&records);
    errno = error;
    return result;
}

int stereo_policy_move_file_choice(const char *from, const char *to)
{
    char key_from[PATH_MAX], key_to[PATH_MAX], entry[PATH_MAX], directory[PATH_MAX], name[PATH_MAX + 64];
    struct stat gone, target;
    struct records records;
    if (!from || !to) {
        errno = EINVAL;
        return -1;
    }
    /* a file that is still at its old path was copied, not moved: its choice stays where it is */
    if (lstat(from, &gone) == 0)
        return 0;
    if (errno != ENOENT && errno != ENOTDIR)
        return -1;
    if (stat(to, &target) < 0 || key_path(from, key_from, sizeof(key_from)) < 0 || key_path(to, key_to, sizeof(key_to)) < 0)
        return -1;
    if (strcmp(key_from, key_to) == 0)
        return 0;
    const int lock = lock_store(0);
    if (lock == -2)
        return 0;
    if (lock < 0)
        return -1;
    int moved = 0, error = 0;
    if (!S_ISDIR(target.st_mode)) {
        struct record carried;
        memset(&carried, 0, sizeof(carried));
        if (entry_path(key_from, entry, sizeof(entry)) < 0 || read_records(entry, &records) < 0) {
            error = errno;
        } else {
            struct record *record = find_record(&records, key_from);
            if (record) {
                carried.choice = record->choice;
                carried.path = key_to;
                drop_record(&records, record);
                if (place_record(&carried) < 0 || write_records(entry, &records) < 0)
                    error = errno;
                else
                    moved = 1;
            }
            free_records(&records);
        }
        /* Nothing to carry: the file took the place of another one, whose choice was for other contents. A record that
         * was carried here a moment ago by another program that saw the same move names this very file and stays. */
        if (!moved && !error) {
            if (entry_path(key_to, entry, sizeof(entry)) < 0 || read_records(entry, &records) < 0) {
                error = errno;
            } else {
                struct record *record = find_record(&records, key_to);
                if (record && record->identified
                    && (record->device != (unsigned long long)target.st_dev || record->inode != (unsigned long long)target.st_ino)) {
                    drop_record(&records, record);
                    if (write_records(entry, &records) < 0)
                        error = errno;
                }
                free_records(&records);
            }
        }
    } else if (store_directory(directory, sizeof(directory)) < 0) {
        error = errno;
    } else {
        /* A folder: every record of a file below its old path follows it. The store's own folder is read; the person's
         * folders are not. The records are written at their new paths first and taken from the old ones afterwards, so
         * a failure in between leaves a choice twice and never loses it. */
        struct dirent **items = NULL;
        const size_t prefix = strlen(key_from);
        const int count = scandir(directory, &items, entry_name, alphasort);
        struct records carried = {NULL, 0};
        for (int pass = 0; pass < 2 && !error; ++pass) {
            for (int i = 0; i < count && !error; ++i) {
                snprintf(name, sizeof(name), "%s/%s", directory, items[i]->d_name);
                if (read_records(name, &records) < 0)
                    continue;
                int changed = 0;
                for (size_t r = records.count; r-- > 0 && !error;) {
                    const struct record *record = &records.list[r];
                    char renamed[PATH_MAX];
                    if (strncmp(record->path, key_from, prefix) != 0 || record->path[prefix] != '/'
                        || (size_t)snprintf(renamed, sizeof(renamed), "%s%s", key_to, record->path + prefix) >= sizeof(renamed))
                        continue;
                    if (pass == 0) {
                        struct record *copy = add_record(&carried, renamed);
                        if (copy)
                            copy->choice = record->choice;
                        else
                            error = ENOMEM;
                    } else {
                        drop_record(&records, &records.list[r]);
                        ++changed;
                    }
                }
                if (changed && !error && write_records(name, &records) < 0)
                    error = errno;
                free_records(&records);
            }
            for (size_t r = 0; pass == 0 && r < carried.count && !error; ++r) {
                if (place_record(&carried.list[r]) < 0)
                    error = errno;
                else
                    ++moved;
            }
        }
        free_records(&carried);
        for (int i = 0; i < count; ++i)
            free(items[i]);
        free(items);
    }
    if (!error)
        prune_locked(STEREO_POLICY_FILE_PRUNE_STEP);
    unlock_store(lock);
    if (error) {
        errno = error;
        return -1;
    }
    return moved;
}
