/*
 * Auxiliary board (panels 5-1 to 5-3): the sodium systems' auxiliaries and
 * the plant's support systems.
 *
 *   5-1  SODIUM AUXILIARIES
 *        reactor sodium level, argon cover gas, failed fuel detection,
 *        purification (oxygen and plugging temperatures), trace heating
 *        temperatures, the failed fuel recorder. On the bench, the cover gas
 *        mimic with its supply, vent and cleanup switches and pressure
 *        control, the cold trap switches, and the trace heating selectors.
 *   5-2  CONTAINMENT AND RADIATION
 *        reactor building and primary cell meters, the area and process
 *        radiation monitors and their recorder. On the bench, the cell
 *        nitrogen and control room ventilation switches and the containment
 *        status lights.
 *   5-3  PLANT SERVICES AND FIRE PROTECTION
 *        component cooling and bearing temperatures, instrument air, the fire
 *        detection panel. On the bench, the cooling water, air compressor
 *        and fire pump switches on their mimics.
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

static const char *const LOG_LAB[4] = {".01", "1", "100", "1E4"};
static double C(double T_K) { return T_K - 273.15; }

/* trace heating circuit temperature: the coldest pipe on the circuit */
static double circuit_T(int i)
{
    if (i == 0) return fmin(rm_na_T(P->loop[0].cold.h[RM_PIPE_N - 1]), rm_na_T(P->loop[0].hot.h[RM_PIPE_N - 1]));
    if (i <= 4)
        return fmin(rm_na_T(P->loop[i - 1].scold.h[RM_PIPE_N - 1]), rm_na_T(P->loop[i - 1].shot.h[RM_PIPE_N - 1]));
    return 473.15;
}

/* ---- 5-1 sodium auxiliaries ------------------------------------------------------------------------ */
static void naaux_face(Rectangle f)
{
    rm_aux *a = &P->aux;
    char b[24];
    float y = f.y + 24;
    plate_c(f.x + 100, f.y, "REACTOR NA AND COVER GAS", 10);
    edgewz((Rectangle){f.x, y, 44, 120}, "NA LVL\nMM", a->na_level, -400, 400, -100, 100, -400, -300, 4, NULL);
    edgewz((Rectangle){f.x + 52, y, 44, 120}, "GAS\nKPA", a->gas_p / 1e3, 100, 150, 112, 128, 140, 150, 5, NULL);
    edgewz((Rectangle){f.x + 104, y, 44, 120}, "GAS ACT\nMBQ/M3", log10(fmax(a->gas_act, 0.01)), -2, 4, -2, 0,
           log10(50.0), 4, 3, LOG_LAB);
    static const char *dl[5] = {"1", "10", "100", "1E3", "1E4"};
    edgewz((Rectangle){f.x + 156, y, 44, 120}, "DND\nCPS", log10(fmax(a->dnd, 1.0)), 0, 4, 0, 2, log10(2000.0), 4, 4, dl);
    float px = f.x + 222;
    plate_c(px + 74, f.y, "PRIMARY PURIFICATION", 10);
    edgewz((Rectangle){px, y, 44, 120}, "O2\nPPM", a->prim_oxygen, 0, 20, 1, 5, 9, 20, 4, NULL);
    edgewz((Rectangle){px + 52, y, 44, 120}, "PLUG\nC", rm_plant_plugging_T(P, -1), 100, 200, 105, 140, 160, 200, 4, NULL);
    edgewz((Rectangle){px + 104, y, 44, 120}, "INVENT\n%", 100 * a->prim_inventory, 95, 100, 99.5, 100, 95, 99.5, 5, NULL);
    float sx = f.x + 390;
    plate_c(sx + 150, f.y, "SECONDARY PLUGGING TEMPERATURE C", 10);
    for (int i = 0; i < RM_NLOOPS; i++) {
        snprintf(b, sizeof b, "LOOP %d", i + 1);
        edgewz((Rectangle){sx + i * 52, y, 44, 120}, b, rm_plant_plugging_T(P, i), 100, 200, 105, 140, 160, 200, 4, NULL);
    }
    tag(sx + 214, y, "LOOP O2 PPM");
    for (int i = 0; i < RM_NLOOPS; i++) {
        textf(sx + 214, y + 22 + i * 24, 10, INK, "%d", i + 1);
        readout(sx + 226, y + 17 + i * 24, 10, 4, "%4.1f", P->loop[i].oxygen);
    }

    /* trace heating temperatures */
    float y2 = f.y + 190;
    static const char *tn[6] = {"PRI", "SEC 1", "SEC 2", "SEC 3", "SEC 4", "DUMP"};
    plate_c(f.x + 164, y2 - 20, "TRACE HEATING - PIPE C AND KW", 10);
    for (int i = 0; i < 6; i++) {
        float x = f.x + i * 56;
        edgewz((Rectangle){x, y2 + 4, 44, 110}, tn[i], C(circuit_T(i)), 0, 400, 200, 400, 0, 150, 4, NULL);
        readout(x - 2, y2 + 134, 10, 4, "%4.0f", a->heat_kw[i]);
    }
    float iy = y2 + 168;
    indicator((Rectangle){f.x, iy, 80, 30}, "DND\nFAILED FUEL", L_RED, a->dnd > 500.0);
    indicator((Rectangle){f.x + 84, iy, 80, 30}, "GUARD VSL\nLEAK", L_RED, a->prim_leak > 0.0005);
    indicator((Rectangle){f.x + 168, iy, 80, 30}, "GAS ACT\nHIGH", L_AMB, a->gas_act > 50.0);
    indicator((Rectangle){f.x + 252, iy, 80, 30}, "PLUGGING\nHIGH", L_AMB, rm_plant_plugging_T(P, -1) > 160.0);
    strip((Rectangle){f.x + 348, y2 - 22, f.width - 348, f.y + f.height - y2 + 22}, TR_DND, 0, 4, "LOG DND", TR_GASACT, -2,
          4, "LOG GAS ACT");
}

static void naaux_bench(Rectangle bb)
{
    rm_aux *a = &P->aux;
    /* cover gas mimic: argon supply, reactor cover gas, vent through the delay beds to the stack */
    float y = bb.y + 2, ly = y + 67;
    Vector2 g1[2] = {{bb.x, ly}, {bb.x + 170, ly}};
    mpipe(g1, 2, MIM_AIR, 5, 1);
    Vector2 g2[2] = {{bb.x + 230, ly}, {bb.x + 440, ly}};
    mpipe(g2, 2, MIM_AIR, 5, 1);
    text("ARGON", bb.x, ly - 26, 10, INK);
    text("SUPPLY", bb.x, ly - 14, 10, INK);
    sym_tank((Rectangle){bb.x + 160, ly - 24, 80, 48}, MIM_PNA, "COVER GAS");
    tri((Vector2){bb.x + 448, ly}, (Vector2){bb.x + 438, ly - 7}, (Vector2){bb.x + 438, ly + 7}, MIM_AIR);
    text("STACK", bb.x + 420, ly + 10, 10, INK);
    onoff_sw(bb.x + 90, y, "SUPPLY", "SHUT", "OPEN", &a->gas_supply, "ARGON SUPPLY VALVE");
    onoff_sw(bb.x + 290, y, "VENT", "SHUT", "OPEN", &a->gas_vent, "COVER GAS VENT VALVE");
    onoff_sw(bb.x + 374, y, "GAS CLEANUP", "OFF", "ON", &a->cleanup, "COVER GAS CLEANUP (DELAY BEDS)");
    float cx = bb.x + 470;
    demarc((Rectangle){cx, y + 6, bb.x + bb.width - cx, 96}, "PRESSURE CONTROL");
    if (lampbutton((Rectangle){cx + 10, y + 22, 54, 30}, "AUTO", L_WHT, a->gas_auto) && !a->gas_auto) {
        a->gas_auto = 1;
        logmsg("COVER GAS PRESSURE CONTROL AUTO");
    }
    if (lampbutton((Rectangle){cx + 68, y + 22, 54, 30}, "MAN", L_AMB, !a->gas_auto) && a->gas_auto) {
        a->gas_auto = 0;
        logmsg("COVER GAS PRESSURE CONTROL MANUAL");
    }
    tag(cx + 130, y + 20, "KPA");
    readout(cx + 130, y + 35, 14, 3, "%3.0f", a->gas_p / 1e3);
    text("AUTO HOLDS 120 KPA. SUPPLY", cx + 10, y + 62, 10, INK);
    text("AND VENT WORK IN MAN ONLY.", cx + 10, y + 75, 10, INK);

    /* cold traps: oxygen out of the sodium, before it plugs anything */
    float ty = bb.y + 112;
    demarc((Rectangle){bb.x, ty + 4, bb.width, 104}, "COLD TRAPS");
    onoff_sw(bb.x + 60, ty + 12, "PRIMARY", "OUT", "IN", &a->prim_cold_trap, "PRIMARY COLD TRAP");
    for (int i = 0; i < RM_NLOOPS; i++) {
        char nm[16], wh[32];
        snprintf(nm, sizeof nm, "LOOP %d", i + 1);
        snprintf(wh, sizeof wh, "LOOP %d COLD TRAP", i + 1);
        onoff_sw(bb.x + 160 + i * 84, ty + 12, nm, "OUT", "IN", &P->loop[i].cold_trap, wh);
    }
    text("CONTAINMENT ISOLATION", bb.x + 510, ty + 26, 10, INK);
    text("SHUTS THE PRIMARY", bb.x + 510, ty + 39, 10, INK);
    text("PURIFICATION LINES.", bb.x + 510, ty + 52, 10, INK);
    indicator((Rectangle){bb.x + 510, ty + 66, 150, 26}, "PURIF LINES SHUT", L_WHT, P->containment_isolated);

    /* trace heating selectors */
    float hy = bb.y + 232;
    demarc((Rectangle){bb.x, hy, bb.width, bb.y + bb.height - hy - 2}, "TRACE HEATING");
    static const char *nm[6] = {"PRIMARY", "SEC LOOP 1", "SEC LOOP 2", "SEC LOOP 3", "SEC LOOP 4", "DUMP TANKS"};
    static const char *pos[3] = {"OFF", "AUTO", "ON"};
    float pw = bb.width / 6;
    for (int i = 0; i < 6; i++) {
        float x = bb.x + pw * (i + 0.5f);
        tag_c(x, hy + 12, nm[i]);
        int cur = a->heat_on[i] ? 2 : (a->heat_auto[i] ? 1 : 0);
        int pk = rotary((Vector2){x, hy + 72}, 13, 3, pos, cur, -150, -30, 30);
        if (pk >= 0 && pk != cur) {
            a->heat_auto[i] = pk == 1;
            a->heat_on[i] = pk == 2;
            logmsg("TRACE HEATING %s %s", nm[i], pos[pk]);
        }
        double T = circuit_T(i);
        lamp(x - 30, hy + 104, 5, L_RED, a->heat_kw[i] > 0);
        text("ON", x - 22, hy + 99, 10, INK);
        lamp(x + 10, hy + 104, 5, L_AMB, T < 423.15);
        text("LOW", x + 18, hy + 99, 10, INK);
    }
    dymo(bb.x + 8, bb.y + bb.height - 20, "SODIUM FREEZES AT 98 C - AUTO HEATS BELOW 200 C - HEATERS NEED THE ESSENTIAL BUSES");
}

/* ---- 5-2 containment and radiation -------------------------------------------------------------------- */
static const char *RAD_N[8] = {"CR", "HALL", "TURB", "SG BLD", "GAS", "STACK", "SEC NA", "STEAM"};
static const double RAD_HI[8] = {1, 25, 5, 5, 50, 10, 5, 5};

static void cont_face(Rectangle f)
{
    rm_aux *a = &P->aux;
    float y = f.y + 24;
    plate_c(f.x + 100, f.y, "CONTAINMENT", 10);
    edgewz((Rectangle){f.x, y, 44, 120}, "BLDG\nKPA", a->cont_p, 95, 110, 99, 103, 103, 110, 3, NULL);
    edgewz((Rectangle){f.x + 52, y, 44, 120}, "BLDG\nC", a->cont_T, 0, 80, 15, 40, 45, 80, 4, NULL);
    edgewz((Rectangle){f.x + 104, y, 44, 120}, "CELL\nO2 %", a->cell_o2, 0, 20, 0, 2, 4, 20, 4, NULL);
    edgewz((Rectangle){f.x + 156, y, 44, 120}, "CELL\nC", a->cell_T, 0, 200, 20, 70, 80, 200, 4, NULL);
    float ix = f.x + 220, iw = (f.x + f.width - ix) / 2;
    struct { const char *t; int on; Color c; } st[8] = {
        {"CONTAINMENT\nISOLATED", P->containment_isolated, L_AMB},
        {"PRIMARY NA\nLEAK", a->prim_leak > 0.0005, L_RED},
        {"PRIMARY CELL\nFIRE", a->fire[RM_FIRE_CELL], L_RED},
        {"CELL O2\nHIGH", a->cell_o2 > 4.0, L_AMB},
        {"BLDG PRESS\nHIGH", a->cont_p > 103.0, L_AMB},
        {"CELL N2\nSUPPLY SHUT", !a->n2_supply, L_AMB},
        {"CR HVAC\nEMERGENCY", a->hvac_emerg, L_AMB},
        {"HIGH RAD\nCIS SIGNAL", a->rad[RM_RAD_STACK] > 50.0 || a->rad[RM_RAD_HALL] > 100.0, L_RED},
    };
    for (int k = 0; k < 8; k++)
        indicator((Rectangle){ix + (k % 2) * iw, y + (k / 2) * 34, iw - 6, 30}, st[k].t, st[k].c,
                  st[k].on && (st[k].c.r != L_RED.r || blink_fast()));

    float y2 = f.y + 186;
    plate_c(f.x + f.width / 2, y2 - 20, "RADIATION MONITORS - AREA USV/H, GAS AND STACK MBQ/M3", 10);
    float pitch = f.width / 8;
    for (int i = 0; i < 8; i++) {
        float x = f.x + i * pitch + 4;
        edgewz((Rectangle){x, y2 + 4, pitch - 12, 110}, RAD_N[i], log10(fmax(a->rad[i], 0.01)), -2, 4, -2, log10(RAD_HI[i]) - 1,
               log10(RAD_HI[i]), 4, 3, LOG_LAB);
        readout(x - 2, y2 + 134, 10, 4, a->rad[i] < 10 ? "%4.2f" : "%4.0f", fmin(a->rad[i], 9999.0));
    }
    float y3 = y2 + 162;
    mpr_draw((Rectangle){f.x, y3, f.width, f.y + f.height - y3}, &MP_RAD, "RADIATION RECORDER - LOG SCALE", ".01", "1E4");
}

static void cont_bench(Rectangle bb)
{
    rm_aux *a = &P->aux;
    float y = bb.y + 2, ly = y + 67;
    /* nitrogen to the primary cell */
    Vector2 n2[2] = {{bb.x, ly}, {bb.x + 200, ly}};
    mpipe(n2, 2, MIM_AIR, 5, 1);
    text("N2 SUPPLY", bb.x, ly - 24, 10, INK);
    sym_tank((Rectangle){bb.x + 196, ly - 26, 96, 52}, MIM_PNA, "PRIMARY CELL");
    onoff_sw(bb.x + 110, y, "CELL N2", "SHUT", "OPEN", &a->n2_supply, "PRIMARY CELL NITROGEN");
    text("THE CELL IS INERTED WITH NITROGEN: A PRIMARY", bb.x + 300, y + 30, 10, INK);
    text("LEAK CANNOT BURN BELOW 5% OXYGEN.", bb.x + 300, y + 43, 10, INK);
    tag(bb.x + 300, y + 64, "CELL O2 %");
    readout(bb.x + 300, y + 79, 14, 4, "%4.1f", a->cell_o2);
    tag(bb.x + 400, y + 64, "PRIM INVENT %");
    readout(bb.x + 400, y + 79, 14, 5, "%5.1f", 100 * a->prim_inventory);

    /* control room ventilation */
    float vy = bb.y + 130;
    demarc((Rectangle){bb.x, vy, bb.width, 128}, "CONTROL ROOM VENTILATION");
    int s = cswitch(bb.x + 60, vy + 14, "CR HVAC", "NORM", "EMERG", a->hvac_emerg, !a->hvac_emerg, SW_PISTOL, NULL);
    if (s) {
        a->hvac_emerg = s > 0;
        logmsg("CONTROL ROOM HVAC %s", s > 0 ? "EMERGENCY FILTRATION" : "NORMAL");
    }
    text("EMERGENCY: RECIRCULATE THROUGH THE", bb.x + 120, vy + 24, 10, INK);
    text("CHARCOAL FILTERS, OUTSIDE AIR SHUT.", bb.x + 120, vy + 37, 10, INK);
    text("STACK > 50 OR HALL > 100 PUTS IT ON", bb.x + 120, vy + 56, 10, INK);
    text("EMERGENCY AND ISOLATES CONTAINMENT.", bb.x + 120, vy + 69, 10, INK);
    tag(bb.x + 380, vy + 20, "CONTROL ROOM C");
    readout(bb.x + 380, vy + 35, 14, 3, "%3.0f", a->cr_T);
    tag(bb.x + 380, vy + 66, "CR RAD USV/H");
    readout(bb.x + 380, vy + 81, 14, 4, "%4.2f", fmin(a->rad[RM_RAD_CR], 99.0));

    /* reactor building */
    float cy = bb.y + 270;
    demarc((Rectangle){bb.x, cy, bb.width, bb.y + bb.height - cy - 2}, "REACTOR BUILDING");
    tag(bb.x + 10, cy + 16, "BLDG KPA");
    readout(bb.x + 10, cy + 31, 14, 5, "%5.1f", a->cont_p);
    tag(bb.x + 110, cy + 16, "BLDG C");
    readout(bb.x + 110, cy + 31, 14, 3, "%3.0f", a->cont_T);
    tag(bb.x + 190, cy + 16, "REACTOR NA MM");
    readout(bb.x + 190, cy + 31, 14, 4, "%4.0f", a->na_level);
    tag(bb.x + 300, cy + 16, "COVER GAS KPA");
    readout(bb.x + 300, cy + 31, 14, 3, "%3.0f", a->gas_p / 1e3);
    text("LOW REACTOR SODIUM LEVEL (-300 MM) TRIPS THE REACTOR.", bb.x + 10, cy + 70, 10, INK);
}

/* ---- 5-3 plant services and fire protection ---------------------------------------------------------- */
static void serv_face(Rectangle f)
{
    rm_aux *a = &P->aux;
    float y = f.y + 24;
    plate_c(f.x + 124, f.y, "COMPONENT COOLING", 10);
    edgewz((Rectangle){f.x, y, 44, 120}, "CCW\nC", a->ccw_T, 0, 80, 20, 35, 40, 80, 4, NULL);
    for (int i = 0; i < RM_NLOOPS; i++) {
        char b[16];
        snprintf(b, sizeof b, "PP-%d\nBRG C", i + 1);
        edgewz((Rectangle){f.x + 52 + i * 52, y, 44, 120}, b, P->loop[i].brg_T, 0, 120, 30, 70, 90, 120, 4, NULL);
    }
    dial((Rectangle){f.x + f.width - 150, f.y, 150, 150}, "INSTRUMENT\nAIR BAR", a->air_p, 0, 10, 5, 6, 8, 0, 4, NULL);

    float y2 = f.y + 184;
    demarc((Rectangle){f.x, y2, f.width, 170}, "FIRE DETECTION");
    static const char *zone[8] = {"SG 1\nCELL", "SG 2\nCELL", "SG 3\nCELL", "SG 4\nCELL", "PRIMARY\nCELL", "TURBINE\nHALL",
                                  "CABLE\nSPREAD RM", "CONTROL\nROOM"};
    float zw = (f.width - 20) / 4;
    for (int z = 0; z < 8; z++) {
        char l1[24];
        const char *nl = strchr(zone[z], '\n');
        snprintf(l1, sizeof l1, "%.*s", (int)(nl - zone[z]), zone[z]);
        window((Rectangle){f.x + 10 + (z % 4) * zw, y2 + 16 + (z / 4) * 50, zw - 6, 44}, l1, nl + 1, L_RED,
               a->fire[z] && blink_fast());
    }
    for (int z = 0; z < 8; z++) {
        if (!a->fire[z]) continue;
        textf(f.x + 14 + (z % 4) * zw, y2 + 50 + (z / 4) * 50, 10, INK, "%3.0f S", fmin(a->fire_t[z], 999.0));
    }
    text("SODIUM FIRES: NO WATER. DUMP THE LOOP AND LET IT BURN OUT.", f.x + 10, y2 + 124, 10, INK);
    text("A CONTROL ROOM FIRE MAY FORCE AN EVACUATION TO THE RSP.", f.x + 10, y2 + 137, 10, INK);
    text("FIRE PUMPS FEED THE HALL AND CABLE ROOM SPRINKLERS.", f.x + 10, y2 + 150, 10, INK);
    float y3 = y2 + 184;
    tag(f.x, y3, "CCW C");
    readout(f.x, y3 + 15, 14, 3, "%3.0f", a->ccw_T);
    tag(f.x + 80, y3, "AIR BAR");
    readout(f.x + 80, y3 + 15, 14, 3, "%3.1f", a->air_p);
    text("BELOW 4 BAR: FEED AND BYPASS VALVES FAIL AS IS.", f.x + 170, y3 + 6, 10, INK);
    text("BELOW 3 BAR: MSIVS DRIFT SHUT, DRACS DAMPERS FAIL OPEN.", f.x + 170, y3 + 19, 10, INK);
}

static void serv_bench(Rectangle bb)
{
    rm_aux *a = &P->aux;
    float pitch = bb.width / 4;
    /* cooling water: service water cools the CCW, the CCW cools the pump bearings */
    float y = bb.y + 2, ly = y + 67;
    Vector2 w1[2] = {{bb.x, ly}, {bb.x + bb.width, ly}};
    mpipe(w1, 2, MIM_WTR, 5, 1);
    onoff_sw(bb.x + pitch * 0.5f, y, "CCW PUMP A", "STOP", "START", &a->ccw[0], "COMPONENT COOLING PUMP A");
    onoff_sw(bb.x + pitch * 1.5f, y, "CCW PUMP B", "STOP", "START", &a->ccw[1], "COMPONENT COOLING PUMP B");
    onoff_sw(bb.x + pitch * 2.5f, y, "SW PUMP A", "STOP", "START", &a->sw[0], "SERVICE WATER PUMP A");
    onoff_sw(bb.x + pitch * 3.5f, y, "SW PUMP B", "STOP", "START", &a->sw[1], "SERVICE WATER PUMP B");
    text("PUMPS A ON THE ESSENTIAL BUSES, B ON THE HOUSE BUSES.", bb.x + 2, y + 100, 10, INK);
    text("LOSE BOTH CCW PUMPS AND THE PRIMARY PUMPS TRIP.", bb.x + 2, y + 112, 10, INK);

    /* instrument air and fire water */
    float y2 = bb.y + 126, ly2 = y2 + 67;
    Vector2 a1[2] = {{bb.x, ly2}, {bb.x + pitch * 2 - 10, ly2}};
    mpipe(a1, 2, MIM_AIR, 5, 1);
    Vector2 f1[2] = {{bb.x + pitch * 2 + 10, ly2}, {bb.x + bb.width, ly2}};
    mpipe(f1, 2, (Color){190, 40, 40, 255}, 5, 1);
    onoff_sw(bb.x + pitch * 0.5f, y2, "AIR COMP A", "STOP", "START", &a->air_comp[0], "INSTRUMENT AIR COMPRESSOR A");
    onoff_sw(bb.x + pitch * 1.5f, y2, "AIR COMP B", "STOP", "START", &a->air_comp[1], "INSTRUMENT AIR COMPRESSOR B");
    onoff_sw(bb.x + pitch * 2.5f, y2, "FIRE PUMP A", "STOP", "START", &a->fire_pump[0], "ELECTRIC FIRE PUMP A");
    onoff_sw(bb.x + pitch * 3.5f, y2, "FIRE PUMP B", "STOP", "START", &a->fire_pump[1], "DIESEL FIRE PUMP B");
    text("INSTRUMENT AIR", bb.x + 2, y2 + 100, 10, INK);
    text("FIRE WATER (A ELECTRIC, B DIESEL)", bb.x + pitch * 2 + 12, y2 + 100, 10, INK);

    float y3 = bb.y + 252;
    demarc((Rectangle){bb.x, y3, bb.width, bb.y + bb.height - y3 - 2}, "PRIMARY PUMP BEARINGS C - TRIP AT 90");
    for (int i = 0; i < RM_NLOOPS; i++) {
        float x = bb.x + 12 + i * pitch;
        char b[16];
        snprintf(b, sizeof b, "PP-%d", i + 1);
        tag(x, y3 + 16, b);
        readout(x, y3 + 31, 16, 3, "%3.0f", P->loop[i].brg_T);
        lamp(x + 70, y3 + 44, 6, L_AMB, P->loop[i].brg_T > 80.0);
    }
    text("AMBER = HOT (80 C). CCW COOLS THEM; SERVICE WATER COOLS THE CCW.", bb.x + 12, y3 + 74, 10, INK);
}

void draw_auxb(void)
{
    static const secdef S[3] = {{"5-1", "SODIUM AUXILIARIES", 720, SEC_NAAUX, 7},
                                {"5-2", "CONTAINMENT AND RADIATION", 620, SEC_CONT, 6},
                                {"5-3", "PLANT SERVICES AND FIRE PROTECTION", 572, SEC_SERV, 6}};
    secrect R[3];
    board_frame(S, 3, R);
    naaux_face(R[0].face);
    naaux_bench(R[0].bench);
    cont_face(R[1].face);
    cont_bench(R[1].bench);
    serv_face(R[2].face);
    serv_bench(R[2].bench);
    board_strip();
}
