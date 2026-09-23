/*
 * RM-ELM graphical control room (raylib). Point-and-click front end for the
 * same plant model the terminal drives: reactor panel with trends, a plant
 * mimic with the four loops, rod and plant controls, annunciators and a
 * message log.
 */
#include "raylib.h"
#include "rm_plant.h"
#include "rm_sodium.h"

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
static const Color BG = {16, 20, 24, 255};
static const Color PANEL = {26, 32, 38, 255};
static const Color EDGE = {52, 62, 72, 255};
static const Color TXT = {220, 226, 232, 255};
static const Color DIMT = {130, 140, 150, 255};
static const Color AMBERC = {255, 176, 0, 255};
static const Color REDC = {220, 50, 40, 255};
static const Color GREENC = {60, 200, 110, 255};
static const Color BLUEC = {70, 150, 240, 255};

/* ---- state -------------------------------------------------------------- */
static rm_plant *P;
static volatile int loaded = 0;
static int speed = 1, paused = 0;
#define NTR 240
static float trend_p[NTR], trend_t[NTR];
static int ntr = 0;
#define NLOG 8
static char logs[NLOG][100];
static char last_first_out[48] = "";

static void logmsg(const char *fmt, ...)
{
    for (int i = 0; i < NLOG - 1; i++) memcpy(logs[i], logs[i + 1], sizeof logs[i]);
    char m[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    int t = P ? (int)P->t : 0;
    snprintf(logs[NLOG - 1], sizeof logs[0], "%02d:%02d:%02d  %s", t / 3600, t / 60 % 60, t % 60, m);
}

static void *loader(void *arg)
{
    (void)arg;
    rm_plant_init(P);
    rm_plant_steady(P);
    loaded = 1;
    return NULL;
}

/* ---- tiny immediate-mode widgets ---------------------------------------- */
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

static void panel(Rectangle r, const char *title)
{
    DrawRectangleRec(r, PANEL);
    DrawRectangleLinesEx(r, 1, EDGE);
    if (title) text(title, r.x + 10, r.y + 8, 16, DIMT);
}

static int button(Rectangle r, const char *label, Color base, int active)
{
    Vector2 m = GetMousePosition();
    int hover = CheckCollisionPointRec(m, r);
    Color c = active ? base : (Color){(unsigned char)(base.r / 3 + 20), (unsigned char)(base.g / 3 + 20),
                                       (unsigned char)(base.b / 3 + 20), 255};
    if (hover) c = (Color){(unsigned char)fminf(c.r + 30, 255), (unsigned char)fminf(c.g + 30, 255),
                           (unsigned char)fminf(c.b + 30, 255), 255};
    DrawRectangleRounded(r, 0.2f, 4, c);
    int fs = r.height > 40 ? 22 : 14;
    int tw = MeasureText(label, fs);
    DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - fs) / 2), fs,
             active ? BLACK : TXT);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void bar(Rectangle r, float frac, Color c)
{
    DrawRectangleRec(r, (Color){10, 12, 14, 255});
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    DrawRectangle((int)r.x, (int)r.y, (int)(r.width * frac), (int)r.height, c);
    DrawRectangleLinesEx(r, 1, EDGE);
}

/* temperature to colour: 300 C blue -> 450 C green -> 560 C amber -> 700 C red */
static Color tcolor(double K)
{
    double C = K - 273.15;
    double s = (C - 300.0) / 400.0;
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    Color a, b;
    double f;
    if (s < 0.4) { a = BLUEC; b = GREENC; f = s / 0.4; }
    else if (s < 0.65) { a = GREENC; b = AMBERC; f = (s - 0.4) / 0.25; }
    else { a = AMBERC; b = REDC; f = (s - 0.65) / 0.35; }
    return (Color){(unsigned char)(a.r + (b.r - a.r) * f), (unsigned char)(a.g + (b.g - a.g) * f),
                   (unsigned char)(a.b + (b.b - a.b) * f), 255};
}

static void pipe(float x1, float y1, float x2, float y2, double K, float thick)
{
    DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, thick, tcolor(K));
}

/* ---- panels --------------------------------------------------------------- */
static const char *bank_name[RM_NBANKS] = {"REG", "SHIM A", "SHIM B", "SHIM C", "SHIM D", "SAFETY"};

static void draw_reactor(Rectangle r)
{
    rm_core *c = &P->core;
    panel(r, "REACTOR");
    double pct = 100 * c->p_thermal / RM_P_RATED;
    textf(r.x + 14, r.y + 34, 44, pct > 110 ? REDC : AMBERC, "%6.1f %%", pct);
    textf(r.x + 250, r.y + 34, 20, TXT, "%6.0f MWt", c->p_thermal / 1e6);
    textf(r.x + 250, r.y + 56, 14, DIMT, "fission %5.0f MW", c->p_fission / 1e6);
    textf(r.x + 250, r.y + 72, 14, DIMT, "decay   %5.0f MW", c->p_decay / 1e6);
    bar((Rectangle){r.x + 14, r.y + 92, r.width - 28, 14}, (float)(pct / 120.0), pct > 110 ? REDC : AMBERC);
    /* trend */
    Rectangle g = {r.x + 14, r.y + 118, r.width - 28, 150};
    DrawRectangleRec(g, (Color){10, 12, 14, 255});
    for (int i = 1; i < 4; i++)
        DrawLine((int)g.x, (int)(g.y + g.height * i / 4), (int)(g.x + g.width), (int)(g.y + g.height * i / 4), EDGE);
    for (int k = NTR - ntr + 1; k < NTR; k++) {
        float x0 = g.x + g.width * (k - 1) / (NTR - 1), x1 = g.x + g.width * k / (NTR - 1);
        float y0 = g.y + g.height * (1 - fminf(trend_p[k - 1], 120.0f) / 120.0f);
        float y1 = g.y + g.height * (1 - fminf(trend_p[k], 120.0f) / 120.0f);
        DrawLineEx((Vector2){x0, y0}, (Vector2){x1, y1}, 2, AMBERC);
        float t0 = g.y + g.height * (1 - fminf(fmaxf(trend_t[k - 1] - 300, 0), 400) / 400.0f);
        float t1 = g.y + g.height * (1 - fminf(fmaxf(trend_t[k] - 300, 0), 400) / 400.0f);
        DrawLineEx((Vector2){x0, t0}, (Vector2){x1, t1}, 2, REDC);
    }
    DrawRectangleLinesEx(g, 1, EDGE);
    text("power 0-120%", g.x + 6, g.y + 4, 12, AMBERC);
    text("core outlet 300-700 C", g.x + 110, g.y + 4, 12, REDC);
    textf(g.x + g.width - 70, g.y + g.height - 14, 12, DIMT, "last %d s", ntr);

    double beta = 0;
    for (int i = 0; i < 6; i++) beta += c->pk.beta[i];
    float y = r.y + 280;
    textf(r.x + 14, y, 18, TXT, "Reactivity  %+7.1f pcm  (%+5.2f $)", 1e5 * c->rho, c->rho / beta);
    if (isfinite(P->period) && fabs(P->period) < 9999)
        textf(r.x + 14, y + 24, 18, fabs(P->period) < 20 ? REDC : TXT, "Period      %+7.1f s", P->period);
    else text("Period       infinite", r.x + 14, y + 24, 18, TXT);
    textf(r.x + 14, y + 52, 18, TXT, "Core flow   %5.1f %%   %5.0f kg/s", 100 * P->W_core / rm_plant_nominal_flow(), P->W_core);
    textf(r.x + 14, y + 76, 18, TXT, "Inlet  %5.1f C    Outlet  ", P->T_core_in - 273.15);
    textf(r.x + 250, y + 76, 18, tcolor(P->T_core_out), "%5.1f C", P->T_core_out - 273.15);
    double tf = rm_core_max_fuel_T(c), tc = rm_core_max_clad_T(c);
    textf(r.x + 14, y + 100, 18, tf > 1900 ? REDC : TXT, "Fuel max    %5.0f C", tf - 273.15);
    textf(r.x + 220, y + 100, 18, tc > 923 ? REDC : TXT, "Clad max %4.0f C", tc - 273.15);
    textf(r.x + 14, y + 124, 16, DIMT, "Doppler %5.2f pcm/K   peaking %4.2f", 1e5 * c->rho_doppler_coef, c->peak_factor);
}

static void draw_mimic(Rectangle r)
{
    panel(r, "PLANT");
    rm_core *c = &P->core;
    /* reactor vessel */
    Rectangle v = {r.x + 20, r.y + 60, 90, 300};
    DrawRectangleRounded(v, 0.25f, 6, (Color){36, 44, 52, 255});
    DrawRectangleRoundedLinesEx(v, 0.25f, 6, 2, EDGE);
    DrawRectangle((int)v.x + 20, (int)(v.y + 110), 50, 90, tcolor((P->T_core_in + P->T_core_out) / 2));
    text("CORE", v.x + 27, v.y + 147, 14, BLACK);
    textf(v.x + 6, v.y + 10, 14, tcolor(P->T_core_out), "%3.0f C", P->T_core_out - 273.15);
    textf(v.x + 6, v.y + 276, 14, tcolor(P->T_core_in), "%3.0f C", P->T_core_in - 273.15);
    if (c->scram) text("TRIPPED", v.x + 10, v.y + 220, 16, REDC);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        float y = r.y + 70 + i * 82;
        double Th = rm_na_T(l->hot.h[RM_PIPE_N - 1]), Tc = rm_na_T(l->cold.h[RM_PIPE_N - 1]);
        double Tsh = rm_na_T(l->shot.h[RM_PIPE_N - 1]), Tsc = rm_na_T(l->scold.h[RM_PIPE_N - 1]);
        float thick = 2 + 5 * (float)(l->W / 2830.0);
        /* primary hot leg to IHX, IHX back through pump */
        pipe(v.x + v.width, y, r.x + 190, y, Th, thick);
        pipe(r.x + 190, y + 34, v.x + v.width, y + 34, Tc, thick);
        Rectangle ihx = {r.x + 190, y - 6, 34, 46};
        DrawRectangleRec(ihx, (Color){60, 70, 80, 255});
        text("IHX", ihx.x + 5, ihx.y + 16, 12, TXT);
        /* primary pump */
        Rectangle pp = {r.x + 132, y + 22, 24, 24};
        Color pc = l->ppump.tripped ? REDC : (l->ppump.motor_on ? GREENC : (l->ppump.pony_on && l->ppump.speed > 0.05 ? AMBERC : DIMT));
        DrawCircle((int)(pp.x + 12), (int)(pp.y + 12), 12, pc);
        textf(pp.x + 1, pp.y + 6, 12, BLACK, "P%d", i + 1);
        if (CheckCollisionPointRec(GetMousePosition(), pp) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (l->ppump.motor_on && !l->ppump.tripped) { l->ppump.motor_on = 0; logmsg("Primary pump %d stopped", i + 1); }
            else { l->ppump.tripped = 0; l->ppump.motor_on = 1; l->ppump.speed_set = 1.0; logmsg("Primary pump %d started", i + 1); }
        }
        textf(r.x + 118, y - 16, 12, DIMT, "%4.0f kg/s", l->W);
        /* secondary loop IHX -> SG */
        float st = 2 + 4 * (float)(l->Ws / 2744.0);
        pipe(r.x + 224, y, r.x + 300, y, Tsh, st);
        pipe(r.x + 300, y + 34, r.x + 224, y + 34, Tsc, st);
        Rectangle sp = {r.x + 250, y + 22, 24, 24};
        Color sc = l->spump.tripped ? REDC : (l->spump.motor_on ? GREENC : DIMT);
        DrawCircle((int)(sp.x + 12), (int)(sp.y + 12), 12, sc);
        textf(sp.x + 1, sp.y + 6, 12, BLACK, "S%d", i + 1);
        if (CheckCollisionPointRec(GetMousePosition(), sp) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (l->spump.motor_on && !l->spump.tripped) { l->spump.motor_on = 0; logmsg("Secondary pump %d stopped", i + 1); }
            else { l->spump.tripped = 0; l->spump.motor_on = 1; logmsg("Secondary pump %d started", i + 1); }
        }
        /* steam generator */
        Rectangle sg = {r.x + 300, y - 10, 40, 54};
        DrawRectangleRounded(sg, 0.4f, 4, (Color){60, 70, 80, 255});
        text("SG", sg.x + 11, sg.y + 20, 14, TXT);
        textf(sg.x - 8, sg.y + 56, 12, DIMT, "%3.0f MW", l->sg.Q / 1e6);
        /* steam line */
        pipe(sg.x + sg.width, y + 4, r.x + 370, y + 4, l->sg.T_steam, 3);
        pipe(r.x + 370, y + 4, r.x + 370, r.y + 200, l->sg.T_steam, 3);
    }
    /* header, turbine, generator */
    Rectangle tb = {r.x + 355, r.y + 190, 70, 60};
    DrawTriangle((Vector2){tb.x, tb.y + 10}, (Vector2){tb.x, tb.y + 50}, (Vector2){tb.x + 50, tb.y + 60},
                 P->turbine_tripped ? (Color){80, 40, 40, 255} : (Color){90, 100, 110, 255});
    DrawTriangle((Vector2){tb.x, tb.y + 10}, (Vector2){tb.x + 50, tb.y + 60}, (Vector2){tb.x + 50, tb.y},
                 P->turbine_tripped ? (Color){80, 40, 40, 255} : (Color){90, 100, 110, 255});
    text("TURBINE", tb.x - 6, tb.y + 64, 12, P->turbine_tripped ? REDC : TXT);
    textf(r.x + 20, r.y + r.height - 70, 18, TXT, "Steam %5.2f MPa   %s", P->p_header / 1e6,
          P->W_relief > 0 ? "SAFETY VALVES OPEN" : (P->bypass_valve > 0.01 ? "bypass to condenser" : ""));
    textf(r.x + 20, r.y + r.height - 44, 26, P->P_net > 0 ? GREENC : DIMT, "%5.0f MWe net", P->P_net / 1e6);
    textf(r.x + 240, r.y + r.height - 38, 16, DIMT, "gross %4.0f  house %3.0f", P->P_gen / 1e6, P->P_house / 1e6);
    text("click a pump to start/stop it", r.x + 150, r.y + 12, 12, DIMT);
}

static void draw_controls(Rectangle r)
{
    rm_core *c = &P->core;
    panel(r, "CONTROLS");
    if (button((Rectangle){r.x + 12, r.y + 32, 180, 56}, "SCRAM", REDC, c->scram)) {
        rm_plant_manual_scram(P);
        logmsg("MANUAL SCRAM");
    }
    if (button((Rectangle){r.x + 200, r.y + 32, 170, 56}, "RESET TRIP", AMBERC, 0)) {
        if (c->scram) {
            rm_core_reset_scram(c);
            P->first_out[0] = 0;
            last_first_out[0] = 0;
            logmsg("Trip reset - rods stay in until you pull them");
        }
    }
    /* rods */
    float y = r.y + 100;
    text("CONTROL RODS  bar = how far in   cm: - out / + in", r.x + 12, y, 12, DIMT);
    y += 20;
    for (int b = 0; b < RM_NBANKS; b++) {
        double pos = rm_core_bank_pos(c, b);
        text(bank_name[b], r.x + 12, y + 4, 14, TXT);
        bar((Rectangle){r.x + 78, y + 3, 110, 14}, (float)(pos / RM_ACTIVE_H), b == BANK_SAFETY ? REDC : BLUEC);
        textf(r.x + 192, y + 4, 12, DIMT, "%3.0f", pos);
        int blocked = c->scram || (b == BANK_REG && P->auto_rod);
        const char *lab[4] = {"-20", "-2", "+2", "+20"};
        const double step[4] = {-20, -2, 2, 20};
        for (int k = 0; k < 4; k++)
            if (button((Rectangle){r.x + 222 + k * 37, y, 34, 20}, lab[k], BLUEC, 0) && !blocked) {
                double tgt = 0;
                int nk = 0;
                for (int i = 0; i < c->nctrl; i++)
                    if (c->ctrl_bank[i] == b) { tgt += c->rod_target[i]; nk++; }
                rm_core_bank_move(c, b, tgt / nk + step[k]);
            }
        y += 26;
    }
    /* auto power */
    y += 6;
    if (button((Rectangle){r.x + 12, y, 110, 26}, "AUTO ROD", GREENC, P->auto_rod)) {
        P->auto_rod = !P->auto_rod;
        logmsg("Automatic rod control %s", P->auto_rod ? "ON" : "OFF");
    }
    textf(r.x + 132, y + 6, 16, TXT, "setpoint %3.0f %%", 100 * P->power_set);
    if (button((Rectangle){r.x + 270, y, 46, 26}, "-5", BLUEC, 0)) P->power_set = fmax(0.05, P->power_set - 0.05);
    if (button((Rectangle){r.x + 320, y, 46, 26}, "+5", BLUEC, 0)) P->power_set = fmin(1.05, P->power_set + 0.05);
    y += 40;
    /* plant */
    text("TURBINE", r.x + 12, y + 6, 14, DIMT);
    if (button((Rectangle){r.x + 90, y, 90, 26}, "TRIP", REDC, P->turbine_tripped)) {
        P->turbine_tripped = 1;
        P->generator_breaker = 0;
        logmsg("Turbine tripped");
    }
    if (button((Rectangle){r.x + 188, y, 90, 26}, "RESET", GREENC, 0) && P->turbine_tripped) {
        P->turbine_tripped = 0;
        P->generator_breaker = 1;
        P->tv_int = -2.0;
        logmsg("Turbine reset, generator synchronised");
    }
    y += 34;
    text("DRACS", r.x + 12, y + 6, 14, DIMT);
    const char *dl[3] = {"AUTO", "OPEN", "CLOSE"};
    for (int k = 0; k < 3; k++) {
        int act = k == 0 ? P->dracs_auto : (!P->dracs_auto && ((k == 1) == (P->dracs_damper_set[0] > 0.5)));
        if (button((Rectangle){r.x + 90 + k * 92, y, 86, 26}, dl[k], AMBERC, act)) {
            P->dracs_auto = k == 0;
            if (k) for (int i = 0; i < 3; i++) P->dracs_damper_set[i] = k == 1 ? 1.0 : 0.0;
            logmsg("DRACS dampers %s", dl[k]);
        }
    }
    y += 34;
    text("FAILURES", r.x + 12, y + 6, 14, DIMT);
    if (button((Rectangle){r.x + 90, y, 86, 26}, "GRID", REDC, !P->offsite_power)) {
        P->offsite_power = !P->offsite_power;
        logmsg(P->offsite_power ? "Offsite power restored" : "LOSS OF OFFSITE POWER");
    }
    for (int i = 0; i < 3; i++) {
        char lb[8];
        snprintf(lb, sizeof lb, "DG%d", i + 1);
        if (button((Rectangle){r.x + 182 + i * 62, y, 58, 26}, lb, REDC, !P->diesel_avail[i])) {
            P->diesel_avail[i] = !P->diesel_avail[i];
            logmsg("Diesel %d %s", i + 1, P->diesel_avail[i] ? "repaired" : "FAILED");
        }
    }
    y += 34;
    text("RPS", r.x + 12, y + 6, 14, DIMT);
    if (button((Rectangle){r.x + 90, y, 130, 26}, P->rps_bypass ? "BYPASSED" : "ARMED",
               P->rps_bypass ? REDC : GREENC, 1)) {
        P->rps_bypass = !P->rps_bypass;
        logmsg(P->rps_bypass ? "RPS BYPASSED - trips disabled" : "RPS armed");
    }
    y += 44;
    const char *tips[] = {
        "HOW TO PLAY",
        "- AUTO ROD holds power at the setpoint:",
        "  use -5 / +5 to change load.",
        "- Turn AUTO ROD off to fly the rods yourself.",
        "- Click P1-P4 / S1-S4 to stop or start pumps.",
        "- Stop 2 primary pumps and the reactor trips.",
        "- GRID and DG1-3 inject failures.",
        "- After a trip: RESET TRIP, then pull rods out.",
    };
    for (int i = 0; i < (int)(sizeof tips / sizeof tips[0]); i++)
        text(tips[i], r.x + 12, y + i * 17, 14, i ? DIMT : TXT);
}

static void draw_annunciators(Rectangle r)
{
    rm_core *c = &P->core;
    panel(r, NULL);
    int pumps_off = 0;
    for (int i = 0; i < RM_NLOOPS; i++) pumps_off += !P->loop[i].ppump.motor_on || P->loop[i].ppump.tripped;
    struct { const char *name; int on; Color c; } a[] = {
        {"REACTOR TRIP", c->scram, REDC},
        {"HIGH POWER", c->p_thermal > 1.05 * RM_P_RATED, REDC},
        {"SHORT PERIOD", isfinite(P->period) && P->period > 0 && P->period < 30, REDC},
        {"LOW CORE FLOW", P->W_core < 0.9 * rm_plant_nominal_flow(), AMBERC},
        {"PUMP OFF", pumps_off > 0, AMBERC},
        {"HIGH OUTLET T", P->T_core_out > 833.15, AMBERC},
        {"HIGH CLAD T", rm_core_max_clad_T(c) > 923.15, REDC},
        {"TURBINE TRIP", P->turbine_tripped, AMBERC},
        {"SAFETY VALVES", P->W_relief > 0, AMBERC},
        {"GRID LOST", !P->offsite_power, REDC},
        {"DIESELS RUNNING", P->diesel_running[0] || P->diesel_running[1] || P->diesel_running[2], AMBERC},
        {"DRACS COOLING", P->Q_dracs > 1e6, BLUEC},
        {"RPS BYPASSED", P->rps_bypass, REDC},
        {"AUTO ROD", P->auto_rod, GREENC},
    };
    int n = sizeof a / sizeof a[0];
    float w = (r.width - 20) / 7, h = 30;
    int blink = ((int)(GetTime() * 2)) & 1;
    for (int i = 0; i < n; i++) {
        Rectangle t = {r.x + 10 + (i % 7) * w, r.y + 8 + (i / 7) * (h + 6), w - 6, h};
        int lit = a[i].on && (a[i].c.r != REDC.r || blink || a[i].c.g != REDC.g);
        DrawRectangleRec(t, lit ? a[i].c : (Color){34, 40, 46, 255});
        int tw = MeasureText(a[i].name, 12);
        DrawText(a[i].name, (int)(t.x + (t.width - tw) / 2), (int)(t.y + 9), 12, lit ? BLACK : DIMT);
    }
}

static void draw_log(Rectangle r)
{
    panel(r, "MESSAGES");
    for (int i = 0; i < NLOG; i++) text(logs[i], r.x + 10, r.y + 28 + i * 16, 14, i == NLOG - 1 ? TXT : DIMT);
    if (P->first_out[0]) textf(r.x + r.width - 330, r.y + 8, 14, REDC, "FIRST OUT: %s", P->first_out);
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
            BeginDrawing();
            ClearBackground(BG);
            text("RM-ELM", 520, 300, 60, AMBERC);
            text("converging the plant to rated power...", 450, 380, 20, TXT);
            int dots = (int)(GetTime() * 3) % 4;
            for (int i = 0; i < dots; i++) DrawCircle(620 + i * 20, 430, 5, AMBERC);
            EndDrawing();
            if (loaded) {
                pthread_join(th, NULL);
                logmsg("Unit at rated power, turbine on line");
                logmsg("Click pumps on the plant diagram; controls on the right");
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
        if (P->t - last >= 1.0) {
            if (ntr < NTR) ntr++;
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
        ClearBackground(BG);
        /* top bar */
        text("RM-ELM  UNIT 1", 14, 12, 24, AMBERC);
        int t = (int)P->t;
        textf(230, 16, 18, TXT, "T+%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
        const int sp[5] = {1, 2, 4, 8, 16};
        for (int i = 0; i < 5; i++) {
            char lb[8];
            snprintf(lb, sizeof lb, "x%d", sp[i]);
            if (button((Rectangle){400 + i * 50, 10, 44, 26}, lb, BLUEC, speed == sp[i] && !paused)) {
                speed = sp[i];
                paused = 0;
            }
        }
        if (button((Rectangle){660, 10, 70, 26}, paused ? "RESUME" : "PAUSE", AMBERC, paused)) paused = !paused;
        draw_reactor((Rectangle){10, 46, 420, 440});
        draw_mimic((Rectangle){440, 46, 440, 440});
        draw_controls((Rectangle){890, 46, 380, 744});
        draw_annunciators((Rectangle){10, 494, 870, 80});
        draw_log((Rectangle){10, 582, 870, 208});
        EndDrawing();
        if (IsKeyPressed(KEY_F12)) {
            static int nshot = 0;
            char fn[64];
            snprintf(fn, sizeof fn, "rmelm_%03d.png", ++nshot);
            TakeScreenshot(fn);
            logmsg("Screenshot saved: %s", fn);
        }
        if (shot && P->t >= shot_at) {
            TakeScreenshot("rmelm_screenshot.png");
            break;
        }
    }
    CloseWindow();
    return 0;
}
