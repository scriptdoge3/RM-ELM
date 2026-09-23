/*
 * Electrical board (panels 4-1 and 4-2):
 *
 *   4-1  ELECTRICAL DISTRIBUTION
 *        switchboard meters for the grid, output and the house buses; the
 *        output recorder, essential bus meters and status lights. On the
 *        bench, the one-line mimic in black: 345 kV switchyard breaker, main
 *        transformer, generator breaker, the startup and unit auxiliary
 *        transformers with the bus transfer switch, the 6.9 kV house buses
 *        and their feeders down to the 4.16 kV essential buses.
 *   4-2  EMERGENCY POWER AND DC
 *        a pair of meters and the status lights for each diesel, the DC and
 *        vital AC meters and recorder. On the bench, the essential buses with
 *        each diesel's control switch (pull-to-lock) and output breaker, and
 *        the battery chargers and inverters on their DC mimic.
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* a live bus line is black, a dead one grey */
static Color bus_c(int live) { return live ? MIM_ELE : (Color){118, 124, 118, 255}; }

static void bus(Vector2 a, Vector2 b, float w, int live) { DrawLineEx(a, b, w, bus_c(live)); }

static int ess_live(int i) { return P->offsite_power || P->diesel_running[i]; }

/* ---- 4-1 distribution ----------------------------------------------------------------------------- */
static void dist_face(Rectangle f)
{
    rm_tg *t = &P->tg;
    float pitch = f.width / 8;
    double house = P->P_house / 1e6;
    struct { const char *l; double v, lo, hi; int n; double glo, ghi; } d[8] = {
        {"345 KV GRID\nKV", P->grid_ok ? 345.0 : 0.0, 0, 400, 4, 330, 360},
        {"GRID FREQ\nHZ", P->grid_ok ? 60.0 : 0.0, 56, 64, 4, 59.8, 60.2},
        {"NET OUTPUT\nMWE", P->P_net / 1e6, 0, 1200, 4, 0, 0},
        {"HOUSE LOAD\nMW", house, 0, 100, 5, 0, 0},
        {"STARTUP XFMR\nMW", P->offsite_power && !t->aux_on_uat ? house : 0.0, 0, 100, 5, 0, 0},
        {"UNIT AUX XFMR\nMW", P->offsite_power && t->aux_on_uat ? house : 0.0, 0, 100, 5, 0, 0},
        {"6.9 KV BUSES\nKV", P->offsite_power ? 6.9 : 0.0, 0, 8, 4, 6.6, 7.2},
        {"ESSENTIAL\nLOAD MW", rm_plant_essential_load(P) / 1e6, 0, 10, 5, 0, 0},
    };
    static const char *hzl[5] = {"56", "58", "60", "62", "64"};
    for (int i = 0; i < 8; i++)
        dial((Rectangle){f.x + i * pitch + 4, f.y, pitch - 8, pitch - 8}, d[i].l, d[i].v, d[i].lo, d[i].hi, d[i].n, d[i].glo,
             d[i].ghi, 0, 0, i == 1 ? hzl : NULL);

    float y2 = f.y + 150;
    strip((Rectangle){f.x, y2, 500, f.y + f.height - y2}, TR_NET, 0, 1200, "NET MWE", TR_DEMAND, 0, 1200, "DISPATCH MWE");
    float gx = f.x + 516;
    plate_c(gx + 66, y2, "ESS BUS KV", 10);
    plate_c(gx + 226, y2, "ESS BUS MW", 10);
    double ess = rm_plant_essential_load(P) / 1e6;
    int nlive = 0;
    for (int i = 0; i < 3; i++) nlive += ess_live(i);
    for (int i = 0; i < 3; i++) {
        char b[16];
        snprintf(b, sizeof b, "BUS %c", 'A' + i);
        edgewz((Rectangle){gx + i * 46, y2 + 26, 40, 130}, b, ess_live(i) ? 4.16 : 0.0, 0, 5, 3.9, 4.4, 0, 3.5, 5, NULL);
        double mw = !ess_live(i) ? 0.0 : (P->offsite_power ? ess / fmax(nlive, 1) : t->dg_mw[i]);
        edgewz((Rectangle){gx + 160 + i * 46, y2 + 26, 40, 130}, b, mw, 0, 4, 0, 0, 0, 0, 4, NULL);
    }
    float ix = f.x + 834, iw = (f.x + f.width - ix) / 2;
    struct { const char *l; Color c; int on; } st[8] = {
        {"GRID\nAVAILABLE", L_WHT, P->grid_ok},
        {"OFFSITE\nPOWER", L_WHT, P->offsite_power},
        {"SWYD BKR\nCLOSED", L_RED, P->grid_breaker},
        {"GEN BKR\nCLOSED", L_RED, P->generator_breaker},
        {"HOUSE ON\nSTARTUP", L_WHT, P->offsite_power && !t->aux_on_uat},
        {"HOUSE ON\nUNIT AUX", L_WHT, P->offsite_power && t->aux_on_uat},
        {"TRANSFER\nTROUBLE", L_AMB, t->transfer_fail && P->generator_breaker},
        {"ESS BUSES\nON DIESEL", L_AMB, !P->offsite_power && nlive > 0},
    };
    for (int k = 0; k < 8; k++)
        indicator((Rectangle){ix + (k % 2) * iw, y2 + 26 + (k / 2) * 36, iw - 6, 30}, st[k].l, st[k].c, st[k].on);
    float ny = y2 + 186;
    text("ON A GENERATOR TRIP THE HOUSE BUSES FAST-TRANSFER FROM", gx, ny, 10, INK);
    text("THE UNIT AUX TRANSFORMER TO THE STARTUP TRANSFORMER.", gx, ny + 13, 10, INK);
    text("IF THAT FAILS, OR THE GRID IS LOST, THE ESSENTIAL BUSES", gx, ny + 26, 10, INK);
    text("SHED AND THE DIESELS START ON UNDERVOLTAGE (4-2).", gx, ny + 39, 10, INK);
}

static void load_stub(float x, float y, const char *l1, const char *l2, int live)
{
    bus((Vector2){x, y}, (Vector2){x, y + 36}, 3, live);
    sym_breaker((Vector2){x, y + 20}, live);
    tri((Vector2){x - 6, y + 36}, (Vector2){x, y + 44}, (Vector2){x + 6, y + 36}, bus_c(live));
    ctext(l1, x, y + 48, 10, INK);
    if (l2) ctext(l2, x, y + 60, 10, INK);
}

static void dist_bench(Rectangle bb)
{
    rm_tg *t = &P->tg;
    int grid = P->grid_ok, swyd = P->grid_ok && P->grid_breaker;
    int gen_live = t->field_breaker && t->speed > 1500;
    int uat_live = P->generator_breaker ? swyd || gen_live : gen_live;
    int nb = P->offsite_power;
    float yl = bb.y + 71, bx = bb.x;

    /* generator line: grid - switchyard breaker - main transformer - generator breaker - generator */
    text("345 KV", bx + 2, yl - 26, 10, INK);
    text("GRID", bx + 2, yl - 14, 10, INK);
    lamp(bx + 14, yl + 4, 6, L_RED, grid);
    bus((Vector2){bx + 22, yl}, (Vector2){bx + 110, yl}, 5, grid);
    bus((Vector2){bx + 110, yl}, (Vector2){bx + 366, yl}, 5, swyd || (P->generator_breaker && gen_live));
    int s = cswitch(bx + 110, bb.y + 4, "SWYD BKR", "TRIP", "CLOSE", P->grid_breaker, !P->grid_breaker, SW_JHANDLE, NULL);
    if (s > 0 && !P->grid_breaker) {
        P->grid_breaker = 1;
        logmsg("SWITCHYARD BREAKER CLOSED");
    }
    if (s < 0 && P->grid_breaker) {
        P->grid_breaker = 0;
        logmsg("SWITCHYARD BREAKER OPENED");
    }
    int mt = swyd || (P->generator_breaker && gen_live);
    sym_xfmr((Vector2){bx + 380, yl}, bus_c(mt), 0);
    ctext("MAIN XFMR", bx + 380, yl + 18, 10, INK);
    ctext("345/22 KV", bx + 380, yl + 30, 10, INK);
    bus((Vector2){bx + 394, yl}, (Vector2){bx + 452, yl}, 5, mt);
    sym_breaker((Vector2){bx + 462, yl}, P->generator_breaker);
    ctext("GEN BKR", bx + 462, yl - 30, 10, INK);
    ctext("(ON 3-3)", bx + 462, yl - 18, 10, INK);
    bus((Vector2){bx + 472, yl}, (Vector2){bx + 546, yl}, 5, gen_live || P->generator_breaker);
    DrawCircleV((Vector2){bx + 562, yl}, 16, bus_c(gen_live));
    DrawCircleV((Vector2){bx + 562, yl}, 12, DESK);
    ctext("G", bx + 562, yl - 5, 10, INK);
    text("22 KV", bx + 504, yl - 14, 10, INK);

    /* startup and unit auxiliary transformers down to the house buses */
    float xs = bx + 250, xu = bx + 510, yb = bb.y + 262;
    bus((Vector2){xs, yl}, (Vector2){xs, bb.y + 158}, 4, swyd);
    sym_xfmr((Vector2){xs, bb.y + 170}, bus_c(swyd), 1);
    text("STARTUP", xs + 16, bb.y + 158, 10, INK);
    text("XFMR", xs + 16, bb.y + 170, 10, INK);
    bus((Vector2){xs, bb.y + 182}, (Vector2){xs, yb}, 4, swyd && !t->aux_on_uat);
    sym_breaker((Vector2){xs, bb.y + 224}, swyd && !t->aux_on_uat);
    bus((Vector2){xu, yl}, (Vector2){xu, bb.y + 158}, 4, uat_live);
    sym_xfmr((Vector2){xu, bb.y + 170}, bus_c(uat_live), 1);
    text("UNIT AUX", xu + 16, bb.y + 158, 10, INK);
    text("XFMR", xu + 16, bb.y + 170, 10, INK);
    bus((Vector2){xu, bb.y + 182}, (Vector2){xu, yb}, 4, uat_live && t->aux_on_uat);
    sym_breaker((Vector2){xu, bb.y + 224}, t->aux_on_uat);
    s = cswitch(bx + 380, bb.y + 118, "BUS TRANSFER", "SAT", "UAT", t->aux_on_uat, !t->aux_on_uat, SW_JHANDLE, NULL);
    if (s > 0 && !t->aux_on_uat) {
        if (!(P->generator_breaker && t->speed > 1700)) logmsg("TRANSFER TO UAT BLOCKED: GENERATOR NOT ON LINE");
        else {
            t->aux_on_uat = 1;
            logmsg("HOUSE BUSES TRANSFERRED TO UNIT AUX TRANSFORMER");
        }
    }
    if (s < 0 && t->aux_on_uat) {
        if (!swyd) logmsg("TRANSFER TO SAT BLOCKED: NO OFFSITE POWER");
        else {
            t->aux_on_uat = 0;
            logmsg("HOUSE BUSES TRANSFERRED TO STARTUP TRANSFORMER");
        }
    }

    /* 6.9 kV house buses, the big motor loads, and the feeders to the essential buses */
    bus((Vector2){bx + 20, yb}, (Vector2){bx + 1000, yb}, 7, nb);
    text("6.9 KV HOUSE BUSES", bx + 20, yb - 16, 10, INK);
    lamp(bx + 12, yb, 5, L_RED, nb);
    load_stub(bx + 60, yb, "PRI", "PUMPS", nb);
    load_stub(bx + 120, yb, "SEC", "PUMPS", nb);
    load_stub(bx + 180, yb, "COND", "PUMPS", nb);
    load_stub(bx + 320, yb, "CIRC", "WATER", nb);
    load_stub(bx + 380, yb, "MDFP", NULL, nb);
    load_stub(bx + 440, yb, "AUX", "LOADS", nb);
    for (int i = 0; i < 3; i++) {
        float x = bx + 640 + i * 124;
        int live = ess_live(i);
        bus((Vector2){x, yb}, (Vector2){x, yb + 30}, 4, nb);
        sym_breaker((Vector2){x, yb + 30}, nb);
        bus((Vector2){x, yb + 40}, (Vector2){x, yb + 64}, 4, nb);
        bus((Vector2){x - 50, yb + 64}, (Vector2){x + 50, yb + 64}, 6, live);
        char b[24];
        snprintf(b, sizeof b, "ESS BUS %c", 'A' + i);
        ctext(b, x, yb + 72, 10, INK);
        ctext("4.16 KV (4-2)", x, yb + 84, 10, INK);
    }

    /* readouts and operator aids */
    float rx = bx + 640;
    tag(rx, bb.y + 4, "GEN MW");
    readout(rx, bb.y + 19, 14, 4, "%4.0f", P->P_gen / 1e6);
    tag(rx + 90, bb.y + 4, "HOUSE MW");
    readout(rx + 90, bb.y + 19, 14, 3, "%3.0f", P->P_house / 1e6);
    tag(rx + 180, bb.y + 4, "NET MWE");
    readout(rx + 180, bb.y + 19, 14, 4, "%4.0f", fmax(P->P_net / 1e6, 0.0));
    tag(rx + 270, bb.y + 4, "ESS LOAD MW");
    readout(rx + 270, bb.y + 19, 14, 4, "%4.1f", rm_plant_essential_load(P) / 1e6);
    dymo(rx, bb.y + 64, "TRANSFER TO UAT ONLY WITH THE UNIT ON LINE");
    dymo(rx, bb.y + 82, "SWYD BKR OPEN = UNIT OFF THE GRID");
    dymo(rx, bb.y + 100, "CLOSE THE GEN BKR FROM THE SYNC PANEL 3-3");
    indicator((Rectangle){rx, bb.y + 130, 100, 30}, "OFFSITE\nPOWER", L_WHT, nb);
    indicator((Rectangle){rx + 106, bb.y + 130, 100, 30}, "TRANSFER\nTROUBLE", L_AMB, t->transfer_fail && P->generator_breaker);
    indicator((Rectangle){rx + 212, bb.y + 130, 100, 30}, "GRID\nLOST", L_RED, !P->grid_ok);
}

/* ---- 4-2 emergency power and DC ---------------------------------------------------------------------- */
static double dg_hz(int i)
{
    if (P->diesel_running[i]) return 60.0;
    return P->diesel_t[i] > 0 ? 6.0 * P->diesel_t[i] : 0.0;
}

static double batt_amps(int d)
{
    rm_tg *t = &P->tg;
    if (t->charger[d] && rm_plant_essential_power(P)) return t->batt[d] < 0.999 ? 60.0 : 4.0;
    return t->inverter[d] ? -120.0 : -60.0;
}

static void epwr_face(Rectangle f)
{
    rm_tg *t = &P->tg;
    char b[40];
    float pitch = f.width / 3;
    static const char *hzl[5] = {"55", "57.5", "60", "62.5", "65"};
    for (int i = 0; i < 3; i++) {
        float x = f.x + i * pitch;
        snprintf(b, sizeof b, "DG %d\nMW", i + 1);
        dial((Rectangle){x + 8, f.y, 124, 124}, b, t->dg_mw[i], 0, 6, 6, 0, 4.5, 5.5, 6, NULL);
        snprintf(b, sizeof b, "DG %d\nFREQ HZ", i + 1);
        dial((Rectangle){x + 140, f.y, 124, 124}, b, dg_hz(i), 55, 65, 4, 59.5, 60.5, 0, 0, hzl);
        float iy = f.y + 132, iw = (pitch - 8) / 4;
        indicator((Rectangle){x + 4, iy, iw - 4, 30}, "RUNNING", L_RED, P->diesel_running[i]);
        indicator((Rectangle){x + 4 + iw, iy, iw - 4, 30}, "CRANKING", L_WHT, P->diesel_t[i] > 0 && !P->diesel_running[i]);
        indicator((Rectangle){x + 4 + 2 * iw, iy, iw - 4, 30}, "OUT OF\nSERVICE", L_AMB, !P->diesel_avail[i]);
        indicator((Rectangle){x + 4 + 3 * iw, iy, iw - 4, 30}, "PULL TO\nLOCK", L_AMB, P->diesel_ptl[i]);
    }

    float y2 = f.y + 206;
    static const char *gn[4] = {"BATTERY VDC", "BATTERY AMPS", "CHARGE %", "VITAL AC V"};
    for (int g = 0; g < 4; g++) plate_c(f.x + g * 106 + 45, y2 - 22, gn[g], 10);
    for (int d = 0; d < 2; d++) {
        float x = f.x + d * 46;
        snprintf(b, sizeof b, "DIV %c", 'A' + d);
        edgewz((Rectangle){x, y2, 40, 120}, b, 105.0 + 25.0 * t->batt[d], 100, 140, 125, 132, 100, 110, 4, NULL);
        static const char *al[5] = {"-200", "-100", "0", "100", "200"};
        edgewz((Rectangle){x + 106, y2, 40, 120}, b, batt_amps(d), -200, 200, 0, 80, -200, -100, 4, al);
        edgewz((Rectangle){x + 212, y2, 40, 120}, b, 100 * t->batt[d], 0, 100, 60, 100, 0, 30, 4, NULL);
        int vital = t->inverter[d] && t->batt[d] > 0.02;
        edgewz((Rectangle){x + 318, y2, 40, 120}, b, vital ? 120.0 : 0.0, 0, 150, 110, 130, 0, 100, 3, NULL);
    }
    float y3 = y2 + 148;
    for (int d = 0; d < 2; d++) {
        char tg[24];
        snprintf(tg, sizeof tg, "DIV %c HOURS LEFT", 'A' + d);
        tag(f.x + d * 150, y3, tg);
        double amps = batt_amps(d);
        if (amps < 0) readout(f.x + d * 150, y3 + 15, 14, 4, "%4.1f", t->batt[d] * 4.0 * (t->inverter[d] ? 1.0 : 2.0));
        else readout(f.x + d * 150, y3 + 15, 14, 4, "----");
    }
    text("BATTERIES LAST ABOUT 4 H WITHOUT THE CHARGERS. THE DIESELS NEED", f.x, y3 + 50, 10, INK);
    text("DC TO CRANK; THE NUCLEAR INSTRUMENTS NEED VITAL AC.", f.x, y3 + 63, 10, INK);
    float sx = f.x + 432;
    strip((Rectangle){sx, y2 - 24, f.x + f.width - sx, f.y + f.height - y2 + 24}, TR_DGMW, 0, 12, "DIESEL MW", TR_BATT, 0,
          100, "BATT A %");
}

static void epwr_bench(Rectangle bb)
{
    rm_tg *t = &P->tg;
    int nb = P->offsite_power;
    float pitch = 184;
    for (int i = 0; i < 3; i++) {
        float x = bb.x + i * pitch;
        int live = ess_live(i);
        char b[32];
        /* essential bus, fed from the house buses (4-1) or its own diesel */
        snprintf(b, sizeof b, "ESS BUS %c  4.16 KV", 'A' + i);
        text(b, x + 8, bb.y + 2, 10, INK);
        bus((Vector2){x + 8, bb.y + 22}, (Vector2){x + 176, bb.y + 22}, 6, live);
        tri((Vector2){x + 8, bb.y + 14}, (Vector2){x + 8, bb.y + 30}, (Vector2){x, bb.y + 22}, bus_c(nb));
        int brk = P->diesel_running[i] && !nb;
        bus((Vector2){x + 146, bb.y + 22}, (Vector2){x + 146, bb.y + 110}, 4, P->diesel_running[i]);
        sym_breaker((Vector2){x + 146, bb.y + 58}, brk);
        DrawCircleV((Vector2){x + 146, bb.y + 126}, 17, bus_c(P->diesel_running[i]));
        DrawCircleV((Vector2){x + 146, bb.y + 126}, 13, DESK);
        snprintf(b, sizeof b, "DG%d", i + 1);
        ctext(b, x + 146, bb.y + 121, 10, INK);

        snprintf(b, sizeof b, "DG %d", i + 1);
        int dg = cswitch3(x + 60, bb.y + 34, b, "STOP", "START", P->diesel_running[i], !P->diesel_running[i], SW_PISTOL,
                          &P->diesel_ptl[i], L_WHT, P->diesel_t[i] > 0 && !P->diesel_running[i]);
        if (dg > 0 && !P->diesel_manual[i]) {
            P->diesel_manual[i] = 1;
            logmsg("DIESEL %d START", i + 1);
        }
        if (dg < 0 && P->diesel_manual[i]) {
            P->diesel_manual[i] = 0;
            logmsg(nb ? "DIESEL %d STOPPED" : "DIESEL %d STAYS ON: BUS UNDERVOLTAGE", i + 1);
        }
        tag(x + 10, bb.y + 150, "MW");
        readout(x + 10, bb.y + 165, 12, 3, "%3.1f", t->dg_mw[i]);
        tag(x + 70, bb.y + 150, "START S");
        readout(x + 70, bb.y + 165, 12, 3, "%3.0f", fmin(P->diesel_t[i], 999.0));
    }
    dymo(bb.x + 10, bb.y + 206, "DIESELS START ON BUS UNDERVOLTAGE, TAKE LOAD AT 10 S");
    dymo(bb.x + 10, bb.y + 224, "RIGHT-CLICK A DIESEL SWITCH: PULL TO LOCK (NO AUTO START)");
    text("ESSENTIAL LOADS: CHARGERS, INSTRUMENTS, VENTILATION, PONY", bb.x + 10, bb.y + 252, 10, INK);
    text("MOTORS, CCW AND SERVICE WATER PUMPS A, TRACE HEATING.", bb.x + 10, bb.y + 265, 10, INK);
    tag(bb.x + 10, bb.y + 290, "ESSENTIAL LOAD MW");
    readout(bb.x + 10, bb.y + 305, 16, 4, "%4.1f", rm_plant_essential_load(P) / 1e6);
    indicator((Rectangle){bb.x + 150, bb.y + 296, 120, 34}, "ESSENTIAL\nPOWER", L_WHT, rm_plant_essential_power(P));
    indicator((Rectangle){bb.x + 280, bb.y + 296, 120, 34}, "BUS\nUNDERVOLTAGE", L_RED, !rm_plant_essential_power(P));

    /* DC and vital AC: charger from the essential bus to the 125 V DC bus and its
     * battery, inverter from the DC bus to the 120 V vital AC bus */
    float dx = bb.x + 3 * pitch + 12;
    for (int d = 0; d < 2; d++) {
        float cx = dx + 64 + d * 136;
        char b[32];
        int ess = ess_live(d == 0 ? 0 : 1);
        int dc = t->batt[d] > 0.02 || (t->charger[d] && ess);
        int vital = t->inverter[d] && t->batt[d] > 0.02;
        bus((Vector2){cx, bb.y + 4}, (Vector2){cx, bb.y + 116}, 4, dc);
        snprintf(b, sizeof b, "CHARGER %c", 'A' + d);
        int c = cswitch(cx, bb.y + 4, b, "OFF", "ON", t->charger[d], !t->charger[d], SW_PISTOL, NULL);
        if (c) {
            t->charger[d] = c > 0;
            logmsg("BATTERY CHARGER %c %s", 'A' + d, c > 0 ? "ON" : "OFF");
        }
        bus((Vector2){cx - 58, bb.y + 116}, (Vector2){cx + 58, bb.y + 116}, 6, dc);
        snprintf(b, sizeof b, "125 VDC %c", 'A' + d);
        ctext(b, cx, bb.y + 122, 10, INK);
        /* the battery hangs off the DC bus */
        float bx2 = cx - 48;
        bus((Vector2){bx2, bb.y + 116}, (Vector2){bx2, bb.y + 140}, 3, dc);
        for (int k = 0; k < 3; k++) {
            DrawLineEx((Vector2){bx2 - 10, bb.y + 140 + k * 8}, (Vector2){bx2 + 10, bb.y + 140 + k * 8}, 2, INK);
            DrawLineEx((Vector2){bx2 - 5, bb.y + 144 + k * 8}, (Vector2){bx2 + 5, bb.y + 144 + k * 8}, 3, INK);
        }
        ctext("BATT", bx2, bb.y + 166, 10, INK);
        bus((Vector2){cx, bb.y + 116}, (Vector2){cx, bb.y + 300}, 4, vital);
        snprintf(b, sizeof b, "INVERTER %c", 'A' + d);
        c = cswitch(cx, bb.y + 138, b, "OFF", "ON", t->inverter[d], !t->inverter[d], SW_PISTOL, NULL);
        if (c) {
            t->inverter[d] = c > 0;
            logmsg("VITAL AC INVERTER %c %s%s", 'A' + d, c > 0 ? "ON" : "OFF", c > 0 ? "" : " - NUCLEAR INSTRUMENTS DIV LOST");
        }
        bus((Vector2){cx - 58, bb.y + 300}, (Vector2){cx + 58, bb.y + 300}, 6, vital);
        snprintf(b, sizeof b, "120 VAC VITAL %c", 'A' + d);
        ctext(b, cx, bb.y + 306, 10, INK);
        int chg = t->charger[d] && rm_plant_essential_power(P);
        indicator((Rectangle){cx - 58, bb.y + 324, 56, 30}, "VITAL\nAC", L_RED, vital);
        indicator((Rectangle){cx + 2, bb.y + 324, 56, 30}, "BATT\nDISCH", L_AMB, !chg);
    }
}

void draw_elec(void)
{
    static const secdef S[2] = {{"4-1", "ELECTRICAL DISTRIBUTION", 1040, SEC_DIST, 10},
                                {"4-2", "EMERGENCY POWER AND DC", 872, SEC_EPWR, 8}};
    secrect R[2];
    board_frame(S, 2, R);
    dist_face(R[0].face);
    dist_bench(R[0].bench);
    epwr_face(R[1].face);
    epwr_bench(R[1].bench);
    board_strip();
}
