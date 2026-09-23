/*
 * The 1978 panel widget kit: painted steel, engraved nameplates and Dymo
 * tape, red LED readouts, needle and edgewise meters, lamps and illuminated
 * pushbuttons, pistol-grip and J-handle control switches, key switches,
 * rotary selectors, mimic bus lines and the two kinds of chart recorder.
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

int blink_fast(void) { return ((int)(GetTime() * 3)) & 1; }
int blink_slow(void) { return ((int)(GetTime() * 1)) & 1; }

/* a click on r, honouring the board's control transfer */
int clicked(Rectangle r)
{
    return input_ok && CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

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

/* black engraved-plastic nameplate with two rivets */
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

/* embossed label-maker tape */
float dymo(float x, float y, const char *s)
{
    float tw = (float)MeasureText(s, 10);
    DrawRectangleRec((Rectangle){x, y, tw + 8, 13}, (Color){20, 22, 30, 255});
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

/* a sloped desk section in front of the vertical board */
void desk(Rectangle d)
{
    DrawRectangleRec(d, DESK);
    DrawRectangleRec((Rectangle){d.x, d.y, d.width, 2}, PAINT_HI);
    DrawRectangleLinesEx(d, 1, BEZEL);
}

/* ---- 7-segment LED readouts ------------------------------------------------ */
static const unsigned char SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

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
    unsigned m = 0;
    if (ch >= '0' && ch <= '9') m = SEG[ch - '0'];
    else if (ch == '-') m = 0x40;
    else if (ch == 'E') m = 0x79;
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

/* vertical edgewise meter: a narrow window with the scale on the left and a
 * red pointer that slides up and down it */
void edgew(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj,
           const char *const *labs)
{
    DrawRectangleRec(r, BEZEL);
    Rectangle f = {r.x + 4, r.y + 4, r.width - 8, r.height - 8};
    DrawRectangleRec(f, FACE);
    float y0 = f.y + f.height - 6, y1 = f.y + 6;
#define EY(val) (y0 + (y1 - y0) * (float)fmin(1.03, fmax(-0.03, ((val) - lo) / (hi - lo))))
    if (red_hi > red_lo) {
        float a = EY(red_hi), b = EY(red_lo);
        DrawRectangleRec((Rectangle){f.x + 1, a, 4, b - a}, (Color){210, 40, 30, 255});
    }
    int nmin = nmaj * 5;
    for (int i = 0; i <= nmin; i++) {
        float y = y0 + (y1 - y0) * i / nmin;
        DrawLineEx((Vector2){f.x + 5, y}, (Vector2){f.x + 5 + (i % 5 ? 4.0f : 8.0f), y}, 1, INK);
        if (i % 5 == 0 && f.width > 26) {
            char b[16];
            if (labs) snprintf(b, sizeof b, "%s", labs[i / 5]);
            else snprintf(b, sizeof b, "%g", lo + (hi - lo) * i / nmin);
            text(b, f.x + 15, y - 4, 10, INK);
        }
    }
    float py = EY(v);
#undef EY
    DrawRectangleRec((Rectangle){f.x + 3, py - 1.5f, f.width - 3, 3}, (Color){200, 24, 20, 255});
    DrawTriangle((Vector2){f.x + f.width, py - 5}, (Vector2){f.x + f.width - 7, py}, (Vector2){f.x + f.width, py + 5},
                 (Color){200, 24, 20, 255});
    DrawRectangleGradientH((int)f.x, (int)f.y, (int)(f.width * 0.5f), (int)f.height, alpha(WHITE, 50), alpha(WHITE, 0));
    if (label) ctext(label, r.x + r.width / 2, r.y + r.height + 3, 10, INK);
}

/* ---- lamps and pushbuttons --------------------------------------------------- */
void lamp(float x, float y, float rad, Color lens, int lit)
{
    DrawCircleV((Vector2){x, y}, rad + 2.5f, BEZEL);
    if (lit) DrawCircleV((Vector2){x, y}, rad + 5, alpha(lens, 50));
    DrawCircleV((Vector2){x, y}, rad, lit ? lens : dimlens(lens));
    DrawCircleV((Vector2){x - rad * 0.35f, y - rad * 0.35f}, rad * 0.3f, alpha(WHITE, lit ? 170 : 60));
}

/* square illuminated pushbutton with a two-line engraved legend */
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
    Color tc = lit ? (Color){20, 16, 12, 255} : (Color){26, 26, 24, 255};
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
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* annunciator window */
void window(Rectangle t, const char *l1, const char *l2, Color lens, int lit)
{
    DrawRectangleRec(t, BEZEL);
    Rectangle in = {t.x + 3, t.y + 3, t.width - 6, t.height - 6};
    DrawRectangleRec(in, lit ? lens : (Color){88, 88, 82, 255});
    if (lit) DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)(in.height / 2), alpha(WHITE, 90), alpha(WHITE, 0));
    Color tc = lit ? (Color){16, 12, 10, 255} : (Color){58, 58, 54, 255};
    ctext(l1, in.x + in.width / 2, in.y + in.height / 2 - (l2 ? 11 : 5), 10, tc);
    if (l2) ctext(l2, in.x + in.width / 2, in.y + in.height / 2 + 2, 10, tc);
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
    /* knob: black bakelite with a white pointer */
    DrawCircleV((Vector2){c.x + 2, c.y + 3}, rad, alpha(BLACK, 90));
    DrawCircleV(c, rad, (Color){24, 24, 22, 255});
    DrawRing(c, rad - 3, rad, 0, 360, 36, (Color){70, 70, 66, 255});
    float a = (a0 + (a1 - a0) * cur / (n - 1)) * DEG2RAD;
    DrawLineEx((Vector2){c.x - (rad - 6) * cosf(a) * 0.4f, c.y - (rad - 6) * sinf(a) * 0.4f},
               (Vector2){c.x + (rad - 4) * cosf(a), c.y + (rad - 4) * sinf(a)}, 5, (Color){236, 234, 224, 255});
    return pick;
}

/* incandescent lamp glowing from ember to white with f = 0..1, red flashing past 0.92 */
void map_lamp(Vector2 p, float rad, double f, int blink)
{
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    DrawCircleV(p, rad, (Color){20, 18, 16, 255});
    float lr = rad * 0.78f;
    if (f > 0.92) {
        Color red = blink ? (Color){255, 70, 40, 255} : (Color){110, 26, 18, 255};
        if (blink) DrawCircleV(p, rad + 1.5f, (Color){255, 60, 30, 70});
        DrawCircleV(p, lr, red);
    } else {
        static const float K[4][4] = {{0.0f, 45, 18, 8}, {0.35f, 190, 70, 14}, {0.65f, 255, 190, 60}, {0.92f, 255, 248, 215}};
        int k = f < K[1][0] ? 0 : (f < K[2][0] ? 1 : 2);
        float t = (float)((f - K[k][0]) / (K[k + 1][0] - K[k][0]));
        if (t > 1) t = 1;
        Color c = {(unsigned char)(K[k][1] + (K[k + 1][1] - K[k][1]) * t), (unsigned char)(K[k][2] + (K[k + 1][2] - K[k][2]) * t),
                   (unsigned char)(K[k][3] + (K[k + 1][3] - K[k][3]) * t), 255};
        if (f > 0.5) DrawCircleV(p, rad + 1.5f, (Color){255, 220, 140, (unsigned char)(120 * (f - 0.5))});
        DrawCircleV(p, lr, c);
    }
    DrawCircleV((Vector2){p.x - lr * 0.3f, p.y - lr * 0.3f}, lr * 0.28f, (Color){255, 255, 255, 60});
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
/* the target flag of each switch, found by its position on the canvas */
#define NFLAG 512
static struct { int key; signed char flag; } flags[NFLAG];

static signed char *flag_of(float cx, float y)
{
    int key = ((int)cx << 12) ^ (int)y ^ 0x5a5a5a;
    unsigned h = (unsigned)key * 2654435761u;
    for (int i = 0; i < NFLAG; i++) {
        int s = (int)((h + i) % NFLAG);
        if (flags[s].key == key) return &flags[s].flag;
        if (flags[s].key == 0) {
            flags[s].key = key;
            flags[s].flag = 0;
            return &flags[s].flag;
        }
    }
    return &flags[0].flag;
}

int cswitch(float cx, float y, const char *name, const char *ll, const char *rl, int red, int green, int style, int *ptl)
{
    Vector2 m = GetMousePosition();
    signed char *flag = flag_of(cx, y);
    if (*flag == 0) *flag = red ? 1 : -1;
    if (name) dymo_c(cx, y, name);
    lamp(cx - 14, y + 23, 6, L_GRN, green);
    lamp(cx + 14, y + 23, 6, L_RED, red);

    /* escutcheon */
    Rectangle e = {cx - 37, y + 34, 74, 44};
    DrawRectangleRec(e, (Color){26, 26, 24, 255});
    DrawRectangleLinesEx(e, 1, (Color){90, 90, 84, 255});
    textf(e.x + 3, e.y + e.height - 12, 10, (Color){220, 220, 210, 255}, "%s", ll);
    int rw = MeasureText(rl, 10);
    textf(e.x + e.width - rw - 3, e.y + e.height - 12, 10, (Color){220, 220, 210, 255}, "%s", rl);
    /* target flag between the lamps: red after a close, green after a trip */
    Rectangle fl = {cx - 4, y + 19, 8, 8};
    DrawRectangleRec(fl, BEZEL);
    DrawRectangleRec((Rectangle){fl.x + 1, fl.y + 1, 6, 6}, *flag > 0 ? (Color){200, 40, 30, 255} : (Color){40, 150, 60, 255});

    int hv = input_ok && CheckCollisionPointRec(m, e);
    int side = 0, ret = 0;
    if (hv && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) side = m.x < cx ? -1 : 1;
    if (hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int s = m.x < cx ? -1 : 1;
        if (!(ptl && *ptl && s > 0)) {
            ret = s;
            *flag = (signed char)s;
        }
    }
    if (ptl && hv && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        *ptl = !*ptl;
        if (*ptl) *flag = -1;
    }
    int locked = ptl && *ptl;
    if (locked) side = -1;
    if (hv) DrawRectangleLinesEx(e, 1, (Color){190, 190, 180, 255});

    /* handle */
    Vector2 hc = {cx, e.y + 20};
    float ang = (float)(side * 45.0 * DEG2RAD);
    float len = locked ? 20.0f : 17.0f;
    Vector2 dir = {sinf(ang), -cosf(ang)};
    Vector2 tip = {hc.x + dir.x * len, hc.y + dir.y * len};
    Vector2 tail = {hc.x - dir.x * 6, hc.y - dir.y * 6};
    DrawCircleV((Vector2){hc.x + 2, hc.y + 2}, 11, alpha(BLACK, 90));
    if (style == SW_PISTOL) {
        DrawLineEx((Vector2){tail.x + 2, tail.y + 2}, (Vector2){tip.x + 2, tip.y + 2}, 10, alpha(BLACK, 80));
        DrawLineEx(tail, tip, 10, (Color){18, 18, 16, 255});
        DrawCircleV(tip, 6, (Color){18, 18, 16, 255});
        DrawCircleV(hc, 10, (Color){34, 34, 32, 255});
        DrawLineEx(hc, tip, 2, (Color){90, 90, 86, 255});
    } else {
        /* J-handle: chrome shank with a hooked end */
        Color ch = {176, 178, 174, 255};
        DrawCircleV(hc, 10, (Color){60, 60, 58, 255});
        DrawLineEx(tail, tip, 6, ch);
        Vector2 perp = {dir.y, -dir.x};
        Vector2 hook = {tip.x + perp.x * 8, tip.y + perp.y * 8};
        DrawLineEx(tip, hook, 6, ch);
        DrawCircleV(tip, 3, ch);
        DrawLineEx((Vector2){hook.x, hook.y}, (Vector2){hook.x - dir.x * 5, hook.y - dir.y * 5}, 6, ch);
    }
    if (locked) {
        DrawRectangleRec((Rectangle){e.x + 2, e.y + 2, 20, 10}, (Color){240, 200, 40, 255});
        text("PTL", e.x + 4, e.y + 2, 10, INK);
    }
    return ret;
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
    /* key bow sticking out of the cylinder */
    DrawLineEx((Vector2){cx - d.x * 7, cy - d.y * 7}, (Vector2){cx + d.x * 7, cy + d.y * 7}, 3, (Color){60, 50, 30, 255});
    DrawCircleV((Vector2){cx + d.x * 12, cy + d.y * 12}, 5, (Color){210, 210, 200, 255});
    int hv = input_ok && CheckCollisionPointCircle(m, c, 14);
    if (hv) DrawCircleLinesV(c, 15, WHITE);
    return hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
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

/* the black tape boxes plants added round groups of controls */
void demarc(Rectangle r, const char *label)
{
    DrawRectangleLinesEx(r, 2, (Color){24, 24, 22, 255});
    if (label) {
        int tw = MeasureText(label, 10);
        DrawRectangleRec((Rectangle){r.x + 8, r.y - 6, tw + 8.0f, 12}, PAINT);
        text(label, r.x + 12, r.y - 5, 10, INK);
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
            DrawTriangle((Vector2){a, wl}, (Vector2){a - 5, wl - 8}, (Vector2){a + 5, wl - 8}, PEN_VIO);
            if (ch_b >= 0) DrawTriangle((Vector2){bb, wl}, (Vector2){bb - 5, wl - 8}, (Vector2){bb + 5, wl - 8}, PEN_RED);
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
    /* point legend */
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
