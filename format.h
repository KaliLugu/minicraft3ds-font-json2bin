// plus d'information dans une doc du repo de Minicraft3ds
// more information in Minicraft3ds repository documentation

#ifndef FONT_BINARY_FORMAT_H
#define FONT_BINARY_FORMAT_H

#include <stdint.h>

#define FONT_MAGIC "FONT"          /* 4 bytes, sans terminateur nul stocké */
#define FONT_FORMAT_VERSION 1

#define FONT_FLAG_IS_SDF            (1 << 0)
#define FONT_FLAG_KERNING_ENABLED   (1 << 1)

#pragma pack(push, 1)

/* Taille totale : 64 octets (32 utiles + 32 de réserve pour extensions futures) */
typedef struct {
    char     magic[4];          /* "FONT" */
    uint16_t version;
    uint16_t flags;              /* FONT_FLAG_* */
    uint16_t glyph_count;
    uint16_t kerning_count;
    uint16_t atlas_width;
    uint16_t atlas_height;
    uint16_t reference_size_px;  /* taille de cuisson (bitmap) ou de référence (SDF) */
    uint16_t sdf_spread_px;      /* 0 si is_sdf == false */
    uint16_t line_height;
    uint16_t baseline_offset;
    uint32_t default_char;       /* codepoint de fallback */
    uint32_t font_id_hash;       /* FNV-1a 32 bits de font_id_name, cf font_ids.h */
    uint8_t  reserved[32];
} FontBinaryHeader;

/* Taille totale : 20 octets. Table triée par codepoint croissant
 * (recherche binaire O(log n) côté runtime). */
typedef struct {
    uint32_t codepoint;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    int16_t  xoffset;
    int16_t  yoffset;
    uint16_t xadvance;
    uint16_t _pad;
} FontGlyph;

/* Taille totale : 12 octets. Table triée par (first, second) croissants. */
typedef struct {
    uint32_t first;
    uint32_t second;
    int16_t  amount;
    int16_t  _pad;
} FontKerningPair;

#pragma pack(pop)

_Static_assert(sizeof(FontBinaryHeader) == 64, "FontBinaryHeader doit faire 64 octets");
_Static_assert(sizeof(FontGlyph) == 20, "FontGlyph doit faire 20 octets");
_Static_assert(sizeof(FontKerningPair) == 12, "FontKerningPair doit faire 12 octets");

#endif /* FONT_BINARY_FORMAT_H */

