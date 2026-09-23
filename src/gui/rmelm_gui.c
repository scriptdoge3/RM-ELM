/*
 * RM-ELM graphical control room (raylib), drawn as a 1978 main control
 * board: painted steel panels, engraved nameplates and Dymo labels, analog
 * needle meters, red LED readouts, a strip-chart recorder, illuminated
 * pushbuttons, an annunciator window box and an alarm typer. It drives the
 * same plant model as the terminal.
 *
 * Lamp convention is the US one of the period: RED = running / closed /
 * energised, GREEN = stopped / open.
 */
#include "raylib.h"
#include "rm_plant.h"
#include "rm_sodium.h"

#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 1280
#define WIN_H 800
#define DT 0.05

/* ---- palette ------------------------------------------------------------ */
static const Color WALL = {58, 60, 56, 255};
static const Color PAINT = {156, 174, 158, 255};     /* institutional green */
static const Color PAINT_HI = {192, 208, 194, 255};
static const Color PAINT_LO = {98, 112, 100, 255};
static const Color INK = {32, 38, 34, 255};
static const Color BEZEL = {30, 30, 28, 255};
static const Color FACE = {238, 230, 206, 255};      /* meter face / chart paper */
static const Color SLATE = {56, 66, 62, 255};        /* mimic board */
static const Color TAPE_PRI = {196, 60, 40, 255};
static const Color TAPE_SEC = {226, 142, 40, 255};
static const Color TAPE_STM = {80, 130, 210, 255};
static const Color TAPE_W = {226, 226, 216, 255};
static const Color LED = {255, 46, 24, 255};
static const Color L_RED = {240, 60, 44, 255};
static const Color L_GRN = {60, 220, 90, 255};
static const Color L_AMB = {250, 186, 60, 255};
static const Color L_WHT = {248, 244, 228, 255};
static const Color PEN_VIO = {120, 50, 170, 255};
static const Color PEN_RED = {200, 30, 30, 255};

/* ---- state -------------------------------------------------------------- */
static rm_plant *P;
static volatile int loaded = 0;
static int speed = 1, paused = 0;
#define NTR 480                         /* recorder samples, one every 0.5 s */
static float trend_p[NTR], trend_t[NTR];
static int ntr = 0;
static long nsamp = 0;
#define NLOG 7
static char logs[NLOG][100];
static char last_first_out[48] = "";
static double last_log_time = -100;

static void logmsg(const char *fmt, ...)
{
    for (int i = 0; i < NLOG - 1; i++) memcpy(logs[i], logs[i + 1], sizeof logs[i]);
    char m[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    for (char *q = m; *q; q++) *q = (char)toupper((unsigned char)*q);   /* teleprinters had one case */
    int t = P ? (int)P->t : 0;
    snprintf(logs[NLOG - 1], sizeof logs[0], "%02d:%02d:%02d  %s", t / 3600, t / 60 % 60, t % 60, m);
    last_log_time = GetTime();
}

static void *loader(void *arg)
{
    (void)arg;
    rm_plant_init(P);
    rm_plant_steady(P);
    loaded = 1;
    return NULL;
}

/* ---- drawing primitives --------------------------------------------------- */
static Color dimlens(Color c)
{
    return (Color){(unsigned char)(c.r * 0.28f + 44), (unsigned char)(c.g * 0.28f + 42),
                   (unsigned char)(c.b * 0.28f + 38), 255};
}

static Color alpha(Color c, unsigned char a) { return (Color){c.r, c.g, c.b, a}; }

static void text(const char *s, float x, float y, int size, Color c) { DrawText(s, (int)x, (int)y, size, c); }

static void textf(float x, float y, int size, Color c, const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    DrawText(b, (int)x, (int)y, size, c);
}

static void ctext(const char *s, float cx, float y, int size, Color c)
{
    DrawText(s, (int)(cx - MeasureText(s, size) / 2.0f), (int)y, size, c);
}

static void screw(float x, float y)
{
    DrawCircleV((Vector2){x, y}, 4, (Color){128, 138, 130, 255});
    DrawCircleLinesV((Vector2){x, y}, 4, INK);
    DrawLineEx((Vector2){x - 3, y + 2}, (Vector2){x + 3, y - 2}, 1, INK);
}

/* black engraved-plastic nameplate with two rivets */
static float plate(float x, float y, const char *s, int size)
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

static void plate_c(float cx, float y, const char *s, int size)
{
    plate(cx - (MeasureText(s, size) + 24) / 2.0f, y, s, size);
}

/* embossed label-maker tape */
static float dymo(float x, float y, const char *s)
{
    float tw = (float)MeasureText(s, 10);
    DrawRectangleRec((Rectangle){x, y, tw + 8, 13}, (Color){20, 22, 30, 255});
    text(s, x + 4, y + 2, 10, (Color){236, 236, 236, 255});
    return tw + 8;
}

static void steel(Rectangle r, const char *title)
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

/* ---- 7-segment LED readouts ------------------------------------------------ */
static const unsigned char SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

static float seg_char(float x, float y, float h, char ch, Color on)
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

static float segw(const char *s, float h)
{
    float w = 0;
    for (; *s; s++) w += *s == '.' ? h * 0.22f : (*s == ':' ? h * 0.3f : h * 0.68f);
    return w;
}

/* LED window, right-justified in a field of ndig digits; returns its width */
static float readout(float x, float y, float h, int ndig, const char *fmt, ...)
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

static void meter(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi,
                  int nmaj)
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
            snprintf(b, sizeof b, "%g", lo + (hi - lo) * i / nmin);
            Vector2 pt = {c.x + (R - 18) * cosf(a), c.y + (R - 18) * sinf(a)};
            ctext(b, pt.x, pt.y - 4, 10, INK);
        }
    }
    ctext(label, c.x, c.y - 20, 10, INK);
    float a = meter_ang(v, lo, hi) * DEG2RAD;
    Vector2 tip = {c.x + (R - 2) * cosf(a), c.y + (R - 2) * sinf(a)};
    DrawLineEx((Vector2){c.x + 2, c.y + 2}, (Vector2){tip.x + 2, tip.y + 2}, 2, alpha(BLACK, 50));
    DrawLineEx(c, tip, 2, (Color){16, 16, 16, 255});
    /* pivot cover */
    DrawRectangleRec((Rectangle){f.x, c.y - 4, f.width, f.y + f.height - c.y + 4}, (Color){60, 58, 52, 255});
    DrawCircleV(c, 6, (Color){20, 20, 20, 255});
    /* glass glare */
    DrawRectangleGradientV((int)f.x, (int)f.y, (int)f.width, (int)(f.height * 0.35f), alpha(WHITE, 60), alpha(WHITE, 0));
}

/* ---- lamps and pushbuttons --------------------------------------------------- */
static void lamp(float x, float y, float rad, Color lens, int lit)
{
    DrawCircleV((Vector2){x, y}, rad + 2.5f, BEZEL);
    if (lit) DrawCircleV((Vector2){x, y}, rad + 5, alpha(lens, 50));
    DrawCircleV((Vector2){x, y}, rad, lit ? lens : dimlens(lens));
    DrawCircleV((Vector2){x - rad * 0.35f, y - rad * 0.35f}, rad * 0.3f, alpha(WHITE, lit ? 170 : 60));
}

/* square illuminated pushbutton with a two-line engraved legend */
static int lampbutton(Rectangle r, const char *legend, Color lens, int lit)
{
    Vector2 m = GetMousePosition();
    int hover = CheckCollisionPointRec(m, r);
    int down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (lit) DrawRectangleRec((Rectangle){r.x - 3, r.y - 3, r.width + 6, r.height + 6}, alpha(lens, 45));
    DrawRectangleRec(r, BEZEL);
    Rectangle in = {r.x + 3, r.y + 3 + (down ? 1.0f : 0.0f), r.width - 6, r.height - 6};
    DrawRectangleRec(in, lit ? lens : dimlens(lens));
    DrawRectangleRec((Rectangle){in.x, in.y, in.width, 2}, alpha(WHITE, lit ? 120 : 40));
    if (hover) DrawRectangleLinesEx(r, 1, (Color){160, 160, 150, 255});
    Color tc = lit ? (Color){20, 16, 12, 255} : (Color){26, 26, 24, 255};
    char l1[24], l2[24] = "";
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
static void window(Rectangle t, const char *l1, const char *l2, Color lens, int lit)
{
    DrawRectangleRec(t, BEZEL);
    Rectangle in = {t.x + 3, t.y + 3, t.width - 6, t.height - 6};
    DrawRectangleRec(in, lit ? lens : (Color){88, 88, 82, 255});
    if (lit) DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)(in.height / 2), alpha(WHITE, 90), alpha(WHITE, 0));
    Color tc = lit ? (Color){16, 12, 10, 255} : (Color){58, 58, 54, 255};
    ctext(l1, in.x + in.width / 2, in.y + in.height / 2 - (l2 ? 11 : 5), 10, tc);
    if (l2) ctext(l2, in.x + in.width / 2, in.y + in.height / 2 + 2, 10, tc);
}

/* ---- panels --------------------------------------------------------------- */
static const char *bank_name[RM_NBANKS] = {"REG", "SHIM A", "SHIM B", "SHIM C", "SHIM D", "SAFETY"};

static void draw_reactor(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, "REACTOR");

    /* strip-chart recorder: paper feeds downward, pens write on the top line */
    Rectangle fr = {r.x + 12, r.y + 32, 196, 300};
    DrawRectangleRec(fr, (Color){44, 44, 40, 255});
    DrawRectangleLinesEx(fr, 2, BEZEL);
    Rectangle pp = {fr.x + 8, fr.y + 22, fr.width - 16, 250};
    const char *ps[3] = {"0", "60", "120"}, *ts[3] = {"300", "500", "700"};
    for (int k = 0; k < 3; k++) {
        float x = fr.x + 8 + (fr.width - 16) * k / 2 + (k == 0 ? 6 : (k == 2 ? -10 : 0));
        ctext(ps[k], x, fr.y + 6, 10, (Color){190, 150, 240, 255});
        ctext(ts[k], x, fr.y + 278, 10, (Color){255, 120, 110, 255});
    }
    ctext("PWR %", fr.x + 8 + (fr.width - 16) * 0.25f, fr.y + 6, 10, (Color){190, 150, 240, 255});
    ctext("OUT C", fr.x + 8 + (fr.width - 16) * 0.25f, fr.y + 278, 10, (Color){255, 120, 110, 255});
    DrawRectangleRec(pp, FACE);
    float wl = pp.y + 6;   /* writing line */
    for (int k = 0; k <= 10; k++) {
        float x = pp.x + pp.width * k / 10;
        DrawLineEx((Vector2){x, pp.y}, (Vector2){x, pp.y + pp.height}, k % 5 ? 1.0f : 1.5f,
                   k % 5 ? (Color){170, 200, 170, 255} : (Color){110, 160, 120, 255});
    }
    for (int j = 0; j < NTR; j++) {
        long s = nsamp - j;
        if (s % 20) continue;                       /* 10 s lines, minute lines darker */
        float y = wl + j * 0.5f;
        if (y > pp.y + pp.height) break;
        DrawLineEx((Vector2){pp.x, y}, (Vector2){pp.x + pp.width, y}, 1,
                   s % 120 ? (Color){196, 214, 190, 255} : (Color){110, 160, 120, 255});
    }
    float xp = 0, xt = 0;
    for (int j = 0; j < ntr && j < NTR; j++) {
        int k = NTR - 1 - j;
        float y = wl + j * 0.5f;
        float p = pp.x + pp.width * fminf(fmaxf(trend_p[k], 0), 120) / 120.0f;
        float t = pp.x + pp.width * fminf(fmaxf(trend_t[k] - 300, 0), 400) / 400.0f;
        if (j) {
            DrawLineEx((Vector2){xp, y - 0.5f}, (Vector2){p, y}, 1.6f, PEN_VIO);
            DrawLineEx((Vector2){xt, y - 0.5f}, (Vector2){t, y}, 1.6f, PEN_RED);
        } else {
            /* pen carriages */
            DrawTriangle((Vector2){p, wl}, (Vector2){p - 5, wl - 8}, (Vector2){p + 5, wl - 8}, PEN_VIO);
            DrawTriangle((Vector2){t, wl}, (Vector2){t - 5, wl - 8}, (Vector2){t + 5, wl - 8}, PEN_RED);
        }
        xp = p;
        xt = t;
    }
    DrawRectangleGradientH((int)pp.x, (int)pp.y, (int)(pp.width * 0.4f), (int)pp.height, alpha(WHITE, 40), alpha(WHITE, 0));
    DrawRectangleLinesEx(pp, 1, BEZEL);

    /* LED readouts */
    float x0 = r.x + 218, y = r.y + 34;
    double beta = 0;
    for (int i = 0; i < 6; i++) beta += c->pk.beta[i];
    dymo(x0, y, "REACTOR POWER  MWT");
    readout(x0, y + 16, 30, 4, "%4.0f", c->p_thermal / 1e6);
    y += 62;
    struct { const char *lab; char val[16]; } cell[8];
    snprintf(cell[0].val, 16, "%5.1f", 100 * c->p_thermal / RM_P_RATED);
    cell[0].lab = "PCT RATED";
    if (isfinite(P->period) && fabs(P->period) < 999) snprintf(cell[1].val, 16, "%5.1f", P->period);
    else snprintf(cell[1].val, 16, "----");
    cell[1].lab = "PERIOD SEC";
    snprintf(cell[2].val, 16, "%5.0f", 1e5 * c->rho);
    cell[2].lab = "REACT PCM";
    snprintf(cell[3].val, 16, "%5.2f", c->rho / beta);
    cell[3].lab = "REACT $";
    snprintf(cell[4].val, 16, "%4.0f", rm_core_max_fuel_T(c) - 273.15);
    cell[4].lab = "FUEL MAX C";
    snprintf(cell[5].val, 16, "%4.0f", rm_core_max_clad_T(c) - 273.15);
    cell[5].lab = "CLAD MAX C";
    snprintf(cell[6].val, 16, "%4.0f", P->T_core_in - 273.15);
    cell[6].lab = "CORE IN C";
    snprintf(cell[7].val, 16, "%4.0f", c->p_decay / 1e6);
    cell[7].lab = "DECAY MW";
    for (int i = 0; i < 8; i++) {
        float cx = x0 + (i % 2) * 98, cy = y + (i / 2) * 50;
        dymo(cx, cy, cell[i].lab);
        readout(cx, cy + 16, 18, 5, "%s", cell[i].val);
    }

    /* meters */
    float mw = (r.width - 30) / 2;
    meter((Rectangle){r.x + 12, r.y + 342, mw, 128}, "CORE FLOW %", 100 * P->W_core / rm_plant_nominal_flow(), 0, 120, 0, 80, 4);
    meter((Rectangle){r.x + 18 + mw, r.y + 342, mw, 128}, "CORE OUTLET C", P->T_core_out - 273.15, 300, 700, 600, 700, 4);
}

static void tape(float x1, float y1, float x2, float y2, Color c)
{
    DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 5, c);
}

/* pump symbol with its status lamp; click toggles */
static void pump(rm_pump *pm, float x, float y, const char *name, int primary, int idx)
{
    int running = pm->motor_on && !pm->tripped;
    DrawCircleV((Vector2){x, y}, 11, SLATE);
    DrawRing((Vector2){x, y}, 9, 11.5f, 0, 360, 24, TAPE_W);
    DrawTriangle((Vector2){x - 5, y - 5}, (Vector2){x - 5, y + 5}, (Vector2){x + 6, y}, TAPE_W);
    lamp(x, y - 20, 4, running ? L_RED : L_GRN, 1);
    text(name, x + 8, y - 25, 10, TAPE_W);
    Rectangle hit = {x - 13, y - 26, 26, 40};
    if (CheckCollisionPointRec(GetMousePosition(), hit) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const char *kind = primary ? "PRIMARY" : "SECONDARY";
        if (running) {
            pm->motor_on = 0;
            logmsg("%s PUMP %d STOPPED", kind, idx + 1);
        } else {
            pm->tripped = 0;
            pm->motor_on = 1;
            if (primary) pm->speed_set = 1.0;
            logmsg("%s PUMP %d STARTED", kind, idx + 1);
        }
    }
}

static void draw_mimic(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, "PLANT MIMIC");
    Rectangle b = {r.x + 8, r.y + 30, r.width - 16, 326};
    DrawRectangleRec(b, SLATE);
    DrawRectangleLinesEx(b, 2, BEZEL);
    float ix = b.x, iy = b.y;

    /* reactor vessel */
    Rectangle v = {ix + 14, iy + 16, 72, 272};
    DrawRectangleRoundedLinesEx(v, 0.3f, 6, 2, TAPE_W);
    Rectangle core = {v.x + 16, v.y + 100, 40, 76};
    DrawRectangleRec(core, (Color){120, 70, 50, 255});
    for (int k = 1; k < 5; k++)
        DrawLineEx((Vector2){core.x + k * 8, core.y + 2}, (Vector2){core.x + k * 8, core.y + core.height - 2}, 1,
                   (Color){200, 140, 90, 255});
    DrawRectangleLinesEx(core, 1, TAPE_W);
    ctext("CORE", v.x + v.width / 2, core.y + core.height + 4, 10, TAPE_W);
    textf(v.x + 8, v.y + 12, 10, TAPE_W, "OUT %3.0fC", P->T_core_out - 273.15);
    textf(v.x + 8, v.y + v.height - 22, 10, TAPE_W, "IN  %3.0fC", P->T_core_in - 273.15);
    int crit = !c->scram && c->p_thermal > 0.01 * RM_P_RATED;
    int blink = ((int)(GetTime() * 2)) & 1;
    lamp(v.x + 20, v.y + 50, 6, L_WHT, crit);
    text("CRIT", v.x + 30, v.y + 45, 10, TAPE_W);
    lamp(v.x + 20, v.y + 72, 6, L_RED, c->scram && blink);
    text("TRIP", v.x + 30, v.y + 67, 10, TAPE_W);

    /* four loops */
    float hx = ix + 320;   /* steam header */
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        float y = iy + 34 + i * 74;
        tape(v.x + v.width, y, ix + 150, y, TAPE_PRI);
        tape(ix + 150, y + 32, v.x + v.width, y + 32, TAPE_PRI);
        textf(ix + 94, y - 14, 10, TAPE_W, "%4.0f KG/S", l->W);
        Rectangle ihx = {ix + 150, y - 8, 28, 48};
        DrawRectangleRec(ihx, SLATE);
        DrawRectangleLinesEx(ihx, 2, TAPE_W);
        ctext("IHX", ihx.x + 14, ihx.y + 19, 10, TAPE_W);
        pump(&l->ppump, ix + 116, y + 32, TextFormat("P%d", i + 1), 1, i);

        tape(ix + 178, y, ix + 262, y, TAPE_SEC);
        tape(ix + 262, y + 32, ix + 178, y + 32, TAPE_SEC);
        pump(&l->spump, ix + 220, y + 32, TextFormat("S%d", i + 1), 0, i);

        Rectangle sg = {ix + 262, y - 10, 26, 52};
        DrawRectangleRoundedLinesEx(sg, 0.5f, 4, 2, TAPE_W);
        ctext("SG", sg.x + 13, sg.y + 20, 10, TAPE_W);
        textf(ix + 200, y - 14, 10, TAPE_W, "%3.0f MW", l->sg.Q / 1e6);
        tape(sg.x + sg.width, y - 4, hx, y - 4, TAPE_STM);
    }
    tape(hx, iy + 30, hx, iy + 34 + 3 * 74 - 4, TAPE_STM);
    tape(hx, iy + 140, ix + 340, iy + 140, TAPE_STM);

    /* turbine and generator */
    float tx = ix + 340, ty = iy + 110;
    Color tc = P->turbine_tripped ? (Color){90, 70, 66, 255} : (Color){150, 156, 160, 255};
    DrawTriangle((Vector2){tx, ty + 18}, (Vector2){tx, ty + 42}, (Vector2){tx + 40, ty + 60}, tc);
    DrawTriangle((Vector2){tx, ty + 18}, (Vector2){tx + 40, ty + 60}, (Vector2){tx + 40, ty}, tc);
    tape(tx + 40, ty + 30, tx + 52, ty + 30, TAPE_W);
    DrawCircleV((Vector2){tx + 64, ty + 30}, 12, SLATE);
    DrawRing((Vector2){tx + 64, ty + 30}, 10, 12.5f, 0, 360, 24, TAPE_W);
    ctext("G", tx + 64, ty + 25, 10, TAPE_W);
    lamp(tx + 8, ty + 76, 5, P->turbine_tripped ? L_GRN : L_RED, 1);
    text("TURB", tx + 16, ty + 71, 10, TAPE_W);
    lamp(tx + 8, ty + 94, 5, P->generator_breaker ? L_RED : L_GRN, 1);
    text("BKR", tx + 16, ty + 89, 10, TAPE_W);
    lamp(tx + 8, ty + 112, 5, L_AMB, P->bypass_valve > 0.01);
    text("BYP", tx + 16, ty + 107, 10, TAPE_W);
    lamp(tx + 8, ty + 130, 5, L_AMB, P->W_relief > 0 && blink);
    text("SRV", tx + 16, ty + 125, 10, TAPE_W);
    text("NET MWE", tx - 6, iy + 262, 10, TAPE_W);
    readout(tx - 6, iy + 276, 18, 4, "%4.0f", fmax(P->P_net / 1e6, 0));

    text("LAMPS: RED = RUNNING/CLOSED  GREEN = STOPPED/OPEN", ix + 10, iy + 298, 10, TAPE_W);
    text("CLICK A PUMP TO START / STOP", ix + 10, iy + 311, 10, (Color){170, 180, 176, 255});

    float mw = (r.width - 16 - 12) / 3;
    meter((Rectangle){r.x + 8, r.y + 362, mw, 110}, "STEAM MPA", P->p_header / 1e6, 0, 20, 16, 20, 4);
    meter((Rectangle){r.x + 14 + mw, r.y + 362, mw, 110}, "GEN MWE", P->P_gen / 1e6, 0, 1200, 1050, 1200, 2);
    meter((Rectangle){r.x + 20 + 2 * mw, r.y + 362, mw, 110}, "DRACS MW", P->Q_dracs / 1e6, 0, 100, 0, 0, 2);
}

static void draw_controls(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, "REACTOR CONTROL CONSOLE");

    /* mushroom-head manual scram */
    Vector2 sc = {r.x + 62, r.y + 78};
    Vector2 m = GetMousePosition();
    int hov = CheckCollisionPointCircle(m, sc, 32);
    int dn = hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    DrawCircleV(sc, 40, (Color){238, 196, 30, 255});
    DrawRing(sc, 37, 40, 0, 360, 48, INK);
    ctext("MANUAL SCRAM", sc.x, sc.y + 44, 10, INK);
    DrawCircleV((Vector2){sc.x + 3, sc.y + 4}, 29, alpha(BLACK, 90));
    DrawCircleV(sc, dn ? 26.0f : 29.0f, dn ? (Color){160, 20, 16, 255} : (Color){206, 28, 22, 255});
    DrawCircleV((Vector2){sc.x - 9, sc.y - 10}, 10, alpha(WHITE, 70));
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        rm_plant_manual_scram(P);
        logmsg("MANUAL SCRAM");
    }

    if (lampbutton((Rectangle){r.x + 124, r.y + 34, 80, 42}, "TRIP\nRESET", L_AMB, c->scram)) {
        if (c->scram) {
            rm_core_reset_scram(c);
            P->first_out[0] = 0;
            last_first_out[0] = 0;
            logmsg("TRIP RESET - RODS STAY IN UNTIL WITHDRAWN");
        }
    }
    if (lampbutton((Rectangle){r.x + 210, r.y + 34, 80, 42}, "RPS\nARMED", L_WHT, !P->rps_bypass) && P->rps_bypass) {
        P->rps_bypass = 0;
        logmsg("RPS ARMED");
    }
    if (lampbutton((Rectangle){r.x + 296, r.y + 34, 88, 42}, "RPS\nBYPASS", L_RED, P->rps_bypass) && !P->rps_bypass) {
        P->rps_bypass = 1;
        logmsg("RPS BYPASSED - TRIPS DISABLED");
    }
    window((Rectangle){r.x + 124, r.y + 84, 260, 40}, "FIRST OUT", P->first_out[0] ? P->first_out : NULL, L_RED,
           P->first_out[0] != 0);

    /* rod banks */
    float y = r.y + 140;
    dymo(r.x + 12, y, "CONTROL ROD BANKS");
    text("CM IN   BOT TOP     WITHDRAW          INSERT", r.x + 80, y + 18, 10, INK);
    y += 32;
    for (int bk = 0; bk < RM_NBANKS; bk++) {
        double pos = rm_core_bank_pos(c, bk);
        dymo(r.x + 12, y + 8, bank_name[bk]);
        readout(r.x + 76, y + 2, 16, 3, "%3.0f", pos);
        lamp(r.x + 134, y + 14, 5, L_GRN, pos >= RM_ACTIVE_H - 0.5);
        lamp(r.x + 156, y + 14, 5, L_RED, pos <= 0.5);
        int blocked = c->scram || (bk == BANK_REG && P->auto_rod);
        const char *lab[4] = {"OUT\n20", "OUT\n2", "IN\n2", "IN\n20"};
        const double step[4] = {-20, -2, 2, 20};
        for (int k = 0; k < 4; k++) {
            Rectangle br = {r.x + 176 + k * 53, y, 48, 28};
            int pressing = !blocked && CheckCollisionPointRec(m, br) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            if (lampbutton(br, lab[k], L_WHT, pressing) && !blocked) {
                double tgt = 0;
                int nk = 0;
                for (int i = 0; i < c->nctrl; i++)
                    if (c->ctrl_bank[i] == bk) { tgt += c->rod_target[i]; nk++; }
                rm_core_bank_move(c, bk, tgt / nk + step[k]);
            }
        }
        y += 34;
    }

    /* regulating bank auto control */
    y = r.y + 380;
    dymo(r.x + 12, y, "REG BANK");
    if (lampbutton((Rectangle){r.x + 12, y + 16, 60, 36}, "AUTO", L_WHT, P->auto_rod) && !P->auto_rod) {
        P->auto_rod = 1;
        logmsg("AUTOMATIC ROD CONTROL ON");
    }
    if (lampbutton((Rectangle){r.x + 76, y + 16, 60, 36}, "MAN", L_AMB, !P->auto_rod) && P->auto_rod) {
        P->auto_rod = 0;
        logmsg("AUTOMATIC ROD CONTROL OFF");
    }
    dymo(r.x + 146, y, "DEMAND %");
    readout(r.x + 146, y + 18, 22, 3, "%3.0f", 100 * P->power_set);
    if (lampbutton((Rectangle){r.x + 222, y + 16, 78, 36}, "LOWER", L_WHT, 0)) P->power_set = fmax(0.05, P->power_set - 0.05);
    if (lampbutton((Rectangle){r.x + 306, y + 16, 78, 36}, "RAISE", L_WHT, 0)) P->power_set = fmin(1.05, P->power_set + 0.05);

    /* turbine and DRACS */
    y = r.y + 444;
    dymo(r.x + 12, y, "TURBINE");
    if (lampbutton((Rectangle){r.x + 12, y + 16, 76, 36}, "TRIP", L_GRN, P->turbine_tripped) && !P->turbine_tripped) {
        P->turbine_tripped = 1;
        P->generator_breaker = 0;
        logmsg("TURBINE TRIPPED");
    }
    if (lampbutton((Rectangle){r.x + 92, y + 16, 76, 36}, "LATCH", L_RED, !P->turbine_tripped) && P->turbine_tripped) {
        P->turbine_tripped = 0;
        P->generator_breaker = 1;
        P->tv_int = -2.0;
        logmsg("TURBINE LATCHED, GENERATOR SYNCHRONISED");
    }
    dymo(r.x + 186, y, "DRACS DAMPERS");
    const char *dl[3] = {"AUTO", "OPEN", "CLOSE"};
    for (int k = 0; k < 3; k++) {
        int act = k == 0 ? P->dracs_auto : (!P->dracs_auto && ((k == 1) == (P->dracs_damper_set[0] > 0.5)));
        Color lc = k == 0 ? L_WHT : (k == 1 ? L_GRN : L_RED);
        if (lampbutton((Rectangle){r.x + 186 + k * 67, y + 16, 62, 36}, dl[k], lc, act)) {
            P->dracs_auto = k == 0;
            if (k) for (int i = 0; i < 3; i++) P->dracs_damper_set[i] = k == 1 ? 1.0 : 0.0;
            logmsg("DRACS DAMPERS %s", dl[k]);
        }
    }

    /* training sub-panel */
    Rectangle tp = {r.x + 10, r.y + 508, r.width - 20, 108};
    DrawRectangleRec(tp, (Color){196, 164, 90, 255});
    DrawRectangleLinesEx(tp, 2, BEZEL);
    for (float sx = tp.x + 4; sx < tp.x + tp.width - 12; sx += 16)
        DrawTriangle((Vector2){sx, tp.y + 2}, (Vector2){sx + 8, tp.y + 10}, (Vector2){sx + 8, tp.y + 2}, BEZEL);
    plate(tp.x + 8, tp.y + 14, "TRAINING - FAULT INJECTION", 10);
    if (lampbutton((Rectangle){tp.x + 8, tp.y + 38, 86, 36}, "LOSE\nGRID", L_RED, !P->offsite_power)) {
        P->offsite_power = !P->offsite_power;
        logmsg(P->offsite_power ? "OFFSITE POWER RESTORED" : "LOSS OF OFFSITE POWER");
    }
    for (int i = 0; i < 3; i++) {
        char lb[16];
        snprintf(lb, sizeof lb, "FAIL\nDG%d", i + 1);
        if (lampbutton((Rectangle){tp.x + 100 + i * 70, tp.y + 38, 64, 36}, lb, L_RED, !P->diesel_avail[i])) {
            P->diesel_avail[i] = !P->diesel_avail[i];
            logmsg("DIESEL %d %s", i + 1, P->diesel_avail[i] ? "REPAIRED" : "FAILED");
        }
    }
    lamp(tp.x + 16, tp.y + 90, 5, P->offsite_power ? L_RED : L_GRN, 1);
    text("GRID BKR", tp.x + 26, tp.y + 85, 10, INK);
    for (int i = 0; i < 3; i++) {
        lamp(tp.x + 116 + i * 70, tp.y + 90, 5, P->diesel_running[i] ? L_RED : L_GRN, 1);
        textf(tp.x + 126 + i * 70, tp.y + 85, 10, INK, "DG%d RUN", i + 1);
    }

    /* operator's index card */
    Rectangle cd = {r.x + 14, r.y + 626, r.width - 28, 112};
    DrawRectangleRec((Rectangle){cd.x + 3, cd.y + 3, cd.width, cd.height}, alpha(BLACK, 60));
    DrawRectangleRec(cd, (Color){244, 238, 214, 255});
    for (int k = 1; k < 8; k++)
        DrawLine((int)cd.x, (int)(cd.y + 18 + k * 12), (int)(cd.x + cd.width), (int)(cd.y + 18 + k * 12), (Color){170, 190, 220, 255});
    DrawLine((int)cd.x, (int)(cd.y + 18), (int)(cd.x + cd.width), (int)(cd.y + 18), (Color){220, 120, 120, 255});
    DrawRectangleRec((Rectangle){cd.x + 20, cd.y - 6, 44, 14}, (Color){230, 226, 200, 150});
    DrawRectangleRec((Rectangle){cd.x + cd.width - 64, cd.y - 6, 44, 14}, (Color){230, 226, 200, 150});
    const char *notes[] = {
        "OPERATOR NOTES - UNIT 1",
        "* AUTO HOLDS POWER AT DEMAND. RAISE/LOWER = LOAD.",
        "* MAN TO DRIVE THE REG BANK BY HAND.",
        "* CLICK A PUMP ON THE MIMIC TO START/STOP IT.",
        "* 2 PRIMARY PUMPS OFF = REACTOR TRIP.",
        "* AFTER A TRIP: TRIP RESET, THEN WITHDRAW RODS.",
        "* NO RPS BYPASS W/O SHIFT SUPV. APPROVAL!",
    };
    for (int i = 0; i < 7; i++)
        text(notes[i], cd.x + 8, cd.y + 5 + i * 12 + (i ? 8 : 0), 10, (Color){30, 34, 70, 255});
}

static void draw_annunciators(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, NULL);
    int pumps_off = 0;
    for (int i = 0; i < RM_NLOOPS; i++) pumps_off += !P->loop[i].ppump.motor_on || P->loop[i].ppump.tripped;
    struct { const char *l1, *l2; int on; int red; Color c; } a[] = {
        {"REACTOR", "TRIP", c->scram, 1, L_RED},
        {"NEUTRON FLUX", "HIGH", c->p_thermal > 1.05 * RM_P_RATED, 1, L_RED},
        {"PERIOD", "SHORT", isfinite(P->period) && P->period > 0 && P->period < 30, 1, L_RED},
        {"CORE FLOW", "LOW", P->W_core < 0.9 * rm_plant_nominal_flow(), 0, L_AMB},
        {"PRIMARY PUMP", "OFF", pumps_off > 0, 0, L_AMB},
        {"CORE OUTLET", "TEMP HIGH", P->T_core_out > 833.15, 0, L_AMB},
        {"CLADDING", "TEMP HIGH", rm_core_max_clad_T(c) > 923.15, 1, L_RED},
        {"TURBINE", "TRIP", P->turbine_tripped, 0, L_AMB},
        {"STEAM SAFETY", "VALVE OPEN", P->W_relief > 0, 0, L_AMB},
        {"OFFSITE", "POWER LOST", !P->offsite_power, 1, L_RED},
        {"DIESEL GEN", "RUNNING", P->diesel_running[0] || P->diesel_running[1] || P->diesel_running[2], 0, L_AMB},
        {"DRACS", "COOLING", P->Q_dracs > 1e6, 0, L_WHT},
        {"RPS", "BYPASSED", P->rps_bypass, 1, L_RED},
        {"ROD CONTROL", "AUTO", P->auto_rod, 0, L_WHT},
    };
    int n = sizeof a / sizeof a[0];
    float w = (r.width - 36) / 7, h = 50;
    int blink = ((int)(GetTime() * 2.5)) & 1;
    for (int i = 0; i < n; i++) {
        Rectangle t = {r.x + 18 + (i % 7) * w, r.y + 10 + (i / 7) * (h + 4), w - 2, h};
        window(t, a[i].l1, a[i].l2, a[i].c, a[i].on && (!a[i].red || blink));
    }
}

static void draw_log(Rectangle r)
{
    steel(r, NULL);
    plate(r.x + 16, r.y + 18, "ALARM TYPER", 10);
    lamp(r.x + 26, r.y + 60, 6, L_AMB, GetTime() - last_log_time < 3.0);
    text("NEW", r.x + 38, r.y + 55, 10, INK);
    lamp(r.x + 26, r.y + 84, 6, L_RED, 1);
    text("ON LINE", r.x + 38, r.y + 79, 10, INK);

    Rectangle pp = {r.x + 130, r.y + 8, r.width - 146, r.height - 16};
    DrawRectangleRec((Rectangle){pp.x - 4, pp.y - 2, pp.width + 8, pp.height + 4}, BEZEL);
    DrawRectangleRec(pp, (Color){240, 236, 220, 255});
    /* green-bar paper and tractor-feed holes */
    for (int i = 0; i < NLOG; i += 2)
        DrawRectangleRec((Rectangle){pp.x + 18, pp.y + 4 + i * 16, pp.width - 36, 16}, (Color){206, 228, 204, 255});
    for (float y = pp.y + 8; y < pp.y + pp.height; y += 14) {
        DrawCircleV((Vector2){pp.x + 8, y}, 3, (Color){60, 60, 56, 255});
        DrawCircleV((Vector2){pp.x + pp.width - 8, y}, 3, (Color){60, 60, 56, 255});
    }
    for (int i = 0; i < NLOG; i++)
        text(logs[i], pp.x + 26, pp.y + 7 + i * 16, 10, i == NLOG - 1 ? (Color){20, 20, 40, 255} : (Color){70, 70, 90, 255});
}

static void header(void)
{
    plate(10, 8, "RM-ELM   UNIT 1   MAIN CONTROL BOARD", 16);
    int t = (int)P->t;
    dymo(420, 4, "PLANT TIME");
    readout(420, 18, 18, 6, "%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    dymo(560, 16, "SIM RATE");
    const int sp[5] = {1, 2, 4, 8, 16};
    for (int i = 0; i < 5; i++) {
        char lb[8];
        snprintf(lb, sizeof lb, "X%d", sp[i]);
        if (lampbutton((Rectangle){(float)(626 + i * 48), 6, 44, 34}, lb, L_WHT, speed == sp[i] && !paused)) {
            speed = sp[i];
            paused = 0;
        }
    }
    if (lampbutton((Rectangle){870, 6, 60, 34}, "HOLD", L_AMB, paused)) paused = !paused;
    dymo(1180, 16, "F12 = PHOTO");
}

static void loading_screen(void)
{
    BeginDrawing();
    ClearBackground(WALL);
    Rectangle b = {340, 230, 600, 320};
    steel(b, "RM-ELM   UNIT 1");
    int blink = ((int)(GetTime() * 2)) & 1;
    readout(b.x + 190, b.y + 60, 60, 4, blink ? "8888" : "    ");
    ctext("PLANT COMPUTER CONVERGING TO RATED POWER", b.x + b.width / 2, b.y + 170, 10, INK);
    ctext("STAND BY", b.x + b.width / 2, b.y + 186, 10, INK);
    int k = (int)(GetTime() * 6) % 8;
    for (int i = 0; i < 8; i++) lamp(b.x + 160 + i * 40, b.y + 240, 8, i % 2 ? L_AMB : L_WHT, i == k);
    EndDrawing();
}

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(WIN_W, WIN_H, "RM-ELM control room");
    SetTargetFPS(60);
    P = calloc(1, sizeof *P);
    pthread_t th;
    pthread_create(&th, NULL, loader, NULL);
    double acc = 0;
    int shot = getenv("RMELM_SCREENSHOT") != NULL;
    double shot_at = shot ? atof(getenv("RMELM_SCREENSHOT")) : 0;
    while (!WindowShouldClose()) {
        if (!loaded) {
            loading_screen();
            if (loaded) {
                pthread_join(th, NULL);
                logmsg("Unit at rated power, turbine on line");
                logmsg("Click pumps on the mimic; rod controls at right");
            }
            continue;
        }
        if (!paused) {
            acc += GetFrameTime() * speed;
            int n = 0;
            while (acc >= DT && n < 16 * speed) {
                rm_plant_step(P, DT);
                acc -= DT;
                n++;
            }
            if (acc > 1.0) acc = 0;
        }
        static double last = -1;
        if (P->t - last >= 0.5) {
            if (ntr < NTR) ntr++;
            nsamp++;
            memmove(trend_p, trend_p + 1, sizeof(float) * (NTR - 1));
            memmove(trend_t, trend_t + 1, sizeof(float) * (NTR - 1));
            trend_p[NTR - 1] = (float)(100 * P->core.p_thermal / RM_P_RATED);
            trend_t[NTR - 1] = (float)(P->T_core_out - 273.15);
            last = P->t;
        }
        if (P->first_out[0] && strcmp(P->first_out, last_first_out)) {
            strcpy(last_first_out, P->first_out);
            if (strcmp(P->first_out, "MANUAL SCRAM")) logmsg("REACTOR TRIP - first out: %s", P->first_out);
        }

        BeginDrawing();
        ClearBackground(WALL);
        header();
        draw_reactor((Rectangle){6, 46, 420, 480});
        draw_mimic((Rectangle){432, 46, 440, 480});
        draw_controls((Rectangle){878, 46, 396, 748});
        draw_annunciators((Rectangle){6, 532, 866, 124});
        draw_log((Rectangle){6, 662, 866, 132});
        EndDrawing();
        if (IsKeyPressed(KEY_F12)) {
            static int nshot = 0;
            char fn[64];
            snprintf(fn, sizeof fn, "rmelm_%03d.png", ++nshot);
            TakeScreenshot(fn);
            logmsg("Photo saved: %s", fn);
        }
        if (shot && P->t >= shot_at) {
            TakeScreenshot("rmelm_screenshot.png");
            break;
        }
    }
    CloseWindow();
    return 0;
}
