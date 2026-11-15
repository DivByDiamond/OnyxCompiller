/* Strict ar framing with GNU and BSD long-name support. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ld_internal.h"

static bool decimal(const uint8_t *s, size_t n, size_t *value) {
    size_t v = 0, i = 0;
    while (i < n && s[i] == ' ') i++;
    size_t first = i;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        unsigned d = s[i++] - '0';
        if (v > (SIZE_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    if (i == first) return false;
    while (i < n && s[i] == ' ') i++;
    *value = v;
    return i == n;
}

int ld_parse_archive(const char *path, uint8_t *data, size_t size) {
    size_t selected[MAX_OBJECTS];
    int count = 0;
    bool changed;
    do {
        changed = false;
        size_t off = 8;
        const uint8_t *names = NULL;
        size_t names_size = 0;
        while (off < size) {
            if (size - off < 60 || memcmp(data + off + 58, "`\n", 2)) goto malformed;
            size_t length;
            if (!decimal(data + off + 48, 10, &length) || length > size - off - 60)
                goto malformed;
            size_t next = off + 60 + length;
            if (length & 1) {
                if (next == size || data[next] != '\n') goto malformed;
                next++;
            }
            char short_name[17];
            memcpy(short_name, data + off, 16);
            short_name[16] = 0;
            for (int i = 15; i >= 0 && short_name[i] == ' '; i--) short_name[i] = 0;
            uint8_t *member = data + off + 60;
            size_t member_size = length;
            const uint8_t *name = (uint8_t *)short_name;
            size_t name_size = strlen(short_name);
            bool metadata = !strcmp(short_name, "/") || !strcmp(short_name, "/SYM64/");
            if (!strcmp(short_name, "//")) {
                names = member;
                names_size = member_size;
                metadata = true;
            } else if (!strncmp(short_name, "#1/", 3)) {
                size_t skip;
                if (!decimal(name + 3, name_size - 3, &skip) || skip > member_size)
                    goto malformed;
                name = member;
                name_size = skip;
                member += skip;
                member_size -= skip;
                while (name_size && !name[name_size - 1]) name_size--;
            } else if (short_name[0] == '/' && !metadata) {
                size_t index;
                if (!decimal(name + 1, name_size - 1, &index) || index >= names_size)
                    goto malformed;
                name = names + index;
                const uint8_t *end = memchr(name, '\n', names_size - index);
                if (!end) goto malformed;
                name_size = (size_t)(end - name);
            }
            if (name_size && name[name_size - 1] == '/') name_size--;
            static const char *const indexes[] = {
                "__.SYMDEF", "__.SYMDEF SORTED", "__.SYMDEF_64", "__.SYMDEF_64 SORTED"
            };
            for (size_t i = 0; i < sizeof(indexes) / sizeof(indexes[0]); i++)
                if (name_size == strlen(indexes[i]) && !memcmp(name, indexes[i], name_size))
                    metadata = true;
            bool seen = false;
            for (int i = 0; i < count; i++) if (selected[i] == off) seen = true;
            if (!metadata && !seen && member_size >= 4 && ld_rd_u32(member) == ONYO_MAGIC) {
                char label[256];
                snprintf(label, sizeof(label), "%s:%.*s", path,
                         (int)(name_size > 120 ? 120 : name_size), (const char *)name);
                obj_t obj = {0};
                int rc = ld_decode_object(&obj, member, member_size, label, true);
                if (!rc && ld_archive_needed(&obj)) {
                    rc = ld_adopt_object(&obj, true);
                    if (!rc) {
                        selected[count++] = off;
                        changed = true;
                    }
                } else {
                    free(obj.syms);
                    free(obj.relocs);
                    if (rc) return -1;
                    off = next;
                    continue;
                }
                if (rc) { free(obj.syms); free(obj.relocs); return -1; }
            }
            off = next;
        }
    } while (changed);
    if (g_ld_opts.verbose)
        fprintf(stderr, "onyx-ld: archive %s: extracted %d member(s)\n", path, count);
    return 0;
malformed:
    fprintf(stderr, "onyx-ld: %s: malformed archive\n", path);
    return -1;
}
