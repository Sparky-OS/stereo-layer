/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The per-file store under the two conditions an ordinary run never meets: every path with one hash (this file builds
 * stereo-policy-file.c itself with STEREO_POLICY_FILE_HASH_MASK 0, so all records share one entry file) and several
 * programs writing at once. Built a second time without the store's lock (policyfiletest-nolock), where the writers
 * lose each other's records: that run must fail, which is what shows the first one can. */

#define _GNU_SOURCE
#include "../src/policy/stereo-policy-file.c"

#include <sys/wait.h>

static int failures;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

#define WRITERS 8
#define FILES 40

static void touch(const char *path)
{
    FILE *file = fopen(path, "w");
    if (file) {
        fputs("x", file);
        fclose(file);
    }
}

static int entries(const char *state)
{
    char directory[PATH_MAX];
    struct dirent **items = NULL;
    snprintf(directory, sizeof(directory), "%s/stereo-policy/files", state);
    const int count = scandir(directory, &items, entry_name, alphasort);
    for (int i = 0; i < count; ++i)
        free(items[i]);
    free(items);
    return count;
}

int main(void)
{
    char state[PATH_MAX], work[] = "policyfiletest-XXXXXX", cwd[PATH_MAX - 64], a[PATH_MAX], b[PATH_MAX], entry_a[PATH_MAX], entry_b[PATH_MAX];
    struct stereo_policy_file_choice choice = {0, 0, 0, 0, 0}, read;
    if (!mkdtemp(work) || !getcwd(cwd, sizeof(cwd)))
        return 2;
    snprintf(state, sizeof(state), "%s/%s/state", cwd, work);
    setenv("XDG_STATE_HOME", state, 1);

    /* two paths, one hash: each keeps its own choice, and forgetting one leaves the other */
    snprintf(a, sizeof(a), "%s/%s/a.png", cwd, work);
    snprintf(b, sizeof(b), "%s/%s/b.png", cwd, work);
    touch(a);
    touch(b);
    CHECK(stereo_policy_file_choice_entry(a, entry_a, sizeof(entry_a)) == 0 && stereo_policy_file_choice_entry(b, entry_b, sizeof(entry_b)) == 0);
    CHECK(strcmp(entry_a, entry_b) == 0);
    choice.view = STEREO_POLICY_VIEW_MONO;
    CHECK(stereo_policy_set_file_choice(a, &choice) == 0);
    choice.view = 0;
    choice.layout = STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM;
    choice.order = STEREO_POLICY_RIGHT_FIRST;
    CHECK(stereo_policy_set_file_choice(b, &choice) == 0);
    CHECK(stereo_policy_file_choice(a, &read) == 1 && read.view == STEREO_POLICY_VIEW_MONO && read.layout == 0);
    CHECK(stereo_policy_file_choice(b, &read) == 1 && read.layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM && read.order == STEREO_POLICY_RIGHT_FIRST);
    CHECK(entries(state) == 1);
    CHECK(stereo_policy_set_file_choice(a, NULL) == 0);
    CHECK(stereo_policy_file_choice(a, &read) == 0);
    CHECK(stereo_policy_file_choice(b, &read) == 1 && read.layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM);
    CHECK(stereo_policy_set_file_choice(b, NULL) == 0);
    CHECK(entries(state) == 0);

    /* several programs at once, every file in the one entry file: no record is lost and none is torn */
    for (int writer = 0; writer < WRITERS; ++writer) {
        for (int file = 0; file < FILES; ++file) {
            snprintf(a, sizeof(a), "%s/%s/w%d-%d.png", cwd, work, writer, file);
            touch(a);
        }
    }
    snprintf(b, sizeof(b), "%s/%s/shared.png", cwd, work);
    touch(b);
    pid_t children[WRITERS];
    for (int writer = 0; writer < WRITERS; ++writer) {
        children[writer] = fork();
        if (children[writer] == 0) {
            struct stereo_policy_file_choice mine = {STEREO_POLICY_LAYOUT_SIDE_BY_SIDE + writer % 2, writer % 2, 0, 0, 1};
            int bad = 0;
            for (int file = 0; file < FILES; ++file) {
                char path[PATH_MAX];
                snprintf(path, sizeof(path), "%s/%s/w%d-%d.png", cwd, work, writer, file);
                bad += stereo_policy_set_file_choice(path, &mine) != 0;
                bad += stereo_policy_set_file_choice(b, &mine) != 0;
            }
            _exit(bad ? 1 : 0);
        }
    }
    for (int writer = 0; writer < WRITERS; ++writer) {
        int status = 0;
        CHECK(children[writer] > 0 && waitpid(children[writer], &status, 0) == children[writer] && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    int kept = 0;
    for (int writer = 0; writer < WRITERS; ++writer) {
        for (int file = 0; file < FILES; ++file) {
            snprintf(a, sizeof(a), "%s/%s/w%d-%d.png", cwd, work, writer, file);
            kept += stereo_policy_file_choice(a, &read) == 1 && read.layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE + writer % 2 && read.order == writer % 2
                && read.swap_eyes == 1;
        }
    }
    if (kept != WRITERS * FILES)
        fprintf(stderr, "%d of %d choices kept\n", kept, WRITERS * FILES);
    CHECK(kept == WRITERS * FILES);
    CHECK(stereo_policy_file_choice(b, &read) == 1 && read.swap_eyes == 1
        && (read.layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE || read.layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM));
    CHECK(entries(state) == 1);
    /* nothing but the one entry file, the lock and the cursor is left in the store */
    snprintf(a, sizeof(a), "%s/stereo-policy/files", state);
    DIR *directory = opendir(a);
    CHECK(directory != NULL);
    for (struct dirent *item; directory && (item = readdir(directory));) {
        if (strcmp(item->d_name, ".") && strcmp(item->d_name, "..") && strcmp(item->d_name, LOCK_NAME) && strcmp(item->d_name, CURSOR_NAME)
            && !entry_name(item)) {
            fprintf(stderr, "left in the store: %s\n", item->d_name);
            ++failures;
        }
    }
    if (directory)
        closedir(directory);

    snprintf(a, sizeof(a), "rm -rf '%s/%s'", cwd, work);
    CHECK(strstr(work, "policyfiletest-") != NULL && system(a) == 0);
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("policyfiletest: all checks passed\n");
    return failures ? 1 : 0;
}
