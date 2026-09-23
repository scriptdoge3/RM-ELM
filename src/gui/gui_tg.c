/*
 * Turbine-generator and feed board (panels 3-1 to 3-3):
 *
 *   3-1  FEEDWATER AND MAIN STEAM
 *        steam header, steam and feed flow dials; per-SG feed, steam
 *        temperature and valve meters; hotwell and deaerator; steam and SG
 *        recorders. On the bench, the feed train mimic with its pump
 *        switches, open/close pushbuttons for each SG's feed and steam
 *        isolation valves, a feed regulating valve M/A station per SG under
 *        the feedwater master, and the throttle pressure loading station.
 *   3-2  TURBINE AND CONDENSER
 *        speed dial, EHC readouts and valve lights, supervisory meters and
 *        recorder, condenser vacuum. On the bench, the trip/latch, turning
 *        gear and oil pump switches, the EHC speed and acceleration
 *        selector pushbuttons, the steam path mimic, and the circulating
 *        water and vacuum pumps.
 *   3-3  GENERATOR
 *        switchboard meters, synchroscope with its lamps, output readouts,
 *        recorder. On the bench, the generator and field breakers, the
 *        voltage regulator M/A station, the auto synchroniser, and the load
 *        dispatcher's order.
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static double C(double T_K) { return T_K - 273.15; }

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

static double feed_total(void)
{
    double w = 0;
    for (int i = 0; i < RM_NLOOPS; i++) w += P->loop[i].sg.W_fw;
    return w;
}

/* ---- 3-1 feedwater and main steam ------------------------------------------------------------- */
static const char *const X100[5] = {"0", "3", "6", "9", "12"};

static void fw_face(Rectangle f)
{
    char b[40];
    const float pitch = 130;
    dial((Rectangle){f.x + 3, f.y, 124, 124}, "STEAM HEADER\nPRESS MPA", P->p_header / 1e6, 0, 20, 4, 13, 15, 16.5, 20,
         NULL);
    dial((Rectangle){f.x + pitch + 3, f.y, 124, 124}, "STEAM FLOW\nKG/S X100", (P->W_turbine + P->W_bypass + P->W_relief) / 100,
         0, 12, 4, 0, 0, 0, 0, X100);
    dial((Rectangle){f.x + 2 * pitch + 3, f.y, 124, 124}, "FEED FLOW\nKG/S X100", feed_total() / 100, 0, 12, 4, 0, 0, 0, 0,
         X100);
    dial((Rectangle){f.x + 3 * pitch + 3, f.y, 124, 124}, "FEED TEMP\nC", C(P->T_fw), 0, 300, 3, 225, 255, 0, 0, NULL);
    float x = f.x + 4 * pitch + 8;
    tag(x, f.y, "PRESS SET MPA");
    readout(x, f.y + 15, 14, 4, "%4.1f", P->p_set / 1e6);
    tag(x + 100, f.y, "BYPASS %");
    readout(x + 100, f.y + 15, 14, 3, "%3.0f", 100 * P->bypass_valve);
    tag(x, f.y + 46, "FEED PUMP SPD %");
    readout(x, f.y + 61, 14, 3, "%3.0f", 100 * P->fw_pump);
    tag(x + 100, f.y + 46, "CAPACITY %");
    readout(x + 100, f.y + 61, 14, 3, "%3.0f", 100 * P->tg.fw_cap);
    tag(x, f.y + 92, "INSTR AIR BAR");
    readout(x, f.y + 107, 14, 3, "%3.1f", P->aux.air_p);
    indicator((Rectangle){x + 100, f.y + 92, 80, 34}, "SAFETY\nVLV OPEN", L_AMB, P->W_relief > 0);

    /* per steam generator */
    float y2 = f.y + 150;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &P->loop[i].sg;
        float gx = f.x + i * 132;
        snprintf(b, sizeof b, "SG %d", i + 1);
        plate_c(gx + 63, y2 - 22, b, 10);
        edgewz((Rectangle){gx, y2, 38, 100}, "FEED\nKG/S", s->W_fw, 0, 400, 200, 300, 0, 0, 4, NULL);
        edgewz((Rectangle){gx + 44, y2, 38, 100}, "STEAM\nC", C(s->T_steam), 300, 600, 465, 495, 510, 600, 3, NULL);
        edgewz((Rectangle){gx + 88, y2, 38, 100}, "FRV\n%", 100 * s->fw_valve / 1.3, 0, 100, 0, 0, 0, 0, 4, NULL);
    }
    float cx = f.x + 4 * 132 + 4;
    plate_c(cx + 42, y2 - 22, "CONDENSATE", 10);
    edgewz((Rectangle){cx, y2, 38, 100}, "HOTWELL\nM", P->tg.hotwell, 0, 2, 0.6, 1.2, 1.4, 2, 4, NULL);
    edgewz((Rectangle){cx + 52, y2, 38, 100}, "DA LVL\nM", P->tg.da_level, 0, 3, 1.5, 2.6, 2.6, 3, 3, NULL);
    float sx = cx + 104;
    tag(sx, y2 - 22, "SG MW");
    for (int i = 0; i < RM_NLOOPS; i++) {
        textf(sx, y2 + 5 + i * 26, 10, INK, "%d", i + 1);
        readout(sx + 12, y2 + i * 26, 10, 3, "%3.0f", fmax(P->loop[i].sg.Q / 1e6, 0.0));
    }

    float y3 = f.y + 284;
    strip((Rectangle){f.x, y3, 352, f.y + f.height - y3}, TR_PHDR, 0, 20, "STEAM MPA", TR_FEED, 0, 1500, "FEED KG/S");
    mpr_draw((Rectangle){f.x + 360, y3, f.width - 360, f.y + f.height - y3}, &MP_SG, "SG STEAM TEMPERATURES", "300 C",
             "600 C");
}

/* an OPEN / CLOSE pushbutton pair with the valve's own lights: red open, green shut */
static int valve_pair(float x, float y, const char *name, int open)
{
    text(name, x, y + 5, 10, INK);
    int r = 0;
    if (lampbutton((Rectangle){x + 34, y, 30, 20}, "OPN", L_RED, open) && !open) r = 1;
    if (lampbutton((Rectangle){x + 66, y, 30, 20}, "CLS", L_GRN, !open) && open) r = -1;
    return r;
}

static void fw_bench(Rectangle bb)
{
    rm_tg *t = &P->tg;
    /* the feed train: condensate pumps, deaerator, feed pumps, HP heaters, on their header */
    float y = bb.y + 2, hy = bb.y + 112;
    const float px[7] = {42, 124, 214, 296, 378, 468, 558};
    Vector2 hdr[2] = {{bb.x, hy}, {bb.x + 600, hy}};
    mpipe(hdr, 2, MIM_WTR, 5, 1);
    for (int k = 0; k < 6; k++) {
        Vector2 st[2] = {{bb.x + px[k], y + 96}, {bb.x + px[k], hy}};
        mpipe(st, 2, MIM_WTR, 4, 0);
    }
    text("HOTWELL", bb.x, hy + 6, 10, INK);
    sym_tank((Rectangle){bb.x + 152, hy - 12, 32, 24}, MIM_WTR, "DA");
    Vector2 dn[3] = {{bb.x + 600, hy}, {bb.x + 620, hy}, {bb.x + 620, hy + 16}};
    mpipe(dn, 3, MIM_WTR, 5, 0);
    tri((Vector2){bb.x + 620, hy + 24}, (Vector2){bb.x + 613, hy + 14}, (Vector2){bb.x + 627, hy + 14}, MIM_WTR);
    text("TO SGS", bb.x + 630, hy + 6, 10, INK);

    pump_sw(bb.x + px[0], y, "COND A", &t->cond_pump[0], "CONDENSATE PUMP A");
    pump_sw(bb.x + px[1], y, "COND B", &t->cond_pump[1], "CONDENSATE PUMP B");
    pump_sw(bb.x + px[2], y, "TDFP A", &t->tdfp[0], "TURBINE FEED PUMP A");
    pump_sw(bb.x + px[3], y, "TDFP B", &t->tdfp[1], "TURBINE FEED PUMP B");
    pump_sw(bb.x + px[4], y, "MDFP", &t->mdfp, "MOTOR FEED PUMP");
    int s = cswitch(bb.x + px[5], y, "HP HTRS", "OUT", "IN", t->heaters_in, !t->heaters_in, SW_PISTOL, NULL);
    if (s) {
        t->heaters_in = s > 0;
        logmsg("HP FEED HEATERS %s", s > 0 ? "IN SERVICE" : "BYPASSED");
    }
    s = cswitch(bb.x + px[6], y, "FEEDWATER", "OUT", "IN", P->fw_on, !P->fw_on, SW_PISTOL, NULL);
    if (s > 0 && !P->fw_on) {
        P->fw_on = 1;
        logmsg("FEEDWATER IN SERVICE");
    }
    if (s < 0 && P->fw_on) {
        P->fw_on = 0;
        logmsg("FEEDWATER OUT OF SERVICE");
    }
    dymo(bb.x + 606, y + 8, "TDFP 60% EACH");
    dymo(bb.x + 606, y + 24, "NEED STEAM 4 MPA");
    dymo(bb.x + 606, y + 40, "MDFP 25% STARTUP");

    /* isolation valves and the feed regulating valve station of each SG */
    float vy = bb.y + 132;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *sg = &P->loop[i].sg;
        float x = bb.x + i * 108;
        int r = valve_pair(x, vy, "FIV", sg->fiv_open);
        if (r > 0) {
            if (sg->isolated) logmsg("SG %d IS BLOWN DOWN - CANNOT REOPEN", i + 1);
            else {
                sg->fiv_open = 1;
                logmsg("SG %d FEED ISOLATION VALVE OPEN", i + 1);
            }
        } else if (r < 0) {
            sg->fiv_open = 0;
            logmsg("SG %d FEED ISOLATION VALVE SHUT", i + 1);
        }
        r = valve_pair(x, vy + 26, "MSIV", sg->msiv_open);
        if (r > 0) {
            if (sg->isolated) logmsg("SG %d IS BLOWN DOWN - CANNOT REOPEN", i + 1);
            else if (P->aux.air_p < 3.0) logmsg("SG %d MSIV WILL NOT OPEN: NO INSTRUMENT AIR", i + 1);
            else {
                sg->msiv_open = 1;
                logmsg("SG %d MAIN STEAM ISOLATION VALVE OPEN", i + 1);
            }
        } else if (r < 0) {
            sg->msiv_open = 0;
            logmsg("SG %d MAIN STEAM ISOLATION VALVE SHUT", i + 1);
        }
        char nm[24];
        snprintf(nm, sizeof nm, "SG %d FEED", i + 1);
        int ev = mastation((Rectangle){x, bb.y + bb.height - MA_H, MA_W, MA_H}, nm, "STEAM C", C(sg->T_steam),
                           C(P->T_steam_set), 300, 600, sg->fw_valve / 1.3, sg->fw_manual ? 0 : 1);
        if ((ev & MA_AUTO) && sg->fw_manual) {
            sg->fw_manual = 0;
            logmsg("SG %d FEED CONTROL AUTO%s", i + 1, P->auto_fw ? "" : " - FEEDWATER MASTER IS IN MAN");
        }
        if ((ev & MA_MAN) && !sg->fw_manual) {
            sg->fw_manual = 1;
            logmsg("SG %d FEED CONTROL MANUAL", i + 1);
        }
        int hand = sg->fw_manual || !P->auto_fw;
        if (hand && (ev & MA_UP)) sg->fw_valve = fmin(1.3, sg->fw_valve + 0.013);
        if (hand && (ev & MA_DOWN)) sg->fw_valve = fmax(0.0, sg->fw_valve - 0.013);
        if (!hand && (ev & (MA_UP | MA_DOWN))) logmsg("SG %d FEED IS ON AUTO - PRESS M", i + 1);
    }

    /* feedwater master: sets the steam temperature the SG feed stations hold */
    float mx = bb.x + 4 * 108;
    double tsum = 0, vsum = 0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        tsum += P->loop[i].sg.T_steam / RM_NLOOPS;
        vsum += P->loop[i].sg.fw_valve / 1.3 / RM_NLOOPS;
    }
    int ev = mastation((Rectangle){mx, bb.y + bb.height - MA_H, MA_W, MA_H}, "FEED MASTER", "STEAM C", C(tsum),
                       C(P->T_steam_set), 300, 600, vsum, P->auto_fw);
    if ((ev & MA_AUTO) && !P->auto_fw) {
        P->auto_fw = 1;
        logmsg("FEEDWATER MASTER AUTO");
    }
    if ((ev & MA_MAN) && P->auto_fw) {
        P->auto_fw = 0;
        logmsg("FEEDWATER MASTER MANUAL - EVERY SG FEED VALVE BY HAND");
    }
    if (ev & MA_UP) P->T_steam_set = fmin(530.0 + 273.15, P->T_steam_set + 1.0);
    if (ev & MA_DOWN) P->T_steam_set = fmax(400.0 + 273.15, P->T_steam_set - 1.0);

    /* throttle pressure: the setpoint the turbine valves (on line) or the bypass (off line) hold */
    float tx = mx + 108;
    ev = mastation((Rectangle){tx, bb.y + bb.height - MA_H, MA_W, MA_H}, "THROTTLE PRESS", "MPA", P->p_header / 1e6,
                   P->p_set / 1e6, 0, 20, P->turbine_tripped ? P->bypass_valve : P->turbine_valve, -1);
    if (ev & MA_UP) P->p_set = fmin(15.5e6, P->p_set + 0.05e6);
    if (ev & MA_DOWN) P->p_set = fmax(8e6, P->p_set - 0.05e6);

    /* all MSIVs at once */
    float ax = tx + 108;
    if (pbguard((Rectangle){ax, bb.y + 218, bb.x + bb.width - ax, 40}, "ALL MSIV\nCLOSE", L_GRN, 0)) {
        for (int i = 0; i < RM_NLOOPS; i++) P->loop[i].sg.msiv_open = 0;
        logmsg("MAIN STEAM ISOLATION - ALL MSIVS SHUT");
    }
    int all = 1;
    for (int i = 0; i < RM_NLOOPS; i++) all &= !P->loop[i].sg.msiv_open;
    indicator((Rectangle){ax, bb.y + 270, bb.x + bb.width - ax, 30}, "ALL MSIVS\nSHUT", L_GRN, all);
    indicator((Rectangle){ax, bb.y + 306, bb.x + bb.width - ax, 30}, "AIR LOW\nVLVS FAIL", L_AMB, P->aux.air_p < 4.0);
}

/* ---- 3-2 turbine and condenser --------------------------------------------------------------------- */
static void turb_face(Rectangle f)
{
    rm_tg *t = &P->tg;
    int latched = !P->turbine_tripped;
    static const char *spl[5] = {"0", "500", "1000", "1500", "2000"};
    dial((Rectangle){f.x, f.y, 150, 150}, "TURBINE\nSPEED RPM", t->speed, 0, 2000, 4, 1790, 1810, 1950, 2000, spl);
    float x = f.x + 162;
    tag(x, f.y, "SPEED RPM");
    readout(x, f.y + 15, 18, 4, "%4.0f", t->speed);
    tag(x + 80, f.y, "REFERENCE");
    readout(x + 80, f.y + 15, 18, 4, "%4.0f", latched && !P->generator_breaker ? t->speed_ref : t->speed);
    tag(x + 160, f.y, "ACCEL RPM/MIN");
    readout(x + 160, f.y + 15, 18, 3, "%3.0f", t->accel);
    tag(x + 262, f.y, "CV %");
    readout(x + 262, f.y + 15, 18, 3, "%3.0f", 100 * P->turbine_valve * latched);
    indicator((Rectangle){x, f.y + 54, 70, 28}, "ACCEL", L_WHT,
              latched && !P->generator_breaker && fabs(t->speed - t->speed_ref) > 20);
    indicator((Rectangle){x + 76, f.y + 54, 70, 28}, "AT SPEED", L_WHT,
              latched && !P->generator_breaker && fabs(t->speed - t->speed_target) < 10);
    indicator((Rectangle){x + 152, f.y + 54, 70, 28}, "CRITICAL\nSPEED", L_AMB,
              t->speed > 900 && t->speed < 1300 && !P->generator_breaker);
    indicator((Rectangle){x + 228, f.y + 54, 70, 28}, "TRIPPED", L_GRN, !latched);
    indicator((Rectangle){x + 304, f.y + 54, 70, 28}, "LATCHED", L_RED, latched);
    /* stop and control valve lights */
    float vy = f.y + 94;
    demarc((Rectangle){x, vy, f.x + f.width - x, 58}, "STOP AND CONTROL VALVES");
    for (int i = 0; i < 4; i++) {
        float lx = x + 14 + i * 110;
        textf(lx, vy + 12, 10, INK, "SV%d", i + 1);
        lamp(lx + 36, vy + 17, 5, L_RED, latched);
        lamp(lx + 52, vy + 17, 5, L_GRN, !latched);
        int cv = latched && P->turbine_valve > 0.01;
        textf(lx, vy + 34, 10, INK, "CV%d", i + 1);
        lamp(lx + 36, vy + 39, 5, L_RED, cv);
        lamp(lx + 52, vy + 39, 5, L_GRN, !cv);
    }

    /* supervisory instruments and the condenser */
    float y2 = f.y + 186;
    plate_c(f.x + 100, y2 - 22, "TURBINE SUPERVISORY", 10);
    edgewz((Rectangle){f.x, y2, 44, 110}, "VIB\nMIL", t->vib, 0, 10, 0, 3, 7, 10, 5, NULL);
    edgewz((Rectangle){f.x + 52, y2, 44, 110}, "ECC\nMIL", t->ecc, 0, 10, 0, 1.5, 2, 10, 5, NULL);
    edgewz((Rectangle){f.x + 104, y2, 44, 110}, "BRG\nC", t->brg_T, 0, 120, 50, 90, 107, 120, 4, NULL);
    edgewz((Rectangle){f.x + 156, y2, 44, 110}, "OIL\nBAR", t->lube_p, 0, 3, 1.2, 2.5, 0, 0.6, 3, NULL);
    plate_c(f.x + 330, y2 - 22, "CONDENSER", 10);
    edgewz((Rectangle){f.x + 230, y2, 44, 110}, "VACUUM\nKPA ABS", P->p_cond / 1e3, 0, 30, 3, 8, 15, 30, 3, NULL);
    edgewz((Rectangle){f.x + 282, y2, 44, 110}, "CW FLOW\n%", 100 * P->cw_pump, 0, 120, 60, 110, 0, 0, 4, NULL);
    edgewz((Rectangle){f.x + 334, y2, 44, 110}, "AIR\nKG", t->cond_air, 0, 5, 0, 1, 3, 5, 5, NULL);
    edgewz((Rectangle){f.x + 386, y2, 44, 110}, "CW IN\nC", C(P->T_cw_in), 0, 40, 5, 30, 35, 40, 4, NULL);
    float rx = f.x + 446;
    tag(rx, y2, "BYPASS %");
    readout(rx, y2 + 15, 14, 3, "%3.0f", 100 * P->bypass_valve);
    tag(rx, y2 + 48, "COND KPA");
    readout(rx, y2 + 63, 14, 4, "%4.1f", P->p_cond / 1e3);
    text("15 KPA: BYPASS AND", rx, y2 + 96, 10, INK);
    text("LATCH BLOCKED", rx, y2 + 108, 10, INK);
    text("25 KPA: TURBINE TRIP", rx, y2 + 122, 10, INK);

    float y3 = f.y + 330;
    mpr_draw((Rectangle){f.x, y3, f.width, f.y + f.height - y3}, &MP_TURB, "TURBINE SUPERVISORY RECORDER", "0",
             "100% OF SCALE");
}

static void turb_bench(Rectangle bb)
{
    rm_tg *t = &P->tg;
    int latched = !P->turbine_tripped;
    float y = bb.y + 2;
    int s = cswitch(bb.x + 42, y, "TURBINE", "TRIP", "LATCH", latched, !latched, SW_PISTOL | SW_RED, NULL);
    if (s < 0 && latched) rm_plant_turbine_trip(P, "MANUAL");
    if (s > 0 && !latched) {
        char why[96];
        if (!rm_plant_turbine_latch(P, why, sizeof why)) logmsg("%s", why);
    }
    s = cswitch(bb.x + 124, y, "TURN GEAR", "OFF", "ENGAGE", t->turning_gear, !t->turning_gear, SW_PISTOL, NULL);
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
    s = cswitch(bb.x + 206, y, "AUX OIL PUMP", "STOP", "START", t->aux_oil_pump, !t->aux_oil_pump, SW_PISTOL, NULL);
    if (s) {
        t->aux_oil_pump = s > 0;
        logmsg("TURBINE AUX OIL PUMP %s", s > 0 ? "STARTED" : "STOPPED");
    }

    /* EHC speed control: selector pushbuttons, the lit one is in effect */
    float ex = bb.x + 256, ew = bb.x + bb.width - ex;
    demarc((Rectangle){ex, y + 6, ew, 176}, "EHC SPEED CONTROL");
    static const char *tgt[6] = {"OFF", "100", "500", "1000", "1500", "1800"};
    static const double tv[6] = {0, 100, 500, 1000, 1500, 1800};
    text("SPEED TARGET RPM", ex + 10, y + 18, 10, INK);
    float bw = (ew - 20) / 6;
    for (int i = 0; i < 6; i++) {
        int on = fabs(t->speed_target - tv[i]) < 1;
        if (lampbutton((Rectangle){ex + 10 + i * bw, y + 32, bw - 4, 30}, tgt[i], L_WHT, on) && !on) {
            t->speed_target = tv[i];
            logmsg("TURBINE SPEED TARGET %s RPM", tgt[i]);
        }
    }
    static const char *acc[4] = {"60", "120", "300", "600"};
    static const double av[4] = {60, 120, 300, 600};
    text("ACCELERATION RPM/MIN", ex + 10, y + 74, 10, INK);
    for (int i = 0; i < 4; i++) {
        int on = fabs(t->accel - av[i]) < 1;
        if (lampbutton((Rectangle){ex + 10 + i * bw, y + 88, bw - 4, 30}, acc[i], L_WHT, on) && !on) {
            t->accel = av[i];
            logmsg("TURBINE ACCELERATION %s RPM/MIN", acc[i]);
        }
    }
    dymo(ex + 10 + 4 * bw, y + 90, "HOLD CLEAR OF");
    dymo(ex + 10 + 4 * bw, y + 106, "900-1300 RPM");
    text("GO TO 1800 AND CLOSE THE FIELD BREAKER", ex + 10, y + 134, 10, INK);
    text("(3-3) BEFORE SYNCHRONISING.", ex + 10, y + 148, 10, INK);

    /* steam path mimic: main steam, stop and control valves, turbine, condenser; bypass */
    float my = bb.y + 140;
    Vector2 ms[2] = {{bb.x, my}, {bb.x + 150, my}};
    mpipe(ms, 2, MIM_STM, 6, 1);
    text("MAIN STEAM", bb.x + 4, my - 16, 10, INK);
    sym_valve((Vector2){bb.x + 60, my}, 7, 0, INK, latched);
    text("SV", bb.x + 54, my + 10, 10, INK);
    sym_valve((Vector2){bb.x + 104, my}, 7, 0, INK, latched && P->turbine_valve > 0.01);
    text("CV", bb.x + 98, my + 10, 10, INK);
    /* turbine casing: a trapezoid, HP end small */
    Vector2 a = {bb.x + 150, my - 12}, b = {bb.x + 230, my - 26}, c2 = {bb.x + 230, my + 26}, d = {bb.x + 150, my + 12};
    tri(a, b, c2, (Color){60, 64, 60, 255});
    tri(a, c2, d, (Color){60, 64, 60, 255});
    DrawLineEx(a, b, 2, INK);
    DrawLineEx(b, c2, 2, INK);
    DrawLineEx(c2, d, 2, INK);
    DrawLineEx(d, a, 2, INK);
    ctext("HP-LP", bb.x + 190, my - 5, 10, (Color){230, 230, 220, 255});
    Vector2 sh[2] = {{bb.x + 230, my}, {bb.x + 240, my}};
    mimic_poly(sh, 2, 6, INK);
    DrawCircleV((Vector2){bb.x + 252, my}, 12, (Color){60, 64, 60, 255});
    DrawCircleLinesV((Vector2){bb.x + 252, my}, 12, INK);
    ctext("G", bb.x + 252, my - 5, 10, (Color){230, 230, 220, 255});
    Vector2 ex2[2] = {{bb.x + 200, my + 22}, {bb.x + 200, my + 44}};
    mpipe(ex2, 2, MIM_STM, 6, 0);
    sym_tank((Rectangle){bb.x + 130, my + 44, 120, 28}, MIM_WTR, "CONDENSER");
    Vector2 bp[3] = {{bb.x + 30, my}, {bb.x + 30, my + 58}, {bb.x + 130, my + 58}};
    mpipe(bp, 3, MIM_STM, 4, 1);
    sym_valve((Vector2){bb.x + 80, my + 58}, 6, 0, INK, P->bypass_valve > 0.02);
    text("BYPASS", bb.x + 60, my + 68, 10, INK);

    /* circulating water and condenser vacuum pumps, on the cooling water line */
    float cy = bb.y + 228;
    Vector2 cw[2] = {{bb.x, cy + 67}, {bb.x + 420, cy + 67}};
    mpipe(cw, 2, MIM_WTR, 5, 1);
    for (int i = 0; i < 3; i++) {
        char nm[16], wh[32];
        snprintf(nm, sizeof nm, "CW PUMP %c", 'A' + i);
        snprintf(wh, sizeof wh, "CIRC WATER PUMP %c", 'A' + i);
        pump_sw(bb.x + 42 + i * 82, cy, nm, &t->cw[i], wh);
    }
    for (int i = 0; i < 2; i++) {
        char nm[16], wh[32];
        snprintf(nm, sizeof nm, "VAC PUMP %c", 'A' + i);
        snprintf(wh, sizeof wh, "CONDENSER VACUUM PUMP %c", 'A' + i);
        pump_sw(bb.x + 300 + i * 82, cy, nm, &t->vac_pump[i], wh);
    }
    text("CIRCULATING WATER", bb.x + 2, cy + 104, 10, INK);
    text("AIR REMOVAL", bb.x + 262, cy + 104, 10, INK);
    float rx = bb.x + 470;
    tag(rx, cy + 6, "COND KPA ABS");
    readout(rx, cy + 21, 14, 4, "%4.1f", P->p_cond / 1e3);
    tag(rx, cy + 54, "AIR IN COND KG");
    readout(rx, cy + 69, 14, 4, "%4.2f", t->cond_air);
    dymo(rx, cy + 106, "VAC PUMPS PULL THE AIR OUT");
}

/* ---- 3-3 generator ------------------------------------------------------------------------------------- */
static void synchroscope(Vector2 c, float R)
{
    rm_tg *t = &P->tg;
    DrawRectangleRounded((Rectangle){c.x - R - 10, c.y - R - 10, 2 * R + 20, 2 * R + 20}, 0.1f, 6, BEZEL);
    DrawCircleV(c, R, FACE);
    for (int i = 0; i < 24; i++) {
        float a = (float)(i * 15.0 * DEG2RAD);
        float l = i % 6 ? 5.0f : 10.0f;
        DrawLineEx((Vector2){c.x + R * sinf(a), c.y - R * cosf(a)}, (Vector2){c.x + (R - l) * sinf(a), c.y - (R - l) * cosf(a)},
                   1.5f, INK);
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
        Vector2 lp = {c.x + k * (R + 26), c.y - R + 12};
        DrawCircleV(lp, 10, BEZEL);
        DrawCircleV(lp, 7,
                    (Color){(unsigned char)(70 + 185 * b), (unsigned char)(66 + 175 * b), (unsigned char)(60 + 150 * b), 255});
    }
    ctext("SYNC", c.x - R - 26, c.y - R + 26, 10, INK);
    ctext("LAMPS", c.x - R - 26, c.y - R + 38, 10, INK);
}

static void gen_face(Rectangle f)
{
    rm_tg *t = &P->tg;
    float pitch = f.width / 4;
    static const char *mvl[5] = {"-400", "-200", "0", "200", "400"};
    static const char *hzl[5] = {"56", "58", "60", "62", "64"};
    dial((Rectangle){f.x + 3, f.y, pitch - 6, pitch - 6}, "GENERATOR\nMW", P->P_gen / 1e6, 0, 1200, 4, 0, 1000, 1050,
         1200, NULL);
    dial((Rectangle){f.x + pitch + 3, f.y, pitch - 6, pitch - 6}, "MVAR", t->mvar, -400, 400, 4, -150, 250, 300, 400, mvl);
    dial((Rectangle){f.x + 2 * pitch + 3, f.y, pitch - 6, pitch - 6}, "GENERATOR\nKV", t->gen_kv, 0, 30, 3, 21, 23, 25, 30,
         NULL);
    dial((Rectangle){f.x + 3 * pitch + 3, f.y, pitch - 6, pitch - 6}, "FREQUENCY\nHZ", t->gen_hz, 56, 64, 4, 59.8, 60.2,
         62.5, 64, hzl);

    float y2 = f.y + pitch + 6;
    synchroscope((Vector2){f.x + 150, y2 + 78}, 62);
    float x = f.x + 270;
    tag(x, y2, "RUNNING KV / HZ");
    readout(x, y2 + 15, 12, 4, "%4.1f", t->gen_kv);
    readout(x + 56, y2 + 15, 12, 5, "%5.2f", t->gen_hz);
    tag(x, y2 + 46, "INCOMING KV / HZ");
    readout(x, y2 + 61, 12, 4, "%4.1f", P->grid_ok && P->grid_breaker ? 22.0 : 0.0);
    readout(x + 56, y2 + 61, 12, 5, "%5.2f", P->grid_ok && P->grid_breaker ? 60.0 : 0.0);
    tag(x, y2 + 92, "GROSS MWE");
    readout(x, y2 + 107, 14, 4, "%4.0f", P->P_gen / 1e6);
    tag(x + 120, y2 + 92, "NET MWE");
    readout(x + 120, y2 + 107, 14, 4, "%4.0f", P->P_net / 1e6);
    tag(x + 120, y2, "HOUSE MW");
    readout(x + 120, y2 + 15, 12, 3, "%3.0f", P->P_house / 1e6);
    tag(x + 120, y2 + 46, "MVAR");
    readout(x + 120, y2 + 61, 12, 4, "%4.0f", t->mvar);

    float y3 = f.y + 300;
    strip((Rectangle){f.x, y3, f.width, f.y + f.height - y3}, TR_GENMW, 0, 1200, "GEN MW", TR_SPEED, 0, 2000, "RPM");
}

static void gen_bench(Rectangle bb)
{
    rm_tg *t = &P->tg;
    float y = bb.y + 2;
    /* the synchronising switches: generator breaker, field breaker */
    int s = cswitch(bb.x + 42, y, "GEN BKR", "TRIP", "CLOSE", P->generator_breaker, !P->generator_breaker, SW_JHANDLE, NULL);
    if (s > 0 && !P->generator_breaker) {
        char why[96];
        if (!rm_plant_gen_breaker_close(P, why, sizeof why)) logmsg("%s", why);
    }
    if (s < 0 && P->generator_breaker) {
        P->generator_breaker = 0;
        logmsg("GENERATOR BREAKER OPENED - TURBINE ON SPEED CONTROL");
    }
    s = cswitch(bb.x + 124, y, "FIELD BKR", "TRIP", "CLOSE", t->field_breaker, !t->field_breaker, SW_JHANDLE, NULL);
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
    text("CLOSE AT 12 O'CLOCK", bb.x + 6, y + 104, 10, INK);
    text("WITH THE POINTER", bb.x + 6, y + 116, 10, INK);
    text("TURNING SLOW (FAST).", bb.x + 6, y + 128, 10, INK);

    /* automatic synchroniser */
    float ax = bb.x + 176, aw = bb.x + bb.width - ax;
    demarc((Rectangle){ax, y + 6, aw, 128}, "AUTO SYNCHRONISER");
    if (lampbutton((Rectangle){ax + 10, y + 22, 100, 40}, "AUTO SYNC", L_WHT, t->auto_sync)) {
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
    float iw = (aw - 130) / 2;
    indicator((Rectangle){ax + 120, y + 22, iw - 4, 26}, "FREQ MATCH", L_WHT,
              !P->generator_breaker && fabs(t->gen_hz - 60.0) < 0.2);
    indicator((Rectangle){ax + 120 + iw, y + 22, iw - 4, 26}, "VOLTS MATCH", L_WHT,
              !P->generator_breaker && fabs(t->gen_kv - 22.0) < 1.5 && t->field_breaker);
    indicator((Rectangle){ax + 120, y + 54, iw - 4, 26}, "IN PHASE", L_WHT,
              !P->generator_breaker && fabs(t->phase) < 10.0 && t->speed > 1500);
    indicator((Rectangle){ax + 120 + iw, y + 54, iw - 4, 26}, "ON LINE", L_RED, P->generator_breaker);
    text("AUTO SYNC MATCHES SPEED AND CLOSES", ax + 10, y + 92, 10, INK);
    text("THE GENERATOR BREAKER IN PHASE.", ax + 10, y + 106, 10, INK);

    /* voltage regulator */
    int ev = mastation((Rectangle){bb.x + 6, bb.y + bb.height - MA_H, MA_W, MA_H}, "VOLTAGE REG", "KV", t->gen_kv, 22.0,
                       0, 30, t->exc / 1.4, t->avr_auto);
    if ((ev & MA_AUTO) && !t->avr_auto) {
        t->avr_auto = 1;
        logmsg("VOLTAGE REGULATOR AUTO");
    }
    if ((ev & MA_MAN) && t->avr_auto) {
        t->avr_auto = 0;
        logmsg("VOLTAGE REGULATOR MANUAL - EXCITER BY HAND");
    }
    if (!t->avr_auto && t->field_breaker && (ev & MA_UP)) t->exc = fmin(1.4, t->exc + 0.01);
    if (!t->avr_auto && t->field_breaker && (ev & MA_DOWN)) t->exc = fmax(0.0, t->exc - 0.01);
    if (t->avr_auto && (ev & (MA_UP | MA_DOWN))) logmsg("VOLTAGE REGULATOR IS ON AUTO - PRESS M");
    float ex = bb.x + 110;
    tag(ex, bb.y + bb.height - MA_H + 20, "EXCITATION PU");
    readout(ex, bb.y + bb.height - MA_H + 35, 14, 4, "%4.2f", t->exc);
    text("AUTO HOLDS 22 KV", ex, bb.y + bb.height - MA_H + 70, 10, INK);
    text("OFF LINE, 1.02 PU ON", ex, bb.y + bb.height - MA_H + 82, 10, INK);

    /* the load dispatcher */
    float dx = bb.x + 250, dy = bb.y + 160, dw = bb.x + bb.width - dx;
    demarc((Rectangle){dx, dy, dw, bb.y + bb.height - dy - 4}, "LOAD DISPATCH");
    tag(dx + 10, dy + 16, "ORDERED MWE");
    if (G.on_line) readout(dx + 10, dy + 31, 16, 4, "%4.0f", G.demand_target);
    else readout(dx + 10, dy + 31, 16, 4, "----");
    tag(dx + 130, dy + 16, "DISPATCH MWE");
    if (G.on_line) readout(dx + 130, dy + 31, 16, 4, "%4.0f", G.demand);
    else readout(dx + 130, dy + 31, 16, 4, "----");
    tag(dx + 10, dy + 72, "NET MWE");
    readout(dx + 10, dy + 87, 16, 4, "%4.0f", fmax(P->P_net / 1e6, 0.0));
    double dev = G.on_line && P->generator_breaker ? rm_game_deviation(&G, P) : 0.0;
    indicator((Rectangle){dx + 130, dy + 70, 58, 30}, "RAISE", L_AMB, dev < -0.02);
    indicator((Rectangle){dx + 192, dy + 70, 58, 30}, "LOWER", L_AMB, dev > 0.02);
    indicator((Rectangle){dx + 130, dy + 106, 120, 26}, "ON TARGET", L_WHT, G.on_line && P->generator_breaker && fabs(dev) <= 0.02);
    tag(dx + 10, dy + 128, "MWH SENT");
    readout(dx + 10, dy + 143, 12, 6, "%6.0f", fmin(G.mwh, 999999.0));
}

void draw_tg(void)
{
    static const secdef S[3] = {{"3-1", "FEEDWATER AND MAIN STEAM", 740, SEC_FW, 7},
                                {"3-2", "TURBINE AND CONDENSER", 640, SEC_TURB, 6},
                                {"3-3", "GENERATOR", 532, SEC_GEN, 5}};
    secrect R[3];
    board_frame(S, 3, R);
    fw_face(R[0].face);
    fw_bench(R[0].bench);
    turb_face(R[1].face);
    turb_bench(R[1].bench);
    gen_face(R[2].face);
    gen_bench(R[2].bench);
    board_strip();
}
