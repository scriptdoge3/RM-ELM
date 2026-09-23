/*
 * The main control board: the reactor and its neutron monitoring and
 * protection, rod control, the sodium pumps, isolation and decay heat
 * removal, with the main annunciators and the alarm typer.
 *
 *   REACTOR | FULL CORE DISPLAY | CORE MONITORING | NEUTRON MONITORING & RPS
 *   ROD CONTROL | SODIUM PUMPS | ANNUNCIATORS / ISOLATION / TYPER | DRACS
 */
#include "gui.h"
#include "rm_xs.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *bank_name[RM_NBANKS] = {"REG", "SHIM A", "SHIM B", "SHIM C", "SHIM D", "SAFETY"};
static const Color BANK_C[RM_NBANKS] = {
    {240, 240, 230, 255}, {250, 200, 60, 255}, {90, 200, 110, 255},
    {80, 150, 230, 255}, {200, 120, 230, 255}, {230, 60, 50, 255},
};
static const char *bank_short[RM_NBANKS] = {"REG", "A", "B", "C", "D", "SAFE"};

/* ---- reactor: recorder and readouts ------------------------------------------ */
static void draw_reactor(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, "REACTOR");
    strip((Rectangle){r.x + 10, r.y + 30, r.width - 20, 320}, TR_POWER, 0, 120, "PWR %", TR_OUTLET, 300, 700, "OUT C");

    float x0 = r.x + 14, y = r.y + 360;
    double beta = 0;
    for (int i = 0; i < 6; i++) beta += c->pk.beta[i];
    struct { const char *lab; char val[16]; } cell[8];
    snprintf(cell[0].val, 16, "%4.0f", c->p_thermal / 1e6);
    cell[0].lab = "REACTOR MWT";
    snprintf(cell[1].val, 16, "%5.1f", 100 * c->p_thermal / RM_P_RATED);
    cell[1].lab = "PCT RATED";
    if (isfinite(P->period) && fabs(P->period) < 999) snprintf(cell[2].val, 16, "%5.1f", P->period);
    else snprintf(cell[2].val, 16, "----");
    cell[2].lab = "PERIOD SEC";
    snprintf(cell[3].val, 16, "%5.0f", 1e5 * c->rho);
    cell[3].lab = "REACT PCM";
    snprintf(cell[4].val, 16, "%5.2f", c->rho / beta);
    cell[4].lab = "REACT $";
    snprintf(cell[5].val, 16, "%5.1f", log10(fmax(c->p_thermal / RM_P_RATED, 1e-12)));
    cell[5].lab = "LOG POWER";
    snprintf(cell[6].val, 16, "%4.0f", rm_core_max_fuel_T(c) - 273.15);
    cell[6].lab = "FUEL MAX C";
    snprintf(cell[7].val, 16, "%4.0f", rm_core_max_clad_T(c) - 273.15);
    cell[7].lab = "CLAD MAX C";
    for (int i = 0; i < 8; i++) {
        float cx = x0 + (i % 2) * 140, cy = y + (i / 2) * 50;
        dymo(cx, cy, cell[i].lab);
        readout(cx, cy + 16, 18, 5, "%s", cell[i].val);
    }
}

/* ---- full core display and rod select matrix -------------------------------- */
/* The rods sit on an index-7 sub-lattice of the hex grid, which is itself a
 * triangular lattice turned by 19.1 degrees. Turned back, the 55 rods fall
 * into 9 staggered rows, laid out like a BWR full core display, and get
 * BWR-style XX-YY coordinates. */
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

static void select_rod(int k)
{
    rm_plant_select_rod(P, P->nms.sel_rod == k ? -1 : k);
}

static void draw_rodselect(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, "FULL CORE DISPLAY");
    Vector2 m = GetMousePosition();
    int blink = blink_fast();
    int hover = -1, sel = P->nms.sel_rod;

    /* upper board: one 2-digit notch readout per rod on a ruled grid */
    const float cp = 22, rp = 32;
    float gx0 = r.x + 40 + (r.width - 60 - (cmax - cmin) * cp) / 2, gy0 = r.y + 46;
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
        if (k == sel)
            DrawRectangleLinesEx((Rectangle){box.x - 3, box.y - 3, box.width + 6, box.height + 6}, 2,
                                 blink ? L_WHT : alpha(L_WHT, 110));
        int drifting = c->rod_drift[k] != 0.0 && !c->scram;
        if (drifting && blink) DrawRectangleRec((Rectangle){box.x - 2, box.y - 2, box.width + 4, box.height + 4}, L_RED);
        readout(box.x, box.y, 14, 2, "%02d", notch(c->rod_ins[k]));
        if (fabs(c->rod_vel[k]) > 0 && !c->scram) DrawCircleV((Vector2){box.x + box.width - 2, box.y + 2}, 2.5f, L_AMB);
        if (CheckCollisionPointRec(m, box)) {
            hover = k;
            if (clicked(box)) select_rod(k);
        }
    }

    /* desk: rod select matrix and drive */
    Rectangle d = {r.x + 6, r.y + 360, r.width - 12, 204};
    desk(d);
    dymo(d.x + 8, d.y + 5, "ROD SELECT");
    Rectangle mx = {d.x + 8, d.y + 22, 188, 128};
    DrawRectangleRec(mx, (Color){44, 48, 46, 255});
    DrawRectangleLinesEx(mx, 2, BEZEL);
    float mx0 = mx.x + 13 + (mx.width - 26 - (cmax - cmin) * 11) / 2, my0 = mx.y + 7;
    for (int k = 0; k < c->nctrl; k++) {
        Rectangle bt = {mx0 + (rod_ci[k] - cmin) * 11 - 9, my0 + (rod_rj[k] - rmin) * 13.5f, 18, 10};
        int on = k == sel;
        DrawRectangleRec(bt, on ? L_WHT : (Color){22, 22, 20, 255});
        DrawRectangleRec((Rectangle){bt.x, bt.y, bt.width, 1}, on ? WHITE : (Color){90, 90, 86, 255});
        if (CheckCollisionPointRec(m, bt)) {
            DrawRectangleLinesEx(bt, 1, (Color){170, 170, 160, 255});
            hover = k;
            if (clicked(bt)) select_rod(k);
        }
    }
    text("SELECT A ROD, THEN DRIVE IT.", d.x + 8, d.y + 156, 10, INK);
    text("NOTCH 00 = FULL IN, 40 = OUT", d.x + 8, d.y + 170, 10, INK);
    text("4 CM PER NOTCH", d.x + 8, d.y + 184, 10, INK);

    float x = d.x + 206, y = d.y + 6;
    int s = P->nms.sel_rod;
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
        int pressing = s >= 0 && input_ok && CheckCollisionPointRec(m, br) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        if (lampbutton(br, lab[k], L_WHT, pressing) && s >= 0) {
            int nt = notch(c->rod_target[s]) + dn[k];
            if (nt < 0) nt = 0;
            if (nt > (int)(RM_ACTIVE_H / NOTCH_CM)) nt = (int)(RM_ACTIVE_H / NOTCH_CM);
            double target = RM_ACTIVE_H - NOTCH_CM * nt;
            char why[80];
            if (c->ctrl_bank[s] == BANK_REG && P->auto_rod && !c->scram)
                logmsg("ROD %s IS IN REG BANK - SELECT MAN FIRST", rod_id[s]);
            else if (!rm_plant_rod_permit(P, -1, s, target, why, sizeof why)) logmsg("%s", why);
            else rm_core_rod_move(c, s, target);
        }
    }
    /* rod block monitor on the selected rod */
    float ry = d.y + 130;
    dymo(x, ry, "RBM PCT");
    if (s >= 0) readout(x, ry + 16, 14, 3, "%3.0f", 100 * P->nms.rbm);
    else readout(x, ry + 16, 14, 3, "---");
    lamp(x + 70, ry + 26, 5, L_AMB, s >= 0 && P->nms.rbm > 1.08 && rm_plant_aprm(P) > 30.0);
    text("RBM HI", x + 80, ry + 21, 10, INK);
    lamp(x + 140, ry + 26, 5, L_RED, s >= 0 && c->rod_drift[s] != 0.0);
    text("DRIFT", x + 150, ry + 21, 10, INK);

    if (hover >= 0) {
        char tip[80];
        snprintf(tip, sizeof tip, "ROD %s  %s  NOTCH %02d  (%.0f CM IN)", rod_id[hover], bank_name[c->ctrl_bank[hover]],
                 notch(c->rod_ins[hover]), c->rod_ins[hover]);
        tooltip(tip, r);
    }
}

/* ---- core statistics ---------------------------------------------------------------- */
static struct {
    double pch[1024], tout[1024], tclad[1024], tfuel[1024], margin[1024];
    double margin_min;
} CS;
double CS_margin_min = 1e9;

void core_statistics(void)
{
    rm_core *c = &P->core;
    rm_coreth *th = &c->th;
    int nch = c->nchan;
    CS.margin_min = 1e9;
    for (int ch = 0; ch < nch && ch < 1024; ch++) {
        double pw = 0, tc = 0, tf = 0, mg = 1e9;
        for (int k = 0; k < RM_NZ_ACT; k++) {
            int fn = core_fnode(c, ch, k);
            pw += th->q_node[fn];
            if (th->T[TH_CL][fn] > tc) tc = th->T[TH_CL][fn];
            if (th->T_center[fn] > tf) tf = th->T_center[fn];
            double p = th->p_out + 830.0 * 9.81 * (RM_ACTIVE_H / 100.0) * (1.0 - (k + 0.5) / RM_NZ_ACT);
            double mm = rm_na_tsat(p) - th->T[TH_C][fn];
            if (mm < mg) mg = mm;
        }
        CS.pch[ch] = pw;
        CS.tout[ch] = th->T_out[ch];
        CS.tclad[ch] = tc;
        CS.tfuel[ch] = tf;
        CS.margin[ch] = mg;
        if (mg < CS.margin_min) CS.margin_min = mg;
    }
    CS_margin_min = CS.margin_min;
}

/* ---- core temperature map: one small gauge per 2x2 group of channels ------------------- */
#define MAXGRP 160
static struct {
    int n;
    int nm[MAXGRP];
    int ch[MAXGRP][4];
    double x[MAXGRP], y[MAXGRP];   /* pitch units */
    double ext;
} GR;

static int floordiv2(int a) { return a >= 0 ? a / 2 : -((1 - a) / 2); }

static void group_channels(void)
{
    rm_core *c = &P->core;
    rm_hexgrid *g = &c->dif.grid;
    int kq[MAXGRP], kr[MAXGRP];
    GR.n = 0;
    GR.ext = 1;
    for (int col = 0; col < g->n; col++) {
        if (g->ring[col] > RM_CORE_RINGS || c->coltype[col] != COL_FUEL) continue;
        int q = floordiv2(g->q[col]), rr = floordiv2(g->r[col]);
        int k;
        for (k = 0; k < GR.n; k++)
            if (kq[k] == q && kr[k] == rr) break;
        if (k == GR.n) {
            if (GR.n >= MAXGRP) continue;
            kq[k] = q;
            kr[k] = rr;
            GR.nm[k] = 0;
            /* centre of the full 2x2 rhombus, so the gauges sit on a regular lattice */
            double aq = 2 * q + 0.5, ar = 2 * rr + 0.5;
            GR.x[k] = aq + 0.5 * ar;
            GR.y[k] = sqrt(3.0) / 2 * ar;
            GR.n++;
        }
        if (GR.nm[k] < 4) GR.ch[k][GR.nm[k]++] = c->chan_of_col[col];
        double e = fmax(fabs(GR.x[k]), fabs(GR.y[k]));
        if (e > GR.ext) GR.ext = e;
    }
}

static int cm_mode = 1;          /* 1 outlet T, 2 clad T */

static double group_value(int k)
{
    double v = cm_mode == 2 ? 0 : 0;
    for (int i = 0; i < GR.nm[k]; i++) {
        int ch = GR.ch[k][i];
        if (cm_mode == 2) v = fmax(v, CS.tclad[ch]);
        else v += CS.tout[ch] / GR.nm[k];
    }
    return v - 273.15;
}

/* a little round dial: 240 degree scale, red zone at the top end */
static void small_gauge(Vector2 c, float R, double f, int alarm)
{
    int bl = blink_fast();
    DrawCircleV(c, R, alarm && bl ? (Color){200, 30, 20, 255} : BEZEL);
    DrawCircleV(c, R - 2.5f, FACE);
    const float a0 = 150, a1 = 390;
    DrawRing(c, R - 6, R - 3, a0 + (a1 - a0) * 0.85f, a1, 8, (Color){210, 40, 30, 255});
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

static void draw_coremon(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, "CORE MONITORING");
    if (GR.n == 0) group_channels();
    Vector2 m = GetMousePosition();

    if (lampbutton((Rectangle){r.x + 12, r.y + 28, 90, 28}, "OUTLET\nTEMP", L_WHT, cm_mode == 1)) cm_mode = 1;
    if (lampbutton((Rectangle){r.x + 106, r.y + 28, 90, 28}, "CLAD\nTEMP", L_WHT, cm_mode == 2)) cm_mode = 2;
    dymo(r.x + 206, r.y + 30, "CORE TEMPERATURE MAP - ONE GAUGE PER 2X2 CHANNEL GROUP");
    text(cm_mode == 2 ? "SCALE 450-700 C (HOTTEST CLAD)" : "SCALE 450-600 C (MIXED OUTLET)", r.x + 206, r.y + 46, 10, INK);

    Rectangle mp = {r.x + 12, r.y + 62, 486, 500};
    DrawRectangleRec(mp, (Color){40, 42, 40, 255});
    DrawRectangleLinesEx(mp, 2, BEZEL);
    Vector2 o = {mp.x + mp.width / 2, mp.y + mp.height / 2};
    float sc = (float)((mp.width / 2 - 26) / GR.ext);
    float R = sc * 0.98f;
    double lo = 450, hi = cm_mode == 2 ? 700 : 600;
    int hover = -1;
    for (int k = 0; k < GR.n; k++) {
        Vector2 p = {o.x + (float)GR.x[k] * sc, o.y + (float)GR.y[k] * sc};
        double v = group_value(k);
        double f = (v - lo) / (hi - lo);
        small_gauge(p, R, f, f > 0.85);
        if (CheckCollisionPointCircle(m, p, R)) hover = k;
    }

    /* ---- right column ---- */
    float x = r.x + 508, w = r.width - 520;
    static const char *srml[8] = {".1", "", "10", "", "1E3", "", "1E5", ""};
    double cps = rm_plant_srm_cps(P);
    meterl((Rectangle){x, r.y + 28, w, 104}, "SOURCE RANGE CPS", log10(fmax(cps, 0.1)), -1, 6, 5, 6, 7, srml);
    double trip = P->mode == RM_MODE_RUN ? 115.0 : 15.0;
    meter((Rectangle){x, r.y + 136, w, 104}, P->mode == RM_MODE_RUN ? "APRM PCT" : "APRM PCT SETDOWN", rm_plant_aprm(P), 0,
          125, trip, 125, 5);
    dymo(x, r.y + 248, "THERMAL POWER MWTH");
    readout(x, r.y + 264, 28, 4, "%4.0f", c->p_thermal / 1e6);
    dymo(x, r.y + 306, "APRM AVG PCT");
    readout(x, r.y + 322, 16, 4, "%5.1f", rm_plant_aprm(P));

    float by = r.y + 360;
    plate(x, by, "REACTOR MODE", 10);
    static const char *modes[4] = {"SHUTDOWN", "REFUEL", "STARTUP", "RUN"};
    int pk = rotary((Vector2){x + w / 2 + 4, by + 84}, 22, 4, modes, P->mode, -200, 20, 50);
    if (pk >= 0 && pk != P->mode) {
        char why[80];
        if (!rm_plant_set_mode(P, pk, why, sizeof why)) logmsg("%s", why);
    }

    static const char *rng[10] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
    ctext("IRM RANGE - ALL", x + w / 2, r.y + 474, 10, INK);
    int rk = rotary((Vector2){x + w / 2, r.y + 522}, 16, 10, rng, P->nms.irm_range[0] - 1, -225, 45, 32);
    if (rk >= 0) {
        for (int k = 0; k < 8; k++) P->nms.irm_range[k] = rk + 1;
        logmsg("IRM RANGE SWITCHES A-H TO %d", rk + 1);
    }

    if (hover >= 0) {
        char tip[160];
        int n = snprintf(tip, sizeof tip, "GROUP %d:", hover + 1);
        for (int i = 0; i < GR.nm[hover] && n < (int)sizeof tip - 40; i++) {
            int ch = GR.ch[hover][i];
            n += snprintf(tip + n, sizeof tip - n, "  CH%03d %.0f/%.0fC", ch + 1, CS.tout[ch] - 273.15, CS.tclad[ch] - 273.15);
        }
        tooltip(tip, r);
    }
}

/* ---- neutron monitoring and reactor protection ------------------------------------------ */
static void cycle_bypass(int *b, const int *opts, int n)
{
    int k = 0;
    for (int i = 0; i < n; i++)
        if (opts[i] == *b) k = i;
    *b = opts[(k + 1) % n];
}

static void draw_nms(Rectangle r)
{
    rm_nms *nm = &P->nms;
    steel(r, "NEUTRON MONITORING - RPS");
    char b[32];

    /* SRM A-D */
    static const char *srml[7] = {".1", "10", "1E3", "1E5", "", "", ""};
    for (int i = 0; i < 4; i++) {
        float x = r.x + 12 + i * 50;
        snprintf(b, sizeof b, "SRM %c", 'A' + i);
        edgew((Rectangle){x, r.y + 30, 44, 104}, b, log10(fmax(rm_plant_srm_ch(P, i), 0.1)), -1, 5, 4.5, 5, 3, srml);
    }
    /* detector drives */
    float dx = r.x + 214;
    const char *dn[2] = {"SRM DETECTORS", "IRM DETECTORS"};
    for (int k = 0; k < 2; k++) {
        float y = r.y + 30 + k * 66;
        double pos = k ? nm->irm_pos : nm->srm_pos;
        int *drv = k ? &nm->irm_drive : &nm->srm_drive;
        dymo(dx, y, dn[k]);
        if (lampbutton((Rectangle){dx, y + 16, 58, 26}, "INSERT", L_WHT, *drv > 0)) *drv = 1;
        if (lampbutton((Rectangle){dx + 60, y + 16, 58, 26}, "RETRACT", L_WHT, *drv < 0)) *drv = -1;
        readout(dx + 124, y + 14, 12, 3, "%3.0f", 100 * pos);
        lamp(dx + 180, y + 20, 4, L_RED, pos >= 0.99);
        text("IN", dx + 188, y + 15, 10, INK);
        lamp(dx + 180, y + 36, 4, L_GRN, pos <= 0.01);
        text("OUT", dx + 188, y + 31, 10, INK);
        text("POS %", dx + 124, y + 44, 10, INK);
    }

    /* IRM A-H with their range switches */
    float iy = r.y + 170;
    for (int i = 0; i < 8; i++) {
        float x = r.x + 12 + i * 54;
        snprintf(b, sizeof b, "IRM %c", 'A' + i);
        int byp = nm->irm_bypass[i & 1] == i;
        edgew((Rectangle){x, iy, 44, 104}, byp ? "BYPASS" : b, rm_plant_irm_ch(P, i), 0, 125, RM_IRM_TRIP, 125, 5, NULL);
        readout(x, iy + 120, 12, 2, "%2d", nm->irm_range[i]);
        if (lampbutton((Rectangle){x + 30, iy + 118, 16, 12}, "", L_WHT, 0) && nm->irm_range[i] < 10) {
            nm->irm_range[i]++;
            logmsg("IRM %c RANGE %d", 'A' + i, nm->irm_range[i]);
        }
        if (lampbutton((Rectangle){x + 30, iy + 131, 16, 12}, "", L_WHT, 0) && nm->irm_range[i] > 1) {
            nm->irm_range[i]--;
            logmsg("IRM %c RANGE %d", 'A' + i, nm->irm_range[i]);
        }
        DrawTriangle((Vector2){x + 38, iy + 121}, (Vector2){x + 34, iy + 127}, (Vector2){x + 42, iy + 127}, INK);
        DrawTriangle((Vector2){x + 34, iy + 134}, (Vector2){x + 38, iy + 140}, (Vector2){x + 42, iy + 134}, INK);
    }
    text("RANGE", r.x + 12, iy + 146, 10, INK);

    /* APRM A-F */
    float ay = r.y + 334;
    double sp = P->mode == RM_MODE_RUN ? 115.0 : 15.0;
    for (int i = 0; i < 6; i++) {
        float x = r.x + 12 + i * 50;
        snprintf(b, sizeof b, "APRM %c", 'A' + i);
        int byp = nm->aprm_bypass[i & 1] == i;
        edgew((Rectangle){x, ay, 44, 100}, byp ? "BYPASS" : b, rm_plant_aprm_ch(P, i), 0, 125, sp, 125, 5, NULL);
    }
    /* bypass joysticks */
    float bx = r.x + 316;
    static const int irm_a[5] = {-1, 0, 2, 4, 6}, irm_b[5] = {-1, 1, 3, 5, 7}, aprm_a[4] = {-1, 0, 2, 4}, aprm_b[4] = {-1, 1, 3, 5};
    struct { const char *n; int *v; const int *o; int k; } bs[4] = {
        {"IRM BYP A", &nm->irm_bypass[0], irm_a, 5}, {"IRM BYP B", &nm->irm_bypass[1], irm_b, 5},
        {"APRM BYP A", &nm->aprm_bypass[0], aprm_a, 4}, {"APRM BYP B", &nm->aprm_bypass[1], aprm_b, 4}};
    for (int k = 0; k < 4; k++) {
        char lg[32];
        if (*bs[k].v < 0) snprintf(lg, sizeof lg, "%s\nNONE", bs[k].n);
        else snprintf(lg, sizeof lg, "%s\n%s %c", bs[k].n, k < 2 ? "IRM" : "APRM", 'A' + *bs[k].v);
        if (lampbutton((Rectangle){bx, ay + k * 30, 122, 28}, lg, L_AMB, *bs[k].v >= 0)) {
            cycle_bypass(bs[k].v, bs[k].o, bs[k].k);
            if (*bs[k].v >= 0) logmsg("%s: %s %c BYPASSED", bs[k].n, k < 2 ? "IRM" : "APRM", 'A' + *bs[k].v);
        }
    }

    /* RPS scram groups (lit = energised) and channel bypass keys */
    float ry = r.y + 470;
    DrawLineEx((Vector2){r.x + 8, ry - 6}, (Vector2){r.x + r.width - 8, ry - 6}, 1, PAINT_LO);
    text("RPS SCRAM GROUPS - LIT = ENERGISED", r.x + 12, ry, 10, INK);
    static const char *gn[4] = {"A1", "A2", "B1", "B2"};
    for (int i = 0; i < 4; i++) {
        float x = r.x + 30 + i * 60;
        lamp(x, ry + 26, 8, L_WHT, !nm->trip[i]);
        text(gn[i], x + 12, ry + 20, 10, INK);
        if (keysw(x + 4, ry + 70, "", "BYP", nm->bypass[i])) {
            /* one bypass per division */
            int other = i ^ 1;
            if (!nm->bypass[i] && nm->bypass[other]) logmsg("RPS %s BYPASS REFUSED: %s ALREADY BYPASSED", gn[i], gn[other]);
            else {
                nm->bypass[i] = !nm->bypass[i];
                logmsg("RPS CHANNEL %s %s", gn[i], nm->bypass[i] ? "BYPASSED" : "IN SERVICE");
            }
        }
    }
    /* rod worth minimizer and rod block monitor keys */
    float kx = r.x + 280;
    char why[80] = "";
    int rwm = nm->sel_rod >= 0 && !rm_plant_rod_permit(P, -1, nm->sel_rod, 0.0, why, sizeof why) && !strncmp(why, "RWM", 3);
    lamp(kx, ry + 26, 6, L_AMB, rwm);
    text("RWM BLOCK", kx + 10, ry + 21, 10, INK);
    lamp(kx + 90, ry + 26, 6, L_WHT, rm_plant_aprm(P) >= 20.0 || nm->rwm_bypass);
    text("RWM OFF", kx + 100, ry + 21, 10, INK);
    if (keysw(kx + 26, ry + 70, "RWM", "BYP", nm->rwm_bypass)) {
        nm->rwm_bypass = !nm->rwm_bypass;
        logmsg("ROD WORTH MINIMIZER %s", nm->rwm_bypass ? "BYPASSED" : "IN SERVICE");
    }
    if (keysw(kx + 116, ry + 70, "RBM", "BYP", nm->rbm_bypass)) {
        nm->rbm_bypass = !nm->rbm_bypass;
        logmsg("ROD BLOCK MONITOR %s", nm->rbm_bypass ? "BYPASSED" : "IN SERVICE");
    }
}

/* ---- rod control console ---------------------------------------------------------------- */
static int mushroom(Vector2 sc, float rad, const char *label)
{
    Vector2 m = GetMousePosition();
    int hov = input_ok && CheckCollisionPointCircle(m, sc, rad);
    int dn = hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    DrawCircleV(sc, rad + 10, (Color){238, 196, 30, 255});
    DrawRing(sc, rad + 7, rad + 10, 0, 360, 48, INK);
    ctext(label, sc.x, sc.y + rad + 14, 10, INK);
    DrawCircleV((Vector2){sc.x + 3, sc.y + 4}, rad, alpha(BLACK, 90));
    DrawCircleV(sc, dn ? rad - 3 : rad, dn ? (Color){160, 20, 16, 255} : (Color){206, 28, 22, 255});
    DrawCircleV((Vector2){sc.x - rad * 0.3f, sc.y - rad * 0.33f}, rad * 0.33f, alpha(WHITE, 70));
    return hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void draw_console(Rectangle r)
{
    rm_core *c = &P->core;
    steel(r, "ROD CONTROL - REACTOR PROTECTION");
    Vector2 m = GetMousePosition();

    /* one manual scram per RPS division: both for a full scram */
    for (int d = 0; d < 2; d++) {
        char lb[24];
        snprintf(lb, sizeof lb, "SCRAM DIV %c", 'A' + d);
        if (mushroom((Vector2){r.x + 48 + d * 96, r.y + 66}, 26, lb)) {
            rm_plant_manual_scram_div(P, d);
            logmsg("MANUAL SCRAM DIVISION %c", 'A' + d);
        }
    }
    for (int d = 0; d < 2; d++) {
        char lb[24];
        snprintf(lb, sizeof lb, "RESET\nDIV %c", 'A' + d);
        if (lampbutton((Rectangle){r.x + 204 + d * 74, r.y + 30, 70, 38}, lb, L_AMB, P->nms.div_trip[d])) {
            rm_plant_rps_reset(P, d);
            logmsg("RPS DIVISION %c RESET", 'A' + d);
        }
    }
    if (keysw(r.x + 398, r.y + 58, "NORM", "BYP", P->rps_bypass)) {
        P->rps_bypass = !P->rps_bypass;
        logmsg(P->rps_bypass ? "RPS BYPASSED - TRIPS DISABLED" : "RPS ARMED");
    }
    text("RPS KEY", r.x + 378, r.y + 76, 10, INK);
    window((Rectangle){r.x + 204, r.y + 74, 222, 38}, "FIRST OUT", P->first_out[0] ? P->first_out : NULL, L_RED,
           P->first_out[0] != 0);

    /* rod banks */
    float y = r.y + 122;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + r.width - 8, y - 4}, 1, PAINT_LO);
    text("BANK      CM IN  BOT TOP   WITHDRAW       INSERT", r.x + 12, y, 10, INK);
    y += 16;
    for (int bk = 0; bk < RM_NBANKS; bk++) {
        double pos = rm_core_bank_pos(c, bk);
        dymo(r.x + 12, y + 6, bank_name[bk]);
        readout(r.x + 74, y, 14, 3, "%3.0f", pos);
        lamp(r.x + 126, y + 12, 5, L_GRN, pos >= RM_ACTIVE_H - 0.5);
        lamp(r.x + 146, y + 12, 5, L_RED, pos <= 0.5);
        int blocked = bk == BANK_REG && P->auto_rod && !c->scram;
        const char *lab[4] = {"OUT 20", "OUT 2", "IN 2", "IN 20"};
        const double step[4] = {-20, -2, 2, 20};
        for (int k = 0; k < 4; k++) {
            Rectangle br = {r.x + 164 + k * 66, y, 62, 26};
            int pressing = !blocked && input_ok && CheckCollisionPointRec(m, br) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            if (lampbutton(br, lab[k], L_WHT, pressing) && !blocked) {
                char why[80];
                double target = fmin(RM_ACTIVE_H, fmax(0.0, pos + step[k]));
                if (!rm_plant_rod_permit(P, bk, -1, target, why, sizeof why)) logmsg("%s", why);
                else rm_core_bank_shift(c, bk, step[k]);
            }
        }
        y += 30;
    }

    /* regulating bank auto control */
    y = r.y + 326;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + r.width - 8, y - 4}, 1, PAINT_LO);
    dymo(r.x + 12, y, "REG BANK");
    if (lampbutton((Rectangle){r.x + 12, y + 16, 60, 34}, "AUTO", L_WHT, P->auto_rod) && !P->auto_rod) {
        P->auto_rod = 1;
        logmsg("AUTOMATIC ROD CONTROL ON");
    }
    if (lampbutton((Rectangle){r.x + 76, y + 16, 60, 34}, "MAN", L_AMB, !P->auto_rod) && P->auto_rod) {
        P->auto_rod = 0;
        logmsg("AUTOMATIC ROD CONTROL OFF");
    }
    dymo(r.x + 146, y, "DEMAND %");
    readout(r.x + 146, y + 16, 22, 3, "%3.0f", 100 * P->power_set);
    if (lampbutton((Rectangle){r.x + 230, y + 16, 90, 34}, "LOWER", L_WHT, 0))
        P->power_set = fmax(0.01, P->power_set - (P->power_set > 0.1 ? 0.05 : 0.01));
    if (lampbutton((Rectangle){r.x + 324, y + 16, 90, 34}, "RAISE", L_WHT, 0))
        P->power_set = fmin(1.05, P->power_set + (P->power_set >= 0.1 ? 0.05 : 0.01));

    /* rod control status */
    y = r.y + 384;
    double reg = rm_core_bank_pos(c, BANK_REG);
    struct { const char *l; int on; Color c; } st[6] = {
        {"WITHDRAWAL BLOCK", rm_plant_rod_block(P, 0, NULL, 0), L_AMB},
        {"REG AT LIMIT", reg < 0.5 || reg > RM_ACTIVE_H - 0.5, L_AMB},
        {"AUTO IN CONTROL", P->auto_rod && !c->scram, L_WHT},
        {"RODS MOVING", 0, L_WHT},
        {"SCRAM BKRS OPEN", c->scram, L_RED},
        {"SAFETY BANK OUT", rm_core_bank_pos(c, BANK_SAFETY) < 0.5, L_RED},
    };
    for (int i = 0; i < c->nctrl; i++)
        if (fabs(c->rod_vel[i]) > 0) st[3].on = 1;
    for (int i = 0; i < 6; i++) {
        float lx = r.x + 20 + (i % 3) * 144, ly = y + 6 + (i / 3) * 20;
        lamp(lx, ly, 5, st[i].c, st[i].on);
        text(st[i].l, lx + 10, ly - 5, 10, INK);
    }
    char why[80];
    if (rm_plant_rod_block(P, 0, why, sizeof why)) text(why, r.x + 14, y + 46, 10, (Color){140, 30, 20, 255});
}

/* ---- sodium pumps ------------------------------------------------------------------ */
static void pump_column(rm_pump *pm, float cx, float y, int primary, int loop)
{
    char nm[16];
    snprintf(nm, sizeof nm, "%s-%d", primary ? "PP" : "SP", loop + 1);
    int run = pm->motor_on && !pm->tripped;
    int s = cswitch(cx, y, nm, "STOP", "START", run, !run, SW_PISTOL, NULL);
    const char *kind = primary ? "PRIMARY" : "SECONDARY";
    if (s > 0 && !run) {
        pm->tripped = 0;
        pm->motor_on = 1;
        if (pm->speed_set < 0.1) pm->speed_set = 1.0;
        logmsg("%s PUMP %d STARTED", kind, loop + 1);
    } else if (s < 0 && (run || pm->tripped)) {
        pm->motor_on = 0;
        pm->tripped = 0;
        logmsg("%s PUMP %d STOPPED", kind, loop + 1);
    }
    float x = cx - 44;
    readout(x, y + CSW_H + 2, 14, 3, "%3.0f", 100 * pm->speed);
    textf(x + 48, y + CSW_H + 4, 10, INK, "SET");
    textf(x + 48, y + CSW_H + 16, 10, INK, "%3.0f%%", 100 * pm->speed_set);
    if (lampbutton((Rectangle){x, y + CSW_H + 30, 42, 20}, "-", L_WHT, 0)) pm->speed_set = fmax(0.10, pm->speed_set - 0.05);
    if (lampbutton((Rectangle){x + 46, y + CSW_H + 30, 42, 20}, "+", L_WHT, 0)) pm->speed_set = fmin(1.05, pm->speed_set + 0.05);
}

static double master_flow = 1.0;

static void draw_pumps(Rectangle r)
{
    steel(r, "SODIUM PUMPS - MASTER FLOW CONTROL");
    const float cx0 = r.x + 122, cw = 92;
    float y = r.y + 30;
    dymo(r.x + 10, y + 2, "PRIMARY");
    text("SPEED %", r.x + 10, y + CSW_H + 6, 10, INK);
    text("SETPOINT", r.x + 10, y + CSW_H + 34, 10, INK);
    text("PONY MOTOR", r.x + 10, y + CSW_H + 60, 10, INK);
    text("FLOW KG/S", r.x + 10, y + CSW_H + 86, 10, INK);
    text("HOT/COLD C", r.x + 10, y + CSW_H + 112, 10, INK);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        float cx = cx0 + i * cw;
        pump_column(&l->ppump, cx, y, 1, i);
        if (lampbutton((Rectangle){cx - 44, y + CSW_H + 54, 88, 22}, "PONY", L_WHT, l->ppump.pony_on)) {
            l->ppump.pony_on = !l->ppump.pony_on;
            logmsg("PONY MOTOR %d %s", i + 1, l->ppump.pony_on ? "ARMED" : "OFF");
        }
        readout(cx - 44, y + CSW_H + 80, 12, 5, "%5.0f", l->W);
        readout(cx - 44, y + CSW_H + 106, 10, 3, "%3.0f", rm_na_T(l->hot.h[RM_PIPE_N - 1]) - 273.15);
        readout(cx + 2, y + CSW_H + 106, 10, 3, "%3.0f", rm_na_T(l->cold.h[RM_PIPE_N - 1]) - 273.15);
    }
    y = r.y + 262;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + 470, y - 4}, 1, PAINT_LO);
    dymo(r.x + 10, y + 2, "SECONDARY");
    text("SPEED %", r.x + 10, y + CSW_H + 6, 10, INK);
    text("SETPOINT", r.x + 10, y + CSW_H + 34, 10, INK);
    text("FLOW KG/S", r.x + 10, y + CSW_H + 60, 10, INK);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        float cx = cx0 + i * cw;
        pump_column(&l->spump, cx, y, 0, i);
        readout(cx - 44, y + CSW_H + 56, 12, 5, "%5.0f", l->Ws);
    }

    /* master flow controller: drives all primary pump speed setpoints together */
    float mx = r.x + r.width - 92;
    demarc((Rectangle){mx - 6, r.y + 34, 90, r.height - 44}, "MASTER");
    edgew((Rectangle){mx + 20, r.y + 48, 44, 170}, "CORE FLOW%", 100 * P->W_core / rm_plant_nominal_flow(), 0, 120, 0, 70,
          6, NULL);
    double avg = 0;
    for (int i = 0; i < RM_NLOOPS; i++) avg += P->loop[i].ppump.speed_set / RM_NLOOPS;
    master_flow = avg;
    dymo(mx, r.y + 240, "DEMAND %");
    readout(mx + 4, r.y + 256, 16, 3, "%3.0f", 100 * master_flow);
    int d = 0;
    if (lampbutton((Rectangle){mx, r.y + 292, 78, 32}, "RAISE", L_WHT, 0)) d = 1;
    if (lampbutton((Rectangle){mx, r.y + 328, 78, 32}, "LOWER", L_WHT, 0)) d = -1;
    if (d) {
        master_flow = fmin(1.05, fmax(0.10, master_flow + 0.05 * d));
        for (int i = 0; i < RM_NLOOPS; i++) P->loop[i].ppump.speed_set = master_flow;
        logmsg("MASTER FLOW DEMAND %.0f%%", 100 * master_flow);
    }
    text("SETS ALL FOUR", mx, r.y + 370, 10, INK);
    text("PRIMARY PUMPS", mx, r.y + 382, 10, INK);
}

/* ---- isolation --------------------------------------------------------------------------- */
static void draw_isolation(Rectangle r)
{
    steel(r, NULL);
    plate(r.x + 6, r.y + 5, "ISOLATION", 10);
    const float x0 = r.x + 48, cw = 76;
    text("FEED", r.x + 10, r.y + 40, 10, INK);
    text("STEAM", r.x + 10, r.y + 68, 10, INK);
    text("SG", r.x + 10, r.y + 96, 10, INK);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &P->loop[i].sg;
        float x = x0 + i * cw;
        char lb[16];
        snprintf(lb, sizeof lb, "SG %d", i + 1);
        dymo(x + 36, r.y + 18, lb);
        if (lampbutton((Rectangle){x, r.y + 34, 36, 24}, "OPEN", L_RED, s->fiv_open) && !s->fiv_open) {
            if (s->isolated) logmsg("SG %d IS BLOWN DOWN - CANNOT REOPEN", i + 1);
            else {
                s->fiv_open = 1;
                logmsg("SG %d FEED ISOLATION VALVE OPEN", i + 1);
            }
        }
        if (lampbutton((Rectangle){x + 38, r.y + 34, 36, 24}, "SHUT", L_GRN, !s->fiv_open) && s->fiv_open) {
            s->fiv_open = 0;
            logmsg("SG %d FEED ISOLATION VALVE SHUT", i + 1);
        }
        if (lampbutton((Rectangle){x, r.y + 62, 36, 24}, "OPEN", L_RED, s->msiv_open) && !s->msiv_open) {
            if (s->isolated) logmsg("SG %d IS BLOWN DOWN - CANNOT REOPEN", i + 1);
            else if (P->aux.air_p < 3.0) logmsg("SG %d MSIV WILL NOT OPEN: NO INSTRUMENT AIR", i + 1);
            else {
                s->msiv_open = 1;
                logmsg("SG %d MAIN STEAM ISOLATION VALVE OPEN", i + 1);
            }
        }
        if (lampbutton((Rectangle){x + 38, r.y + 62, 36, 24}, "SHUT", L_GRN, !s->msiv_open) && s->msiv_open) {
            s->msiv_open = 0;
            logmsg("SG %d MAIN STEAM ISOLATION VALVE SHUT", i + 1);
        }
        if (lampbutton((Rectangle){x, r.y + 90, 74, 24}, s->disc_burst ? "DISC BURST" : "ISOLATE",
                       s->disc_burst ? L_RED : L_AMB, s->isolated) && !s->isolated)
            rm_plant_isolate_sg(P, i);
    }
    float x = r.x + r.width - 96;
    dymo(x, r.y + 18, "CONTAINMENT");
    if (lampbutton((Rectangle){x, r.y + 34, 86, 24}, "ISOLATE", L_AMB, P->containment_isolated) && !P->containment_isolated) {
        P->containment_isolated = 1;
        logmsg("CONTAINMENT ISOLATION - PURIFICATION LINES SHUT");
    }
    if (lampbutton((Rectangle){x, r.y + 62, 86, 24}, "RESET", L_WHT, 0) && P->containment_isolated) {
        P->containment_isolated = 0;
        logmsg("CONTAINMENT ISOLATION RESET");
    }
    if (lampbutton((Rectangle){x, r.y + 90, 86, 24}, "ALL MSIV\nSHUT", L_GRN, 0)) {
        for (int i = 0; i < RM_NLOOPS; i++) P->loop[i].sg.msiv_open = 0;
        logmsg("MAIN STEAM ISOLATION - ALL MSIVS SHUT");
    }
}

/* ---- DRACS, hydrogen meters and the sodium temperature recorder --------------------------------- */
extern mpr MP_NA;

static void draw_dracs(Rectangle r)
{
    steel(r, "DECAY HEAT REMOVAL - DRACS");
    float y = r.y + 28;
    if (lampbutton((Rectangle){r.x + 10, y, 84, 32}, "AUTO\nON TRIP", L_WHT, P->dracs_auto) && !P->dracs_auto) {
        P->dracs_auto = 1;
        logmsg("DRACS DAMPERS AUTO");
    }
    dymo(r.x + 104, y, "NAT CIRC KG/S");
    readout(r.x + 104, y + 14, 12, 4, "%4.0f", P->W_dracs);
    dymo(r.x + 200, y, "TOTAL MW");
    readout(r.x + 200, y + 14, 12, 4, "%4.1f", P->Q_dracs / 1e6);
    dymo(r.x + 280, y, "AIR C");
    readout(r.x + 280, y + 14, 12, 3, "%3.0f", P->T_air - 273.15);
    dymo(r.x + 342, y, "DECAY MW");
    readout(r.x + 342, y + 14, 12, 3, "%3.0f", P->core.p_decay / 1e6);

    y = r.y + 66;
    for (int i = 0; i < 3; i++) {
        float cx = r.x + 52 + i * 146;
        char lb[24];
        snprintf(lb, sizeof lb, "DAMPER %c", 'A' + i);
        int open = P->dracs_damper[i] > 0.5;
        int s = cswitch(cx, y, lb, "CLOSE", "OPEN", open, !open, SW_PISTOL, NULL);
        if (s) {
            P->dracs_auto = 0;
            P->dracs_damper_set[i] = s > 0 ? 1.0 : 0.0;
            logmsg("DRACS TRAIN %c DAMPER %s", 'A' + i, s > 0 ? "OPEN" : "CLOSED");
        }
        readout(cx + 40, y + 36, 10, 3, "%3.0f", 100 * P->dracs_damper[i]);
        text("%", cx + 76, y + 40, 10, INK);
        readout(cx + 40, y + 58, 10, 3, "%4.1f", P->Q_dracs_train[i] / 1e6);
        text("MW", cx + 76, y + 62, 10, INK);
    }

    y = r.y + 164;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + r.width - 8, y - 4}, 1, PAINT_LO);
    dymo(r.x + 10, y + 4, "H2 IN NA PPM");
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &P->loop[i].sg;
        float x = r.x + 110 + i * 80;
        textf(x, y, 10, INK, "SG %d", i + 1);
        readout(x, y + 12, 12, 4, "%4.2f", fmin(s->h2, 99.99));
        lamp(x + 64, y + 22, 4, L_RED, s->h2 > RM_H2_ALARM && blink_fast());
    }
    mpr_draw((Rectangle){r.x + 10, r.y + 206, r.width - 20, r.height - 214}, &MP_NA, "SODIUM TEMPERATURES", "300 C",
             "700 C");
}

/* ---- annunciators and alarm typer --------------------------------------------------------------- */
static void draw_annunciators(Rectangle r)
{
    steel(r, NULL);
    ann_box((Rectangle){r.x + 6, r.y + 6, r.width - 12, r.height - 48}, BOARD_MAIN, 5, 30);
    ann_controls(r.x + 12, r.y + r.height - 38);
    text("HORN / SEQUENCE", r.x + 290, r.y + r.height - 28, 10, INK);
}

void draw_log(Rectangle r)
{
    steel(r, NULL);
    plate(r.x + 12, r.y + 12, "ALARM TYPER", 10);
    lamp(r.x + 22, r.y + 44, 5, L_AMB, GetTime() - last_log_time < 3.0);
    text("NEW", r.x + 32, r.y + 39, 10, INK);
    Rectangle pp = {r.x + 116, r.y + 6, r.width - 128, r.height - 12};
    DrawRectangleRec((Rectangle){pp.x - 4, pp.y - 2, pp.width + 8, pp.height + 4}, BEZEL);
    DrawRectangleRec(pp, (Color){240, 236, 220, 255});
    int nl = (int)((pp.height - 6) / 14);
    if (nl > NLOG) nl = NLOG;
    for (int i = 0; i < nl; i += 2)
        DrawRectangleRec((Rectangle){pp.x + 18, pp.y + 3 + i * 14, pp.width - 36, 14}, (Color){206, 228, 204, 255});
    for (float y = pp.y + 8; y < pp.y + pp.height; y += 14) {
        DrawCircleV((Vector2){pp.x + 8, y}, 3, (Color){60, 60, 56, 255});
        DrawCircleV((Vector2){pp.x + pp.width - 8, y}, 3, (Color){60, 60, 56, 255});
    }
    for (int i = 0; i < nl; i++) {
        int k = NLOG - nl + i;
        char line[100];
        snprintf(line, sizeof line, "%s", logs[k]);
        for (size_t n = strlen(line); n > 0 && MeasureText(line, 10) > pp.width - 44; n--) line[n - 1] = 0;
        text(line, pp.x + 24, pp.y + 5 + i * 14, 10, k == NLOG - 1 ? (Color){20, 20, 40, 255} : (Color){70, 70, 90, 255});
    }
}

void draw_main(void)
{
    /* upper row: the reactor */
    draw_reactor((Rectangle){6, 48, 300, 572});
    draw_rodselect((Rectangle){312, 48, 440, 572});
    draw_coremon((Rectangle){758, 48, 700, 572});
    draw_nms((Rectangle){1464, 48, 450, 572});
    /* lower row */
    draw_console((Rectangle){6, 626, 440, 450});
    draw_pumps((Rectangle){452, 626, 560, 450});
    draw_annunciators((Rectangle){1018, 626, 450, 250});
    draw_isolation((Rectangle){1018, 882, 450, 120});
    draw_log((Rectangle){1018, 1008, 450, 68});
    draw_dracs((Rectangle){1474, 626, 440, 450});
}
