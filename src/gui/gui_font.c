/*
 * Panel lettering. raylib's built-in font is a 10-pixel bitmap, which turns
 * blocky once the boards are drawn at more than 1920x1080. Instead the
 * lettering is DejaVu Sans Bold (compiled in from assets/fonts), rasterised
 * for the current display scale with mipmaps, and squeezed horizontally to the
 * width the built-in font would have taken: that keeps every layout that was
 * measured with MeasureText, and gives the condensed look of engraved
 * lamicoid lettering.
 */
#define GUI_FONT_IMPL
#include "gui.h"
#include "rlgl.h"

#include <math.h>
#include <stdlib.h>

extern const unsigned char gui_font_ttf[];
extern const unsigned int gui_font_ttf_len;

static Font font;
static int have_font;
static int loaded_px;

void gui_font_load(float scale)
{
    if (getenv("RMELM_BITMAP_FONT")) return;
    /* rasterise at twice the common 10-unit size, so every size used on the
     * boards is a downscale through the mipmaps */
    int px = (int)ceilf(24.0f * fmaxf(scale, 0.5f));
    if (px == loaded_px) return;
    gui_font_unload();
    font = LoadFontFromMemory(".ttf", gui_font_ttf, (int)gui_font_ttf_len, px, NULL, 0);
    have_font = font.texture.id != 0 && font.glyphCount > 0;
    if (have_font) {
        GenTextureMipmaps(&font.texture);
        SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
    }
    loaded_px = px;
}

void gui_font_unload(void)
{
    if (have_font) UnloadFont(font);
    have_font = 0;
    loaded_px = 0;
}

void gui_text(const char *s, int x, int y, int size, Color c)
{
    if (!have_font) {
        DrawText(s, x, y, size, c);
        return;
    }
    /* cap height and baseline close to the built-in font's */
    float fs = size * 1.15f, sp = size * 0.05f;
    float want = (float)MeasureText(s, size);
    float got = MeasureTextEx(font, s, fs, sp).x;
    float sx = got > 0 ? want / got : 1.0f;
    if (sx < 0.6f) sx = 0.6f;
    if (sx > 1.1f) sx = 1.1f;
    rlPushMatrix();
    rlTranslatef((float)x, (float)y - size * 0.12f, 0);
    rlScalef(sx, 1, 1);
    DrawTextEx(font, s, (Vector2){0, 0}, fs, sp, c);
    rlPopMatrix();
}
