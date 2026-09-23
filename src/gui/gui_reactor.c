/*
 * Reactor board (panels 1-1 to 1-3), after the GE BWR reactor control
 * benchboard and the FFTF reactor console:
 *
 *   1-1  NUCLEAR INSTRUMENTATION AND PROTECTION
 *        SRM / startup rate / IRM / APRM edgewise meters, NI recorder, PPS
 *        trip status matrix and scram group lamps; IRM range switches,
 *        detector drives, channel bypass selectors and keys.
 *   1-2  REACTOR CONTROL
 *        full core display (a status tile per rod), reactor readouts, trip
 *        first-out box, power recorder; manual scram, rod control and
 *        selection matrix with its lamp groups, gang drive, reactor power
 *        controller.
 *   1-3  CORE MONITORING
 *        fuel temperature map (one gauge per control rod cell), source range,
 *        APRM, thermal power; reactor mode switch, IRM range (all channels).
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* rod groups, numbered in withdrawal order: 1 safety, 2-5 shims, 6 regulating */
static const char *bank_name[RM_NBANKS] = {"REG", "SHIM A", "SHIM B", "SHIM C", "SHIM D", "SAFETY"};
static int group_of(int bank) { return bank == BANK_SAFETY ? 1 : (bank == BANK_REG ? 6 : bank + 1); }

/* ---- rod coordinates ----------------------------------------------------------------- */
/* The rods sit on an index-7 sub-lattice of the hex grid, itself a triangular
 * lattice turned by 19.1 degrees. Turned back, the 55 rods fall into 9
 * staggered rows, laid out like a BWR full core display, with BWR-style
 * XX-YY coordinates. */
static int rod_ci[128], rod_rj[128], cmin, cmax, rmin, rmax;
static char rod_id[128][24];

void number_rods(void)
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

/* ---- core statistics ---------------------------------------------------------------- */
static struct {
    double tout[1024], tclad[1024], tfuel[1024];
} CS;
double CS_margin_min = 1e9;
double CS_fuel_max = 0;

void core_statistics(void)
{
    rm_core *c = &P->core;
    rm_coreth *th = &c->th;
    double mmin = 1e9, fmax_ = 0;
    for (int ch = 0; ch < c->nchan && ch < 1024; ch++) {
        double tc = 0, tf = 0, mg = 1e9;
        for (int k = 0; k < RM_NZ_ACT; k++) {
            int fn = core_fnode(c, ch, k);
            if (th->T[TH_CL][fn] > tc) tc = th->T[TH_CL][fn];
            if (th->T_center[fn] > tf) tf = th->T_center[fn];
            double p = th->p_out + 830.0 * 9.81 * (RM_ACTIVE_H / 100.0) * (1.0 - (k + 0.5) / RM_NZ_ACT);
            double mm = rm_na_tsat(p) - th->T[TH_C][fn];
            if (mm < mg) mg = mm;
        }
        CS.tout[ch] = th->T_out[ch];
        CS.tclad[ch] = tc;
        CS.tfuel[ch] = tf;
        if (mg < mmin) mmin = mg;
        if (tf > fmax_) fmax_ = tf;
    }
    CS_margin_min = mmin;
    CS_fuel_max = fmax_ - 273.15;
}

/* ---- fuel temperature map: one gauge per control rod cell ------------------------------ */
/* The core divides into cells of a rod and the six fuel channels round it
 * (the hex version of a BWR four-bundle control cell); edge channels go to
 * the nearest rod. Each gauge reads the hottest fuel centreline in its cell. */
static struct {
    int ready;
    int n[128];
    int ch[128][16];
    double ext;
} CELL;

static void cell_channels(void)
{
    rm_core *c = &P->core;
    rm_hexgrid *g = &c->dif.grid;
    CELL.ext = 1;
    for (int col = 0; col < g->n; col++) {
        if (g->ring[col] > RM_CORE_RINGS || c->coltype[col] != COL_FUEL) continue;
        double x, y, best = 1e9;
        int kb = -1;
        rm_hexgrid_xy(g, col, 1.0, &x, &y);
        for (int k = 0; k < c->nctrl && k < 128; k++) {
            double rx, ry;
            rm_hexgrid_xy(g, c->ctrl_col[k], 1.0, &rx, &ry);
            double d = (x - rx) * (x - rx) + (y - ry) * (y - ry);
            if (d < best - 1e-9) {
                best = d;
                kb = k;
            }
        }
        if (kb >= 0 && CELL.n[kb] < 16) CELL.ch[kb][CELL.n[kb]++] = c->chan_of_col[col];
    }
    for (int k = 0; k < c->nctrl && k < 128; k++) {
        double rx, ry;
        rm_hexgrid_xy(g, c->ctrl_col[k], 1.0, &rx, &ry);
        CELL.ext = fmax(CELL.ext, fmax(fabs(rx), fabs(ry)));
    }
    CELL.ready = 1;
}

static double cell_fuel(int k)
{
    double v = 0;
    for (int i = 0; i < CELL.n[k]; i++) v = fmax(v, CS.tfuel[CELL.ch[k][i]]);
    return v - 273.15;
}

#define FUEL_LO 300.0
#define FUEL_HI 1500.0
#define FUEL_ALARM 1300.0

/* a little round dial: 240 degree scale, red zone from the alarm point */
static void small_gauge(Vector2 c, float R, double f, int alarm)
{
    DrawCircleV(c, R, alarm && blink_fast() ? (Color){200, 30, 20, 255} : BEZEL);
    DrawCircleV(c, R - 2.5f, FACE);
    const float a0 = 150, a1 = 390;
    float fa = (float)((FUEL_ALARM - FUEL_LO) / (FUEL_HI - FUEL_LO));
    DrawRing(c, R - 6, R - 3, a0 + (a1 - a0) * fa, a1, 16, (Color){210, 40, 30, 255});
    for (int i = 0; i <= 6; i++) {
        float a = (a0 + (a1 - a0) * i / 6) * DEG2RAD;
        DrawLineEx((Vector2){c.x + (R - 3) * cosf(a), c.y + (R - 3) * sinf(a)},
                   (Vector2){c.x + (R - 7) * cosf(a), c.y + (R - 7) * sinf(a)}, 1, INK);
    }
    if (f < -0.02) f = -0.02;
    if (f > 1.02) f = 1.02;
    float a = (float)((a0 + (a1 - a0) * f) * DEG2RAD);
    DrawLineEx(c, (Vector2){c.x + (R - 4) * cosf(a), c.y + (R - 4) * sinf(a)}, 1.8f, (Color){16, 16, 16, 255});
    DrawCircleV(c, 2.2f, (Color){16, 16, 16, 255});
}

/* ---- rod motion ------------------------------------------------------------------------ */
static int hover_rod = -1;
static double settle_until = 0, drift_test_until = 0, cont_next = 0;

static int rod_group6_auto(int k)
{
    rm_core *c = &P->core;
    return c->ctrl_bank[k] == BANK_REG && P->auto_rod && !c->scram;
}

/* move the selected rod by dn notches (+ = withdraw), honouring the permissives */
static void rod_single(int s, int dn)
{
    rm_core *c = &P->core;
    if (s < 0) {
        logmsg("NO ROD SELECTED");
        return;
    }
    if (rod_group6_auto(s)) {
        logmsg("ROD %s IS IN GROUP 6 ON AUTO - PUT THE POWER CONTROLLER IN MAN", rod_id[s]);
        return;
    }
    int nt = notch(c->rod_target[s]) + dn;
    if (nt < 0) nt = 0;
    if (nt > (int)(RM_ACTIVE_H / NOTCH_CM)) nt = (int)(RM_ACTIVE_H / NOTCH_CM);
    double target = RM_ACTIVE_H - NOTCH_CM * nt;
    char why[80];
    if (!rm_plant_rod_permit(P, -1, s, target, why, sizeof why)) logmsg("%s", why);
    else rm_core_rod_move(c, s, target);
}

static int drive_all = 0;          /* GANG DRIVE switch: 0 the selected rod's group, 1 all rods */

/* withdraw (step < 0) or insert a group or every rod; all-or-nothing on the permissives */
static void drive_rods(double step)
{
    rm_core *c = &P->core;
    int s = P->nms.sel_rod;
    int move[RM_NBANKS] = {0};
    if (drive_all) {
        for (int b = 0; b < RM_NBANKS; b++) move[b] = 1;
        if (P->auto_rod && !c->scram) move[BANK_REG] = 0;
    } else {
        if (s < 0) {
            logmsg("SELECT A ROD ON THE ROD SELECT MATRIX FIRST");
            return;
        }
        int bk = c->ctrl_bank[s];
        if (bk == BANK_REG && P->auto_rod && !c->scram) {
            logmsg("GROUP 6 IS ON AUTO - PUT THE POWER CONTROLLER IN MAN");
            return;
        }
        move[bk] = 1;
    }
    char why[80];
    for (int b = 0; b < RM_NBANKS; b++) {
        if (!move[b]) continue;
        /* check against where the group is already heading, so quick clicks can't run ahead */
        double tgt = 0;
        int n = 0;
        for (int k = 0; k < c->nctrl; k++)
            if (c->ctrl_bank[k] == b) {
                tgt += c->rod_target[k];
                n++;
            }
        double target = fmin(RM_ACTIVE_H, fmax(0.0, (n ? tgt / n : 0.0) + step));
        if (!rm_plant_rod_permit(P, b, -1, target, why, sizeof why)) {
            logmsg("%s", why);
            return;
        }
    }
    for (int b = 0; b < RM_NBANKS; b++)
        if (move[b]) rm_core_bank_shift(c, b, step);
}

/* ---- 1-1 nuclear instrumentation and protection ------------------------------------------ */
static const char *TRIP_SHORT[NTRIP] = {"MANUAL SCRAM",     "MODE SW SHUTDOWN", "LOSS OF OFFSITE",  "PERIOD < 10 S",
                                        "POWER/FLOW HIGH",  "PRI FLOW LOW",     "2/4 PRI PUMPS OFF", "OUTLET T HIGH",
                                        "CLAD T HIGH",      "STEAM P HIGH",     "TURB TRIP > 50%",  "DND HIGH",
                                        "NA LEVEL LOW",     "IRM HI-HI",        "APRM HI-HI",       "NA-WATER RXN"};

static double sur_dpm(void)
{
    if (!isfinite(P->period) || fabs(P->period) > 1e4) return 0.0;
    return 26.06 / P->period;       /* decades per minute */
}

static void ni_face(Rectangle f)
{
    rm_nms *nm = &P->nms;
    char b[32];
    float pitch = f.width / 13.0f, mw = pitch - 5;
    /* row 1: source range, startup rate, intermediate range */
    plate_c(f.x + pitch * 2.0f, f.y, "SOURCE RANGE", 10);
    plate_c(f.x + pitch * 9.0f, f.y, "INTERMEDIATE RANGE - LAMPS UPSCALE / DOWNSCALE", 10);
    static const char *srml[4] = {".1", "10", "1E3", "1E5"};
    static const char *surl[4] = {"-1", "1", "3", "5"};
    float my = f.y + 24;
    for (int i = 0; i < 4; i++) {
        snprintf(b, sizeof b, "SRM %c", 'A' + i);
        edgewz((Rectangle){f.x + i * pitch, my, mw, 100}, b, log10(fmax(rm_plant_srm_ch(P, i), 0.1)), -1, 5, 0, 0, 4.5,
               5, 3, srml);
    }
    edgewz((Rectangle){f.x + 4 * pitch, my, mw, 100}, "SUR\nDPM", fmin(5.0, fmax(-1.0, sur_dpm())), -1, 5, -0.3, 1.0,
           2.6, 5, 3, surl);
    for (int i = 0; i < 8; i++) {
        float x = f.x + (5 + i) * pitch;
        int byp = nm->irm_bypass[i & 1] == i;
        snprintf(b, sizeof b, byp ? "BYP" : "IRM %c", 'A' + i);
        double v = rm_plant_irm_ch(P, i);
        edgewz((Rectangle){x, my, mw, 100}, b, v, 0, 125, 15, 100, RM_IRM_TRIP, 125, 5, NULL);
        int start = P->mode == RM_MODE_STARTUP || P->mode == RM_MODE_REFUEL;
        lamp(x + mw * 0.28f, my + 122, 3, L_AMB, start && v > 108.0);
        lamp(x + mw * 0.72f, my + 122, 3, L_WHT, start && nm->irm_range[i] > 1 && v < RM_IRM_DOWNSCALE);
    }

    /* row 2: APRMs and the NI recorder */
    float y2 = f.y + 150;
    plate_c(f.x + pitch * 3.0f, y2, "AVERAGE POWER RANGE", 10);
    double sp = P->mode == RM_MODE_RUN ? 115.0 : 15.0;
    for (int i = 0; i < 6; i++) {
        int byp = nm->aprm_bypass[i & 1] == i;
        snprintf(b, sizeof b, byp ? "BYP" : "%c", 'A' + i);
        edgewz((Rectangle){f.x + i * pitch, y2 + 22, mw, 96}, b, rm_plant_aprm_ch(P, i), 0, 125, 0, 100, sp, 125, 5, NULL);
    }
    strip((Rectangle){f.x + 6 * pitch + 6, y2, f.width - 6 * pitch - 6, 132}, TR_LOGPWR, -8, 2, "LOG PWR", TR_PERIOD,
          -1, 5, "SUR DPM");

    /* row 3: PPS trip status matrix and the scram group lamps */
    float y3 = f.y + 294;
    demarc((Rectangle){f.x, y3, f.width, f.y + f.height - y3}, "PPS TRIP STATUS - LIT = CHANNEL TRIPPED");
    static const char *chn[4] = {"A1", "A2", "B1", "B2"};
    for (int col = 0; col < 2; col++) {
        float cx = f.x + 8 + col * 210;
        for (int ch = 0; ch < 4; ch++) text(chn[ch], cx + 118 + ch * 20, y3 + 8, 10, INK);
        for (int j = 0; j < 8; j++) {
            int k = col * 8 + j;
            float ry = y3 + 22 + j * 13;
            text(TRIP_SHORT[k], cx, ry - 5, 10, INK);
            for (int ch = 0; ch < 4; ch++) {
                int on;
                if (k == 0) on = nm->trip[ch] && !strncmp(nm->div_why[ch / 2], "MANUAL", 6);
                else on = trip_active(k, ch);
                lamp(cx + 124 + ch * 20, ry, 4, L_AMB, on);
            }
        }
    }
    float gx = f.x + f.width - 96;
    ctext("SCRAM GROUPS", gx + 44, y3 + 8, 10, INK);
    for (int ch = 0; ch < 4; ch++) {
        float lx = gx + 22 + (ch % 2) * 46, ly = y3 + 40 + (ch / 2) * 36;
        lamp(lx, ly, 8, L_WHT, !nm->trip[ch]);
        text(chn[ch], lx + 12, ly - 5, 10, INK);
    }
    ctext("LIT = ENERGISED", gx + 44, y3 + 98, 10, INK);
}

static void cycle_to(int *v, int pick, const int *opts) { *v = opts[pick]; }

static void ni_bench(Rectangle bb)
{
    rm_nms *nm = &P->nms;
    char b[32];
    /* IRM range switches, A to H left to right (no mirror imaging) */
    plate_c(bb.x + bb.width / 2, bb.y, "IRM RANGE SWITCHES", 10);
    static const char *rng[10] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
    float pitch = bb.width / 8.0f;
    for (int i = 0; i < 8; i++) {
        float cx = bb.x + pitch * (i + 0.5f);
        snprintf(b, sizeof b, "IRM %c", 'A' + i);
        tag_c(cx, bb.y + 22, b);
        int pk = rotary((Vector2){cx, bb.y + 84}, 14, 10, rng, nm->irm_range[i] - 1, -225, 45, 28);
        if (pk >= 0 && pk + 1 != nm->irm_range[i]) {
            nm->irm_range[i] = pk + 1;
            logmsg("IRM %c RANGE %d", 'A' + i, nm->irm_range[i]);
        }
    }

    /* detector drives */
    float y = bb.y + 140;
    demarc((Rectangle){bb.x, y, 262, 112}, "DETECTOR DRIVES");
    const char *dn[2] = {"SRM", "IRM"};
    for (int k = 0; k < 2; k++) {
        float ry = y + 16 + k * 48;
        double pos = k ? nm->irm_pos : nm->srm_pos;
        int *drv = k ? &nm->irm_drive : &nm->srm_drive;
        tag(bb.x + 8, ry + 8, dn[k]);
        if (lampbutton((Rectangle){bb.x + 44, ry, 62, 30}, "DRIVE\nIN", L_WHT, *drv > 0)) *drv = 1;
        if (lampbutton((Rectangle){bb.x + 110, ry, 62, 30}, "DRIVE\nOUT", L_WHT, *drv < 0)) *drv = -1;
        readout(bb.x + 178, ry + 2, 12, 3, "%3.0f", 100 * pos);
        lamp(bb.x + 224, ry + 7, 4, L_RED, pos >= 0.99);
        text("IN", bb.x + 232, ry + 2, 10, INK);
        lamp(bb.x + 224, ry + 22, 4, L_GRN, pos <= 0.01);
        text("OUT", bb.x + 232, ry + 17, 10, INK);
    }

    /* channel bypass joysticks, one channel per division */
    float bx = bb.x + 276;
    demarc((Rectangle){bx, y, bb.x + bb.width - bx, 112}, "CHANNEL BYPASS");
    static const char *ia[5] = {"OFF", "A", "C", "E", "G"}, *ib[5] = {"OFF", "B", "D", "F", "H"};
    static const char *aa[4] = {"OFF", "A", "C", "E"}, *ab[4] = {"OFF", "B", "D", "F"};
    static const int via[5] = {-1, 0, 2, 4, 6}, vib[5] = {-1, 1, 3, 5, 7}, vaa[4] = {-1, 0, 2, 4}, vab[4] = {-1, 1, 3, 5};
    struct { const char *n; int *v; const char *const *leg; const int *val; int np; } bs[4] = {
        {"IRM DIV A", &nm->irm_bypass[0], ia, via, 5}, {"IRM DIV B", &nm->irm_bypass[1], ib, vib, 5},
        {"APRM DIV A", &nm->aprm_bypass[0], aa, vaa, 4}, {"APRM DIV B", &nm->aprm_bypass[1], ab, vab, 4}};
    /* pushbutton selectors: the lit button is in effect */
    for (int k = 0; k < 4; k++) {
        float ry = y + 14 + k * 24;
        text(bs[k].n, bx + 8, ry + 5, 10, INK);
        int cur = 0;
        for (int i = 0; i < bs[k].np; i++)
            if (bs[k].val[i] == *bs[k].v) cur = i;
        for (int i = 0; i < bs[k].np; i++) {
            if (lampbutton((Rectangle){bx + 78 + i * 38, ry, 34, 20}, bs[k].leg[i], i ? L_AMB : L_WHT, i == cur) && i != cur) {
                cycle_to(bs[k].v, i, bs[k].val);
                if (*bs[k].v >= 0) logmsg("%s: CHANNEL %s BYPASSED", bs[k].n, bs[k].leg[i]);
                else logmsg("%s: NO CHANNEL BYPASSED", bs[k].n);
            }
        }
    }

    /* RPS channel bypass keys */
    y = bb.y + 268;
    demarc((Rectangle){bb.x, y, bb.width, bb.y + bb.height - y - 4}, "RPS CHANNEL BYPASS");
    static const char *gn[4] = {"A1", "A2", "B1", "B2"};
    for (int i = 0; i < 4; i++) {
        float kx = bb.x + 52 + i * 90;
        tag_c(kx, y + 14, gn[i]);
        if (keysw(kx, y + 64, "NORM", "BYP", nm->bypass[i])) {
            int other = i ^ 1;
            if (!nm->bypass[i] && nm->bypass[other]) logmsg("RPS %s BYPASS REFUSED: %s ALREADY BYPASSED", gn[i], gn[other]);
            else {
                nm->bypass[i] = !nm->bypass[i];
                logmsg("RPS CHANNEL %s %s", gn[i], nm->bypass[i] ? "BYPASSED" : "IN SERVICE");
            }
        }
    }
    dymo(bb.x + 388, y + 16, "KEEP IRMS 15 TO 100");
    dymo(bb.x + 388, y + 34, "RANGE UP BEFORE 108");
    dymo(bb.x + 388, y + 52, "ONE BYPASS PER DIVISION");
}

/* ---- 1-2 reactor control ---------------------------------------------------------------- */
static void rod_cell(float x, float y, int k)
{
    rm_core *c = &P->core;
    Rectangle box = {x - 21, y - 17, 42, 34};
    int sel = k == P->nms.sel_rod;
    int drifting = c->rod_drift[k] != 0.0 && !c->scram;
    if (sel) DrawRectangleRec((Rectangle){box.x - 3, box.y - 3, box.width + 6, box.height + 6}, L_WHT);
    DrawRectangleRec(box, drifting && blink_fast() ? L_RED : BEZEL);
    readout(box.x + 5, box.y + 2, 11, 2, "%02d", notch(c->rod_ins[k]));
    int in = c->rod_ins[k] >= RM_ACTIVE_H - 0.5, out = c->rod_ins[k] <= 0.5;
    Rectangle li = {box.x + 3, box.y + 25, 17, 7}, lo = {box.x + 22, box.y + 25, 17, 7};
    DrawRectangleRec(li, in ? L_GRN : dimlens(L_GRN));
    DrawRectangleRec(lo, out ? L_RED : dimlens(L_RED));
    if (fabs(c->rod_target[k] - c->rod_ins[k]) > 0.1 && !c->scram)
        DrawCircleV((Vector2){box.x + box.width - 4, box.y + 4}, 2.5f, L_AMB);
    if (CheckCollisionPointRec(GetMousePosition(), box)) {
        hover_rod = k;
        if (clicked(box)) rm_plant_select_rod(P, P->nms.sel_rod == k ? -1 : k);
    }
}

static void rc_face(Rectangle f)
{
    rm_core *c = &P->core;
    /* full core display */
    const float cp = 23, rp = 42;
    float dw = 424;
    float gx0 = f.x + 34 + (dw - 60 - (cmax - cmin) * cp) / 2, gy0 = f.y + 30;
    DrawRectangleRec((Rectangle){f.x, f.y, dw, 416}, (Color){50, 54, 50, 255});
    DrawRectangleLinesEx((Rectangle){f.x, f.y, dw, 416}, 2, BEZEL);
    ctext("FULL CORE DISPLAY", f.x + dw / 2, f.y + 4, 10, (Color){226, 224, 214, 255});
    for (int j = rmin; j <= rmax; j++)
        textf(f.x + 6, gy0 + (j - rmin) * rp - 5, 10, (Color){226, 224, 214, 255}, "%02d", 4 * (rmax - j) + 3);
    for (int i = cmin; i <= cmax; i++) {
        int any = 0;
        for (int k = 0; k < c->nctrl; k++) any |= rod_ci[k] == i;
        if (!any) continue;
        char lb[12];
        snprintf(lb, sizeof lb, "%02d", 2 * (i - cmin) + 2);
        ctext(lb, gx0 + (i - cmin) * cp, gy0 + (rmax - rmin) * rp + 19, 10, (Color){226, 224, 214, 255});
    }
    for (int k = 0; k < c->nctrl; k++) rod_cell(gx0 + (rod_ci[k] - cmin) * cp, gy0 + (rod_rj[k] - rmin) * rp, k);
    text("GREEN = FULL IN   RED = FULL OUT   AMBER = MOVING   FLASHING = DRIFT", f.x + 8, f.y + 402, 10,
         (Color){226, 224, 214, 255});

    /* reactor readouts */
    float x = f.x + dw + 12, w = f.x + f.width - x;
    double beta = 0;
    for (int i = 0; i < 6; i++) beta += c->pk.beta[i];
    tag(x, f.y, "REACTOR POWER MWT");
    readout(x, f.y + 15, 22, 4, "%4.0f", c->p_thermal / 1e6);
    tag(x + 128, f.y, "PCT RATED");
    readout(x + 128, f.y + 15, 22, 5, "%5.1f", 100 * c->p_thermal / RM_P_RATED);
    tag(x, f.y + 52, "PERIOD SEC");
    if (isfinite(P->period) && fabs(P->period) < 999) readout(x, f.y + 67, 16, 5, "%5.1f", P->period);
    else readout(x, f.y + 67, 16, 5, "-----");
    tag(x + 96, f.y + 52, "REACTIVITY $");
    readout(x + 96, f.y + 67, 16, 5, "%5.2f", c->rho / beta);
    tag(x + 208, f.y + 52, "LOG POWER");
    readout(x + 208, f.y + 67, 16, 4, "%4.1f", log10(fmax(c->p_thermal / RM_P_RATED, 1e-12)));
    tag(x, f.y + 98, "CORE OUTLET C");
    readout(x, f.y + 113, 16, 3, "%3.0f", P->T_core_out - 273.15);
    tag(x + 96, f.y + 98, "CORE FLOW %");
    readout(x + 96, f.y + 113, 16, 3, "%3.0f", 100 * P->W_core / rm_plant_nominal_flow());
    tag(x + 208, f.y + 98, "DEMAND %");
    readout(x + 208, f.y + 113, 16, 5, "%5.1f", 100 * P->power_set);

    firstout_box((Rectangle){x, f.y + 146, w, 130});
    strip((Rectangle){x, f.y + 284, w, f.height - 284}, TR_POWER, 0, 120, "PWR %", TR_OUTLET, 300, 700, "OUT C");
}

static void rc_bench(Rectangle bb)
{
    rm_core *c = &P->core;
    rm_nms *nm = &P->nms;
    Vector2 m = GetMousePosition();

    /* ---- manual scram, reset, RPS key, power controller ---- */
    demarc((Rectangle){bb.x, bb.y + 6, 150, 196}, "REACTOR SCRAM");
    for (int d = 0; d < 2; d++) {
        char lb[24];
        snprintf(lb, sizeof lb, "SCRAM %c", 'A' + d);
        if (pbround((Vector2){bb.x + 38 + d * 74, bb.y + 50}, 19, lb, (Color){206, 28, 22, 255}, (Color){238, 196, 30, 255}, 0)) {
            rm_plant_manual_scram_div(P, d);
            logmsg("MANUAL SCRAM DIVISION %c", 'A' + d);
        }
        char rl[24];
        snprintf(rl, sizeof rl, "RESET\nDIV %c", 'A' + d);
        if (lampbutton((Rectangle){bb.x + 8 + d * 70, bb.y + 104, 64, 32}, rl, L_AMB, nm->div_trip[d])) {
            rm_plant_rps_reset(P, d);
            logmsg("RPS DIVISION %c RESET", 'A' + d);
        }
    }
    if (keysw(bb.x + 75, bb.y + 176, "NORM", "BYP", P->rps_bypass)) {
        P->rps_bypass = !P->rps_bypass;
        logmsg(P->rps_bypass ? "RPS BYPASSED - TRIPS DISABLED" : "RPS ARMED");
    }
    text("RPS", bb.x + 66, bb.y + 186, 10, INK);

    double reg = rm_core_bank_pos(c, BANK_REG);
    int ev = mastation((Rectangle){bb.x + 27, bb.y + bb.height - MA_H, MA_W, MA_H}, "REACTOR POWER", "% RATED",
                       100 * c->p_thermal / RM_P_RATED, 100 * P->power_set, 0, 125, 1.0 - reg / RM_ACTIVE_H,
                       P->auto_rod ? 1 : 0);
    if ((ev & MA_AUTO) && !P->auto_rod) {
        P->auto_rod = 1;
        logmsg("REACTOR POWER CONTROLLER IN AUTO - GROUP 6 HOLDS DEMAND");
    }
    if ((ev & MA_MAN) && P->auto_rod) {
        P->auto_rod = 0;
        logmsg("REACTOR POWER CONTROLLER IN MANUAL");
    }
    double step = P->power_set < 0.1 ? 0.002 : 0.01;
    if (P->auto_rod && (ev & MA_UP)) P->power_set = fmin(1.05, P->power_set + step);
    if (P->auto_rod && (ev & MA_DOWN)) P->power_set = fmax(0.002, P->power_set - step);
    /* in manual the raise/lower buttons drive group 6 a notch at a time */
    if (!P->auto_rod && (ev & (MA_UP | MA_DOWN))) {
        double st = (ev & MA_UP) ? -NOTCH_CM : NOTCH_CM;
        char why[80];
        double tgt = fmin(RM_ACTIVE_H, fmax(0.0, reg + st));
        if (!rm_plant_rod_permit(P, BANK_REG, -1, tgt, why, sizeof why)) logmsg("%s", why);
        else rm_core_bank_shift(c, BANK_REG, st);
    }

    /* ---- rod select matrix ---- */
    float mx0 = bb.x + 160, mw = 400;
    const float cp = 23, rp = 22;
    plate_c(mx0 + mw / 2, bb.y, "ROD SELECT", 10);
    Rectangle mr = {mx0, bb.y + 20, mw, (rmax - rmin) * rp + 34};
    DrawRectangleRec(mr, (Color){44, 48, 46, 255});
    DrawRectangleLinesEx(mr, 2, BEZEL);
    float gx0 = mx0 + 20 + (mw - 40 - (cmax - cmin) * cp) / 2, gy0 = mr.y + 16;
    for (int k = 0; k < c->nctrl; k++) {
        Rectangle bt = {gx0 + (rod_ci[k] - cmin) * cp - 20, gy0 + (rod_rj[k] - rmin) * rp - 9, 40, 18};
        int on = k == nm->sel_rod;
        int hv = input_ok && CheckCollisionPointRec(m, bt);
        DrawRectangleRec(bt, on ? L_WHT : (Color){24, 24, 22, 255});
        DrawRectangleRec((Rectangle){bt.x, bt.y, bt.width, 1}, on ? WHITE : (Color){90, 90, 86, 255});
        ctext(rod_id[k], bt.x + bt.width / 2, bt.y + 4, 10, on ? INK : (Color){206, 204, 196, 255});
        if (CheckCollisionPointRec(m, bt)) hover_rod = k;
        if (hv) {
            DrawRectangleLinesEx(bt, 1, (Color){190, 190, 180, 255});
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) rm_plant_select_rod(P, nm->sel_rod == k ? -1 : k);
        }
    }

    /* ---- gang drive: the selected rod's group, or every rod ---- */
    float gy = mr.y + mr.height + 18;
    demarc((Rectangle){mx0, gy, mw, bb.y + bb.height - gy - 2}, "GANG DRIVE");
    static const char *mot[2] = {"GROUP", "ALL"};
    int pk = rotary((Vector2){mx0 + 50, gy + 62}, 18, 2, mot, drive_all, -150, -30, 36);
    if (pk >= 0 && pk != drive_all) {
        drive_all = pk;
        logmsg("GANG DRIVE: %s", drive_all ? "ALL RODS" : "SELECTED ROD'S GROUP");
    }
    int s = nm->sel_rod, bk = s >= 0 ? c->ctrl_bank[s] : -1;
    float gx = mx0 + 110;
    tag(gx, gy + 12, drive_all ? "RODS" : "GROUP");
    if (drive_all) readout(gx, gy + 28, 18, 2, "%d", c->nctrl);
    else if (bk >= 0) readout(gx, gy + 28, 18, 2, "%d", group_of(bk));
    else readout(gx, gy + 28, 18, 2, "--");
    text(drive_all ? "ALL GROUPS" : (bk >= 0 ? bank_name[bk] : "SELECT A ROD"), gx, gy + 62, 10, INK);
    double pos = 0;
    if (drive_all) {
        for (int k = 0; k < c->nctrl; k++) pos += c->rod_ins[k] / c->nctrl;
    } else if (bk >= 0) pos = rm_core_bank_pos(c, bk);
    tag(gx + 90, gy + 12, drive_all ? "AVG NOTCH" : "NOTCH");
    if (drive_all || bk >= 0) readout(gx + 90, gy + 28, 18, 2, "%02d", notch(pos));
    else readout(gx + 90, gy + 28, 18, 2, "--");
    lamp(gx + 176, gy + 34, 5, L_GRN, (drive_all || bk >= 0) && pos >= RM_ACTIVE_H - 0.5);
    text("FULL IN", gx + 186, gy + 29, 10, INK);
    lamp(gx + 176, gy + 52, 5, L_RED, (drive_all || bk >= 0) && pos <= 0.5);
    text("FULL OUT", gx + 186, gy + 47, 10, INK);
    const char *lab[4] = {"WITHDRAW\n5 NOTCH", "WITHDRAW\n1 NOTCH", "INSERT\n1 NOTCH", "INSERT\n5 NOTCH"};
    const double stp[4] = {-5 * NOTCH_CM, -NOTCH_CM, NOTCH_CM, 5 * NOTCH_CM};
    for (int k = 0; k < 4; k++) {
        Rectangle br = {mx0 + 10 + k * 96, gy + 86, 90, 32};
        int pressing = input_ok && CheckCollisionPointRec(m, br) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        if (lampbutton(br, lab[k], L_WHT, pressing)) drive_rods(stp[k]);
    }

    /* ---- rod motion controls and status (after the BWR rod control & selection matrix) ---- */
    float rx = bb.x + 574, rw = bb.x + bb.width - rx;
    tag(rx, bb.y + 2, "SELECTED ROD");
    readout(rx, bb.y + 17, 15, 5, "%s", s >= 0 ? rod_id[s] : "-----");
    tag(rx + 92, bb.y + 2, "NOTCH");
    if (s >= 0) readout(rx + 92, bb.y + 17, 15, 2, "%02d", notch(c->rod_ins[s]));
    else readout(rx + 92, bb.y + 17, 15, 2, "--");
    indicator((Rectangle){rx + 136, bb.y + 14, 66, 28}, "NO ROD\nSEL", L_WHT, s < 0);

    int inserting = s >= 0 && c->rod_target[s] > c->rod_ins[s] + 0.1 && !c->scram;
    int withdrawing = s >= 0 && c->rod_target[s] < c->rod_ins[s] - 0.1 && !c->scram;
    static int was_moving = 0;
    if (was_moving && !inserting && !withdrawing) settle_until = GetTime() + 1.5;
    was_moving = inserting || withdrawing;
    float ty = bb.y + 58;
    tag(rx, ty, "ROD MOTION CONTROLS");
    indicator((Rectangle){rx, ty + 16, 64, 22}, "INSERT", L_WHT, inserting);
    indicator((Rectangle){rx + 68, ty + 16, 64, 22}, "WITHDRAW", L_WHT, withdrawing);
    indicator((Rectangle){rx + 136, ty + 16, 64, 22}, "SETTLE", L_WHT, GetTime() < settle_until);
    if (lampbutton((Rectangle){rx, ty + 44, 98, 30}, "INSERT", L_WHT, 0)) rod_single(s, -1);
    if (lampbutton((Rectangle){rx + 102, ty + 44, 98, 30}, "WITHDRAW", L_WHT, 0)) rod_single(s, 1);

    ty = bb.y + 140;
    tag(rx, ty, "ROD MOTION OVERRIDES");
    Rectangle ci = {rx, ty + 16, 98, 30}, cw = {rx + 102, ty + 16, 98, 30};
    int hold_i = input_ok && CheckCollisionPointRec(m, ci) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    int hold_w = input_ok && CheckCollisionPointRec(m, cw) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    lampbutton(ci, "CONTINUOUS\nINSERT", L_WHT, hold_i);
    lampbutton(cw, "CONTINUOUS\nWITHDRAW", L_WHT, hold_w);
    if ((hold_i || hold_w) && GetTime() >= cont_next) {
        rod_single(s, hold_w ? 1 : -1);
        cont_next = GetTime() + 0.6;
    }
    if (!hold_i && !hold_w) cont_next = 0;

    ty = bb.y + 198;
    tag(rx, ty, "ROD MONITOR BLOCKS");
    char why[80] = "";
    int rwm = s >= 0 && !rm_plant_rod_permit(P, -1, s, 0.0, why, sizeof why) && !strncmp(why, "RWM", 3);
    int rbm = s >= 0 && !nm->rbm_bypass && rm_plant_aprm(P) > 30.0 && nm->rbm > 1.08;
    indicator((Rectangle){rx, ty + 16, 64, 28}, "WITHDRAW\nBLOCK", L_AMB, rm_plant_rod_block(P, 1, NULL, 0));
    indicator((Rectangle){rx + 68, ty + 16, 64, 28}, "RWM\nBLOCK", L_AMB, rwm);
    indicator((Rectangle){rx + 136, ty + 16, 64, 28}, "RBM\nUPSCALE", L_AMB, rbm);

    ty = bb.y + 254;
    tag(rx, ty, "ROD DRIFT");
    int drift = 0;
    for (int k = 0; k < c->nctrl; k++) drift |= c->rod_drift[k] != 0.0 && !c->scram;
    indicator((Rectangle){rx, ty + 16, 64, 28}, "ROD\nDRIFT", L_RED, drift || GetTime() < drift_test_until);
    if (lampbutton((Rectangle){rx + 68, ty + 16, 64, 28}, "TEST", L_WHT, 0)) drift_test_until = GetTime() + 2.0;
    if (lampbutton((Rectangle){rx + 136, ty + 16, 64, 28}, "RESET", L_WHT, 0))
        logmsg(drift ? "ROD DRIFT ALARM RESET - ROD STILL DRIFTING, INSERT IT" : "ROD DRIFT ALARM RESET");

    ty = bb.y + 310;
    tag(rx, ty, "RWM / RBM");
    if (keysw(rx + 34, ty + 44, "RWM", "BYP", nm->rwm_bypass)) {
        nm->rwm_bypass = !nm->rwm_bypass;
        logmsg("ROD WORTH MINIMIZER %s", nm->rwm_bypass ? "BYPASSED" : "IN SERVICE");
    }
    if (keysw(rx + 122, ty + 44, "RBM", "BYP", nm->rbm_bypass)) {
        nm->rbm_bypass = !nm->rbm_bypass;
        logmsg("ROD BLOCK MONITOR %s", nm->rbm_bypass ? "BYPASSED" : "IN SERVICE");
    }
    tag(rx + 164, ty + 16, "RBM %");
    if (s >= 0) readout(rx + 164, ty + 30, 12, 3, "%3.0f", 100 * nm->rbm);
    else readout(rx + 164, ty + 30, 12, 3, "---");
    (void)rw;
}

/* ---- 1-3 core monitoring ------------------------------------------------------------------ */
static int hover_cell = -1;

static void cm_face(Rectangle f)
{
    rm_core *c = &P->core;
    if (!CELL.ready) cell_channels();
    Vector2 m = GetMousePosition();
    Rectangle mp = {f.x, f.y, 424, 424};
    DrawRectangleRec(mp, (Color){40, 42, 40, 255});
    DrawRectangleLinesEx(mp, 2, BEZEL);
    ctext("FUEL TEMPERATURE - HOTTEST CENTRELINE", mp.x + mp.width / 2, mp.y + 4, 10, (Color){226, 224, 214, 255});
    ctext("ONE GAUGE PER CONTROL ROD CELL", mp.x + mp.width / 2, mp.y + 16, 10, (Color){226, 224, 214, 255});
    Vector2 o = {mp.x + mp.width / 2, mp.y + mp.height / 2 + 12};
    rm_hexgrid *g = &c->dif.grid;
    float sc = (float)((mp.width / 2 - 10) / (CELL.ext + 0.5 * sqrt(7.0)));
    float R = (float)(0.5 * sqrt(7.0) * sc * 0.96);
    hover_cell = -1;
    for (int k = 0; k < c->nctrl && k < 128; k++) {
        double x, y;
        rm_hexgrid_xy(g, c->ctrl_col[k], sc, &x, &y);
        Vector2 p = {o.x + (float)x, o.y + (float)y};
        double f2 = (cell_fuel(k) - FUEL_LO) / (FUEL_HI - FUEL_LO);
        small_gauge(p, R, f2, cell_fuel(k) > FUEL_ALARM);
        ctext(rod_id[k], p.x, p.y + R * 0.42f, 10, (Color){90, 84, 70, 255});
        if (CheckCollisionPointCircle(m, p, R)) hover_cell = k;
    }

    /* source range, APRM, thermal power */
    float x = f.x + 434, w = f.x + f.width - x;
    static const char *srml[4] = {".1", "10", "1E3", "1E5"};
    double cps = rm_plant_srm_cps(P);
    edgewz((Rectangle){x, f.y + 4, w / 2 - 3, 190}, "SOURCE\nRANGE", log10(fmax(cps, 0.1)), -1, 5, 0, 0, 4.5, 5, 3, srml);
    double trip = P->mode == RM_MODE_RUN ? 115.0 : 15.0;
    edgewz((Rectangle){x + w / 2 + 3, f.y + 4, w / 2 - 3, 190}, "APRM\nPCT", rm_plant_aprm(P), 0, 125, 0, 100, trip, 125, 5,
           NULL);
    tag(x, f.y + 230, "THERMAL MWT");
    readout(x, f.y + 245, 18, 4, "%4.0f", c->p_thermal / 1e6);
    tag(x, f.y + 290, "APRM AVG %");
    readout(x, f.y + 305, 14, 4, "%5.1f", rm_plant_aprm(P));
    text(P->mode == RM_MODE_RUN ? "TRIP 115%" : "SETDOWN 15%", x, f.y + 336, 10, (Color){150, 20, 10, 255});
    /* the scale of the cell gauges */
    tag(x, f.y + 350, "GAUGE SCALE C");
    Vector2 kc = {x + w / 2, f.y + 398};
    small_gauge(kc, 22, (900 - FUEL_LO) / (FUEL_HI - FUEL_LO), 0);
    ctext("900", kc.x, kc.y - 32, 10, INK);
    text("300", x, kc.y + 18, 10, INK);
    text("1500", x + w - 26, kc.y + 18, 10, INK);
}

static void cm_bench(Rectangle bb)
{
    /* reactor mode switch: a key-lock rotary */
    float cx = bb.x + 170, cy = bb.y + 150;
    plate_c(cx, bb.y + 4, "REACTOR MODE SWITCH", 12);
    static const char *modes[4] = {"SHUTDOWN", "REFUEL", "STARTUP", "RUN"};
    int pk = rotary((Vector2){cx, cy}, 30, 4, modes, P->mode, -200, 20, 70);
    if (pk >= 0 && pk != P->mode) {
        char why[80];
        if (!rm_plant_set_mode(P, pk, why, sizeof why)) logmsg("%s", why);
    }
    DrawCircleV((Vector2){cx, cy}, 9, (Color){196, 164, 80, 255});
    DrawLineEx((Vector2){cx - 5, cy}, (Vector2){cx + 5, cy}, 3, (Color){60, 50, 30, 255});
    DrawCircleV((Vector2){cx + 16, cy + 12}, 6, (Color){210, 210, 200, 255});
    DrawLineEx((Vector2){cx + 5, cy + 3}, (Vector2){cx + 12, cy + 9}, 3, (Color){180, 180, 170, 255});
    dymo(cx - 90, cy + 70, "RUN ONLY ABOVE 5% APRM");
    dymo(cx - 90, cy + 88, "STARTUP: APRM TRIP 15%");

    /* IRM range, all channels at once */
    float ix = bb.x + bb.width - 130, iy = bb.y + 150;
    plate_c(ix, bb.y + 4, "IRM RANGE - ALL", 12);
    static const char *rng[10] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
    int rk = rotary((Vector2){ix, iy}, 22, 10, rng, P->nms.irm_range[0] - 1, -225, 45, 42);
    if (rk >= 0) {
        for (int k = 0; k < 8; k++) P->nms.irm_range[k] = rk + 1;
        logmsg("IRM RANGE SWITCHES A-H TO %d", rk + 1);
    }
    text("EACH RANGE HALF A DECADE", ix - 80, iy + 70, 10, INK);
    text("RANGE 10 = 40% FULL SCALE", ix - 80, iy + 84, 10, INK);
}

void draw_reactor(void)
{
    static const secdef S[3] = {{"1-1", "NUCLEAR INSTRUMENTATION AND PROTECTION", 560, SEC_NI, 6},
                                {"1-2", "REACTOR CONTROL", 800, SEC_RC, 7},
                                {"1-3", "CORE MONITORING", 552, SEC_CM, 6}};
    secrect R[3];
    hover_rod = -1;
    board_frame(S, 3, R);
    ni_face(R[0].face);
    ni_bench(R[0].bench);
    rc_face(R[1].face);
    rc_bench(R[1].bench);
    cm_face(R[2].face);
    cm_bench(R[2].bench);
    board_strip();
    rm_core *c = &P->core;
    if (hover_rod >= 0) {
        char tip[96];
        int k = hover_rod;
        snprintf(tip, sizeof tip, "ROD %s  GROUP %d (%s)  NOTCH %02d  (%.0f CM IN)", rod_id[k], group_of(c->ctrl_bank[k]),
                 bank_name[c->ctrl_bank[k]], notch(c->rod_ins[k]), c->rod_ins[k]);
        tooltip(tip, (Rectangle){0, 0, CW, CH});
    } else if (hover_cell >= 0) {
        char tip[96];
        int k = hover_cell;
        snprintf(tip, sizeof tip, "ROD %s  GROUP %d  CELL OF %d CHANNELS  FUEL MAX %.0f C  NOTCH %02d", rod_id[k],
                 group_of(c->ctrl_bank[k]), CELL.n[k], cell_fuel(k), notch(c->rod_ins[k]));
        tooltip(tip, (Rectangle){0, 0, CW, CH});
    }
}
