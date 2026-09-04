#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "cJSON.h"
#include "format.h"

static uint32_t fnv1a_32(const char *s) {
    uint32_t h = 0x811C9DC5u;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 0x01000193u;
    }
    return h;
}

typedef struct {
    uint32_t codepoint;
    int x, y, w, h;
    int xoffset, yoffset;
    int xadvance;
} SrcGlyph;

typedef struct {
    uint32_t first, second;
    int amount;
} SrcKerning;

static int cmp_glyph(const void *a, const void *b) {
    const SrcGlyph *ga = a, *gb = b;
    if (ga->codepoint < gb->codepoint) return -1;
    if (ga->codepoint > gb->codepoint) return 1;
    return 0;
}

static int cmp_kerning(const void *a, const void *b) {
    const SrcKerning *ka = a, *kb = b;
    if (ka->first != kb->first) return (ka->first < kb->first) ? -1 : 1;
    if (ka->second != kb->second) return (ka->second < kb->second) ? -1 : 1;
    return 0;
}

static int json_get_int(const cJSON *obj, const char *key, int default_val, int required, const char *ctx) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        if (required) {
            fprintf(stderr, "Erreur: champ requis manquant '%s' (%s)\n", key, ctx);
            exit(1);
        }
        return default_val;
    }
    return (int)item->valuedouble;
}

static const char *json_get_string(const cJSON *obj, const char *key, int required, const char *ctx) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item)) {
        if (required) {
            fprintf(stderr, "Erreur: champ string requis manquant '%s' (%s)\n", key, ctx);
            exit(1);
        }
        return NULL;
    }
    return item->valuestring;
}

static int json_get_bool(const cJSON *obj, const char *key, int default_val) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsBool(item)) return default_val;
    return cJSON_IsTrue(item) ? 1 : 0;
}

static char *read_file(const char *path, long *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Erreur: impossible d'ouvrir '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fprintf(stderr, "Erreur: OOM lecture fichier\n"); exit(1); }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "Erreur: lecture incomplète de '%s'\n", path);
        exit(1);
    }
    buf[size] = '\0';
    fclose(f);
    if (out_size) *out_size = size;
    return buf;
}

int convert(const char *input_path, const char *output_path) {
    long file_size;
    char *raw = read_file(input_path, &file_size);

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root) {
        fprintf(stderr, "Erreur: JSON invalide dans '%s': %s\n",
                input_path, cJSON_GetErrorPtr());
        return 1;
    }

    cJSON *header_json = cJSON_GetObjectItemCaseSensitive(root, "header");
    cJSON *glyphs_json = cJSON_GetObjectItemCaseSensitive(root, "glyphs");
    cJSON *kerning_json = cJSON_GetObjectItemCaseSensitive(root, "kerning");

    if (!cJSON_IsObject(header_json)) {
        fprintf(stderr, "Erreur: champ 'header' manquant ou invalide\n");
        cJSON_Delete(root);
        return 1;
    }
    if (!cJSON_IsArray(glyphs_json) || cJSON_GetArraySize(glyphs_json) == 0) {
        fprintf(stderr, "Erreur: 'glyphs' doit être un tableau non vide\n");
        cJSON_Delete(root);
        return 1;
    }

    const char *font_id_name = json_get_string(header_json, "font_id_name", 1, "header");
    int is_sdf              = json_get_bool(header_json, "is_sdf", 0);
    int atlas_width         = json_get_int(header_json, "atlas_width", 0, 1, "header");
    int atlas_height        = json_get_int(header_json, "atlas_height", 0, 1, "header");
    int reference_size_px   = json_get_int(header_json, "reference_size_px", 0, 0, "header");
    int sdf_spread_px       = json_get_int(header_json, "sdf_spread_px", 0, 0, "header");
    int line_height         = json_get_int(header_json, "line_height", 0, 0, "header");
    int baseline_offset     = json_get_int(header_json, "baseline_offset", 0, 0, "header");
    int default_char        = json_get_int(header_json, "default_char", '?', 0, "header");
    int kerning_enabled     = json_get_bool(header_json, "kerning_enabled", 0);

    if (is_sdf && sdf_spread_px <= 0) {
        fprintf(stderr, "Erreur: is_sdf=true nécessite 'sdf_spread_px' > 0\n");
        cJSON_Delete(root);
        return 1;
    }

    int glyph_count = cJSON_GetArraySize(glyphs_json);
    SrcGlyph *glyphs = calloc((size_t)glyph_count, sizeof(SrcGlyph));
    if (!glyphs) { fprintf(stderr, "Erreur: OOM glyphes\n"); return 1; }

    int default_char_found = 0;
    for (int i = 0; i < glyph_count; i++) {
        cJSON *g = cJSON_GetArrayItem(glyphs_json, i);
        char ctx[64];
        snprintf(ctx, sizeof(ctx), "glyphe #%d", i);

        glyphs[i].codepoint = (uint32_t)json_get_int(g, "codepoint", 0, 1, ctx);
        glyphs[i].x         = json_get_int(g, "x", 0, 1, ctx);
        glyphs[i].y         = json_get_int(g, "y", 0, 1, ctx);
        glyphs[i].w         = json_get_int(g, "w", 0, 1, ctx);
        glyphs[i].h         = json_get_int(g, "h", 0, 1, ctx);
        glyphs[i].xadvance  = json_get_int(g, "xadvance", 0, 1, ctx);
        glyphs[i].xoffset   = json_get_int(g, "xoffset", 0, 0, ctx);
        glyphs[i].yoffset   = json_get_int(g, "yoffset", 0, 0, ctx);

        if ((int)glyphs[i].codepoint == default_char) default_char_found = 1;

        for (int j = 0; j < i; j++) {
            if (glyphs[j].codepoint == glyphs[i].codepoint) {
                fprintf(stderr, "Erreur: codepoint dupliqué: %u\n", glyphs[i].codepoint);
                free(glyphs);
                cJSON_Delete(root);
                return 1;
            }
        }
    }

    if (!default_char_found) {
        fprintf(stderr, "Erreur: default_char (%d) n'a pas de glyphe correspondant\n", default_char);
        free(glyphs);
        cJSON_Delete(root);
        return 1;
    }

    qsort(glyphs, (size_t)glyph_count, sizeof(SrcGlyph), cmp_glyph);

    int kerning_count = cJSON_IsArray(kerning_json) ? cJSON_GetArraySize(kerning_json) : 0;
    SrcKerning *kerning = NULL;
    if (kerning_count > 0) {
        kerning = calloc((size_t)kerning_count, sizeof(SrcKerning));
        if (!kerning) { fprintf(stderr, "Erreur: OOM kerning\n"); free(glyphs); return 1; }
        for (int i = 0; i < kerning_count; i++) {
            cJSON *k = cJSON_GetArrayItem(kerning_json, i);
            char ctx[64];
            snprintf(ctx, sizeof(ctx), "kerning #%d", i);
            kerning[i].first  = (uint32_t)json_get_int(k, "first", 0, 1, ctx);
            kerning[i].second = (uint32_t)json_get_int(k, "second", 0, 1, ctx);
            kerning[i].amount = json_get_int(k, "amount", 0, 1, ctx);
        }
        qsort(kerning, (size_t)kerning_count, sizeof(SrcKerning), cmp_kerning);
    }

    FontBinaryHeader out_header;
    memset(&out_header, 0, sizeof(out_header));
    memcpy(out_header.magic, FONT_MAGIC, 4);
    out_header.version          = FONT_FORMAT_VERSION;
    out_header.flags            = (is_sdf ? FONT_FLAG_IS_SDF : 0)
                                 | (kerning_enabled ? FONT_FLAG_KERNING_ENABLED : 0);
    out_header.glyph_count      = (uint16_t)glyph_count;
    out_header.kerning_count    = (uint16_t)kerning_count;
    out_header.atlas_width      = (uint16_t)atlas_width;
    out_header.atlas_height     = (uint16_t)atlas_height;
    out_header.reference_size_px= (uint16_t)reference_size_px;
    out_header.sdf_spread_px    = (uint16_t)sdf_spread_px;
    out_header.line_height      = (uint16_t)line_height;
    out_header.baseline_offset  = (uint16_t)baseline_offset;
    out_header.default_char     = (uint32_t)default_char;
    out_header.font_id_hash     = fnv1a_32(font_id_name);

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Erreur: impossible d'écrire '%s'\n", output_path);
        free(glyphs); free(kerning); cJSON_Delete(root);
        return 1;
    }

    fwrite(&out_header, sizeof(out_header), 1, out);

    for (int i = 0; i < glyph_count; i++) {
        FontGlyph fg;
        fg.codepoint = glyphs[i].codepoint;
        fg.x = (uint16_t)glyphs[i].x;
        fg.y = (uint16_t)glyphs[i].y;
        fg.w = (uint16_t)glyphs[i].w;
        fg.h = (uint16_t)glyphs[i].h;
        fg.xoffset = (int16_t)glyphs[i].xoffset;
        fg.yoffset = (int16_t)glyphs[i].yoffset;
        fg.xadvance = (uint16_t)glyphs[i].xadvance;
        fg._pad = 0;
        fwrite(&fg, sizeof(fg), 1, out);
    }

    for (int i = 0; i < kerning_count; i++) {
        FontKerningPair fk;
        fk.first = kerning[i].first;
        fk.second = kerning[i].second;
        fk.amount = (int16_t)kerning[i].amount;
        fk._pad = 0;
        fwrite(&fk, sizeof(fk), 1, out);
    }

    fclose(out);

    long total = (long)sizeof(out_header)
               + (long)glyph_count * (long)sizeof(FontGlyph)
               + (long)kerning_count * (long)sizeof(FontKerningPair);

    printf("OK: %s\n", output_path);
    printf("  font_id_name     : %s\n", font_id_name);
    printf("  font_id_hash     : 0x%08X\n", out_header.font_id_hash);
    printf("  is_sdf           : %s\n", is_sdf ? "true" : "false");
    printf("  glyphs           : %d (%zu octets)\n", glyph_count, (size_t)glyph_count * sizeof(FontGlyph));
    printf("  kerning pairs    : %d (%zu octets)\n", kerning_count, (size_t)kerning_count * sizeof(FontKerningPair));
    printf("  header           : %zu octets\n", sizeof(out_header));
    printf("  total            : %ld octets\n", total);

    free(glyphs);
    free(kerning);
    cJSON_Delete(root);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.json> <output.bin> [--verify]\n", argv[0]);
        return 1;
    }
    int rc = convert(argv[1], argv[2]);
    if (rc == 0 && argc > 3 && strcmp(argv[3], "--verify") == 0) {
        long size;
        char *buf = read_file(argv[2], &size);
        FontBinaryHeader *h = (FontBinaryHeader *)buf;
        if (memcmp(h->magic, FONT_MAGIC, 4) != 0) {
            fprintf(stderr, "VERIFY FAIL: magic invalide\n");
            free(buf);
            return 1;
        }
        long expected = (long)sizeof(FontBinaryHeader)
                       + (long)h->glyph_count * (long)sizeof(FontGlyph)
                       + (long)h->kerning_count * (long)sizeof(FontKerningPair);
        if (expected != size) {
            fprintf(stderr, "VERIFY FAIL: taille fichier %ld != attendue %ld\n", size, expected);
            free(buf);
            return 1;
        }
        printf("VERIFY OK: %u glyphes, %u paires de kerning, taille fichier cohérente\n",
               h->glyph_count, h->kerning_count);
        free(buf);
    }
    return rc;
}

