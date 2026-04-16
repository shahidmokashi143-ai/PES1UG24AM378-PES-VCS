#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

// 🔥 ADD THIS LINE
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
// ─── FIND ENTRY ────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

// ─── LOAD INDEX ────────────────────────────────────────────────

int index_load(Index *index) {
    index->count = 0;

    FILE *f = fopen(".pes/index", "r");
    if (!f) return 0;

    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        char hex[HASH_HEX_SIZE + 1];

        int ret = fscanf(f, "%o %64s %ld %u %[^\n]\n",
                         &e->mode,
                         hex,
                         &e->mtime_sec,
                         &e->size,
                         e->path);

        if (ret != 5) break;

        if (hex_to_hash(hex, &e->hash) != 0) break;

        index->count++;
    }

    fclose(f);
    return 0;
}

// ─── SORT HELPER ───────────────────────────────────────────────

static int cmp(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path,
                  ((IndexEntry*)b)->path);
}

// ─── SAVE INDEX ────────────────────────────────────────────────
int index_save(const Index *index) {
    // 🔥 ensure .pes directory exists
    struct stat st = {0};
    if (stat(".pes", &st) == -1) {
        mkdir(".pes", 0755);
    }

    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) return -1;

    for (int i = 0; i < index->count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&index->entries[i].hash, hex);

        fprintf(f, "%o %s %ld %u %s\n",
                index->entries[i].mode,
                hex,
                index->entries[i].mtime_sec,
                index->entries[i].size,
                index->entries[i].path);
    }

    fclose(f);

    // 🔥 safe rename
    rename(".pes/index.tmp", ".pes/index");

    return 0;
}
// ─── ADD FILE ──────────────────────────────────────────────────

int index_add(Index *index, const char *path) {
    if (!index || !path) return -1;

    // Always ensure safe state
    if (index->count < 0 || index->count > MAX_INDEX_ENTRIES)
        index->count = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    // 🔥 FIX: allow empty files safely
    if (size < 0) {
        fclose(f);
        return -1;
    }

    void *data = NULL;
    if (size > 0) {
        data = malloc(size);
        if (!data) {
            fclose(f);
            return -1;
        }

        if (fread(data, 1, size, f) != (size_t)size) {
            free(data);
            fclose(f);
            return -1;
        }
    }

    fclose(f);

    ObjectID hash;
    if (object_write(OBJ_BLOB, data, size, &hash) != 0) {
        free(data);
        return -1;
    }

    free(data);

    struct stat st;
    if (stat(path, &st) != 0) return -1;

    IndexEntry *e = index_find(index, path);

    if (e) {
        e->hash = hash;
        e->mtime_sec = st.st_mtime;
        e->size = st.st_size;
        e->mode = st.st_mode;
    } else {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;

        e = &index->entries[index->count++];
        e->hash = hash;
        e->mtime_sec = st.st_mtime;
        e->size = st.st_size;
        e->mode = st.st_mode;
        strcpy(e->path, path);
    }

    return index_save(index);
}
// ─── STATUS ────────────────────────────────────────────────────

int index_status(const Index *index) {
    printf("Staged changes:\n");
    if (index->count == 0) printf("  (nothing to show)\n");

    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
    }

    printf("\nUnstaged changes:\n  (nothing to show)\n\n");

    printf("Untracked files:\n");

    DIR *dir = opendir(".");
    struct dirent *ent;
    int found = 0;

    if (dir) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;

            int tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    tracked = 1;
                    break;
                }
            }

            if (!tracked) {
                printf("  untracked:  %s\n", ent->d_name);
                found = 1;
            }
        }
        closedir(dir);
    }

    if (!found) printf("  (nothing to show)\n");
    printf("\n");

    return 0;
}
