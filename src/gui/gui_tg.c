/*
 * Turbine-generator and electrical board:
 *
 *   TURBINE | GENERATOR | STEAM AND FEEDWATER CONTROL
 *   FEED, CONDENSATE AND CIRCULATING WATER | ELECTRICAL ONE-LINE | ANNUNCIATORS
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

extern mpr MP_TURB;
void draw_log(Rectangle r);

/* ---- turbine ------------------------------------------------------------------------------ */
static void draw_turbine(Rectangle r)
{
    rm_tg *t = &P->tg;
    steel(r, "MAIN TURBINE");
    int latched = !P->turbine_tripped;
    meter((Rectangle){r.x + 10, r.y + 28, 240, 160}, "SPEED RPM", t->speed, 0, 2000, 1980, 2000, 4);

    float x = r.x + 262, y = r.y + 30;
    dymo(x, y, "SPEED RPM");
    readout(x, y + 16, 20, 4, "%4.0f", t->speed);
    dymo(x + 100, y, "REFERENCE");
    readout(x + 100, y + 16, 20, 4, "%4.0f", latched && !P->generator_breaker ? t->speed_ref : t->speed);
    dymo(x + 200, y, "ACCEL RPM/MIN");
    readout(x + 200, y + 16, 20, 3, "%3.0f", t->accel);

    static const char *tgt[6] = {"0", "100", "500", "1000", "1500", "1800"};
    static const double tv[6] = {0, 100, 500, 1000, 1500, 1800};
    int cur = 0;
    for (int i = 0; i < 6; i++)
        if (fabs(t->speed_target - tv[i]) < 1) cur = i;
    ctext("SPEED TARGET", x + 50, y + 62, 10, INK);
    int pk = rotary((Vector2){x + 50, y + 126}, 20, 6, tgt, cur, -210, 30, 44);
    if (pk >= 0) {
        t->speed_target = tv[pk];
        logmsg("TURBINE SPEED TARGET %s RPM", tgt[pk]);
    }
    static const char *acc[4] = {"60", "120", "300", "600"};
    static const double av[4] = {60, 120, 300, 600};
    int ca = 0;
    for (int i = 0; i < 4; i++)
        if (fabs(t->accel - av[i]) < 1) ca = i;
    ctext("ACCELERATION", x + 180, y + 62, 10, INK);
    int pa = rotary((Vector2){x + 180, y + 126}, 20, 4, acc, ca, -180, 0, 44);
    if (pa >= 0) {
        t->accel = av[pa];
        logmsg("TURBINE ACCELERATION %s RPM/MIN", acc[pa]);
    }
    text("RPM/MIN", x + 158, y + 150, 10, INK);
    lamp(x + 270, y + 90, 6, L_WHT, latched && !P->generator_breaker && fabs(t->speed - t->speed_ref) > 20);
    text("ACCEL", x + 280, y + 85, 10, INK);
    lamp(x + 270, y + 112, 6, L_WHT, latched && !P->generator_breaker && fabs(t->speed - t->speed_target) < 10);
    text("AT SPEED", x + 280, y + 107, 10, INK);
    lamp(x + 270, y + 134, 6, L_AMB, t->speed > 900 && t->speed < 1300 && !P->generator_breaker);
    text("CRITICAL", x + 280, y + 129, 10, INK);

    /* trip / latch, turning gear, oil pump, valves */
    y = r.y + 200;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + r.width - 8, y - 4}, 1, PAINT_LO);
    int s = cswitch(r.x + 50, y, "TURBINE", "TRIP", "LATCH", latched, !latched, SW_PISTOL, NULL);
    if (s < 0 && latched) rm_plant_turbine_trip(P, "MANUAL");
    if (s > 0 && !latched) {
        char why[96];
        if (!rm_plant_turbine_latch(P, why, sizeof why)) logmsg("%s", why);
    }
    s = cswitch(r.x + 130, y, "TURN GEAR", "OFF", "ENGAGE", t->turning_gear, !t->turning_gear, SW_PISTOL, NULL);
    if (s > 0 && !t->turning_gear) {
        if (t->speed > 5.0) logmsg("TURNING GEAR WILL NOT ENGAGE: ROTOR STILL TURNING");
        else if (t->lube_p < 1.0) logmsg("TURNING GEAR BLOCKED: NO BEARING OIL");
        else {
            t->turning_gear = 1;
            logmsg("TURNING GEAR ENGAGED");
        }
    }
    if (s < 0 && t->turning_gear) {
        t->turning_gear = 0;
        logmsg("TURNING GEAR OFF");
    }
    s = cswitch(r.x + 210, y, "AUX OIL PUMP", "STOP", "START", t->aux_oil_pump, !t->aux_oil_pump, SW_PISTOL, NULL);
    if (s) {
        t->aux_oil_pump = s > 0;
        logmsg("TURBINE AUX OIL PUMP %s", s > 0 ? "STARTED" : "STOPPED");
    }
    /* stop and control valve position lamps */
    float vx = r.x + 280;
    demarc((Rectangle){vx, y + 6, 330, 84}, "STEAM VALVES");
    for (int i = 0; i < 4; i++) {
        float lx = vx + 30 + i * 76;
        textf(lx - 14, y + 16, 10, INK, "SV%d", i + 1);
        lamp(lx + 22, y + 22, 5, L_RED, latched);
        lamp(lx + 36, y + 22, 5, L_GRN, !latched);
        textf(lx - 14, y + 44, 10, INK, "CV%d", i + 1);
        lamp(lx + 22, y + 50, 5, L_RED, latched && P->turbine_valve > 0.01);
        lamp(lx + 36, y + 50, 5, L_GRN, !(latched && P->turbine_valve > 0.01));
    }
    text("CONTROL VALVE %", vx + 14, y + 68, 10, INK);
    readout(vx + 120, y + 62, 10, 3, "%3.0f", 100 * P->turbine_valve * latched);
    text("BYPASS %", vx + 180, y + 68, 10, INK);
    readout(vx + 240, y + 62, 10, 3, "%3.0f", 100 * P->bypass_valve);

    /* supervisory instruments */
    y = r.y + 306;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + r.width - 8, y - 4}, 1, PAINT_LO);
    dymo(r.x + 10, y, "TURBINE SUPERVISORY");
    edgew((Rectangle){r.x + 12, y + 20, 48, 150}, "VIB MIL", t->vib, 0, 10, 7, 10, 5, NULL);
    edgew((Rectangle){r.x + 72, y + 20, 48, 150}, "ECC MIL", t->ecc, 0, 10, 2, 10, 5, NULL);
    edgew((Rectangle){r.x + 132, y + 20, 48, 150}, "BRG C", t->brg_T, 0, 120, 107, 120, 4, NULL);
    edgew((Rectangle){r.x + 192, y + 20, 48, 150}, "OIL BAR", t->lube_p, 0, 3, 0, 0.6, 3, NULL);
    mpr_draw((Rectangle){r.x + 254, y, r.width - 264, r.height - (y - r.y) - 10}, &MP_TURB, "TURBINE SUPERVISORY RECORDER",
             "0", "100% OF SCALE");
}

/* ---- generator ----------------------------------------------------------------------------- */
static void synchroscope(Vector2 c, float R)
{
    rm_tg *t = &P->tg;
    DrawCircleV(c, R + 8, BEZEL);
    DrawCircleV(c, R, FACE);
    for (int i = 0; i < 24; i++) {
        float a = (float)(i * 15.0 * DEG2RAD);
        float l = i % 6 ? 5.0f : 10.0f;
        DrawLineEx((Vector2){c.x + R * sinf(a), c.y - R * cosf(a)}, (Vector2){c.x + (R - l) * sinf(a), c.y - (R - l) * cosf(a)}, 1.5f, INK);
    }
    DrawRing(c, R - 12, R - 4, -100, -80, 8, (Color){40, 150, 60, 255});
    ctext("SLOW", c.x - R * 0.55f, c.y + 8, 10, INK);
    ctext("FAST", c.x + R * 0.55f, c.y + 8, 10, INK);
    ctext("SYNCHROSCOPE", c.x, c.y + R * 0.45f, 10, INK);
    int live = t->field_breaker && t->speed > 1500 && !P->generator_breaker;
    float ph = P->generator_breaker ? 0.0f : (float)t->phase;
    if (live || P->generator_breaker) {
        float a = ph * DEG2RAD;
        Vector2 tip = {c.x + (R - 6) * sinf(a), c.y - (R - 6) * cosf(a)};
        Vector2 tail = {c.x - 16 * sinf(a), c.y + 16 * cosf(a)};
        DrawLineEx(tail, tip, 4, (Color){20, 20, 20, 255});
    }
    DrawCircleV(c, 7, (Color){20, 20, 20, 255});
    /* dark-lamp synchronising lamps: out when in phase */
    float b = live ? fabsf(sinf(ph * DEG2RAD / 2)) : 0.0f;
    for (int k = -1; k <= 1; k += 2) {
        Vector2 lp = {c.x + k * (R + 26), c.y - R + 10};
        DrawCircleV(lp, 10, BEZEL);
        DrawCircleV(lp, 7, (Color){(unsigned char)(70 + 185 * b), (unsigned char)(66 + 175 * b), (unsigned char)(60 + 150 * b), 255});
    }
    ctext("SYNC LAMPS", c.x, c.y - R - 24, 10, INK);
}

static void draw_generator(Rectangle r)
{
    rm_tg *t = &P->tg;
    steel(r, "GENERATOR - EXCITATION - SYNCHRONISING");
    float mw = (r.width - 40) / 4;
    meter((Rectangle){r.x + 10, r.y + 28, mw, 112}, "GEN MW", P->P_gen / 1e6, 0, 1200, 1050, 1200, 4);
    static const char *mvl[5] = {"-400", "-200", "0", "200", "400"};
    meterl((Rectangle){r.x + 16 + mw, r.y + 28, mw, 112}, "MVAR", t->mvar, -400, 400, 300, 400, 4, mvl);
    meter((Rectangle){r.x + 22 + 2 * mw, r.y + 28, mw, 112}, "GEN KV", t->gen_kv, 0, 30, 25, 30, 3);
    static const char *hzl[5] = {"56", "58", "60", "62", "64"};
    meterl((Rectangle){r.x + 28 + 3 * mw, r.y + 28, mw, 112}, "GEN HZ", t->gen_hz, 56, 64, 62.5, 64, 4, hzl);

    synchroscope((Vector2){r.x + 120, r.y + 262}, 70);
    float x = r.x + 10, y = r.y + 360;
    dymo(x, y, "RUNNING KV / HZ");
    readout(x, y + 16, 12, 4, "%4.1f", t->gen_kv);
    readout(x + 60, y + 16, 12, 4, "%4.2f", t->gen_hz);
    dymo(x + 130, y, "INCOMING KV / HZ");
    readout(x + 130, y + 16, 12, 4, "%4.1f", P->grid_ok && P->grid_breaker ? 22.0 : 0.0);
    readout(x + 190, y + 16, 12, 4, "%4.2f", P->grid_ok && P->grid_breaker ? 60.0 : 0.0);

    /* excitation */
    float cx = r.x + 310;
    y = r.y + 150;
    int s = cswitch(cx, y, "FIELD BKR", "TRIP", "CLOSE", t->field_breaker, !t->field_breaker, SW_JHANDLE, NULL);
    if (s > 0 && !t->field_breaker) {
        if (t->speed < 1500) logmsg("FIELD BREAKER: FLASH THE FIELD ABOVE 1500 RPM");
        else {
            t->field_breaker = 1;
            logmsg("GENERATOR FIELD BREAKER CLOSED");
        }
    }
    if (s < 0 && t->field_breaker) {
        t->field_breaker = 0;
        if (P->generator_breaker) rm_plant_turbine_trip(P, "LOSS OF FIELD");
        logmsg("GENERATOR FIELD BREAKER OPENED");
    }
    float ax = cx + 70;
    dymo(ax, y, "VOLTAGE REGULATOR");
    if (lampbutton((Rectangle){ax, y + 16, 60, 30}, "AUTO", L_WHT, t->avr_auto) && !t->avr_auto) {
        t->avr_auto = 1;
        logmsg("AVR IN AUTO");
    }
    if (lampbutton((Rectangle){ax + 64, y + 16, 60, 30}, "MAN", L_AMB, !t->avr_auto) && t->avr_auto) {
        t->avr_auto = 0;
        logmsg("AVR IN MANUAL - EXCITER BY HAND");
    }
    dymo(ax, y + 54, "EXCITATION PU");
    readout(ax, y + 70, 14, 4, "%4.2f", t->exc);
    if (lampbutton((Rectangle){ax + 70, y + 66, 26, 26}, "-", L_WHT, 0) && !t->avr_auto && t->field_breaker)
        t->exc = fmax(0.0, t->exc - 0.02);
    if (lampbutton((Rectangle){ax + 98, y + 66, 26, 26}, "+", L_WHT, 0) && !t->avr_auto && t->field_breaker)
        t->exc = fmin(1.4, t->exc + 0.02);

    /* automatic synchroniser */
    y = r.y + 262;
    demarc((Rectangle){cx - 40, y, 330, 90}, "SYNCHRONISING");
    if (lampbutton((Rectangle){cx - 28, y + 14, 110, 38}, "AUTO SYNC", L_WHT, t->auto_sync)) {
        if (t->auto_sync) {
            t->auto_sync = 0;
            logmsg("AUTO SYNCHRONISER OFF");
        } else if (P->turbine_tripped) logmsg("AUTO SYNC: LATCH THE TURBINE FIRST");
        else if (!t->field_breaker) logmsg("AUTO SYNC: CLOSE THE FIELD BREAKER FIRST");
        else {
            t->auto_sync = 1;
            logmsg("AUTO SYNCHRONISER ON - MATCHING SPEED, WILL CLOSE IN PHASE");
        }
    }
    lamp(cx + 100, y + 24, 5, L_WHT, !P->generator_breaker && fabs(t->gen_hz - 60.0) < 0.2);
    text("FREQ MATCH", cx + 110, y + 19, 10, INK);
    lamp(cx + 100, y + 42, 5, L_WHT, !P->generator_breaker && fabs(t->gen_kv - 22.0) < 1.5 && t->field_breaker);
    text("VOLTS MATCH", cx + 110, y + 37, 10, INK);
    lamp(cx + 200, y + 24, 5, L_WHT, !P->generator_breaker && fabs(t->phase) < 10.0 && t->speed > 1500);
    text("IN PHASE", cx + 210, y + 19, 10, INK);
    lamp(cx + 200, y + 42, 5, L_RED, P->generator_breaker);
    text("ON LINE", cx + 210, y + 37, 10, INK);
    text("CLOSE THE GENERATOR BREAKER (ONE-LINE) WITH", cx - 28, y + 58, 10, INK);
    text("THE POINTER AT 12 O'CLOCK, TURNING SLOW.", cx - 28, y + 70, 10, INK);

    /* outputs */
    y = r.y + 360;
    x = r.x + 280;
    dymo(x, y, "GROSS MWE");
    readout(x, y + 16, 14, 4, "%4.0f", P->P_gen / 1e6);
    dymo(x + 80, y, "HOUSE MW");
    readout(x + 80, y + 16, 14, 3, "%3.0f", P->P_house / 1e6);
    dymo(x + 160, y, "NET MWE");
    readout(x + 160, y + 16, 14, 4, "%4.0f", P->P_net / 1e6);
    dymo(x + 240, y, "MVAR");
    readout(x + 240, y + 16, 14, 4, "%4.0f", t->mvar);

    strip((Rectangle){r.x + 10, r.y + 404, r.width - 20, r.height - 414}, TR_GENMW, 0, 1200, "GEN MW", TR_SPEED, 0, 2000,
          "RPM");
}

/* ---- steam and feedwater control ------------------------------------------------------------ */
static void draw_steam(Rectangle r)
{
    steel(r, "MAIN STEAM AND FEEDWATER CONTROL");
    float mw = (r.width - 28) / 3;
    meter((Rectangle){r.x + 8, r.y + 28, mw, 108}, "STEAM MPA", P->p_header / 1e6, 0, 20, 16, 20, 4);
    meter((Rectangle){r.x + 14 + mw, r.y + 28, mw, 108}, "STEAM FLOW KG/S", P->W_turbine + P->W_bypass + P->W_relief, 0,
          1200, 0, 0, 4);
    meter((Rectangle){r.x + 20 + 2 * mw, r.y + 28, mw, 108}, "FEED TEMP C", P->T_fw - 273.15, 0, 300, 0, 0, 3);

    float y = r.y + 146;
    dymo(r.x + 10, y, "PRESSURE SET MPA");
    readout(r.x + 10, y + 16, 16, 3, "%4.1f", P->p_set / 1e6);
    if (lampbutton((Rectangle){r.x + 76, y + 14, 52, 32}, "LOWER", L_WHT, 0)) P->p_set = fmax(8e6, P->p_set - 0.1e6);
    if (lampbutton((Rectangle){r.x + 130, y + 14, 52, 32}, "RAISE", L_WHT, 0)) P->p_set = fmin(15.5e6, P->p_set + 0.1e6);
    dymo(r.x + 200, y, "BYPASS %");
    readout(r.x + 200, y + 16, 16, 3, "%3.0f", 100 * P->bypass_valve);
    lamp(r.x + 272, y + 26, 5, L_AMB, P->W_relief > 0);
    text("SAFETY VLV", r.x + 282, y + 21, 10, INK);
    dymo(r.x + 370, y, "COND KPA");
    readout(r.x + 370, y + 16, 16, 4, "%4.1f", P->p_cond / 1e3);
    dymo(r.x + 460, y, "INSTR AIR BAR");
    readout(r.x + 460, y + 16, 16, 3, "%3.1f", P->aux.air_p);
    text(P->aux.air_p < 4.0 ? "VALVES FAILED" : "", r.x + 560, y + 21, 10, (Color){150, 20, 10, 255});

    y = r.y + 204;
    DrawLineEx((Vector2){r.x + 8, y - 4}, (Vector2){r.x + r.width - 8, y - 4}, 1, PAINT_LO);
    dymo(r.x + 10, y + 10, "FEEDWATER");
    if (lampbutton((Rectangle){r.x + 90, y, 70, 34}, "IN\nSERVICE", L_RED, P->fw_on) && !P->fw_on) {
        P->fw_on = 1;
        logmsg("FEEDWATER IN SERVICE");
    }
    if (lampbutton((Rectangle){r.x + 164, y, 70, 34}, "OUT OF\nSERVICE", L_GRN, !P->fw_on) && P->fw_on) {
        P->fw_on = 0;
        logmsg("FEEDWATER OUT OF SERVICE");
    }
    if (lampbutton((Rectangle){r.x + 250, y, 62, 34}, "AUTO", L_WHT, P->auto_fw) && !P->auto_fw) {
        P->auto_fw = 1;
        logmsg("FEEDWATER CONTROL AUTO");
    }
    if (lampbutton((Rectangle){r.x + 316, y, 62, 34}, "MAN", L_AMB, !P->auto_fw) && P->auto_fw) {
        P->auto_fw = 0;
        logmsg("FEEDWATER CONTROL MANUAL");
    }
    dymo(r.x + 396, y, "PUMP CAPACITY %");
    readout(r.x + 396, y + 14, 14, 3, "%3.0f", 100 * P->tg.fw_cap);
    dymo(r.x + 520, y, "FEED KG/S");
    double wf = 0;
    for (int i = 0; i < RM_NLOOPS; i++) wf += P->loop[i].sg.W_fw;
    readout(r.x + 520, y + 14, 14, 4, "%4.0f", wf);

    /* per steam generator */
    y = r.y + 256;
    const char *hd[8] = {"SG", "FEED KG/S", "VALVE %", "MANUAL", "STEAM C", "STEAM KG/S", "SG MW", "MSIV"};
    const float hx[8] = {10, 44, 124, 196, 300, 380, 480, 570};
    for (int k = 0; k < 8; k++) text(hd[k], r.x + hx[k], y, 10, INK);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &P->loop[i].sg;
        float yy = y + 16 + i * 60;
        char lb[8];
        snprintf(lb, sizeof lb, "%d", i + 1);
        dymo(r.x + 12, yy + 8, lb);
        readout(r.x + 44, yy, 18, 4, "%4.0f", s->W_fw);
        readout(r.x + 124, yy, 18, 3, "%3.0f", 100 * s->fw_valve / 1.3);
        if (lampbutton((Rectangle){r.x + 196, yy, 44, 30}, "-", L_WHT, 0) && !P->auto_fw) s->fw_valve = fmax(0, s->fw_valve - 0.02);
        if (lampbutton((Rectangle){r.x + 244, yy, 44, 30}, "+", L_WHT, 0) && !P->auto_fw) s->fw_valve = fmin(1.3, s->fw_valve + 0.02);
        readout(r.x + 300, yy, 18, 3, "%3.0f", s->T_steam - 273.15);
        readout(r.x + 380, yy, 18, 4, "%4.0f", s->W_steam);
        readout(r.x + 480, yy, 18, 3, "%3.0f", fmax(s->Q / 1e6, 0.0));
        lamp(r.x + 580, yy + 14, 6, L_RED, s->msiv_open);
        lamp(r.x + 600, yy + 14, 6, L_GRN, !s->msiv_open);
    }
    text("VALVES MOVE BY HAND ONLY IN MANUAL.  PRESSURE IS HELD BY THE TURBINE ON LINE, BY THE BYPASS OFF LINE.",
         r.x + 10, r.y + r.height - 20, 10, INK);
}

/* ---- feed, condensate, condenser -------------------------------------------------------------- */
static void pump_sw(float cx, float y, const char *name, int *on, const char *what)
{
    int s = cswitch(cx, y, name, "STOP", "START", *on, !*on, SW_PISTOL, NULL);
    if (s > 0 && !*on) {
        *on = 1;
        logmsg("%s STARTED", what);
    } else if (s < 0 && *on) {
        *on = 0;
        logmsg("%s STOPPED", what);
    }
}

static void draw_feed(Rectangle r)
{
    rm_tg *t = &P->tg;
    steel(r, "FEEDWATER - CONDENSATE - CIRCULATING WATER");
    float y = r.y + 30;
    pump_sw(r.x + 60, y, "TDFP A", &t->tdfp[0], "TURBINE FEED PUMP A");
    pump_sw(r.x + 150, y, "TDFP B", &t->tdfp[1], "TURBINE FEED PUMP B");
    pump_sw(r.x + 240, y, "MDFP", &t->mdfp, "MOTOR FEED PUMP");
    pump_sw(r.x + 350, y, "COND PUMP A", &t->cond_pump[0], "CONDENSATE PUMP A");
    pump_sw(r.x + 440, y, "COND PUMP B", &t->cond_pump[1], "CONDENSATE PUMP B");
    int s = cswitch(r.x + 550, y, "HP HEATERS", "OUT", "IN", t->heaters_in, !t->heaters_in, SW_PISTOL, NULL);
    if (s) {
        t->heaters_in = s > 0;
        logmsg("HP FEED HEATERS %s", s > 0 ? "IN SERVICE" : "BYPASSED");
    }
    demarc((Rectangle){r.x + 14, y - 12, 272, CSW_H + 16}, NULL);

    y = r.y + 136;
    text("TDFP 60% EACH (NEED STEAM > 4 MPA)", r.x + 12, y, 10, INK);
    text("MDFP 25% STARTUP PUMP", r.x + 12, y + 12, 10, INK);
    dymo(r.x + 12, y + 32, "FEED CAPACITY %");
    readout(r.x + 12, y + 48, 16, 3, "%3.0f", 100 * t->fw_cap);
    dymo(r.x + 130, y + 32, "PUMP SPEED %");
    readout(r.x + 130, y + 48, 16, 3, "%3.0f", 100 * P->fw_pump);
    dymo(r.x + 240, y + 32, "FEED TEMP C");
    readout(r.x + 240, y + 48, 16, 3, "%3.0f", P->T_fw - 273.15);
    edgew((Rectangle){r.x + 380, y - 4, 48, 120}, "HOTWELL M", t->hotwell, 0, 2, 1.4, 2, 4, NULL);
    edgew((Rectangle){r.x + 450, y - 4, 48, 120}, "DA LVL M", t->da_level, 0, 3, 2.6, 3, 3, NULL);
    edgew((Rectangle){r.x + 520, y - 4, 48, 120}, "CW FLOW%", 100 * P->cw_pump, 0, 120, 0, 0, 6, NULL);

    y = r.y + 290;
    DrawLineEx((Vector2){r.x + 8, y - 6}, (Vector2){r.x + r.width - 8, y - 6}, 1, PAINT_LO);
    for (int i = 0; i < 3; i++) {
        char nm[16], wh[32];
        snprintf(nm, sizeof nm, "CW PUMP %c", 'A' + i);
        snprintf(wh, sizeof wh, "CIRC WATER PUMP %c", 'A' + i);
        pump_sw(r.x + 60 + i * 90, y, nm, &t->cw[i], wh);
    }
    for (int i = 0; i < 2; i++) {
        char nm[16], wh[32];
        snprintf(nm, sizeof nm, "VAC PUMP %c", 'A' + i);
        snprintf(wh, sizeof wh, "CONDENSER VACUUM PUMP %c", 'A' + i);
        pump_sw(r.x + 350 + i * 90, y, nm, &t->vac_pump[i], wh);
    }
    y = r.y + 392;
    meter((Rectangle){r.x + 10, y, 200, 100}, "CONDENSER KPA ABS", P->p_cond / 1e3, 0, 30, 15, 30, 3);
    dymo(r.x + 230, y, "AIR IN CONDENSER KG");
    readout(r.x + 230, y + 16, 16, 4, "%4.2f", t->cond_air);
    dymo(r.x + 230, y + 52, "CW INLET C");
    readout(r.x + 230, y + 68, 16, 3, "%3.0f", P->T_cw_in - 273.15);
    text("VACUUM LOW 15 KPA: BYPASS AND LATCH BLOCKED", r.x + 380, y + 4, 10, INK);
    text("25 KPA: TURBINE TRIP", r.x + 380, y + 18, 10, INK);
}

/* ---- electrical one-line ---------------------------------------------------------------------- */
static Color lc(Color c, int live) { return live ? c : (Color){c.r / 3 + 40, c.g / 3 + 40, c.b / 3 + 40, 255}; }

static void breaker(Vector2 p, int closed)
{
    Rectangle b = {p.x - 9, p.y - 9, 18, 18};
    DrawRectangleRec(b, closed ? L_RED : L_GRN);
    DrawRectangleLinesEx(b, 2, BEZEL);
}

static void xfmr(Vector2 p, const char *name, int live, int vertical)
{
    Vector2 a = vertical ? (Vector2){p.x, p.y - 8} : (Vector2){p.x - 8, p.y};
    Vector2 b = vertical ? (Vector2){p.x, p.y + 8} : (Vector2){p.x + 8, p.y};
    DrawRing(a, 9, 12, 0, 360, 24, lc(MIM_ON, live));
    DrawRing(b, 9, 12, 0, 360, 24, lc(MIM_ON, live));
    text(name, p.x + 16, p.y - 5, 10, INK);
}

static void draw_electrical(Rectangle r)
{
    rm_tg *t = &P->tg;
    steel(r, "ELECTRICAL DISTRIBUTION");
    int grid = P->grid_ok, swyd = P->grid_ok && P->grid_breaker;
    int gen_live = t->field_breaker && t->speed > 1500;
    int uat_live = P->generator_breaker || gen_live;
    int nb = P->offsite_power;
    float yl = r.y + 60;

    /* generator line: grid - switchyard breaker - main transformer - generator breaker - generator */
    text("345 KV GRID", r.x + 14, yl - 22, 10, INK);
    lamp(r.x + 30, yl, 7, L_RED, grid);
    mimic((Vector2){r.x + 38, yl}, (Vector2){r.x + 150, yl}, 5, lc(MIM_ON, grid));
    breaker((Vector2){r.x + 160, yl}, P->grid_breaker);
    mimic((Vector2){r.x + 170, yl}, (Vector2){r.x + 400, yl}, 5, lc(MIM_ON, swyd));
    xfmr((Vector2){r.x + 412, yl}, "", swyd || P->generator_breaker, 0);
    text("MAIN XFMR", r.x + 380, yl + 16, 10, INK);
    mimic((Vector2){r.x + 424, yl}, (Vector2){r.x + 470, yl}, 5, lc(MIM_ON, swyd || P->generator_breaker));
    breaker((Vector2){r.x + 480, yl}, P->generator_breaker);
    mimic((Vector2){r.x + 490, yl}, (Vector2){r.x + 600, yl}, 5, lc(MIM_ON, gen_live || P->generator_breaker));
    DrawCircleV((Vector2){r.x + 616, yl}, 16, lc(MIM_ON, gen_live));
    DrawCircleV((Vector2){r.x + 616, yl}, 12, PAINT);
    ctext("G", r.x + 616, yl - 5, 10, INK);
    text("22 KV", r.x + 540, yl - 16, 10, INK);

    int s = cswitch(r.x + 160, yl + 14, "SWYD BKR", "TRIP", "CLOSE", P->grid_breaker, !P->grid_breaker, SW_JHANDLE, NULL);
    if (s > 0 && !P->grid_breaker) {
        P->grid_breaker = 1;
        logmsg("SWITCHYARD BREAKER CLOSED");
    }
    if (s < 0 && P->grid_breaker) {
        P->grid_breaker = 0;
        logmsg("SWITCHYARD BREAKER OPENED");
    }
    s = cswitch(r.x + 480, yl + 14, "GEN BKR", "TRIP", "CLOSE", P->generator_breaker, !P->generator_breaker, SW_JHANDLE, NULL);
    if (s > 0 && !P->generator_breaker) {
        char why[96];
        if (!rm_plant_gen_breaker_close(P, why, sizeof why)) logmsg("%s", why);
    }
    if (s < 0 && P->generator_breaker) {
        P->generator_breaker = 0;
        logmsg("GENERATOR BREAKER OPENED - TURBINE ON SPEED CONTROL");
    }

    /* startup and unit auxiliary transformers down to the normal house buses */
    float yb = r.y + 262;
    float xs = r.x + 300, xu = r.x + 560;
    mimic((Vector2){xs, yl}, (Vector2){xs, r.y + 150}, 4, lc(MIM_ON, swyd));
    xfmr((Vector2){xs, r.y + 170}, "SAT", swyd, 1);
    mimic((Vector2){xs, r.y + 190}, (Vector2){xs, yb - 14}, 4, lc(MIM_ON, swyd));
    breaker((Vector2){xs, yb - 14}, !t->aux_on_uat && swyd);
    mimic((Vector2){xu, yl}, (Vector2){xu, r.y + 150}, 4, lc(MIM_ON, uat_live));
    xfmr((Vector2){xu, r.y + 170}, "UAT", uat_live, 1);
    mimic((Vector2){xu, r.y + 190}, (Vector2){xu, yb - 14}, 4, lc(MIM_ON, uat_live));
    breaker((Vector2){xu, yb - 14}, t->aux_on_uat);
    mimic((Vector2){r.x + 60, yb}, (Vector2){r.x + 640, yb}, 6, lc(MIM_ON, nb));
    text("6.9 KV NORMAL HOUSE BUSES", r.x + 380, yb + 6, 10, INK);
    lamp(r.x + 50, yb, 6, L_RED, nb);

    s = cswitch(r.x + 430, r.y + 126, "BUS TRANSFER", "SAT", "UAT", t->aux_on_uat, !t->aux_on_uat, SW_JHANDLE, NULL);
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
    lamp(r.x + 478, r.y + 200, 5, L_AMB, t->transfer_fail && P->generator_breaker);
    text("FAULT", r.x + 486, r.y + 195, 10, INK);

    /* essential buses and the diesels */
    float ye = r.y + 318;
    for (int i = 0; i < 3; i++) {
        float x0 = r.x + 40 + i * 206, cx = x0 + 80;
        int live = nb || P->diesel_running[i];
        mimic((Vector2){cx, yb}, (Vector2){cx, ye - 12}, 4, lc(MIM_ON, nb));
        breaker((Vector2){cx, ye - 12}, nb);
        mimic((Vector2){x0, ye}, (Vector2){x0 + 170, ye}, 6, lc(MIM_ON, live));
        textf(x0, ye + 5, 10, INK, "ESS BUS %c  4.16 KV", 'A' + i);
        textf(x0 + 118, ye + 5, 10, INK, "%4.2f", live ? 4.16 : 0.0);
        mimic((Vector2){x0 + 150, ye}, (Vector2){x0 + 150, ye + 30}, 4, lc(MIM_ON, P->diesel_running[i]));
        breaker((Vector2){x0 + 150, ye + 30}, P->diesel_running[i] && !nb);
        char nm[16];
        snprintf(nm, sizeof nm, "DG %d", i + 1);
        int dg = cswitch(x0 + 50, ye + 24, nm, "STOP", "START", P->diesel_running[i], !P->diesel_running[i], SW_PISTOL,
                         &P->diesel_ptl[i]);
        if (dg > 0 && !P->diesel_manual[i]) {
            P->diesel_manual[i] = 1;
            logmsg("DIESEL %d START", i + 1);
        }
        if (dg < 0 && P->diesel_manual[i]) {
            P->diesel_manual[i] = 0;
            logmsg(nb ? "DIESEL %d STOPPED" : "DIESEL %d STAYS ON: BUS UNDERVOLTAGE", i + 1);
        }
        readout(x0 + 104, ye + 50, 12, 3, "%3.1f", t->dg_mw[i]);
        text("MW", x0 + 146, ye + 54, 10, INK);
        lamp(x0 + 110, ye + 86, 4, L_RED, P->diesel_running[i]);
        text("RUN", x0 + 118, ye + 81, 10, INK);
        lamp(x0 + 110, ye + 100, 4, L_AMB, !P->diesel_avail[i]);
        text("OOS", x0 + 118, ye + 95, 10, INK);
        lamp(x0 + 150, ye + 86, 4, L_WHT, P->diesel_t[i] > 0 && !P->diesel_running[i]);
        text("CRANK", x0 + 158, ye + 81, 10, INK);
    }
    text("RIGHT-CLICK A DIESEL SWITCH FOR PULL-TO-LOCK (NO AUTO START)", r.x + 40, r.y + r.height - 20, 10, INK);
    textf(r.x + 440, r.y + r.height - 20, 10, INK, "ESSENTIAL LOAD %.1f MW", rm_plant_essential_load(P) / 1e6);

    /* DC and vital AC */
    float dx = r.x + 690;
    demarc((Rectangle){dx - 10, r.y + 34, 214, r.height - 44}, "125 V DC / 120 V VITAL AC");
    for (int d = 0; d < 2; d++) {
        float cx = dx + 50 + d * 100;
        textf(cx - 30, r.y + 44, 10, INK, "DIVISION %c", 'A' + d);
        readout(cx - 30, r.y + 58, 14, 3, "%3.0f", 105.0 + 25.0 * t->batt[d]);
        text("VDC", cx + 20, r.y + 62, 10, INK);
        readout(cx - 30, r.y + 88, 14, 3, "%3.0f", 100 * t->batt[d]);
        text("% CHG", cx + 20, r.y + 92, 10, INK);
        char nm[16];
        snprintf(nm, sizeof nm, "CHARGER %c", 'A' + d);
        int c = cswitch(cx, r.y + 118, nm, "OFF", "ON", t->charger[d], !t->charger[d], SW_PISTOL, NULL);
        if (c) {
            t->charger[d] = c > 0;
            logmsg("BATTERY CHARGER %c %s", 'A' + d, c > 0 ? "ON" : "OFF");
        }
        snprintf(nm, sizeof nm, "INVERTER %c", 'A' + d);
        c = cswitch(cx, r.y + 218, nm, "OFF", "ON", t->inverter[d], !t->inverter[d], SW_PISTOL, NULL);
        if (c) {
            t->inverter[d] = c > 0;
            logmsg("VITAL AC INVERTER %c %s%s", 'A' + d, c > 0 ? "ON" : "OFF", c > 0 ? "" : " - NUCLEAR INSTRUMENTS DIV LOST");
        }
        int vital = t->inverter[d] && t->batt[d] > 0.02;
        lamp(cx - 20, r.y + 324, 6, L_RED, vital);
        text("VITAL", cx - 8, r.y + 319, 10, INK);
        int chg = t->charger[d] && rm_plant_essential_power(P);
        lamp(cx - 20, r.y + 344, 6, L_AMB, !chg);
        text("DISCH", cx - 8, r.y + 339, 10, INK);
    }
    text("BATTERIES LAST ABOUT 4 H", dx, r.y + 372, 10, INK);
    text("WITHOUT THE CHARGERS. DIESELS", dx, r.y + 384, 10, INK);
    text("NEED DC TO CRANK; NUCLEAR", dx, r.y + 396, 10, INK);
    text("INSTRUMENTS NEED VITAL AC.", dx, r.y + 408, 10, INK);
}

static void draw_tg_ann(Rectangle r)
{
    steel(r, NULL);
    ann_box((Rectangle){r.x + 6, r.y + 6, r.width - 12, 320}, BOARD_TG, 3, 32);
    ann_controls(r.x + 10, r.y + 334);
    draw_log((Rectangle){r.x + 4, r.y + 372, r.width - 8, r.height - 376});
}

void draw_tg(void)
{
    draw_turbine((Rectangle){6, 48, 620, 520});
    draw_generator((Rectangle){632, 48, 620, 520});
    draw_steam((Rectangle){1258, 48, 656, 520});
    draw_feed((Rectangle){6, 574, 620, 502});
    draw_electrical((Rectangle){632, 574, 900, 502});
    draw_tg_ann((Rectangle){1538, 574, 376, 502});
}
