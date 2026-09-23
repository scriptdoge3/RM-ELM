/*
 * Auxiliary board: the sodium systems and the plant's support systems.
 *
 *   REACTOR SODIUM, COVER GAS, FAILED FUEL | SECONDARY SODIUM & SG PROTECTION | TRACE HEATING
 *   CONTAINMENT & PRIMARY CELL | RADIATION | COOLING WATER, AIR, HVAC | FIRE PROTECTION
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void draw_log(Rectangle r);

static void onoff_sw(float cx, float y, const char *name, const char *ll, const char *rl, int *v, const char *what)
{
    int s = cswitch(cx, y, name, ll, rl, *v, !*v, SW_PISTOL, NULL);
    if (s > 0 && !*v) {
        *v = 1;
        logmsg("%s %s", what, rl);
    } else if (s < 0 && *v) {
        *v = 0;
        logmsg("%s %s", what, ll);
    }
}

static const char *log_lab[5] = {".01", "1", "100", "1E4", ""};

/* ---- reactor sodium, cover gas and failed fuel -------------------------------------------- */
static void draw_reactor_na(Rectangle r)
{
    rm_aux *a = &P->aux;
    steel(r, "REACTOR SODIUM - COVER GAS - FAILED FUEL");
    float y = r.y + 30;
    edgew((Rectangle){r.x + 12, y, 48, 160}, "NA LVL MM", a->na_level, -400, 400, -400, -300, 4, NULL);
    edgew((Rectangle){r.x + 72, y, 48, 160}, "GAS KPA", a->gas_p / 1e3, 100, 150, 140, 150, 5, NULL);
    edgew((Rectangle){r.x + 132, y, 48, 160}, "GAS MBQ", log10(fmax(a->gas_act, 0.01)), -2, 4, log10(50.0), 4, 3, log_lab);
    static const char *dl[5] = {"1", "10", "100", "1E3", "1E4"};
    edgew((Rectangle){r.x + 192, y, 48, 160}, "DND CPS", log10(fmax(a->dnd, 1.0)), 0, 4, log10(2000.0), 4, 4, dl);

    float x = r.x + 262;
    dymo(x, y, "ARGON COVER GAS");
    if (lampbutton((Rectangle){x, y + 16, 60, 28}, "AUTO", L_WHT, a->gas_auto) && !a->gas_auto) {
        a->gas_auto = 1;
        logmsg("COVER GAS PRESSURE CONTROL AUTO");
    }
    if (lampbutton((Rectangle){x + 64, y + 16, 60, 28}, "MAN", L_AMB, !a->gas_auto) && a->gas_auto) {
        a->gas_auto = 0;
        logmsg("COVER GAS PRESSURE CONTROL MANUAL");
    }
    readout(x + 130, y + 14, 14, 4, "%4.0f", a->gas_p / 1e3);
    text("KPA", x + 176, y + 34, 10, INK);
    onoff_sw(x + 34, y + 54, "SUPPLY", "SHUT", "OPEN", &a->gas_supply, "ARGON SUPPLY VALVE");
    onoff_sw(x + 118, y + 54, "VENT", "SHUT", "OPEN", &a->gas_vent, "COVER GAS VENT VALVE");
    text("VALVES WORK IN MAN ONLY", x, y + 150, 10, INK);

    y = r.y + 214;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + r.width - 8, y - 4}, 1, PAINT_LO);
    onoff_sw(r.x + 54, y, "GAS CLEANUP", "OFF", "ON", &a->cleanup, "COVER GAS CLEANUP (DELAY BEDS)");
    onoff_sw(r.x + 144, y, "PRIM COLD TRAP", "OUT", "IN", &a->prim_cold_trap, "PRIMARY COLD TRAP");
    x = r.x + 210;
    dymo(x, y, "PRIM O2 PPM");
    readout(x, y + 16, 14, 4, "%4.1f", a->prim_oxygen);
    dymo(x + 90, y, "PLUGGING C");
    readout(x + 90, y + 16, 14, 3, "%3.0f", rm_plant_plugging_T(P, -1));
    dymo(x + 170, y, "NA INVENT %");
    readout(x + 170, y + 16, 14, 4, "%5.1f", 100 * a->prim_inventory);
    lamp(x + 8, y + 64, 6, L_RED, a->dnd > 500.0);
    text("DND FAILED FUEL", x + 18, y + 59, 10, INK);
    lamp(x + 150, y + 64, 6, L_RED, a->prim_leak > 0.0005);
    text("GUARD VSL LEAK", x + 160, y + 59, 10, INK);
    lamp(x + 8, y + 84, 6, L_AMB, a->gas_act > 50.0);
    text("GAS ACTIVITY HIGH", x + 18, y + 79, 10, INK);
    lamp(x + 150, y + 84, 6, L_AMB, rm_plant_plugging_T(P, -1) > 160.0);
    text("PLUGGING HIGH", x + 160, y + 79, 10, INK);

    strip((Rectangle){r.x + 10, r.y + 320, r.width - 20, r.height - 330}, TR_DND, 0, 4, "LOG DND", TR_GASACT, -2, 4,
          "LOG GAS ACT");
}

/* ---- secondary sodium and SG protection --------------------------------------------------- */
static void draw_secondary(Rectangle r)
{
    steel(r, "SECONDARY SODIUM - STEAM GENERATOR PROTECTION");
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        rm_sg *s = &l->sg;
        float x = r.x + 10 + i * 222, y = r.y + 30;
        if (i) DrawLineEx((Vector2){x - 6, y}, (Vector2){x - 6, r.y + r.height - 10}, 1, PAINT_LO);
        char lb[24];
        snprintf(lb, sizeof lb, "LOOP %d", i + 1);
        plate(x, y - 2, lb, 10);
        y += 20;
        edgew((Rectangle){x, y, 46, 150}, "H2 PPM", s->h2, 0, 1, RM_H2_ALARM, 1, 5, NULL);
        edgew((Rectangle){x + 52, y, 46, 150}, "EXP TK %", l->exp_level, 0, 100, 85, 100, 4, NULL);
        edgew((Rectangle){x + 104, y, 46, 150}, "INVENT %", 100 * l->sec_inventory, 50, 100, 50, 85, 5, NULL);
        float lx = x + 162;
        struct { const char *t; int on; Color c; } st[6] = {
            {"NA LEAK", l->na_leak > 0.001, L_RED},
            {"DISC", s->disc_burst, L_RED},
            {"N2 PURGE", s->n2_purge, L_WHT},
            {"DUMPED", l->dumped, L_AMB},
            {"REFILL", l->refill_t > 0, L_WHT},
            {"FIRE", P->aux.fire[RM_FIRE_SG1 + i], L_RED},
        };
        for (int k = 0; k < 6; k++) {
            lamp(lx, y + 8 + k * 24, 5, st[k].c, st[k].on && (st[k].c.r != L_RED.r || blink_fast() || k == 1));
            text(st[k].t, lx + 9, y + 3 + k * 24, 10, INK);
        }
        y = r.y + 222;
        dymo(x, y, "EXP P MPA");
        readout(x, y + 14, 12, 4, "%4.2f", l->exp_p / 1e6);
        dymo(x + 76, y, "O2 PPM");
        readout(x + 76, y + 14, 12, 3, "%3.1f", l->oxygen);
        dymo(x + 142, y, "PLUG C");
        readout(x + 142, y + 14, 12, 3, "%3.0f", rm_plant_plugging_T(P, i));

        y = r.y + 268;
        int iso = cswitch(x + 36, y, "SG ISOL", "NORM", "ISOL", s->isolated, !s->isolated, SW_JHANDLE, NULL);
        if (iso > 0 && !s->isolated) rm_plant_isolate_sg(P, i);
        if (iso < 0 && s->isolated) logmsg("SG %d STAYS BLOWN DOWN UNTIL THE LOOP IS REFILLED", i + 1);
        int dmp = cswitch(x + 111, y, "DUMP VLV", "SHUT", "DUMP", l->dumped, !l->dumped, SW_JHANDLE, NULL);
        if (dmp > 0 && !l->dumped) rm_plant_dump_loop(P, i);
        if (dmp < 0 && l->dumped) logmsg("LOOP %d: DUMP TANK - USE REFILL", i + 1);
        int ct = cswitch(x + 184, y, "COLD TRAP", "OUT", "IN", l->cold_trap, !l->cold_trap, SW_PISTOL, NULL);
        if (ct) {
            l->cold_trap = ct > 0;
            logmsg("LOOP %d COLD TRAP %s", i + 1, ct > 0 ? "IN SERVICE" : "OUT OF SERVICE");
        }
        y = r.y + 372;
        if (lampbutton((Rectangle){x, y, 96, 30}, "REFILL\nLOOP", L_WHT, l->refill_t > 0)) rm_plant_refill_loop(P, i);
        readout(x + 104, y + 2, 12, 4, "%4.0f", l->refill_t);
        text("S", x + 156, y + 8, 10, INK);
        dymo(x, y + 44, "SEC FLOW KG/S");
        readout(x, y + 60, 12, 5, "%5.0f", l->Ws);
        dymo(x + 104, y + 44, "SG STEAM C");
        readout(x + 104, y + 60, 12, 3, "%3.0f", s->T_steam - 273.15);
    }
    text("A LEAKING SG: ISOLATE IT (BLOWS DOWN THE WATER SIDE AND N2-PURGES IT), STOP ITS PUMP, RUN BACK. A BURST DISC "
         "DUMPS THE LOOP. REFILL TAKES 20 MIN.",
         r.x + 12, r.y + r.height - 22, 10, INK);
}

/* ---- trace heating ------------------------------------------------------------------------------ */
static void draw_trace(Rectangle r)
{
    rm_aux *a = &P->aux;
    steel(r, "TRACE HEATING");
    static const char *nm[6] = {"PRIMARY PIPING", "SECONDARY LOOP 1", "SECONDARY LOOP 2", "SECONDARY LOOP 3",
                                "SECONDARY LOOP 4", "DUMP TANKS"};
    static const char *pos[3] = {"OFF", "AUTO", "ON"};
    text("CIRCUIT                      MODE            KW       PIPE C", r.x + 12, r.y + 30, 10, INK);
    for (int i = 0; i < 6; i++) {
        float y = r.y + 48 + i * 72;
        dymo(r.x + 12, y + 18, nm[i]);
        int cur = a->heat_on[i] ? 2 : (a->heat_auto[i] ? 1 : 0);
        int pk = rotary((Vector2){r.x + 210, y + 40}, 13, 3, pos, cur, -150, -30, 28);
        if (pk >= 0 && pk != cur) {
            a->heat_auto[i] = pk == 1;
            a->heat_on[i] = pk == 2;
            logmsg("TRACE HEATING %s %s", nm[i], pos[pk]);
        }
        readout(r.x + 270, y + 14, 14, 3, "%3.0f", a->heat_kw[i]);
        double T;
        if (i == 0) T = fmin(rm_na_T(P->loop[0].cold.h[RM_PIPE_N - 1]), rm_na_T(P->loop[0].hot.h[RM_PIPE_N - 1]));
        else if (i <= 4) T = fmin(rm_na_T(P->loop[i - 1].scold.h[RM_PIPE_N - 1]), rm_na_T(P->loop[i - 1].shot.h[RM_PIPE_N - 1]));
        else T = 473.15;
        readout(r.x + 340, y + 14, 14, 3, "%3.0f", T - 273.15);
        lamp(r.x + 420, y + 20, 5, L_RED, a->heat_kw[i] > 0);
        text("ON", r.x + 430, y + 15, 10, INK);
        lamp(r.x + 470, y + 20, 5, L_AMB, T < 423.15);
        text("LOW", r.x + 480, y + 15, 10, INK);
    }
    text("SODIUM FREEZES AT 98 C. AUTO HEATS BELOW 200 C.", r.x + 12, r.y + r.height - 34, 10, INK);
    text("HEATERS NEED THE ESSENTIAL BUSES.", r.x + 12, r.y + r.height - 22, 10, INK);
}

/* ---- containment ------------------------------------------------------------------------------------ */
static void draw_containment(Rectangle r)
{
    rm_aux *a = &P->aux;
    steel(r, "CONTAINMENT - PRIMARY CELL ATMOSPHERE");
    float y = r.y + 30;
    edgew((Rectangle){r.x + 12, y, 48, 170}, "BLDG KPA", a->cont_p, 95, 110, 103, 110, 3, NULL);
    edgew((Rectangle){r.x + 72, y, 48, 170}, "BLDG C", a->cont_T, 0, 80, 60, 80, 4, NULL);
    edgew((Rectangle){r.x + 132, y, 48, 170}, "CELL O2 %", a->cell_o2, 0, 20, 4, 20, 4, NULL);
    edgew((Rectangle){r.x + 192, y, 48, 170}, "CELL C", a->cell_T, 0, 200, 150, 200, 4, NULL);

    float x = r.x + 262;
    onoff_sw(x + 40, y, "CELL N2 SUPPLY", "SHUT", "OPEN", &a->n2_supply, "PRIMARY CELL NITROGEN");
    text("THE PRIMARY CELL IS INERTED", x, y + 100, 10, INK);
    text("WITH NITROGEN: A PRIMARY LEAK", x, y + 112, 10, INK);
    text("CANNOT BURN BELOW 5% O2.", x, y + 124, 10, INK);

    y = r.y + 230;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + r.width - 8, y - 4}, 1, PAINT_LO);
    struct { const char *t; int on; Color c; } st[6] = {
        {"CONTAINMENT ISOLATED", P->containment_isolated, L_AMB},
        {"PRIMARY NA LEAK", a->prim_leak > 0.0005, L_RED},
        {"CELL FIRE", a->fire[RM_FIRE_CELL], L_RED},
        {"CELL O2 HIGH", a->cell_o2 > 4.0, L_AMB},
        {"BLDG PRESS HIGH", a->cont_p > 103.0, L_AMB},
        {"PURIFICATION SHUT", P->containment_isolated, L_WHT},
    };
    for (int k = 0; k < 6; k++) {
        float lx = r.x + 20 + (k % 2) * 220, ly = y + 12 + (k / 2) * 22;
        lamp(lx, ly, 6, st[k].c, st[k].on);
        text(st[k].t, lx + 12, ly - 5, 10, INK);
    }
    y = r.y + 312;
    if (lampbutton((Rectangle){r.x + 12, y, 110, 34}, "CONTAINMENT\nISOLATE", L_AMB, P->containment_isolated) &&
        !P->containment_isolated) {
        P->containment_isolated = 1;
        logmsg("CONTAINMENT ISOLATION - PURIFICATION LINES SHUT");
    }
    if (lampbutton((Rectangle){r.x + 126, y, 110, 34}, "ISOLATION\nRESET", L_WHT, 0) && P->containment_isolated) {
        P->containment_isolated = 0;
        logmsg("CONTAINMENT ISOLATION RESET");
    }
    dymo(r.x + 256, y, "PRIM NA INVENT %");
    readout(r.x + 256, y + 16, 14, 4, "%5.1f", 100 * a->prim_inventory);
    dymo(r.x + 12, y + 50, "REACTOR NA LEVEL MM");
    readout(r.x + 12, y + 66, 14, 4, "%4.0f", a->na_level);
    dymo(r.x + 160, y + 50, "COVER GAS KPA");
    readout(r.x + 160, y + 66, 14, 4, "%4.0f", a->gas_p / 1e3);
    text("LOW REACTOR SODIUM LEVEL (-300 MM) TRIPS THE REACTOR.", r.x + 12, r.y + r.height - 22, 10, INK);
}

/* ---- radiation monitoring ------------------------------------------------------------------------------ */
static void draw_radiation(Rectangle r)
{
    rm_aux *a = &P->aux;
    steel(r, "RADIATION MONITORING");
    static const char *nm[8] = {"CR", "HALL", "TURB", "SG BLD", "GAS", "STACK", "SEC NA", "STEAM"};
    static const double hi[8] = {1, 25, 5, 5, 50, 10, 5, 5};
    for (int i = 0; i < 8; i++) {
        float x = r.x + 14 + i * 52;
        edgew((Rectangle){x, r.y + 30, 44, 200}, nm[i], log10(fmax(a->rad[i], 0.01)), -2, 4, log10(hi[i]), 4, 3, log_lab);
        readout(x - 2, r.y + 250, 10, 4, a->rad[i] < 10 ? "%4.2f" : "%4.0f", fmin(a->rad[i], 9999.0));
    }
    text("AREA MONITORS USV/H - GAS AND STACK MBQ/M3", r.x + 14, r.y + 280, 10, INK);
    float y = r.y + 306;
    int s = cswitch(r.x + 60, y, "CR HVAC", "NORM", "EMERG", a->hvac_emerg, !a->hvac_emerg, SW_PISTOL, NULL);
    if (s) {
        a->hvac_emerg = s > 0;
        logmsg("CONTROL ROOM HVAC %s", s > 0 ? "EMERGENCY FILTRATION" : "NORMAL");
    }
    text("STACK > 50 OR HALL > 100:", r.x + 120, y + 20, 10, INK);
    text("AUTOMATIC CONTAINMENT ISOLATION", r.x + 120, y + 32, 10, INK);
    text("AND CONTROL ROOM EMERGENCY", r.x + 120, y + 44, 10, INK);
    text("FILTRATION.", r.x + 120, y + 56, 10, INK);
    dymo(r.x + 120, y + 74, "CONTROL ROOM C");
    readout(r.x + 120, y + 90, 14, 3, "%3.0f", a->cr_T);
}

/* ---- cooling water, instrument air ------------------------------------------------------------------------ */
static void draw_cooling(Rectangle r)
{
    rm_aux *a = &P->aux;
    steel(r, "COOLING WATER - INSTRUMENT AIR");
    float y = r.y + 34;
    onoff_sw(r.x + 60, y, "CCW PUMP A", "STOP", "START", &a->ccw[0], "COMPONENT COOLING PUMP A");
    onoff_sw(r.x + 150, y, "CCW PUMP B", "STOP", "START", &a->ccw[1], "COMPONENT COOLING PUMP B");
    onoff_sw(r.x + 260, y, "SW PUMP A", "STOP", "START", &a->sw[0], "SERVICE WATER PUMP A");
    onoff_sw(r.x + 350, y, "SW PUMP B", "STOP", "START", &a->sw[1], "SERVICE WATER PUMP B");
    text("PUMPS A ON THE ESSENTIAL BUS, B ON THE NORMAL BUS", r.x + 14, y + CSW_H + 4, 10, INK);

    y = r.y + 150;
    edgew((Rectangle){r.x + 12, y, 48, 120}, "CCW C", a->ccw_T, 0, 80, 40, 80, 4, NULL);
    text("PRIMARY PUMP BEARINGS C (TRIP 90)", r.x + 80, y, 10, INK);
    for (int i = 0; i < RM_NLOOPS; i++) {
        textf(r.x + 80 + i * 86, y + 16, 10, INK, "PP-%d", i + 1);
        readout(r.x + 80 + i * 86, y + 30, 14, 3, "%3.0f", P->loop[i].brg_T);
    }
    text("CCW COOLS THE PRIMARY PUMP BEARINGS; SERVICE WATER", r.x + 80, y + 70, 10, INK);
    text("COOLS THE CCW. LOSE BOTH AND THE PUMPS TRIP.", r.x + 80, y + 82, 10, INK);

    y = r.y + 294;
    DrawLineEx((Vector2){r.x + 8, y - 6}, (Vector2){r.x + r.width - 8, y - 6}, 1, PAINT_LO);
    onoff_sw(r.x + 60, y, "AIR COMP A", "STOP", "START", &a->air_comp[0], "INSTRUMENT AIR COMPRESSOR A");
    onoff_sw(r.x + 150, y, "AIR COMP B", "STOP", "START", &a->air_comp[1], "INSTRUMENT AIR COMPRESSOR B");
    meter((Rectangle){r.x + 214, y, 226, 110}, "INSTRUMENT AIR BAR", a->air_p, 0, 10, 0, 4, 5);
    text("BELOW 4 BAR: FEED AND BYPASS VALVES FAIL AS IS", r.x + 14, r.y + r.height - 46, 10, INK);
    text("BELOW 3 BAR: MSIVS DRIFT SHUT, DRACS DAMPERS FAIL OPEN", r.x + 14, r.y + r.height - 34, 10, INK);
}

/* ---- fire protection ------------------------------------------------------------------------------------ */
static void draw_fire(Rectangle r)
{
    rm_aux *a = &P->aux;
    steel(r, NULL);
    plate(r.x + 12, r.y + 8, "FIRE PROTECTION", 10);
    static const char *zone[8] = {"SG 1 CELL", "SG 2 CELL", "SG 3 CELL", "SG 4 CELL", "PRIMARY CELL", "TURBINE HALL",
                                  "CABLE SPREAD RM", "CONTROL ROOM"};
    for (int f = 0; f < 8; f++) {
        float lx = r.x + 22 + (f % 2) * 150, ly = r.y + 40 + (f / 2) * 22;
        lamp(lx, ly, 6, L_RED, a->fire[f] && blink_fast());
        text(zone[f], lx + 12, ly - 5, 10, INK);
    }
    onoff_sw(r.x + 360, r.y + 26, "FIRE PUMP A", "STOP", "START", &a->fire_pump[0], "ELECTRIC FIRE PUMP A");
    onoff_sw(r.x + 460, r.y + 26, "FIRE PUMP B", "STOP", "START", &a->fire_pump[1], "DIESEL FIRE PUMP B");
    text("SODIUM FIRES: NO WATER - DUMP AND LET BURN OUT", r.x + 12, r.y + 132, 10, INK);

    ann_box((Rectangle){r.x + 6, r.y + 148, r.width - 12, 212}, BOARD_AUX, 4, 32);
    ann_controls(r.x + 10, r.y + 366);
    draw_log((Rectangle){r.x + 4, r.y + 402, r.width - 8, r.height - 406});
}

void draw_auxb(void)
{
    draw_reactor_na((Rectangle){6, 48, 470, 520});
    draw_secondary((Rectangle){482, 48, 900, 520});
    draw_trace((Rectangle){1388, 48, 526, 520});
    draw_containment((Rectangle){6, 574, 470, 502});
    draw_radiation((Rectangle){482, 574, 440, 502});
    draw_cooling((Rectangle){928, 574, 454, 502});
    draw_fire((Rectangle){1388, 574, 526, 502});
}
