#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ENC_UTF8,
    ENC_UTF8_BOM,
    ENC_UTF16_LE,
    ENC_UTF16_BE,
    ENC_UTF16_LE_BOM,
    ENC_UTF16_BE_BOM,
    ENC_UTF32_LE,
    ENC_UTF32_BE,
    ENC_UTF32_LE_BOM,
    ENC_UTF32_BE_BOM,
    ENC_UNKNOWN
} Encoding;

static int starts_with(const char *a, const char *b) { return strcmp(a,b)==0; }

static Encoding parse_encoding(const char *s) {
    if (starts_with(s, "UTF-8")) return ENC_UTF8;
    if (starts_with(s, "UTF-8-BOM")) return ENC_UTF8_BOM;
    if (starts_with(s, "UTF-16-LE")) return ENC_UTF16_LE;
    if (starts_with(s, "UTF-16-BE")) return ENC_UTF16_BE;
    if (starts_with(s, "UTF-16-LE-BOM")) return ENC_UTF16_LE_BOM;
    if (starts_with(s, "UTF-16-BE-BOM")) return ENC_UTF16_BE_BOM;
    if (starts_with(s, "UTF-32-LE")) return ENC_UTF32_LE;
    if (starts_with(s, "UTF-32-BE")) return ENC_UTF32_BE;
    if (starts_with(s, "UTF-32-LE-BOM")) return ENC_UTF32_LE_BOM;
    if (starts_with(s, "UTF-32-BE-BOM")) return ENC_UTF32_BE_BOM;
    return ENC_UNKNOWN;
}

static unsigned char *read_file(const char *path, size_t *out_n) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    fseek(fp, 0, SEEK_SET);
    unsigned char *buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    *out_n = n;
    return buf;
}

static void write_file(const char *path, const unsigned char *buf, size_t n) {
    FILE *fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "chgenc: cannot open output %s\n", path); exit(1); }
    fwrite(buf, 1, n, fp);
    fclose(fp);
}

// Decode to a vector of code points (U+0000..U+10FFFF). Invalid sequences -> U+FFFD.
static uint32_t *decode(const unsigned char *b, size_t n, Encoding enc, size_t *out_len) {
    uint32_t *out = NULL; size_t cap = 0, len = 0; size_t i = 0;
    // Skip BOM for BOM encodings on input, if present
    if (enc == ENC_UTF8 || enc == ENC_UTF8_BOM) {
        if (n >= 3 && b[0]==0xEF && b[1]==0xBB && b[2]==0xBF) { i = 3; }
    } else if (enc == ENC_UTF16_LE || enc == ENC_UTF16_LE_BOM) {
        if (n >= 2 && b[0]==0xFF && b[1]==0xFE) { i = 2; }
    } else if (enc == ENC_UTF16_BE || enc == ENC_UTF16_BE_BOM) {
        if (n >= 2 && b[0]==0xFE && b[1]==0xFF) { i = 2; }
    } else if (enc == ENC_UTF32_LE || enc == ENC_UTF32_LE_BOM) {
        if (n >= 4 && b[0]==0xFF && b[1]==0xFE && b[2]==0x00 && b[3]==0x00) { i = 4; }
    } else if (enc == ENC_UTF32_BE || enc == ENC_UTF32_BE_BOM) {
        if (n >= 4 && b[0]==0x00 && b[1]==0x00 && b[2]==0xFE && b[3]==0xFF) { i = 4; }
    }

    #define PUSH(cp) do { if (len+1 > cap) { size_t nc = cap?cap*2:1024; uint32_t *nb=(uint32_t*)realloc(out,nc*sizeof(uint32_t)); if(!nb){free(out);return NULL;} out=nb; cap=nc;} out[len++]=(cp); } while(0)

    switch (enc) {
        case ENC_UTF8:
        case ENC_UTF8_BOM: {
            while (i < n) {
                unsigned char c = b[i++];
                if (c < 0x80) { PUSH(c); continue; }
                if ((c & 0xE0) == 0xC0) {
                    if (i+1 > n) { PUSH(0xFFFD); break; }
                    unsigned char c1=b[i++]; if ((c1&0xC0)!=0x80) { PUSH(0xFFFD); continue; }
                    uint32_t cp = ((c & 0x1F)<<6) | (c1 & 0x3F);
                    if (cp < 0x80) cp = 0xFFFD; // overlong
                    PUSH(cp);
                } else if ((c & 0xF0) == 0xE0) {
                    if (i+2 > n) { PUSH(0xFFFD); break; }
                    unsigned char c1=b[i++], c2=b[i++]; if ((c1&0xC0)!=0x80 || (c2&0xC0)!=0x80) { PUSH(0xFFFD); continue; }
                    uint32_t cp = ((c & 0x0F)<<12) | ((c1 & 0x3F)<<6) | (c2 & 0x3F);
                    if (cp < 0x800) cp = 0xFFFD; // overlong
                    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD; // surrogate
                    PUSH(cp);
                } else if ((c & 0xF8) == 0xF0) {
                    if (i+3 > n) { PUSH(0xFFFD); break; }
                    unsigned char c1=b[i++], c2=b[i++], c3=b[i++]; if ((c1&0xC0)!=0x80 || (c2&0xC0)!=0x80 || (c3&0xC0)!=0x80) { PUSH(0xFFFD); continue; }
                    uint32_t cp = ((c & 0x07)<<18) | ((c1 & 0x3F)<<12) | ((c2 & 0x3F)<<6) | (c3 & 0x3F);
                    if (cp < 0x10000 || cp > 0x10FFFF) cp = 0xFFFD;
                    PUSH(cp);
                } else {
                    PUSH(0xFFFD);
                }
            }
            break;
        }
        case ENC_UTF16_LE:
        case ENC_UTF16_LE_BOM: {
            while (i+1 < n) {
                uint16_t unit = (uint16_t)(b[i] | (b[i+1]<<8)); i += 2;
                if (unit >= 0xD800 && unit <= 0xDBFF) {
                    if (i+1 >= n) { PUSH(0xFFFD); break; }
                    uint16_t unit2 = (uint16_t)(b[i] | (b[i+1]<<8)); i += 2;
                    if (unit2 < 0xDC00 || unit2 > 0xDFFF) { PUSH(0xFFFD); continue; }
                    uint32_t cp = 0x10000 + (((unit - 0xD800) << 10) | (unit2 - 0xDC00));
                    PUSH(cp);
                } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                    PUSH(0xFFFD);
                } else {
                    PUSH(unit);
                }
            }
            break;
        }
        case ENC_UTF16_BE:
        case ENC_UTF16_BE_BOM: {
            while (i+1 < n) {
                uint16_t unit = (uint16_t)((b[i]<<8) | b[i+1]); i += 2;
                if (unit >= 0xD800 && unit <= 0xDBFF) {
                    if (i+1 >= n) { PUSH(0xFFFD); break; }
                    uint16_t unit2 = (uint16_t)((b[i]<<8) | b[i+1]); i += 2;
                    if (unit2 < 0xDC00 || unit2 > 0xDFFF) { PUSH(0xFFFD); continue; }
                    uint32_t cp = 0x10000 + (((unit - 0xD800) << 10) | (unit2 - 0xDC00));
                    PUSH(cp);
                } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                    PUSH(0xFFFD);
                } else {
                    PUSH(unit);
                }
            }
            break;
        }
        case ENC_UTF32_LE:
        case ENC_UTF32_LE_BOM: {
            while (i+3 < n) {
                uint32_t cp = (uint32_t)(b[i] | (b[i+1]<<8) | (b[i+2]<<16) | ((uint32_t)b[i+3]<<24)); i += 4;
                if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                PUSH(cp);
            }
            break;
        }
        case ENC_UTF32_BE:
        case ENC_UTF32_BE_BOM: {
            while (i+3 < n) {
                uint32_t cp = (uint32_t)(((uint32_t)b[i]<<24) | (b[i+1]<<16) | (b[i+2]<<8) | b[i+3]); i += 4;
                if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                PUSH(cp);
            }
            break;
        }
        default:
            return NULL;
    }
    #undef PUSH
    *out_len = len;
    return out;
}

static unsigned char *encode(const uint32_t *cp, size_t len, Encoding enc, size_t *out_n) {
    unsigned char *out = NULL; size_t cap = 0, n = 0;
    #define EMIT(byte) do { if (n+1 > cap) { size_t nc = cap?cap*2:1024; unsigned char *nb=(unsigned char*)realloc(out,nc); if(!nb){free(out);return NULL;} out=nb; cap=nc;} out[n++]=(unsigned char)(byte); } while(0)
    // Write BOM if BOM variant selected
    if (enc == ENC_UTF8_BOM) { EMIT(0xEF); EMIT(0xBB); EMIT(0xBF); enc = ENC_UTF8; }
    else if (enc == ENC_UTF16_LE_BOM) { EMIT(0xFF); EMIT(0xFE); enc = ENC_UTF16_LE; }
    else if (enc == ENC_UTF16_BE_BOM) { EMIT(0xFE); EMIT(0xFF); enc = ENC_UTF16_BE; }
    else if (enc == ENC_UTF32_LE_BOM) { EMIT(0xFF); EMIT(0xFE); EMIT(0x00); EMIT(0x00); enc = ENC_UTF32_LE; }
    else if (enc == ENC_UTF32_BE_BOM) { EMIT(0x00); EMIT(0x00); EMIT(0xFE); EMIT(0xFF); enc = ENC_UTF32_BE; }

    switch (enc) {
        case ENC_UTF8: {
            for (size_t i = 0; i < len; i++) {
                uint32_t c = cp[i];
                if (c <= 0x7F) { EMIT(c); }
                else if (c <= 0x7FF) { EMIT(0xC0 | (c>>6)); EMIT(0x80 | (c & 0x3F)); }
                else if (c <= 0xFFFF) {
                    if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
                    EMIT(0xE0 | (c>>12)); EMIT(0x80 | ((c>>6) & 0x3F)); EMIT(0x80 | (c & 0x3F));
                } else if (c <= 0x10FFFF) {
                    EMIT(0xF0 | (c>>18)); EMIT(0x80 | ((c>>12) & 0x3F)); EMIT(0x80 | ((c>>6) & 0x3F)); EMIT(0x80 | (c & 0x3F));
                } else {
                    // replace invalid with U+FFFD
                    EMIT(0xEF); EMIT(0xBB); EMIT(0xBF); // encode U+FFFD = EF BB BF? Actually U+FFFD in UTF-8 is EF BF BD
                    // Fix:
                    EMIT(0xEF); EMIT(0xBF); EMIT(0xBD);
                }
            }
            break;
        }
        case ENC_UTF16_LE: {
            for (size_t i = 0; i < len; i++) {
                uint32_t c = cp[i];
                if (c <= 0xFFFF) {
                    if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
                    EMIT(c & 0xFF); EMIT((c>>8) & 0xFF);
                } else {
                    c -= 0x10000; uint16_t hi = 0xD800 + (uint16_t)(c >> 10); uint16_t lo = 0xDC00 + (uint16_t)(c & 0x3FF);
                    EMIT(hi & 0xFF); EMIT((hi>>8) & 0xFF); EMIT(lo & 0xFF); EMIT((lo>>8) & 0xFF);
                }
            }
            break;
        }
        case ENC_UTF16_BE: {
            for (size_t i = 0; i < len; i++) {
                uint32_t c = cp[i];
                if (c <= 0xFFFF) {
                    if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
                    EMIT((c>>8) & 0xFF); EMIT(c & 0xFF);
                } else {
                    c -= 0x10000; uint16_t hi = 0xD800 + (uint16_t)(c >> 10); uint16_t lo = 0xDC00 + (uint16_t)(c & 0x3FF);
                    EMIT((hi>>8) & 0xFF); EMIT(hi & 0xFF); EMIT((lo>>8) & 0xFF); EMIT(lo & 0xFF);
                }
            }
            break;
        }
        case ENC_UTF32_LE: {
            for (size_t i = 0; i < len; i++) {
                uint32_t c = cp[i];
                EMIT(c & 0xFF); EMIT((c>>8) & 0xFF); EMIT((c>>16) & 0xFF); EMIT((c>>24) & 0xFF);
            }
            break;
        }
        case ENC_UTF32_BE: {
            for (size_t i = 0; i < len; i++) {
                uint32_t c = cp[i];
                EMIT((c>>24) & 0xFF); EMIT((c>>16) & 0xFF); EMIT((c>>8) & 0xFF); EMIT(c & 0xFF);
            }
            break;
        }
        default:
            free(out); return NULL;
    }
    #undef EMIT
    *out_n = n;
    return out;
}

static void print_usage(void) {
    printf("Usage: chgenc <input-encoding> <output-encoding> <infile> <outfile>\n");
}

int main(int argc, char *argv[]) {
    if (argc != 5) { print_usage(); return 1; }
    Encoding in_enc = parse_encoding(argv[1]);
    Encoding out_enc = parse_encoding(argv[2]);
    if (in_enc == ENC_UNKNOWN || out_enc == ENC_UNKNOWN) { fprintf(stderr, "chgenc: unknown encoding\n"); return 1; }
    size_t n = 0; unsigned char *buf = read_file(argv[3], &n);
    if (!buf) { fprintf(stderr, "chgenc: cannot read %s\n", argv[3]); return 1; }
    size_t len = 0; uint32_t *codepoints = decode(buf, n, in_enc, &len);
    free(buf);
    if (!codepoints) { fprintf(stderr, "chgenc: decode failed\n"); return 1; }
    size_t out_n = 0; unsigned char *out = encode(codepoints, len, out_enc, &out_n);
    free(codepoints);
    if (!out) { fprintf(stderr, "chgenc: encode failed\n"); return 1; }
    write_file(argv[4], out, out_n);
    free(out);
    return 0;
}
