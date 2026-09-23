/*
 * The 1978 panel widget kit: painted steel, engraved lamicoid nameplates and
 * operator-added label tape, red LED readouts, 250-degree switchboard
 * meters and vertical edgewise meters with zone bands, indicating lights and
 * legend lamps, control switches on square escutcheons (pistol grip and
 * J-handle), key switches, collared and guarded pushbuttons, Bailey-style
 * manual/automatic control stations, rotary selectors, mimic pipes with flow
 * arrows and component symbols, and the two kinds of chart recorder.
 */
#include "gui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

Color dimlens(Color c)
{
    return (Color){(unsigned char)(c.r * 0.28f + 44), (unsigned char)(c.g * 0.28f + 42),
                   (unsigned char)(c.b * 0.28f + 38), 255};
}

Color alpha(Color c, unsigned char a) { return (Color){c.r, c.g, c.b, a}; }

Color mixc(Color a, Color b, float t)
{
    return (Color){(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                   (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

/* raylib culls one winding; draw both so callers need not care */
void tri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

int blink_fast(void) { return ((int)(GetTime() * 3)) & 1; }
int blink_slow(void) { return ((int)(GetTime() * 1)) & 1; }

/* a click on r, honouring the board's control transfer */
int clicked(Rectangle r)
{
    return input_ok && CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* ---- per-widget memory, found by position on the canvas ----------------------- */
#define NMEM 1024
static struct { int key; float f; double t; } mem[NMEM];

static int *mem_slot(float x, float y, int kind, float **f, double **t)
{
    int key = (((int)x) << 12) ^ ((int)y) ^ (kind << 24) ^ 0x5a5a5a;
    if (key == 0) key = 1;
    unsigned h = (unsigned)key * 2654435761u;
    for (int i = 0; i < NMEM; i++) {
        int s = (int)((h + i) % NMEM);
        if (mem[s].key == key || mem[s].key == 0) {
            if (mem[s].key == 0) {
                mem[s].key = key;
                mem[s].f = 0;
                mem[s].t = 0;
            }
            if (f) *f = &mem[s].f;
            if (t) *t = &mem[s].t;
            return &mem[s].key;
        }
    }
    if (f) *f = &mem[0].f;
    if (t) *t = &mem[0].t;
    return &mem[0].key;
}

int held_repeat(Rectangle r)
{
    double *next;
    mem_slot(r.x, r.y, 3, NULL, &next);
    if (!input_ok || !CheckCollisionPointRec(GetMousePosition(), r)) return 0;
    double now = GetTime();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        *next = now + 0.4;
        return 1;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && now >= *next) {
        *next = now + 0.1;
        return 1;
    }
    return 0;
}

/* ---- text and labels ------------------------------------------------------------ */
void text(const char *s, float x, float y, int size, Color c) { DrawText(s, (int)x, (int)y, size, c); }

void textf(float x, float y, int size, Color c, const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    DrawText(b, (int)x, (int)y, size, c);
}

void ctext(const char *s, float cx, float y, int size, Color c)
{
    DrawText(s, (int)(cx - MeasureText(s, size) / 2.0f), (int)y, size, c);
}

void screw(float x, float y)
{
    DrawCircleV((Vector2){x, y}, 4, (Color){128, 138, 130, 255});
    DrawCircleLinesV((Vector2){x, y}, 4, INK);
    DrawLineEx((Vector2){x - 3, y + 2}, (Vector2){x + 3, y - 2}, 1, INK);
}

/* black engraved-plastic nameplate with two rivets (system and group labels) */
float plate(float x, float y, const char *s, int size)
{
    float tw = (float)MeasureText(s, size);
    Rectangle p = {x, y, tw + 24, size + 8.0f};
    DrawRectangleRec(p, (Color){22, 22, 20, 255});
    DrawRectangleLinesEx(p, 1, (Color){96, 96, 90, 255});
    DrawCircleV((Vector2){p.x + 5, p.y + p.height / 2}, 2, (Color){150, 150, 144, 255});
    DrawCircleV((Vector2){p.x + p.width - 5, p.y + p.height / 2}, 2, (Color){150, 150, 144, 255});
    text(s, x + 12, y + 4, size, (Color){234, 232, 222, 255});
    return p.width;
}

void plate_c(float cx, float y, const char *s, int size) { plate(cx - (MeasureText(s, size) + 24) / 2.0f, y, s, size); }

/* small engraved component label */
float tag(float x, float y, const char *s)
{
    float tw = (float)MeasureText(s, 10);
    Rectangle p = {x, y, tw + 10, 13};
    DrawRectangleRec(p, (Color){26, 26, 24, 255});
    DrawRectangleLinesEx(p, 1, (Color){74, 74, 70, 255});
    text(s, x + 5, y + 2, 10, (Color){236, 234, 226, 255});
    return p.width;
}

void tag_c(float cx, float y, const char *s) { tag(cx - (MeasureText(s, 10) + 10) / 2.0f, y, s); }

/* embossed label-maker tape: what operators stuck on afterwards */
float dymo(float x, float y, const char *s)
{
    float tw = (float)MeasureText(s, 10);
    DrawRectangleRec((Rectangle){x, y, tw + 8, 13}, (Color){24, 40, 96, 255});
    text(s, x + 4, y + 2, 10, (Color){236, 236, 236, 255});
    return tw + 8;
}

void dymo_c(float cx, float y, const char *s) { dymo(cx - (MeasureText(s, 10) + 8) / 2.0f, y, s); }

void steel(Rectangle r, const char *title)
{
    DrawRectangleRec((Rectangle){r.x - 1, r.y - 1, r.width + 2, r.height + 2}, (Color){18, 20, 18, 255});
    DrawRectangleRec(r, PAINT);
    DrawRectangleRec((Rectangle){r.x, r.y, r.width, 2}, PAINT_HI);
    DrawRectangleRec((Rectangle){r.x, r.y, 2, r.height}, PAINT_HI);
    DrawRectangleRec((Rectangle){r.x, r.y + r.height - 2, r.width, 2}, PAINT_LO);
    DrawRectangleRec((Rectangle){r.x + r.width - 2, r.y, 2, r.height}, PAINT_LO);
    screw(r.x + 8, r.y + 8);
    screw(r.x + r.width - 8, r.y + 8);
    screw(r.x + 8, r.y + r.height - 8);
    screw(r.x + r.width - 8, r.y + r.height - 8);
    if (title) plate_c(r.x + r.width / 2, r.y + 5, title, 12);
}

void desk(Rectangle d)
{
    DrawRectangleRec(d, DESK);
    DrawRectangleRec((Rectangle){d.x, d.y, d.width, 2}, PAINT_HI);
    DrawRectangleLinesEx(d, 1, BEZEL);
}

/* ---- 7-segment LED readouts ------------------------------------------------ */
static unsigned seg_bits(char ch)
{
    static const unsigned char D[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    if (ch >= '0' && ch <= '9') return D[ch - '0'];
    switch (ch) {
    case '-': return 0x40;
    case 'A': return 0x77;
    case 'b': return 0x7C;
    case 'C': return 0x39;
    case 'd': return 0x5E;
    case 'E': return 0x79;
    case 'F': return 0x71;
    case 'H': return 0x76;
    case 'L': return 0x38;
    case 'n': return 0x54;
    case 'o': return 0x5C;
    case 'P': return 0x73;
    case 'r': return 0x50;
    case 'S': return 0x6D;
    case 't': return 0x78;
    case 'U': return 0x3E;
    default: return 0;
    }
}

float seg_char(float x, float y, float h, char ch, Color on)
{
    Color off = {58, 14, 10, 255};
    float w = h * 0.5f, t = h * 0.11f, hh = h / 2;
    if (ch == '.') {
        DrawCircleV((Vector2){x + t, y + h - t * 0.6f}, t * 0.6f, on);
        return h * 0.22f;
    }
    if (ch == ':') {
        DrawCircleV((Vector2){x + h * 0.12f, y + h * 0.3f}, t * 0.55f, on);
        DrawCircleV((Vector2){x + h * 0.12f, y + h * 0.7f}, t * 0.55f, on);
        return h * 0.3f;
    }
    unsigned m = seg_bits(ch);
    Rectangle s[7] = {
        {x + t, y, w - 2 * t, t},
        {x + w - t, y + t, t, hh - 1.5f * t},
        {x + w - t, y + hh + 0.5f * t, t, hh - 1.5f * t},
        {x + t, y + h - t, w - 2 * t, t},
        {x, y + hh + 0.5f * t, t, hh - 1.5f * t},
        {x, y + t, t, hh - 1.5f * t},
        {x + t, y + hh - t / 2, w - 2 * t, t},
    };
    for (int i = 0; i < 7; i++) {
        if ((m >> i) & 1) {
            DrawRectangleRec((Rectangle){s[i].x - 1, s[i].y - 1, s[i].width + 2, s[i].height + 2}, alpha(on, 70));
            DrawRectangleRec(s[i], on);
        } else {
            DrawRectangleRec(s[i], off);
        }
    }
    return h * 0.68f;
}

float segw(const char *s, float h)
{
    float w = 0;
    for (; *s; s++) w += *s == '.' ? h * 0.22f : (*s == ':' ? h * 0.3f : h * 0.68f);
    return w;
}

/* LED window, right-justified in a field of ndig digits; returns its width */
float readout(float x, float y, float h, int ndig, const char *fmt, ...)
{
    char b[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    float sw = segw(b, h), fw = fmaxf(ndig * h * 0.68f, sw);
    DrawRectangleRec((Rectangle){x, y, fw + 12, h + 10}, BEZEL);
    DrawRectangleRec((Rectangle){x + 3, y + 3, fw + 6, h + 4}, (Color){20, 4, 4, 255});
    float sx = x + 6 + fw - sw;
    for (const char *q = b; *q; q++) sx += seg_char(sx, y + 5, h, *q, LED);
    return fw + 12;
}

/* ---- analog needle meter --------------------------------------------------- */
static float meter_ang(double v, double lo, double hi)
{
    double f = (v - lo) / (hi - lo);
    if (f < -0.03) f = -0.03;
    if (f > 1.03) f = 1.03;
    return (float)(-145.0 + 110.0 * f);
}

void meter(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj)
{
    meterl(r, label, v, lo, hi, red_lo, red_hi, nmaj, NULL);
}

void meterl(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj,
            const char *const *labs)
{
    DrawRectangleRec(r, BEZEL);
    Rectangle f = {r.x + 5, r.y + 5, r.width - 10, r.height - 10};
    DrawRectangleRec(f, FACE);
    float R = fminf(f.width * 0.55f, f.height * 0.74f);
    Vector2 c = {f.x + f.width / 2, f.y + 10 + R};
    if (red_hi > red_lo)
        DrawRing(c, R - 7, R, meter_ang(red_lo, lo, hi), meter_ang(red_hi, lo, hi), 24, (Color){210, 40, 30, 255});
    DrawRing(c, R - 1.2f, R, -145, -35, 48, INK);
    int nmin = nmaj * 5;
    for (int i = 0; i <= nmin; i++) {
        float a = (float)((-145.0 + 110.0 * i / nmin) * DEG2RAD);
        float len = i % 5 ? 4.0f : 9.0f;
        Vector2 p0 = {c.x + R * cosf(a), c.y + R * sinf(a)};
        Vector2 p1 = {c.x + (R - len) * cosf(a), c.y + (R - len) * sinf(a)};
        DrawLineEx(p0, p1, i % 5 ? 1.0f : 1.6f, INK);
        if (i % 5 == 0) {
            char b[16];
            if (labs) snprintf(b, sizeof b, "%s", labs[i / 5]);
            else snprintf(b, sizeof b, "%g", lo + (hi - lo) * i / nmin);
            Vector2 pt = {c.x + (R - 18) * cosf(a), c.y + (R - 18) * sinf(a)};
            ctext(b, pt.x, pt.y - 4, 10, INK);
        }
    }
    ctext(label, c.x, c.y - 20, 10, INK);
    float a = meter_ang(v, lo, hi) * DEG2RAD;
    Vector2 tip = {c.x + (R - 2) * cosf(a), c.y + (R - 2) * sinf(a)};
    DrawLineEx((Vector2){c.x + 2, c.y + 2}, (Vector2){tip.x + 2, tip.y + 2}, 2, alpha(BLACK, 50));
    DrawLineEx(c, tip, 2, (Color){16, 16, 16, 255});
    DrawRectangleRec((Rectangle){f.x, c.y - 4, f.width, f.y + f.height - c.y + 4}, (Color){60, 58, 52, 255});
    DrawCircleV(c, 6, (Color){20, 20, 20, 255});
    DrawRectangleGradientV((int)f.x, (int)f.y, (int)f.width, (int)(f.height * 0.35f), alpha(WHITE, 60), alpha(WHITE, 0));
}

/* 250-degree switchboard meter in a square case ("big look") */
void dial(Rectangle r, const char *label, double v, double lo, double hi, int nmaj, double g_lo, double g_hi,
          double r_lo, double r_hi, const char *const *labs)
{
    DrawRectangleRounded(r, 0.10f, 6, BEZEL);
    float R = fminf(r.width, r.height) / 2 - 6;
    Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
    DrawCircleV(c, R, FACE);
    const float a0 = 145, a1 = 395;
#define DA(val) (a0 + (a1 - a0) * (float)fmin(1.02, fmax(-0.02, ((val) - lo) / (hi - lo))))
    float Rs = R - 6;
    if (g_hi > g_lo) DrawRing(c, Rs - 6, Rs, DA(g_lo), DA(g_hi), 24, (Color){60, 160, 70, 255});
    if (r_hi > r_lo) DrawRing(c, Rs - 6, Rs, DA(r_lo), DA(r_hi), 24, (Color){210, 40, 30, 255});
    DrawRing(c, Rs - 0.8f, Rs + 0.4f, a0, a1, 48, INK);
    int nmin = nmaj * 5;
    for (int i = 0; i <= nmin; i++) {
        float a = (a0 + (a1 - a0) * i / nmin) * DEG2RAD;
        float len = i % 5 ? 4.0f : 8.0f;
        DrawLineEx((Vector2){c.x + Rs * cosf(a), c.y + Rs * sinf(a)},
                   (Vector2){c.x + (Rs - len) * cosf(a), c.y + (Rs - len) * sinf(a)}, i % 5 ? 1.0f : 1.6f, INK);
        if (i % 5 == 0) {
            char b[16];
            if (labs) snprintf(b, sizeof b, "%s", labs[i / 5]);
            else snprintf(b, sizeof b, "%g", lo + (hi - lo) * i / nmin);
            ctext(b, c.x + (Rs - 17) * cosf(a), c.y + (Rs - 17) * sinf(a) - 4, 10, INK);
        }
    }
    /* legend on the dial, below the scale ends, in two lines if it has a '\n' */
    const char *nl = strchr(label, '\n');
    float ly = c.y + 0.574f * (Rs - 17) + 8;
    if (nl) {
        char l1[40];
        snprintf(l1, sizeof l1, "%.*s", (int)(nl - label), label);
        ctext(l1, c.x, ly, 10, INK);
        ctext(nl + 1, c.x, ly + 11, 10, INK);
    } else {
        ctext(label, c.x, ly + 4, 10, INK);
    }
    float a = DA(v) * DEG2RAD;
#undef DA
    Vector2 tip = {c.x + (Rs - 3) * cosf(a), c.y + (Rs - 3) * sinf(a)};
    Vector2 tail = {c.x - 12 * cosf(a), c.y - 12 * sinf(a)};
    DrawLineEx((Vector2){tail.x + 2, tail.y + 2}, (Vector2){tip.x + 2, tip.y + 2}, 2.4f, alpha(BLACK, 50));
    DrawLineEx(tail, tip, 2.4f, (Color){16, 16, 16, 255});
    DrawCircleV(c, 5, (Color){16, 16, 16, 255});
    DrawCircleV((Vector2){c.x - R * 0.35f, c.y - R * 0.45f}, R * 0.35f, alpha(WHITE, 26));
}

/* vertical edgewise meter: a narrow window with the scale on the left and a
 * red pointer that slides up and down it; green normal band, red limit band */
void edgewz(Rectangle r, const char *label, double v, double lo, double hi, double g_lo, double g_hi, double red_lo,
            double red_hi, int nmaj, const char *const *labs)
{
    DrawRectangleRec(r, BEZEL);
    Rectangle f = {r.x + 4, r.y + 4, r.width - 8, r.height - 8};
    DrawRectangleRec(f, FACE);
    float y0 = f.y + f.height - 6, y1 = f.y + 6;
#define EY(val) (y0 + (y1 - y0) * (float)fmin(1.03, fmax(-0.03, ((val) - lo) / (hi - lo))))
    if (g_hi > g_lo) {
        float a = EY(g_hi), b = EY(g_lo);
        DrawRectangleRec((Rectangle){f.x + 1, a, 4, b - a}, (Color){60, 160, 70, 255});
    }
    if (red_hi > red_lo) {
        float a = EY(red_hi), b = EY(red_lo);
        DrawRectangleRec((Rectangle){f.x + 1, a, 4, b - a}, (Color){210, 40, 30, 255});
    }
    int nmin = nmaj * 5;
    for (int i = 0; i <= nmin; i++) {
        float y = y0 + (y1 - y0) * i / nmin;
        DrawLineEx((Vector2){f.x + 5, y}, (Vector2){f.x + 5 + (i % 5 ? 4.0f : 8.0f), y}, 1, INK);
        if (i % 5 == 0 && f.width >= 24) {
            char b[16];
            if (labs) snprintf(b, sizeof b, "%s", labs[i / 5]);
            else snprintf(b, sizeof b, "%g", lo + (hi - lo) * i / nmin);
            text(b, f.x + (f.width > 34 ? 15 : 11), y - 4, 10, INK);
        }
    }
    float py = EY(v);
#undef EY
    DrawRectangleRec((Rectangle){f.x + 3, py - 1.5f, f.width - 3, 3}, (Color){200, 24, 20, 255});
    tri((Vector2){f.x + f.width, py - 5}, (Vector2){f.x + f.width - 7, py}, (Vector2){f.x + f.width, py + 5},
        (Color){200, 24, 20, 255});
    DrawRectangleGradientH((int)f.x, (int)f.y, (int)(f.width * 0.5f), (int)f.height, alpha(WHITE, 50), alpha(WHITE, 0));
    if (label) {
        const char *nl = strchr(label, '\n');
        if (nl) {
            char l1[32];
            snprintf(l1, sizeof l1, "%.*s", (int)(nl - label), label);
            ctext(l1, r.x + r.width / 2, r.y + r.height + 3, 10, INK);
            ctext(nl + 1, r.x + r.width / 2, r.y + r.height + 14, 10, INK);
        } else {
            ctext(label, r.x + r.width / 2, r.y + r.height + 3, 10, INK);
        }
    }
}

void edgew(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj,
           const char *const *labs)
{
    edgewz(r, label, v, lo, hi, 0, 0, red_lo, red_hi, nmaj, labs);
}

/* ---- lamps and pushbuttons --------------------------------------------------- */
/* GE ET-16 style indicating light */
void lamp(float x, float y, float rad, Color lens, int lit)
{
    DrawCircleV((Vector2){x, y}, rad + 2.5f, BEZEL);
    if (lit) DrawCircleV((Vector2){x, y}, rad + 5, alpha(lens, 50));
    DrawCircleV((Vector2){x, y}, rad, lit ? lens : dimlens(lens));
    DrawCircleV((Vector2){x - rad * 0.35f, y - rad * 0.35f}, rad * 0.3f, alpha(WHITE, lit ? 170 : 60));
}

static void legend2(Rectangle in, const char *legend, Color tc)
{
    char l1[32], l2[32] = "";
    const char *nl = strchr(legend, '\n');
    if (nl) {
        snprintf(l1, sizeof l1, "%.*s", (int)(nl - legend), legend);
        snprintf(l2, sizeof l2, "%s", nl + 1);
        ctext(l1, in.x + in.width / 2, in.y + in.height / 2 - 11, 10, tc);
        ctext(l2, in.x + in.width / 2, in.y + in.height / 2 + 1, 10, tc);
    } else {
        ctext(legend, in.x + in.width / 2, in.y + in.height / 2 - 5, 10, tc);
    }
}

/* square illuminated pushbutton with an engraved legend */
int lampbutton(Rectangle r, const char *legend, Color lens, int lit)
{
    Vector2 m = GetMousePosition();
    int hover = input_ok && CheckCollisionPointRec(m, r);
    int down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (lit) DrawRectangleRec((Rectangle){r.x - 3, r.y - 3, r.width + 6, r.height + 6}, alpha(lens, 45));
    DrawRectangleRec(r, BEZEL);
    Rectangle in = {r.x + 3, r.y + 3 + (down ? 1.0f : 0.0f), r.width - 6, r.height - 6};
    DrawRectangleRec(in, lit ? lens : dimlens(lens));
    DrawRectangleRec((Rectangle){in.x, in.y, in.width, 2}, alpha(WHITE, lit ? 120 : 40));
    if (hover) DrawRectangleLinesEx(r, 1, (Color){160, 160, 150, 255});
    legend2(in, legend, lit ? (Color){20, 16, 12, 255} : (Color){26, 26, 24, 255});
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* legend light: an engraved lens that lights up (no button) */
void indicator(Rectangle r, const char *legend, Color lens, int lit)
{
    if (lit) DrawRectangleRec((Rectangle){r.x - 2, r.y - 2, r.width + 4, r.height + 4}, alpha(lens, 40));
    DrawRectangleRec(r, BEZEL);
    Rectangle in = {r.x + 2, r.y + 2, r.width - 4, r.height - 4};
    DrawRectangleRec(in, lit ? lens : mixc(lens, (Color){70, 70, 66, 255}, 0.62f));
    if (lit) DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)(in.height / 2), alpha(WHITE, 90), alpha(WHITE, 0));
    legend2(in, legend, lit ? (Color){18, 14, 10, 255} : (Color){36, 36, 32, 255});
}

/* annunciator window: white engraved lens, backlit in its priority colour */
void window(Rectangle t, const char *l1, const char *l2, Color lens, int lit)
{
    DrawRectangleRec(t, BEZEL);
    Rectangle in = {t.x + 2, t.y + 2, t.width - 4, t.height - 4};
    int white = lens.r > 240 && lens.g > 230;
    Color off = white ? (Color){196, 194, 184, 255} : mixc(lens, (Color){168, 164, 156, 255}, 0.72f);
    Color on = white ? (Color){255, 253, 238, 255} : mixc(lens, WHITE, 0.18f);
    DrawRectangleRec(in, lit ? on : off);
    if (lit) DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)(in.height / 2), alpha(WHITE, 110), alpha(WHITE, 0));
    Color tc = lit ? (Color){14, 12, 10, 255} : (Color){52, 50, 46, 255};
    if (l1) ctext(l1, in.x + in.width / 2, in.y + in.height / 2 - (l2 ? 11 : 5), 10, tc);
    if (l2) ctext(l2, in.x + in.width / 2, in.y + in.height / 2 + 1, 10, tc);
}

/* rotary selector: positions spread over an arc, click a legend to turn it */
int rotary(Vector2 c, float rad, int n, const char *const *leg, int cur, float a0, float a1, float lr)
{
    Vector2 m = GetMousePosition();
    int pick = -1;
    for (int i = 0; i < n; i++) {
        float a = (a0 + (a1 - a0) * i / (n - 1)) * DEG2RAD;
        Vector2 t = {c.x + lr * cosf(a), c.y + lr * sinf(a)};
        DrawLineEx((Vector2){c.x + (rad + 2) * cosf(a), c.y + (rad + 2) * sinf(a)},
                   (Vector2){c.x + (rad + 7) * cosf(a), c.y + (rad + 7) * sinf(a)}, 2, INK);
        int tw = MeasureText(leg[i], 10);
        Rectangle hit = {t.x - tw / 2.0f - 4, t.y - 8, tw + 8.0f, 16};
        int hv = input_ok && CheckCollisionPointRec(m, hit);
        if (hv) DrawRectangleRec(hit, alpha(WHITE, 60));
        ctext(leg[i], t.x, t.y - 5, 10, i == cur ? (Color){150, 20, 10, 255} : INK);
        if (hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) pick = i;
    }
    /* knob: black bakelite bar knob with a white line */
    DrawCircleV((Vector2){c.x + 2, c.y + 3}, rad, alpha(BLACK, 90));
    DrawCircleV(c, rad, (Color){24, 24, 22, 255});
    DrawRing(c, rad - 3, rad, 0, 360, 36, (Color){70, 70, 66, 255});
    float a = (a0 + (a1 - a0) * cur / (n - 1)) * DEG2RAD;
    Vector2 d = {cosf(a), sinf(a)};
    DrawLineEx((Vector2){c.x - d.x * rad * 0.9f, c.y - d.y * rad * 0.9f}, (Vector2){c.x + d.x * rad * 0.9f, c.y + d.y * rad * 0.9f},
               rad * 0.55f, (Color){40, 40, 38, 255});
    DrawLineEx((Vector2){c.x - d.x * (rad - 6) * 0.3f, c.y - d.y * (rad - 6) * 0.3f},
               (Vector2){c.x + d.x * (rad - 3), c.y + d.y * (rad - 3)}, 3, (Color){236, 234, 224, 255});
    return pick;
}

void tooltip(const char *tip, Rectangle within)
{
    Vector2 m = GetMousePosition();
    float tw = (float)MeasureText(tip, 10) + 10;
    float tx = fminf(m.x + 12, within.x + within.width - tw - 6), ty = m.y - 24;
    DrawRectangleRec((Rectangle){tx, ty, tw, 16}, (Color){244, 238, 214, 255});
    DrawRectangleLinesEx((Rectangle){tx, ty, tw, 16}, 1, BEZEL);
    text(tip, tx + 5, ty + 3, 10, INK);
}

/* ---- control switches ------------------------------------------------------------ */
int cswitch3(float cx, float y, const char *name, const char *ll, const char *rl, int red, int green, int style,
             int *ptl, Color c3, int lit3)
{
    Vector2 m = GetMousePosition();
    float *flag;
    mem_slot(cx, y, 1, &flag, NULL);
    if (*flag == 0) *flag = red ? 1.0f : -1.0f;
    if (name) tag_c(cx, y, name);
    int three = c3.a != 0;
    lamp(cx - 20, y + 25, 6, L_GRN, green);
    if (three) lamp(cx, y + 25, 6, c3, lit3);
    lamp(cx + 20, y + 25, 6, L_RED, red);

    /* square escutcheon with four screws and the engraved positions */
    Rectangle e = {cx - 40, y + 38, 80, 58};
    DrawRectangleRounded(e, 0.12f, 4, (Color){24, 24, 22, 255});
    DrawRectangleRoundedLines(e, 0.12f, 4, (Color){92, 92, 86, 255});
    for (int k = 0; k < 4; k++)
        DrawCircleV((Vector2){e.x + (k & 1 ? e.width - 5 : 5), e.y + (k & 2 ? e.height - 5 : 24)}, 2.0f, (Color){120, 120, 114, 255});
    Color eng = {226, 224, 214, 255};
    text(ll, e.x + 4, e.y + 3, 10, eng);
    int rw = MeasureText(rl, 10);
    text(rl, e.x + e.width - rw - 4, e.y + 3, 10, eng);
    /* target flag: red after a close, green after a trip */
    Rectangle fl = {cx - 6, e.y + e.height - 12, 12, 8};
    DrawRectangleRec(fl, BEZEL);
    DrawRectangleRec((Rectangle){fl.x + 1, fl.y + 1, 10, 6}, *flag > 0 ? (Color){200, 40, 30, 255} : (Color){40, 150, 60, 255});

    int hv = input_ok && CheckCollisionPointRec(m, e);
    int side = 0, ret = 0;
    if (hv && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) side = m.x < cx ? -1 : 1;
    if (hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int s = m.x < cx ? -1 : 1;
        if (!(ptl && *ptl && s > 0)) {
            ret = s;
            *flag = (float)s;
        }
    }
    if (ptl && hv && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        *ptl = !*ptl;
        if (*ptl) *flag = -1;
    }
    int locked = ptl && *ptl;
    if (locked) side = -1;
    if (hv) DrawRectangleRoundedLines(e, 0.12f, 4, (Color){200, 200, 190, 255});

    /* handle */
    Vector2 hc = {cx, e.y + 34};
    float ang = (float)(side * 48.0 * DEG2RAD);
    float len = locked ? 18.0f : 15.0f;
    Vector2 dir = {sinf(ang), -cosf(ang)};
    Vector2 tip = {hc.x + dir.x * len, hc.y + dir.y * len};
    Vector2 tail = {hc.x - dir.x * 7, hc.y - dir.y * 7};
    DrawCircleV((Vector2){hc.x + 2, hc.y + 2}, 11, alpha(BLACK, 90));
    if ((style & SW_SHAPE) == SW_PISTOL) {
        Color hcol = style & SW_GREEN ? (Color){30, 96, 44, 255} : style & SW_RED ? (Color){150, 24, 18, 255}
                                                                                  : (Color){18, 18, 16, 255};
        DrawLineEx((Vector2){tail.x + 2, tail.y + 2}, (Vector2){tip.x + 2, tip.y + 2}, 10, alpha(BLACK, 80));
        DrawLineEx(tail, tip, 10, hcol);
        DrawCircleV(tip, 5, hcol);
        DrawCircleV(hc, 10, mixc(hcol, (Color){60, 60, 58, 255}, 0.5f));
        DrawLineEx(hc, tip, 2, (Color){120, 120, 116, 255});
    } else {
        /* J-handle: chrome shank with a hooked end */
        Color ch = {184, 186, 182, 255};
        DrawCircleV(hc, 10, (Color){60, 60, 58, 255});
        DrawLineEx(tail, tip, 6, ch);
        Vector2 perp = {dir.y, -dir.x};
        Vector2 hook = {tip.x + perp.x * 8, tip.y + perp.y * 8};
        DrawLineEx(tip, hook, 6, ch);
        DrawCircleV(tip, 3, ch);
        DrawLineEx((Vector2){hook.x, hook.y}, (Vector2){hook.x - dir.x * 5, hook.y - dir.y * 5}, 6, ch);
    }
    if (locked) {
        DrawRectangleRec((Rectangle){e.x + e.width - 24, e.y + e.height - 13, 20, 10}, (Color){240, 200, 40, 255});
        text("PTL", e.x + e.width - 22, e.y + e.height - 13, 10, INK);
    }
    return ret;
}

int cswitch(float cx, float y, const char *name, const char *ll, const char *rl, int red, int green, int style, int *ptl)
{
    return cswitch3(cx, y, name, ll, rl, red, green, style, ptl, (Color){0, 0, 0, 0}, 0);
}

int keysw(float cx, float cy, const char *l0, const char *l1, int state)
{
    Vector2 m = GetMousePosition();
    Vector2 c = {cx, cy};
    text(l0, cx - 16 - MeasureText(l0, 10), cy - 18, 10, INK);
    text(l1, cx + 16, cy - 18, 10, INK);
    DrawLineEx((Vector2){cx - 11, cy - 11}, (Vector2){cx - 15, cy - 15}, 2, INK);
    DrawLineEx((Vector2){cx + 11, cy - 11}, (Vector2){cx + 15, cy - 15}, 2, INK);
    DrawCircleV(c, 13, (Color){40, 40, 38, 255});
    DrawCircleV(c, 10, (Color){196, 164, 80, 255});        /* brass lock cylinder */
    float a = (float)((state ? 45.0 : -45.0) * DEG2RAD);
    Vector2 d = {sinf(a), -cosf(a)};
    DrawLineEx((Vector2){cx - d.x * 7, cy - d.y * 7}, (Vector2){cx + d.x * 7, cy + d.y * 7}, 3, (Color){60, 50, 30, 255});
    DrawCircleV((Vector2){cx + d.x * 12, cy + d.y * 12}, 5, (Color){210, 210, 200, 255});
    int hv = input_ok && CheckCollisionPointCircle(m, c, 14);
    if (hv) DrawCircleLinesV(c, 15, WHITE);
    return hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* round pushbutton with a coloured collar and the legend engraved below */
int pbround(Vector2 c, float r, const char *label, Color cap, Color collar, int lit)
{
    Vector2 m = GetMousePosition();
    int hv = input_ok && CheckCollisionPointCircle(m, c, r + 4);
    int dn = hv && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    DrawCircleV(c, r + 6, collar);
    DrawRing(c, r + 5, r + 6.5f, 0, 360, 32, BEZEL);
    DrawCircleV((Vector2){c.x + 1.5f, c.y + 2}, r, alpha(BLACK, 90));
    Color k = lit ? cap : (cap.r + cap.g + cap.b > 500 ? mixc(cap, (Color){120, 120, 116, 255}, 0.4f) : cap);
    DrawCircleV(c, dn ? r - 2 : r, dn ? mixc(k, BLACK, 0.25f) : k);
    DrawCircleV((Vector2){c.x - r * 0.3f, c.y - r * 0.35f}, r * 0.3f, alpha(WHITE, lit ? 150 : 50));
    if (hv) DrawCircleLinesV(c, r + 7, (Color){220, 220, 210, 255});
    if (label) {
        const char *nl = strchr(label, '\n');
        if (nl) {
            char l1[32];
            snprintf(l1, sizeof l1, "%.*s", (int)(nl - label), label);
            ctext(l1, c.x, c.y + r + 9, 10, INK);
            ctext(nl + 1, c.x, c.y + r + 20, 10, INK);
        } else {
            ctext(label, c.x, c.y + r + 9, 10, INK);
        }
    }
    return hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* pushbutton under a hinged red guard: the first click lifts the guard for a
 * few seconds, the second pushes the button */
int pbguard(Rectangle r, const char *legend, Color lens, int lit)
{
    double *open_until;
    mem_slot(r.x, r.y, 2, NULL, &open_until);
    double now = GetTime();
    int open = now < *open_until;
    int ret = 0;
    if (open) {
        ret = lampbutton(r, legend, lens, lit);
        if (ret) *open_until = 0;
        /* the lifted guard stands up behind the button */
        DrawRectangleRec((Rectangle){r.x - 3, r.y - 12, r.width + 6, 9}, (Color){170, 30, 24, 220});
        DrawRectangleLinesEx((Rectangle){r.x - 3, r.y - 12, r.width + 6, 9}, 1, BEZEL);
    } else {
        lampbutton(r, legend, lens, lit);
        Rectangle g = {r.x - 3, r.y - 3, r.width + 6, r.height + 6};
        DrawRectangleRec(g, (Color){190, 36, 28, 150});
        DrawRectangleLinesEx(g, 2, (Color){120, 20, 14, 255});
        DrawRectangleRec((Rectangle){g.x, g.y, g.width, 4}, (Color){60, 60, 58, 255});   /* hinge */
        if (clicked(g)) *open_until = now + 6.0;
    }
    return ret;
}

/* ---- manual / automatic control station --------------------------------------- */
int mastation(Rectangle r, const char *name, const char *unit, double pv, double sp, double lo, double hi, double out,
              int mode)
{
    int ev = 0;
    if (name) tag_c(r.x + r.width / 2, r.y, name);
    Rectangle b = {r.x, r.y + 16, r.width, r.height - 16};
    DrawRectangleRounded(b, 0.06f, 4, (Color){54, 56, 54, 255});
    DrawRectangleRoundedLines(b, 0.06f, 4, BEZEL);
    /* process indicator with the setpoint index */
    Rectangle s = {b.x + 8, b.y + 8, 36, 90};
    DrawRectangleRec((Rectangle){s.x - 2, s.y - 2, s.width + 4, s.height + 4}, BEZEL);
    DrawRectangleRec(s, FACE);
    float y0 = s.y + s.height - 5, y1 = s.y + 5;
#define SY(val) (y0 + (y1 - y0) * (float)fmin(1.03, fmax(-0.03, ((val) - lo) / (hi - lo))))
    for (int i = 0; i <= 10; i++) {
        float y = y0 + (y1 - y0) * i / 10;
        DrawLineEx((Vector2){s.x + s.width - (i % 5 ? 5.0f : 9.0f), y}, (Vector2){s.x + s.width, y}, 1, INK);
        if (i % 5 == 0) {
            char t[16];
            snprintf(t, sizeof t, "%g", lo + (hi - lo) * i / 10);
            text(t, s.x + 2, y - 4, 10, INK);
        }
    }
    if (isfinite(sp)) {
        float y = SY(sp);
        tri((Vector2){s.x - 2, y - 5}, (Vector2){s.x + 6, y}, (Vector2){s.x - 2, y + 5}, (Color){16, 16, 16, 255});
    }
    if (isfinite(pv)) {
        float y = SY(pv);
        DrawLineEx((Vector2){s.x + 12, y}, (Vector2){s.x + s.width, y}, 2, (Color){200, 24, 20, 255});
        tri((Vector2){s.x + s.width + 2, y - 5}, (Vector2){s.x + s.width - 5, y}, (Vector2){s.x + s.width + 2, y + 5},
            (Color){200, 24, 20, 255});
    }
#undef SY
    /* setpoint window and units */
    float rx = b.x + 50;
    text("SET", rx, b.y + 8, 10, (Color){210, 210, 200, 255});
    Rectangle sw = {rx, b.y + 20, b.width - 56, 16};
    DrawRectangleRec(sw, (Color){228, 224, 208, 255});
    DrawRectangleLinesEx(sw, 1, BEZEL);
    if (isfinite(sp)) {
        char t[16];
        snprintf(t, sizeof t, fabs(sp) < 10 ? "%.2f" : "%.0f", sp);
        ctext(t, sw.x + sw.width / 2, sw.y + 3, 10, INK);
    }
    if (unit) ctext(unit, sw.x + sw.width / 2, b.y + 40, 10, (Color){210, 210, 200, 255});
    text("PV", rx, b.y + 60, 10, (Color){240, 100, 90, 255});
    DrawRectangleRec((Rectangle){rx + 20, b.y + 59, 18, 12}, (Color){200, 200, 190, 255});
    text("SP", rx + 22, b.y + 60, 10, INK);
    /* output meter */
    Rectangle o = {b.x + 8, b.y + 106, b.width - 16, 13};
    DrawRectangleRec((Rectangle){o.x - 2, o.y - 2, o.width + 4, o.height + 4}, BEZEL);
    DrawRectangleRec(o, FACE);
    for (int i = 0; i <= 4; i++) {
        float x = o.x + o.width * i / 4;
        DrawLineEx((Vector2){x, o.y}, (Vector2){x, o.y + (i % 2 ? 4.0f : 7.0f)}, 1, INK);
    }
    if (isfinite(out)) {
        float x = o.x + o.width * (float)fmin(1.0, fmax(0.0, out));
        DrawLineEx((Vector2){x, o.y}, (Vector2){x, o.y + o.height}, 2, (Color){16, 16, 16, 255});
    }
    text("OUT", o.x, o.y + o.height + 2, 10, (Color){210, 210, 200, 255});
    /* A, M, raise and lower */
    float by = b.y + b.height - 24;
    if (mode >= 0) {
        if (lampbutton((Rectangle){b.x + 5, by, 22, 18}, "A", L_WHT, mode == 1)) ev |= MA_AUTO;
        if (lampbutton((Rectangle){b.x + 29, by, 22, 18}, "M", L_AMB, mode == 0)) ev |= MA_MAN;
    }
    Rectangle up = {b.x + b.width - 42, by, 18, 18}, dn = {b.x + b.width - 22, by, 18, 18};
    if (mode < 0) {
        up = (Rectangle){b.x + 14, by, 30, 18};
        dn = (Rectangle){b.x + b.width - 44, by, 30, 18};
    }
    lampbutton(up, "", (Color){210, 210, 200, 255}, 0);
    lampbutton(dn, "", (Color){210, 210, 200, 255}, 0);
    tri((Vector2){up.x + up.width / 2, up.y + 4}, (Vector2){up.x + 4, up.y + up.height - 5},
        (Vector2){up.x + up.width - 4, up.y + up.height - 5}, INK);
    tri((Vector2){dn.x + 4, dn.y + 5}, (Vector2){dn.x + dn.width - 4, dn.y + 5}, (Vector2){dn.x + dn.width / 2, dn.y + dn.height - 4},
        INK);
    if (held_repeat(up)) ev |= MA_UP;
    if (held_repeat(dn)) ev |= MA_DOWN;
    return ev;
}

/* ---- mimic and demarcation --------------------------------------------------------- */
void mimic(Vector2 a, Vector2 b, float w, Color c) { DrawLineEx(a, b, w, c); }

void mimic_poly(const Vector2 *pts, int n, float w, Color c)
{
    for (int i = 0; i + 1 < n; i++) {
        DrawLineEx(pts[i], pts[i + 1], w, c);
        DrawCircleV(pts[i + 1], w / 2, c);
    }
}

/* painted mimic line with flow-direction chevrons */
void mpipe(const Vector2 *pts, int n, Color c, float w, int arrows)
{
    mimic_poly(pts, n, w, c);
    DrawCircleV(pts[0], w / 2, c);
    if (!arrows) return;
    for (int i = 0; i + 1 < n; i++) {
        float dx = pts[i + 1].x - pts[i].x, dy = pts[i + 1].y - pts[i].y;
        float L = sqrtf(dx * dx + dy * dy);
        if (L < 36) continue;
        dx /= L;
        dy /= L;
        int na = (int)(L / 90) + 1;
        for (int k = 0; k < na; k++) {
            float t = (k + 0.5f) / na * L;
            Vector2 m = {pts[i].x + dx * t, pts[i].y + dy * t};
            float s = w * 1.6f + 3;
            Vector2 tip = {m.x + dx * s, m.y + dy * s};
            Vector2 l = {m.x - dy * s, m.y + dx * s}, r = {m.x + dy * s, m.y - dx * s};
            tri(tip, l, r, c);
        }
    }
}

/* pump: a circle with a triangle pointing the way the fluid goes (dir 0 right, 1 down, 2 left, 3 up) */
void sym_pump(Vector2 c, float r, Color col, int running, int dir)
{
    DrawCircleV(c, r + 2, col);
    DrawCircleV(c, r - 1, (Color){30, 30, 28, 255});
    float a = dir * 90.0f * DEG2RAD, ca = cosf(a), sa = sinf(a);
    Vector2 p[3] = {{-0.45f * r, -0.55f * r}, {-0.45f * r, 0.55f * r}, {0.65f * r, 0}};
    for (int i = 0; i < 3; i++) p[i] = (Vector2){c.x + p[i].x * ca - p[i].y * sa, c.y + p[i].x * sa + p[i].y * ca};
    tri(p[0], p[1], p[2], running ? L_RED : dimlens(L_RED));
}

void sym_valve(Vector2 c, float s, int vertical, Color col, int open)
{
    Color f = open ? L_RED : L_GRN;
    if (vertical) {
        tri((Vector2){c.x - s, c.y - s}, (Vector2){c.x + s, c.y - s}, c, f);
        tri((Vector2){c.x - s, c.y + s}, (Vector2){c.x + s, c.y + s}, c, f);
        DrawLineEx((Vector2){c.x - s, c.y - s}, (Vector2){c.x + s, c.y - s}, 1.5f, col);
        DrawLineEx((Vector2){c.x - s, c.y + s}, (Vector2){c.x + s, c.y + s}, 1.5f, col);
    } else {
        tri((Vector2){c.x - s, c.y - s}, (Vector2){c.x - s, c.y + s}, c, f);
        tri((Vector2){c.x + s, c.y - s}, (Vector2){c.x + s, c.y + s}, c, f);
        DrawLineEx((Vector2){c.x - s, c.y - s}, (Vector2){c.x - s, c.y + s}, 1.5f, col);
        DrawLineEx((Vector2){c.x + s, c.y - s}, (Vector2){c.x + s, c.y + s}, 1.5f, col);
    }
}

/* heat exchanger: shell in one colour, the tube coil in the other */
void sym_hx(Rectangle r, Color a, Color b, const char *name)
{
    DrawRectangleRec(r, (Color){36, 36, 34, 255});
    DrawRectangleLinesEx(r, 3, a);
    int n = 5;
    for (int i = 0; i < n; i++) {
        float y0 = r.y + 5 + (r.height - 10) * i / n, y1 = r.y + 5 + (r.height - 10) * (i + 1) / n;
        DrawLineEx((Vector2){r.x + (i & 1 ? r.width - 6 : 6), y0}, (Vector2){r.x + (i & 1 ? 6 : r.width - 6), y1}, 2.5f, b);
    }
    if (name) ctext(name, r.x + r.width / 2, r.y + r.height + 3, 10, INK);
}

void sym_tank(Rectangle r, Color col, const char *name)
{
    DrawRectangleRounded(r, 0.4f, 6, (Color){36, 36, 34, 255});
    DrawRectangleRoundedLines(r, 0.4f, 6, col);
    if (name) ctext(name, r.x + r.width / 2, r.y + r.height / 2 - 5, 10, (Color){220, 220, 210, 255});
}

void sym_breaker(Vector2 p, int closed)
{
    Rectangle b = {p.x - 9, p.y - 9, 18, 18};
    DrawRectangleRec(b, closed ? L_RED : L_GRN);
    DrawRectangleLinesEx(b, 2, BEZEL);
}

void sym_xfmr(Vector2 p, Color col, int vertical)
{
    Vector2 a = vertical ? (Vector2){p.x, p.y - 8} : (Vector2){p.x - 8, p.y};
    Vector2 b = vertical ? (Vector2){p.x, p.y + 8} : (Vector2){p.x + 8, p.y};
    DrawRing(a, 9, 12, 0, 360, 24, col);
    DrawRing(b, 9, 12, 0, 360, 24, col);
}

/* the black tape boxes round groups of controls */
void demarc(Rectangle r, const char *label)
{
    DrawRectangleLinesEx(r, 3, (Color){20, 20, 18, 255});
    if (label) {
        int tw = MeasureText(label, 10);
        DrawRectangleRec((Rectangle){r.x + 10, r.y - 7, tw + 12.0f, 14}, (Color){26, 26, 24, 255});
        text(label, r.x + 16, r.y - 5, 10, (Color){236, 234, 226, 255});
    }
}

void mimic_legend(float x, float y)
{
    struct { const char *n; Color c; } k[5] = {
        {"PRIMARY NA", MIM_PNA}, {"INTERMED NA", MIM_SNA}, {"FEED/COND", MIM_WTR}, {"STEAM", MIM_STM}, {"AIR/GAS", MIM_AIR}};
    DrawRectangleRec((Rectangle){x, y, 112, 76}, (Color){228, 226, 214, 255});
    DrawRectangleLinesEx((Rectangle){x, y, 112, 76}, 1, BEZEL);
    text("MIMIC CODE", x + 6, y + 3, 10, INK);
    for (int i = 0; i < 5; i++) {
        DrawRectangleRec((Rectangle){x + 6, y + 18 + i * 11, 20, 5}, k[i].c);
        text(k[i].n, x + 32, y + 15 + i * 11, 10, INK);
    }
}

/* ---- recorders ---------------------------------------------------------------------- */
/* two-pen strip chart: paper feeds downward, the pens write on the top line */
void strip(Rectangle fr, int ch_a, float lo_a, float hi_a, const char *name_a, int ch_b, float lo_b, float hi_b,
           const char *name_b)
{
    DrawRectangleRec(fr, (Color){44, 44, 40, 255});
    DrawRectangleLinesEx(fr, 2, BEZEL);
    Rectangle pp = {fr.x + 8, fr.y + 22, fr.width - 16, fr.height - 44};
    char b[16];
    for (int k = 0; k < 3; k++) {
        float x = pp.x + pp.width * k / 2 + (k == 0 ? 6 : (k == 2 ? -10 : 0));
        snprintf(b, sizeof b, "%g", lo_a + (hi_a - lo_a) * k / 2);
        ctext(b, x, fr.y + 6, 10, (Color){190, 150, 240, 255});
        if (ch_b >= 0) {
            snprintf(b, sizeof b, "%g", lo_b + (hi_b - lo_b) * k / 2);
            ctext(b, x, fr.y + fr.height - 18, 10, (Color){255, 120, 110, 255});
        }
    }
    ctext(name_a, pp.x + pp.width * 0.25f, fr.y + 6, 10, (Color){190, 150, 240, 255});
    if (ch_b >= 0) ctext(name_b, pp.x + pp.width * 0.25f, fr.y + fr.height - 18, 10, (Color){255, 120, 110, 255});
    DrawRectangleRec(pp, FACE);
    float wl = pp.y + 6;
    for (int k = 0; k <= 10; k++) {
        float x = pp.x + pp.width * k / 10;
        DrawLineEx((Vector2){x, pp.y}, (Vector2){x, pp.y + pp.height}, k % 5 ? 1.0f : 1.5f,
                   k % 5 ? (Color){170, 200, 170, 255} : (Color){110, 160, 120, 255});
    }
    float sp = (pp.height - 8) / (float)NTR;
    for (int j = 0; j < NTR; j++) {
        long s = nsamp - j;
        if (s % 20) continue;
        float y = wl + j * sp;
        DrawLineEx((Vector2){pp.x, y}, (Vector2){pp.x + pp.width, y}, 1,
                   s % 120 ? (Color){196, 214, 190, 255} : (Color){110, 160, 120, 255});
    }
    float xa = 0, xb = 0;
    for (int j = 0; j < ntr && j < NTR; j++) {
        int k = NTR - 1 - j;
        float y = wl + j * sp;
        float a = pp.x + pp.width * fminf(fmaxf((trend[ch_a][k] - lo_a) / (hi_a - lo_a), 0), 1);
        float bb = ch_b >= 0 ? pp.x + pp.width * fminf(fmaxf((trend[ch_b][k] - lo_b) / (hi_b - lo_b), 0), 1) : 0;
        if (j) {
            DrawLineEx((Vector2){xa, y - sp}, (Vector2){a, y}, 1.6f, PEN_VIO);
            if (ch_b >= 0) DrawLineEx((Vector2){xb, y - sp}, (Vector2){bb, y}, 1.6f, PEN_RED);
        } else {
            tri((Vector2){a, wl}, (Vector2){a - 5, wl - 8}, (Vector2){a + 5, wl - 8}, PEN_VIO);
            if (ch_b >= 0) tri((Vector2){bb, wl}, (Vector2){bb - 5, wl - 8}, (Vector2){bb + 5, wl - 8}, PEN_RED);
        }
        xa = a;
        xb = bb;
    }
    DrawRectangleGradientH((int)pp.x, (int)pp.y, (int)(pp.width * 0.4f), (int)pp.height, alpha(WHITE, 40), alpha(WHITE, 0));
    DrawRectangleLinesEx(pp, 1, BEZEL);
}

void mpr_push(mpr *m, const float *vals)
{
    memmove(m->v, m->v + 1, sizeof(float) * (NTR - 1));
    memmove(m->k, m->k + 1, NTR - 1);
    int k = (int)(nsamp % m->n);
    m->v[NTR - 1] = vals[k];
    m->k[NTR - 1] = (unsigned char)k;
}

static const Color MPR_C[6] = {{120, 50, 170, 255}, {200, 30, 30, 255}, {30, 120, 50, 255},
                               {30, 70, 190, 255},  {140, 80, 20, 255}, {20, 20, 20, 255}};

/* multipoint recorder: each point prints its number in its own colour */
void mpr_draw(Rectangle fr, const mpr *m, const char *title, const char *scale_lo, const char *scale_hi)
{
    DrawRectangleRec(fr, (Color){44, 44, 40, 255});
    DrawRectangleLinesEx(fr, 2, BEZEL);
    ctext(title, fr.x + fr.width / 2, fr.y + 5, 10, (Color){230, 230, 220, 255});
    Rectangle pp = {fr.x + 8, fr.y + 34, fr.width - 16, fr.height - 42};
    float lx = fr.x + 8;
    for (int i = 0; i < m->n; i++) {
        char b[24];
        snprintf(b, sizeof b, "%d %s", i + 1, m->lab[i]);
        text(b, lx, fr.y + 19, 10, (Color){(unsigned char)fminf(255, MPR_C[i].r + 90), (unsigned char)fminf(255, MPR_C[i].g + 90),
                                           (unsigned char)fminf(255, MPR_C[i].b + 90), 255});
        lx += MeasureText(b, 10) + 8;
    }
    DrawRectangleRec(pp, FACE);
    for (int k = 0; k <= 10; k++) {
        float x = pp.x + pp.width * k / 10;
        DrawLineEx((Vector2){x, pp.y}, (Vector2){x, pp.y + pp.height}, 1, k % 5 ? (Color){200, 190, 170, 255} : (Color){160, 140, 120, 255});
    }
    text(scale_lo, pp.x + 2, pp.y + pp.height - 12, 10, (Color){120, 100, 80, 255});
    int tw = MeasureText(scale_hi, 10);
    text(scale_hi, pp.x + pp.width - tw - 2, pp.y + pp.height - 12, 10, (Color){120, 100, 80, 255});
    float sp = 3.0f;
    for (int j = 0; j < NTR && j < ntr; j++) {
        float y = pp.y + 4 + j * sp;
        if (y > pp.y + pp.height - 8) break;
        int k = NTR - 1 - j;
        float x = pp.x + pp.width * fminf(fmaxf(m->v[k], 0), 100) / 100.0f;
        char d[2] = {(char)('1' + m->k[k]), 0};
        DrawText(d, (int)x - 2, (int)y - 4, 10, MPR_C[m->k[k] % 6]);
    }
    DrawRectangleLinesEx(pp, 1, BEZEL);
}
