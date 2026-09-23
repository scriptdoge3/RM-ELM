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
#include "rm_game.h"
#include "rm_plant.h"
#include "rm_sodium.h"

#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
static const Color LED = {255, 46, 24, 255};
static const Color L_RED = {240, 60, 44, 255};
static const Color L_GRN = {60, 220, 90, 255};
static const Color L_AMB = {250, 186, 60, 255};
static const Color L_WHT = {248, 244, 228, 255};
static const Color PEN_VIO = {120, 50, 170, 255};
static const Color PEN_RED = {200, 30, 30, 255};

/* ---- state -------------------------------------------------------------- */
static rm_plant *P;
static rm_game G;
static volatile int loaded = 0;
static int paused = 0;
static int start_hot = 0;               /* start from hot shutdown instead of rated power */
static int random_failures = 1;
static unsigned msg_seen = 0;
#define NTR 480                         /* recorder samples, one every 0.5 s */
static float trend_p[NTR], trend_t[NTR];
static int ntr = 0;
static long nsamp = 0;
#define NLOG 7
static char logs[NLOG][100];
static char last_first_out[48] = "";
static double last_log_time = -100;
static int sel_rod = -1;                /* selected rod (core index) or -1 */

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
    if (start_hot) rm_plant_hot_standby(P);
    else rm_plant_steady(P);
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
    snprintf(cell[7].val, 16, "%5.1f", log10(fmax(c->p_thermal / RM_P_RATED, 1e-12)));
    cell[7].lab = "LOG POWER";
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

/* ---- full core display and rod select matrix -------------------------------- */
static const Color BANK_C[RM_NBANKS] = {
    {240, 240, 230, 255}, {250, 200, 60, 255}, {90, 200, 110, 255},
    {80, 150, 230, 255}, {200, 120, 230, 255}, {230, 60, 50, 255},
};
static const char *bank_short[RM_NBANKS] = {"REG", "A", "B", "C", "D", "SAFE"};

/* The rods sit on an index-7 sub-lattice of the hex grid, which is itself a
 * triangular lattice turned by 19.1 degrees. Turned back, the 55 rods fall
 * into 9 staggered rows, laid out like a BWR full core display, and get
 * BWR-style XX-YY coordinates. */
static int rod_ci[128], rod_rj[128], cmin, cmax, rmin, rmax;
static char rod_id[128][24];

static void number_rods(void)
{
    rm_core *c = &P->core;
    rm_hexgrid *g = &c->dif.grid;
    double th = atan2(sqrt(3.0) / 2, 2.5), cs = cos(th), sn = sin(th);
    cmin = rmin = 1000;
    cmax = rmax = -1000;
    for (int k = 0; k < c->nctrl; k++) {
        int col = c->ctrl_col[k];
        double x = g->q[col] + 0.5 * g->r[col], y = sqrt(3.0) / 2 * g->r[col];
        rod_ci[k] = (int)lround((x * cs - y * sn) / (sqrt(7.0) / 2));
        rod_rj[k] = (int)lround((x * sn + y * cs) / (sqrt(21.0) / 2));
        if (rod_ci[k] < cmin) cmin = rod_ci[k];
        if (rod_ci[k] > cmax) cmax = rod_ci[k];
        if (rod_rj[k] < rmin) rmin = rod_rj[k];
        if (rod_rj[k] > rmax) rmax = rod_rj[k];
    }
    for (int k = 0; k < c->nctrl; k++)
        snprintf(rod_id[k], sizeof rod_id[k], "%02d-%02d", 2 * (rod_ci[k] - cmin) + 2, 4 * (rmax - rod_rj[k]) + 3);
}

/* notch position: 00 = fully inserted, 40 = fully withdrawn, 4 cm per notch */
#define NOTCH_CM 4.0
static int notch(double ins) { return (int)lround((RM_ACTIVE_H - ins) / NOTCH_CM); }

static int rod_blocked(int k, int log)
{
    rm_core *c = &P->core;
    if (c->scram) {
        if (log) logmsg("ROD MOTION BLOCKED: REACTOR TRIPPED");
        return 1;
    }
    if (c->ctrl_bank[k] == BANK_REG && P->auto_rod) {
        if (log) logmsg("ROD %s IS IN REG BANK - SELECT MAN FIRST", rod_id[k]);
        return 1;
    }
    return 0;
}

static void draw_rodselect(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, "FULL CORE DISPLAY");
    Vector2 m = GetMousePosition();
    int blink = ((int)(GetTime() * 3)) & 1;
    int hover = -1;

    /* upper board: one 2-digit notch readout per rod on a ruled grid */
    const float cp = 22, rp = 29;
    float gx0 = r.x + 40 + (r.width - 60 - (cmax - cmin) * cp) / 2, gy0 = r.y + 42;
    float lx = r.x + 30, rx = r.x + r.width - 12, by = gy0 + (rmax - rmin) * rp + 22;
    for (int j = rmin; j <= rmax; j++) {
        float y = gy0 + (j - rmin) * rp;
        DrawLineEx((Vector2){lx, y}, (Vector2){rx, y}, 1.5f, INK);
        textf(r.x + 12, y - 4, 10, INK, "%02d", 4 * (rmax - j) + 3);
    }
    for (int i = cmin; i <= cmax; i++) {
        int top = rmax + 1;
        for (int k = 0; k < c->nctrl; k++)
            if (rod_ci[k] == i && rod_rj[k] < top) top = rod_rj[k];
        if (top > rmax) continue;
        float x = gx0 + (i - cmin) * cp;
        DrawLineEx((Vector2){x, gy0 + (top - rmin) * rp}, (Vector2){x, by}, 1.5f, INK);
        char lb[12];
        snprintf(lb, sizeof lb, "%02d", 2 * (i - cmin) + 2);
        ctext(lb, x, by + 3, 10, INK);
    }
    for (int k = 0; k < c->nctrl; k++) {
        float x = gx0 + (rod_ci[k] - cmin) * cp, y = gy0 + (rod_rj[k] - rmin) * rp;
        Rectangle box = {x - 15.5f, y - 12, 31, 24};
        if (k == sel_rod)
            DrawRectangleLinesEx((Rectangle){box.x - 3, box.y - 3, box.width + 6, box.height + 6}, 2,
                                 blink ? L_WHT : alpha(L_WHT, 110));
        readout(box.x, box.y, 14, 2, "%02d", notch(c->rod_ins[k]));
        if (fabs(c->rod_vel[k]) > 0 && !c->scram) DrawCircleV((Vector2){box.x + box.width - 2, box.y + 2}, 2.5f, L_AMB);
        if (CheckCollisionPointRec(m, box)) {
            hover = k;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) sel_rod = sel_rod == k ? -1 : k;
        }
    }

    /* desk: rod select matrix and drive */
    Rectangle d = {r.x + 6, r.y + 318, r.width - 12, 156};
    DrawRectangleRec(d, (Color){126, 144, 128, 255});
    DrawRectangleRec((Rectangle){d.x, d.y, d.width, 2}, PAINT_HI);
    DrawRectangleLinesEx(d, 1, BEZEL);
    dymo(d.x + 8, d.y + 5, "ROD SELECT");
    Rectangle mx = {d.x + 8, d.y + 22, 188, 128};
    DrawRectangleRec(mx, (Color){44, 48, 46, 255});
    DrawRectangleLinesEx(mx, 2, BEZEL);
    float mx0 = mx.x + 13 + (mx.width - 26 - (cmax - cmin) * 11) / 2, my0 = mx.y + 7;
    for (int k = 0; k < c->nctrl; k++) {
        Rectangle bt = {mx0 + (rod_ci[k] - cmin) * 11 - 9, my0 + (rod_rj[k] - rmin) * 13.5f, 18, 10};
        int on = k == sel_rod;
        int hv = CheckCollisionPointRec(m, bt);
        DrawRectangleRec(bt, on ? L_WHT : (Color){22, 22, 20, 255});
        DrawRectangleRec((Rectangle){bt.x, bt.y, bt.width, 1}, on ? WHITE : (Color){90, 90, 86, 255});
        if (hv) {
            DrawRectangleLinesEx(bt, 1, (Color){170, 170, 160, 255});
            hover = k;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) sel_rod = sel_rod == k ? -1 : k;
        }
    }

    float x = d.x + 206, y = d.y + 6;
    int s = sel_rod;
    dymo(x, y, "SELECTED ROD");
    readout(x, y + 16, 16, 5, "%s", s >= 0 ? rod_id[s] : "-----");
    dymo(x + 90, y, "NOTCH");
    if (s >= 0) readout(x + 90, y + 16, 16, 2, "%02d", notch(c->rod_ins[s]));
    else readout(x + 90, y + 16, 16, 2, "--");
    dymo(x + 140, y, "BANK");
    DrawRectangleRec((Rectangle){x + 140, y + 16, 60, 26}, BEZEL);
    if (s >= 0) {
        DrawRectangleRec((Rectangle){x + 143, y + 19, 54, 20}, BANK_C[c->ctrl_bank[s]]);
        ctext(bank_short[c->ctrl_bank[s]], x + 170, y + 24, 10, INK);
    }
    const char *lab[4] = {"WITHDRAW\n1 NOTCH", "INSERT\n1 NOTCH", "WITHDRAW\n5 NOTCH", "INSERT\n5 NOTCH"};
    const int dn[4] = {1, -1, 5, -5};
    for (int k = 0; k < 4; k++) {
        Rectangle br = {x + (k % 2) * 106, d.y + 54 + (k / 2) * 36, 100, 32};
        int pressing = s >= 0 && CheckCollisionPointRec(m, br) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        if (lampbutton(br, lab[k], L_WHT, pressing) && s >= 0 && !rod_blocked(s, 1)) {
            int nt = notch(c->rod_target[s]) + dn[k];
            if (nt < 0) nt = 0;
            if (nt > (int)(RM_ACTIVE_H / NOTCH_CM)) nt = (int)(RM_ACTIVE_H / NOTCH_CM);
            rm_core_rod_move(c, s, RM_ACTIVE_H - NOTCH_CM * nt);
        }
    }
    text("NOTCH 00 = FULL IN   40 = FULL OUT", x, d.y + 128, 10, INK);
    text("4 CM PER NOTCH", x, d.y + 140, 10, INK);

    if (hover >= 0) {
        char tip[64];
        snprintf(tip, sizeof tip, "ROD %s  %s  NOTCH %02d  (%.0f CM IN)", rod_id[hover],
                 bank_name[c->ctrl_bank[hover]], notch(c->rod_ins[hover]), c->rod_ins[hover]);
        float tw = (float)MeasureText(tip, 10) + 10;
        float tx = fminf(m.x + 12, r.x + r.width - tw - 6), ty = m.y - 24;
        DrawRectangleRec((Rectangle){tx, ty, tw, 16}, (Color){244, 238, 214, 255});
        DrawRectangleLinesEx((Rectangle){tx, ty, tw, 16}, 1, BEZEL);
        text(tip, tx + 5, ty + 3, 10, INK);
    }
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
            if (lampbutton(br, lab[k], L_WHT, pressing) && !blocked) rm_core_bank_shift(c, bk, step[k]);
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
    if (lampbutton((Rectangle){r.x + 222, y + 16, 78, 36}, "LOWER", L_WHT, 0)) P->power_set = fmax(0.01, P->power_set - (P->power_set > 0.1 ? 0.05 : 0.01));
    if (lampbutton((Rectangle){r.x + 306, y + 16, 78, 36}, "RAISE", L_WHT, 0)) P->power_set = fmin(1.05, P->power_set + (P->power_set >= 0.1 ? 0.05 : 0.01));

    /* turbine and DRACS */
    y = r.y + 444;
    dymo(r.x + 12, y, "TURBINE");
    if (lampbutton((Rectangle){r.x + 12, y + 16, 76, 36}, "TRIP", L_GRN, P->turbine_tripped) && !P->turbine_tripped) {
        P->turbine_tripped = 1;
        P->generator_breaker = 0;
        logmsg("TURBINE TRIPPED");
    }
    if (lampbutton((Rectangle){r.x + 92, y + 16, 76, 36}, "LATCH", L_RED, !P->turbine_tripped) && P->turbine_tripped) {
        if (c->scram) logmsg("LATCH BLOCKED: REACTOR TRIPPED");
        else if (!P->offsite_power) logmsg("LATCH BLOCKED: NO GRID TO SYNCHRONISE TO");
        else if (P->p_header < 10.0e6) logmsg("LATCH BLOCKED: STEAM PRESSURE BELOW 10 MPA");
        else if (c->p_thermal < 0.08 * RM_P_RATED) logmsg("LATCH BLOCKED: REACTOR POWER BELOW 8%%");
        else {
            P->turbine_tripped = 0;
            P->generator_breaker = 1;
            P->tv_int = -2.0;
            logmsg("TURBINE LATCHED, GENERATOR SYNCHRONISED");
        }
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

    /* loop control: pumps, hydrogen meters, SG isolation, feedwater */
    Rectangle tp = {r.x + 10, r.y + 506, r.width - 20, 232};
    DrawRectangleRec(tp, (Color){138, 156, 140, 255});
    DrawRectangleLinesEx(tp, 1, BEZEL);
    dymo(tp.x + 6, tp.y + 8, "LOOPS");
    const char *rows[4] = {"PRIMARY", "SECONDARY", "H2 PPM", "SG"};
    for (int k = 0; k < 4; k++) dymo(tp.x + 6, tp.y + 36 + k * 38, rows[k]);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        float x = tp.x + 76 + i * 74, yy = tp.y + 8;
        char lb[16];
        snprintf(lb, sizeof lb, "LOOP %d", i + 1);
        dymo(x + 12, yy, lb);
        rm_pump *pm[2] = {&l->ppump, &l->spump};
        for (int k = 0; k < 2; k++) {
            int run = pm[k]->motor_on && !pm[k]->tripped;
            float by = tp.y + 28 + k * 38;
            if (lampbutton((Rectangle){x, by, 34, 30}, "RUN", L_RED, run) && !run) {
                pm[k]->tripped = 0;
                pm[k]->motor_on = 1;
                pm[k]->speed_set = 1.0;
                logmsg("%s PUMP %d STARTED", k ? "SECONDARY" : "PRIMARY", i + 1);
            }
            if (lampbutton((Rectangle){x + 36, by, 34, 30}, "STOP", L_GRN, !run) && run) {
                pm[k]->motor_on = 0;
                logmsg("%s PUMP %d STOPPED", k ? "SECONDARY" : "PRIMARY", i + 1);
            }
        }
        readout(x, tp.y + 105, 15, 4, "%4.2f", fmin(l->sg.h2, 99.99));
        if (l->sg.h2 > RM_H2_ALARM) lamp(x + 64, tp.y + 105, 4, L_RED, ((int)(GetTime() * 3)) & 1);
        int iso = l->sg.isolated;
        if (lampbutton((Rectangle){x, tp.y + 142, 70, 30}, l->sg.disc_burst ? "DISC\nBURST" : "ISOLATE",
                       l->sg.disc_burst ? L_RED : L_AMB, iso) && !iso) {
            rm_plant_isolate_sg(P, i);
        }
    }
    float fy = tp.y + 186;
    dymo(tp.x + 6, fy + 10, "FEEDWATER");
    if (lampbutton((Rectangle){tp.x + 76, fy, 70, 36}, "START", L_RED, P->fw_on) && !P->fw_on) {
        P->fw_on = 1;
        logmsg("FEEDWATER PUMPS STARTED");
    }
    if (lampbutton((Rectangle){tp.x + 150, fy, 70, 36}, "STOP", L_GRN, !P->fw_on) && P->fw_on) {
        P->fw_on = 0;
        logmsg("FEEDWATER PUMPS STOPPED");
    }
    double wfw = 0;
    for (int i = 0; i < RM_NLOOPS; i++) wfw += P->loop[i].sg.W_fw;
    dymo(tp.x + 232, fy - 4, "FEED KG/S");
    readout(tp.x + 232, fy + 10, 16, 4, "%4.0f", wfw);
}

static void draw_annunciators(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, NULL);
    int pumps_off = 0, h2 = 0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        pumps_off += !l->ppump.motor_on || l->ppump.tripped || !l->spump.motor_on || l->spump.tripped;
        h2 |= l->sg.h2 > RM_H2_ALARM;
    }
    double reg = rm_core_bank_pos(c, BANK_REG);
    struct { const char *l1, *l2; int on; int red; Color c; } a[] = {
        {"REACTOR", "TRIP", c->scram, 1, L_RED},
        {"NEUTRON FLUX", "HIGH", c->p_thermal > 1.05 * RM_P_RATED, 1, L_RED},
        {"PERIOD", "SHORT", isfinite(P->period) && P->period > 0 && P->period < 30, 1, L_RED},
        {"CORE FLOW", "LOW", P->W_core < 0.9 * rm_plant_nominal_flow(), 0, L_AMB},
        {"SODIUM PUMP", "OFF", pumps_off > 0, 0, L_AMB},
        {"CORE OUTLET", "TEMP HIGH", P->T_core_out > 833.15, 0, L_AMB},
        {"CLADDING", "TEMP HIGH", rm_core_max_clad_T(c) > 923.15, 1, L_RED},
        {"H2 IN SODIUM", "HIGH", h2, 1, L_RED},
        {"TURBINE", "TRIP", P->turbine_tripped, 0, L_AMB},
        {"STEAM SAFETY", "VALVE OPEN", P->W_relief > 0, 0, L_AMB},
        {"OFFSITE", "POWER LOST", !P->offsite_power, 1, L_RED},
        {"DIESEL GEN", "RUNNING", P->diesel_running[0] || P->diesel_running[1] || P->diesel_running[2], 0, L_AMB},
        {"DRACS", "COOLING", P->Q_dracs > 1e6, 0, L_WHT},
        {"RPS", "BYPASSED", P->rps_bypass, 1, L_RED},
        {"REG BANK", "AT LIMIT", P->auto_rod && !c->scram && (reg < 0.5 || reg > RM_ACTIVE_H - 0.5), 0, L_AMB},
        {"LOAD", "DEVIATION", G.on_line && fabs(rm_game_deviation(&G, P)) > 0.05, 0, L_AMB},
    };
    int n = sizeof a / sizeof a[0];
    float w = (r.width - 36) / 8, h = 50;
    int blink = ((int)(GetTime() * 2.5)) & 1;
    for (int i = 0; i < n; i++) {
        Rectangle t = {r.x + 18 + (i % 8) * w, r.y + 10 + (i / 8) * (h + 4), w - 2, h};
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
    dymo(400, 4, "PLANT TIME");
    readout(400, 18, 18, 6, "%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    dymo(540, 4, "DISPATCH MWE");
    if (G.on_line) readout(540, 18, 18, 4, "%4.0f", G.demand);
    else readout(540, 18, 18, 4, "----");
    dymo(630, 4, "NET MWE");
    readout(630, 18, 18, 4, "%4.0f", fmax(P->P_net / 1e6, 0.0));
    dymo(720, 4, "STEAM MPA");
    readout(720, 18, 18, 3, "%4.1f", P->p_header / 1e6);
    dymo(800, 4, "SCORE");
    readout(800, 18, 18, 6, "%6.0f", fmax(fmin(G.score, 999999.0), -99999.0));
    dymo(920, 4, "MWH SENT");
    readout(920, 18, 18, 6, "%6.0f", fmin(G.mwh, 999999.0));
    if (lampbutton((Rectangle){1060, 6, 60, 34}, "HOLD", L_AMB, paused)) paused = !paused;
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
    ctext(start_hot ? "PLANT COMPUTER SETTING UP HOT SHUTDOWN" : "PLANT COMPUTER CONVERGING TO RATED POWER",
          b.x + b.width / 2, b.y + 170, 10, INK);
    ctext("STAND BY", b.x + b.width / 2, b.y + 186, 10, INK);
    int k = (int)(GetTime() * 6) % 8;
    for (int i = 0; i < 8; i++) lamp(b.x + 160 + i * 40, b.y + 240, 8, i % 2 ? L_AMB : L_WHT, i == k);
    EndDrawing();
}

/* returns 1 once a start has been chosen */
static int start_menu(void)
{
    int go = 0;
    BeginDrawing();
    ClearBackground(WALL);
    Rectangle b = {290, 150, 700, 500};
    steel(b, "RM-ELM   UNIT 1   SHIFT TURNOVER");
    float x = b.x + 40, y = b.y + 50;
    const char *brief[] = {
        "YOU HAVE THE WATCH. THE LOAD DISPATCHER WILL ORDER NET OUTPUT IN MWE:",
        "FOLLOW IT WITH THE POWER DEMAND. POINTS FOR EVERY MWH SENT ON TARGET,",
        "PENALTIES FOR TRIPS. EQUIPMENT CAN FAIL AT ANY TIME - WATCH THE",
        "ANNUNCIATORS, THE ALARM TYPER AND THE HYDROGEN METERS.",
        "",
        "HOT SHUTDOWN START: WITHDRAW SAFETY BANK, THEN SHIMS UNTIL THE PERIOD",
        "GOES POSITIVE. SELECT AUTO WITH A LOW DEMAND, START FEEDWATER AT A FEW",
        "PERCENT, LATCH THE TURBINE ABOVE 8%, THEN RAISE POWER. PULL SHIMS WHEN",
        "THE REG BANK REACHES ITS LIMIT.",
    };
    for (int i = 0; i < 9; i++) text(brief[i], x, y + i * 16, 10, INK);
    y += 170;
    if (lampbutton((Rectangle){x, y, 300, 70}, "START AT\nRATED POWER", L_WHT, !start_hot)) {
        start_hot = 0;
        go = 1;
    }
    if (lampbutton((Rectangle){x + 320, y, 300, 70}, "START FROM\nHOT SHUTDOWN", L_AMB, start_hot)) {
        start_hot = 1;
        go = 1;
    }
    y += 100;
    dymo(x, y + 12, "RANDOM EQUIPMENT FAILURES");
    if (lampbutton((Rectangle){x + 190, y, 70, 36}, "ON", L_RED, random_failures)) random_failures = 1;
    if (lampbutton((Rectangle){x + 264, y, 70, 36}, "OFF", L_GRN, !random_failures)) random_failures = 0;
    text("ESC QUITS", b.x + b.width - 100, b.y + b.height - 24, 10, INK);
    EndDrawing();
    return go;
}

int main(void)
{
    /* no MSAA: many drivers refuse a multisampled GLX config, and the flat
     * 1978 panel art does not need it */
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(WIN_W, WIN_H, "RM-ELM control room");
    if (!IsWindowReady()) {
        fprintf(stderr,
                "rmelm_gui: could not open an OpenGL 2.1 window.\n"
                "Check that your graphics driver works (glxinfo -B),\n"
                "or play the terminal version: ./build/rmelm\n");
        return 1;
    }
    SetTargetFPS(60);
    P = calloc(1, sizeof *P);
    pthread_t th;
    double acc = 0;
    int shot = getenv("RMELM_SCREENSHOT") != NULL;
    double shot_at = shot ? atof(getenv("RMELM_SCREENSHOT")) : 0;
    /* RMELM_START=rated|hot skips the menu (screenshots, scripted runs) */
    const char *st = getenv("RMELM_START");
    int started = 0;
    if (st || shot) {
        start_hot = st && st[0] == 'h';
        started = 1;
    }
    if (getenv("RMELM_FAILURES")) random_failures = atoi(getenv("RMELM_FAILURES"));
    if (started) pthread_create(&th, NULL, loader, NULL);
    while (!WindowShouldClose()) {
        if (!started) {
            if (start_menu()) {
                started = 1;
                pthread_create(&th, NULL, loader, NULL);
            }
            continue;
        }
        if (!loaded) {
            loading_screen();
            if (loaded) {
                pthread_join(th, NULL);
                number_rods();
                rm_game_init(&G, P, (unsigned)time(NULL), random_failures);
                msg_seen = P->nmsg;
                if (start_hot) {
                    logmsg("Unit in hot shutdown: all rods in, sodium at 380 C");
                    logmsg("Withdraw SAFETY bank, then shims to criticality");
                } else {
                    logmsg("Unit at rated power, turbine on line");
                    logmsg("Follow the load dispatcher's orders");
                }
            }
            continue;
        }
        if (!paused) {
            acc += GetFrameTime();
            int n = 0;
            while (acc >= DT && n < 8) {
                rm_plant_step(P, DT);
                rm_game_step(&G, P, DT);
                acc -= DT;
                n++;
            }
            if (acc > 1.0) acc = 0;
        }
        while (msg_seen < P->nmsg) logmsg("%s", P->msg[msg_seen++ % 16]);
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
        draw_rodselect((Rectangle){432, 46, 440, 480});
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
