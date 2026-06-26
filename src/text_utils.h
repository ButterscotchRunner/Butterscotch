#pragma once

#include "common.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "data_win.h"
#include "runner.h"
#include "utils.h"

// ===[ Text Utility Functions ]===
// Platform-agnostic text measurement and processing helpers.
// Used by both the renderer (for drawing text) and the VM (for string_width/string_height).

static inline FontGlyph* TextUtils_findGlyph(Font* font, uint16_t ch) {
    // Fast path: ASCII codepoints go through a direct LUT, skipping the linear scan.
    if (128 > ch) return font->glyphLUT[ch];
    repeat(font->glyphCount, i) {
        if (font->glyphs[i].character == ch) return &font->glyphs[i];
    }
    return nullptr;
}

static inline float TextUtils_getKerningOffset(FontGlyph* glyph, uint16_t nextCh) {
    repeat(glyph->kerningCount, k) {
        if (glyph->kerning[k].character == (int16_t) nextCh) {
            return glyph->kerning[k].shiftModifier;
        }
    }
    return 0;
}

// Decodes a single UTF-8 codepoint from str at *pos, advances *pos past the consumed bytes.
// Returns the codepoint as uint16_t (sufficient for BMP glyphs). Returns 0xFFFD for invalid sequences.
static inline uint16_t TextUtils_decodeUtf8(const char* str, int32_t len, int32_t* pos) {
    uint8_t b = (uint8_t) str[*pos];
    if (128 > b) {
        // ASCII (0xxxxxxx)
        (*pos)++;
        return b;
    } else if ((b & 0xE0) == 0xC0) {
        // 2-byte sequence (110xxxxx 10xxxxxx)
        if (len > *pos + 1 && ((uint8_t) str[*pos + 1] & 0xC0) == 0x80) {
            uint16_t cp = ((b & 0x1F) << 6) | ((uint8_t) str[*pos + 1] & 0x3F);
            *pos += 2;
            return cp;
        }
    } else if ((b & 0xF0) == 0xE0) {
        // 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx)
        if (len > *pos + 2 && ((uint8_t) str[*pos + 1] & 0xC0) == 0x80 && ((uint8_t) str[*pos + 2] & 0xC0) == 0x80) {
            uint16_t cp = ((b & 0x0F) << 12) | (((uint8_t) str[*pos + 1] & 0x3F) << 6) | ((uint8_t) str[*pos + 2] & 0x3F);
            *pos += 3;
            return cp;
        }
    } else if ((b & 0xF8) == 0xF0) {
        // 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx) - truncated to uint16_t
        if (len > *pos + 3 && ((uint8_t) str[*pos + 1] & 0xC0) == 0x80 && ((uint8_t) str[*pos + 2] & 0xC0) == 0x80 && ((uint8_t) str[*pos + 3] & 0xC0) == 0x80) {
            *pos += 4;
            return 0xFFFD; // Beyond BMP, return replacement character
        }
    }
    // Invalid or truncated sequence
    (*pos)++;
    return 0xFFFD;
}

static inline int32_t TextUtils_utf8AdvanceCodepoints(const char* str, int32_t byteLen, int32_t codepointsToSkip) {
    int32_t pos = 0;
    while (pos < byteLen && codepointsToSkip > 0) {
        pos++;
        while (pos < byteLen && ((uint8_t)str[pos] & 0xC0) == 0x80) {
            pos++;
        }
        codepointsToSkip--;
    }
    return pos;
}

static inline int32_t TextUtils_utf8CodepointCount(const char* str, int32_t byteLen) {
    int32_t count = 0;
    for (int32_t i = 0; i < byteLen; i++) {
        if (((uint8_t)str[i] & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

static inline int32_t TextUtils_utf8EncodeCodepoint(uint32_t cp, char* out) {
    if (cp <= 0x7FU) {
        out[0] = (char) cp;
        return 1;
    }
    if (cp <= 0x7FFU) {
        out[0] = (char) (0xC0U | (cp >> 6));
        out[1] = (char) (0x80U | (cp & 0x3FU));
        return 2;
    }
    if (cp <= 0xFFFFU) {
        out[0] = (char) (0xE0U | (cp >> 12));
        out[1] = (char) (0x80U | ((cp >> 6) & 0x3FU));
        out[2] = (char) (0x80U | (cp & 0x3FU));
        return 3;
    }
    if (cp <= 0x10FFFFU) {
        out[0] = (char) (0xF0U | (cp >> 18));
        out[1] = (char) (0x80U | ((cp >> 12) & 0x3FU));
        out[2] = (char) (0x80U | ((cp >> 6) & 0x3FU));
        out[3] = (char) (0x80U | (cp & 0x3FU));
        return 4;
    }
    return 0;
}

// Line stride used for multi-line text. Matches HTML5 runner behavior:
// - When `linesep` is not provided to draw_text, it defaults to `font.TextHeight('M')`
//   which is `max_glyph_height * scaleY`. We apply scaleY via the transform matrix already,
//   so we return the raw max glyph height here.
// - Falls back to emSize only if the font has no glyphs recorded.
static inline float TextUtils_lineStride(Font* font) {
    if (font->maxGlyphHeight > 0) return (float) font->maxGlyphHeight;
    return font->emSize;
}

static inline float TextUtils_measureLineWidth(Font* font, const char* line, int32_t len) {
    float width = 0;
    int32_t pos = 0;
    uint16_t ch = 0;
    bool hasCh = false;
    if (len > pos) {
        ch = TextUtils_decodeUtf8(line, len, &pos);
        hasCh = true;
    }

    while (hasCh) {
        FontGlyph* glyph = TextUtils_findGlyph(font, ch);

        // Decode the next codepoint once - reused for kerning AND as next iteration's ch
        uint16_t nextCh = 0;
        bool hasNext = len > pos;
        if (hasNext) nextCh = TextUtils_decodeUtf8(line, len, &pos);

        if (glyph != nullptr) {
            width += glyph->shift;
            if (hasNext) width += TextUtils_getKerningOffset(glyph, nextCh);
        }

        ch = nextCh;
        hasCh = hasNext;
    }
    return width;
}

// Result of GML text preprocessing. If owning is true, the caller must free the text pointer.
typedef struct {
    const char* text;
    bool owning;
} PreprocessedText;

// Frees the text pointer if it is owning.
static inline void PreprocessedText_free(PreprocessedText pt) {
    if (pt.owning) free((char*) pt.text);
}

// Preprocesses GML text: converts unescaped # to \n, and \# to literal #.
// Uses a fused single-pass approach: scans for # and only allocates if one is found.
static inline PreprocessedText TextUtils_preprocessGmlText(const char* text) {
    PreprocessedText ret = {0};
    int32_t len = (int32_t) strlen(text);

    // Scan until we find a #
    for (int32_t i = 0; len > i; i++) {
        if (text[i] == '#') {
            // Found one - allocate and process from here
            char* result = (char *)safeMalloc(len + 1);
            memcpy(result, text, i);
            int32_t out = i;

            // Check if the # is escaped (\#)
            if (out > 0 && result[out - 1] == '\\') {
                result[out - 1] = '#';
            } else {
                result[out++] = '\n';
            }

            // Process the rest of the string
            for (int32_t j = i + 1; len > j; j++) {
                if (text[j] == '#') {
                    if (out > 0 && result[out - 1] == '\\') {
                        result[out - 1] = '#';
                    } else {
                        result[out++] = '\n';
                    }
                } else {
                    result[out++] = text[j];
                }
            }
            result[out] = '\0';
            ret.text = result;
            ret.owning = true;
            return ret;
        }
    }

    // No # found, return original pointer without allocating
    ret.text = text;
    ret.owning = false;
    return ret;
}

// Preprocess GML text ONLY if the runner is not GameMaker: Studio 2
static inline PreprocessedText TextUtils_preprocessGmlTextIfNeeded(Runner* runner, const char* text) {
    if (DataWin_isVersionAtLeast(runner->dataWin, 2, 0, 0, 0)) {
        PreprocessedText ret = {0};
        ret.text = text;
        ret.owning = false;
        return ret;
    }
    return TextUtils_preprocessGmlText(text);
}

// Returns true if c is \r or \n
static inline bool TextUtils_isNewlineChar(char c) {
    return c == '\n' || c == '\r';
}

// Returns true if c is ' ' or \t
static inline bool TextUtils_isWhitespaceChar(char c) {
    return c == ' ' || c == '\t';
}

// Counts the number of lines in preprocessed text, treating \r\n and \n\r as single breaks
static inline int32_t TextUtils_countLines(const char* text, int32_t len) {
    int32_t count = 1;
    for (int32_t i = 0; len > i; i++) {
        if (TextUtils_isNewlineChar(text[i])) {
            count++;
            // Treat \r\n or \n\r as a single line break
            if (len > i + 1 && TextUtils_isNewlineChar(text[i + 1]) && text[i] != text[i + 1]) {
                i++;
            }
        }
    }
    return count;
}

// Advances lineStart past the newline at lineEnd, treating \r\n and \n\r as single breaks
static inline int32_t TextUtils_skipNewline(const char* text, int32_t lineEnd, int32_t textLen) {
    int32_t next = lineEnd + 1;
    if (textLen > next && TextUtils_isNewlineChar(text[next]) && text[lineEnd] != text[next]) {
        next++;
    }
    return next;
}

// Port of yyFontManager.prototype.Split_TextBlock from GameMaker-HTML5 to C.
// Pass "0 > maxWidth" to disable wrapping (returns the original pointer non-owning).
static inline PreprocessedText TextUtils_wrapText(Font* font, const char* text, int32_t maxWidth) {
    PreprocessedText ret = {0};
    int32_t len = 0;
    if (text != nullptr)
        len = (int32_t) strlen(text);

    if (0 == len) {
        ret.text = text;
        ret.owning = false;
        return ret;
    }

    int32_t linewidth = (0 > maxWidth) ? 10000000 : maxWidth; // means nothing will "wrap"

    // Worst case: each byte becomes itself plus a '\n' separator.
    char* out = (char *)safeMalloc((size_t) len * 2 + 1);
    int32_t outLen = 0;
    bool wroteAny = false;

    ret.text = out;
    ret.owning = true;

    // put newlines in
    const char* pNew = text;
    char lastChar = pNew[0];
    int32_t start = 0;
    int32_t end = 0;

    while (len > start) {
        float total = 0.0f;

        // If width < 0 (i.e. no wrapping required), then we DON'T strip spaces from the start... we just copy it!  (sounds wrong.. but its what they do...)
        if (linewidth == 10000000) {
            while (len > end && pNew[end] != '\n' && pNew[end] != '\r') {
                end++;
                if (len > end) lastChar = pNew[end];
                else lastChar = '\0';
            }
            char endByte = (len > end) ? pNew[end] : '\0';

            if (lastChar == '\n' && endByte == '\r') { end++; continue; } // ignore, we've already split the line on #10
            if (lastChar == '\r' && endByte == '\n') { end++; continue; } // ignore, we've already split the line on #13

            lastChar = endByte;

            // add into our list...
            if (wroteAny) out[outLen++] = '\n';
            memcpy(out + outLen, pNew + start, (size_t) (end - start));
            outLen += end - start;
            wroteAny = true;
        } else {
            // Skip leading whitespace
            while (len > end && (float) linewidth > total) {
                if (pNew[end] != ' ') break;
                FontGlyph* spGlyph = TextUtils_findGlyph(font, (uint16_t) ' ');
                total += (spGlyph != nullptr) ? (float) spGlyph->shift : 0.0f;
                end++;
            }

            // Loop through string and get the number of chars that will fit in the line.
            while (len > end && (float) linewidth > total) {
                if (pNew[end] == '\n') break; // if we hit a newline, then "break" here...
                int32_t tentative = end;
                uint16_t cp = TextUtils_decodeUtf8(pNew, len, &tentative); // advance `end` by one codepoint
                FontGlyph* glyph = TextUtils_findGlyph(font, cp);
                float size = (glyph != nullptr) ? (float) glyph->shift : 0.0f; // width of character
                float newTotal = total + size;
                // Won't fit, bail out!
                if (newTotal > (float) linewidth) break;
                // It fits :3
                total = newTotal;
                end = tentative;
            }

            // END of line
            if (len > end && pNew[end] == '\n') {
                if (wroteAny) out[outLen++] = '\n';
                memcpy(out + outLen, pNew + start, (size_t) (end - start));
                outLen += end - start;
                wroteAny = true;
            } else {
                // NOT a new line, but we didn't move on... fatal error. Probably a single char doesn't even fit!
                if (end == start) {
                    out[outLen] = '\0';
                    return ret;
                }

                // If we don't END on a "space", OR if the next character isn't a space AS WELL.
                // then backtrack to the start of the last "word"
                if (end != len) {
                    // This replaces the "if ((pNew[end] != whitespace) || (pNew[end] != whitespace && pNew[end + 1] != whitespace))" check
                    bool nextNotSpace = (end + 1 >= len) || (pNew[end + 1] != ' ');
                    if ((pNew[end] != ' ') || (pNew[end] != ' ' && nextNotSpace)) {
                        int32_t e = end;
                        while (e > start) {
                            e--;
                            if (pNew[e] == ' ') break; // FOUND start of word
                        }

                        if (e != start) {
                            end = e;
                        } else {
                            // This is where we diverge from the GameMaker-HTML5 behavior to match how the original runner works
                            // The GameMaker-HTML5 runner does NOT wrap if it doesn't fit AND none of the characters are space, and we WANT that
                        }
                    }
                }
                int32_t _end = end;
                if (_end > start) {
                    while (_end > 0 && pNew[_end - 1] == ' ') _end--;
                }
                //  else if (end == start) // if we're back to the START of the string... look for the next space - or string end.
                //  {
                //      while (pNew[end] != whitespace && end < len)
                //          end++;
                //  }

                if (_end != start) {
                    if (wroteAny) out[outLen++] = '\n';
                    memcpy(out + outLen, pNew + start, (size_t) (_end - start));
                    outLen += _end - start;
                    wroteAny = true;
                }
            }
        }
        start = ++end;
    }
    out[outLen] = '\0';
    return ret;
}

static inline char* TextUtils_trimTrailingWhitespace(char* str) {
    size_t len = strlen(str);
    while (len > 0 && (TextUtils_isWhitespaceChar(str[len - 1]) || TextUtils_isNewlineChar(str[len - 1]))) {
        len--;
    }
    str[len] = '\0';
    return str;
}
