/*
 * Remote shutdown panel: a small board in a separate room, with just enough
 * to trip the reactor and hold it in hot shutdown when the main control room
 * has to be abandoned. Its controls only work once the transfer key switch
 * has taken control from the main control room, and while it has control
 * the main boards are dead.
 *
 *   RSP-1  REACTOR AND DECAY HEAT REMOVAL
 *          control transfer, reactor scram, primary pumps and pony motors,
 *          DRACS dampers, with the meters to watch them by.
 *   RSP-2  EMERGENCY POWER, STEAM AND FEED
 *          diesels, SG isolation, feedwater and turbine trip, and the
 *          evacuation procedure card.
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static double C(double T_K) { return T_K - 273.15; }

static void rsp1_face(Rectangle f)
{
    rm_core *c = &P->core;
    rm_aux *a = &P->aux;
    char b[24];
    float y = f.y + 24;
    plate_c(f.x + 48, f.y, "NUCLEAR", 10);
    static const char *lpl[6] = {"-8", "-6", "-4", "-2", "0", "2"};
    edgewz((Rectangle){f.x, y, 44, 130}, "LOG\nPWR %", log10(fmax(100 * c->p_thermal / RM_P_RATED, 1e-8)), -8, 2, -8, 2,
           1.9, 2, 5, lpl);
    static const char *srml[4] = {".1", "10", "1E3", "1E5"};
    edgewz((Rectangle){f.x + 52, y, 44, 130}, "SRM\nCPS", log10(fmax(rm_plant_srm_cps(P), 0.1)), -1, 5, 0, 0, 4.5, 5, 3,
           srml);
    float rx = f.x + 110;
    tag(rx, y, "PERIOD S");
    if (isfinite(P->period) && fabs(P->period) < 999) readout(rx, y + 15, 14, 4, "%5.0f", P->period);
    else readout(rx, y + 15, 14, 4, "----");
    tag(rx, y + 48, "DECAY MW");
    readout(rx, y + 63, 14, 3, "%3.0f", c->p_decay / 1e6);
    tag(rx, y + 96, "MWT");
    readout(rx, y + 111, 14, 4, "%4.0f", c->p_thermal / 1e6);

    float cx = f.x + 200;
    plate_c(cx + 98, f.y, "REACTOR COOLANT", 10);
    edgewz((Rectangle){cx, y, 44, 130}, "CORE\nOUT C", C(P->T_core_out), 200, 700, 380, 560, 600, 700, 5, NULL);
    edgewz((Rectangle){cx + 52, y, 44, 130}, "CORE\nIN C", C(P->T_core_in), 200, 700, 330, 400, 0, 0, 5, NULL);
    edgewz((Rectangle){cx + 104, y, 44, 130}, "CORE\nFLOW %", 100 * P->W_core / rm_plant_nominal_flow(), 0, 125, 5, 105, 0,
           0, 5, NULL);
    edgewz((Rectangle){cx + 156, y, 44, 130}, "NA LVL\nMM", a->na_level, -400, 400, -100, 100, -400, -300, 4, NULL);

    float px = f.x + 420;
    plate_c(px + 98, f.y, "PRIMARY PUMP SPEED %", 10);
    for (int i = 0; i < RM_NLOOPS; i++) {
        snprintf(b, sizeof b, "PP-%d", i + 1);
        edgewz((Rectangle){px + i * 52, y, 44, 130}, b, 100 * P->loop[i].ppump.speed, 0, 125, 5, 105, 0, 0, 5, NULL);
    }
    float dx = f.x + 640;
    plate_c(dx + 72, f.y, "DRACS HEAT MW", 10);
    for (int i = 0; i < 3; i++) {
        snprintf(b, sizeof b, "DRACS %c", 'A' + i);
        edgewz((Rectangle){dx + i * 52, y, 44, 130}, b, P->Q_dracs_train[i] / 1e6, 0, 30, 5, 30, 0, 0, 3, NULL);
    }
    float tx = dx + 164;
    tag(tx, y, "TOTAL MW");
    readout(tx, y + 15, 14, 4, "%4.1f", P->Q_dracs / 1e6);
    tag(tx, y + 48, "NAT CIRC");
    readout(tx, y + 63, 14, 4, "%4.0f", P->W_dracs);

    float y2 = f.y + 190;
    float iw = f.width / 5;
    indicator((Rectangle){f.x, y2, iw - 8, 34}, "ALL RODS IN\nSCRAMMED", L_GRN, c->scram);
    indicator((Rectangle){f.x + iw, y2, iw - 8, 34}, "REACTOR\nNOT TRIPPED", L_RED, !c->scram && blink_fast());
    indicator((Rectangle){f.x + 2 * iw, y2, iw - 8, 34}, "RSP IN\nCONTROL", L_AMB, a->rsp_control);
    indicator((Rectangle){f.x + 3 * iw, y2, iw - 8, 34}, "MAIN CONTROL\nRM EVACUATED", L_RED, a->evacuated && blink_fast());
    indicator((Rectangle){f.x + 4 * iw, y2, iw - 8, 34}, "CONTROL ROOM\nFIRE", L_RED, a->fire[RM_FIRE_CR]);
    float y3 = y2 + 48;
    strip((Rectangle){f.x, y3, f.width / 2 - 5, f.y + f.height - y3}, TR_LOGPWR, -8, 2, "LOG POWER", TR_PERIOD, -1, 5,
          "SUR DPM");
    strip((Rectangle){f.x + f.width / 2 + 5, y3, f.width / 2 - 5, f.y + f.height - y3}, TR_OUTLET, 300, 700, "CORE OUT C",
          TR_FLOW, 0, 125, "CORE FLOW %");
}

static void rsp1_bench(Rectangle bb, int ok)
{
    rm_core *c = &P->core;
    rm_aux *a = &P->aux;
    /* control transfer: always works, it is how the panel takes over */
    input_ok = 1;
    demarc((Rectangle){bb.x, bb.y + 8, 270, 150}, "CONTROL TRANSFER");
    if (keysw(bb.x + 60, bb.y + 70, "MCR", "RSP", a->rsp_control)) {
        a->rsp_control = !a->rsp_control;
        logmsg(a->rsp_control ? "CONTROL TRANSFERRED TO THE REMOTE SHUTDOWN PANEL"
                              : "CONTROL RETURNED TO THE MAIN CONTROL ROOM");
    }
    text("TRANSFER", bb.x + 36, bb.y + 92, 10, INK);
    indicator((Rectangle){bb.x + 120, bb.y + 26, 140, 30}, "RSP IN CONTROL", L_AMB, a->rsp_control);
    indicator((Rectangle){bb.x + 120, bb.y + 62, 140, 30}, "MCR IN CONTROL", L_WHT, !a->rsp_control);
    int can_return = a->evacuated && !a->fire[RM_FIRE_CR];
    if (lampbutton((Rectangle){bb.x + 120, bb.y + 104, 140, 40}, "CREW RETURN\nTO MAIN CONTROL RM", L_WHT, can_return) &&
        can_return) {
        a->evacuated = 0;
        logmsg("CREW BACK IN THE MAIN CONTROL ROOM - TRANSFER CONTROL BACK WITH THE KEY");
    }
    input_ok = ok && a->rsp_control;

    /* reactor scram */
    float sy = bb.y + 172;
    demarc((Rectangle){bb.x, sy, 270, bb.y + bb.height - sy - 4}, "REACTOR SCRAM");
    if (pbround((Vector2){bb.x + 80, sy + 70}, 34, "REACTOR SCRAM", (Color){206, 28, 22, 255}, (Color){238, 196, 30, 255}, 0)) {
        rm_plant_manual_scram(P);
        logmsg("REACTOR SCRAMMED FROM THE REMOTE SHUTDOWN PANEL");
    }
    indicator((Rectangle){bb.x + 150, sy + 30, 110, 34}, "ALL RODS IN", L_GRN, c->scram);
    indicator((Rectangle){bb.x + 150, sy + 72, 110, 34}, "NOT TRIPPED", L_RED, !c->scram);
    text("BOTH RPS DIVISIONS", bb.x + 150, sy + 118, 10, INK);

    /* primary pumps and pony motors */
    float px = bb.x + 290;
    demarc((Rectangle){px, bb.y + 8, 400, 186}, "PRIMARY PUMPS");
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_pump *pm = &P->loop[i].ppump;
        char nm[16];
        snprintf(nm, sizeof nm, "PP-%d", i + 1);
        int run = pm->motor_on && !pm->tripped;
        float cx = px + 52 + i * 98;
        int s = cswitch3(cx, bb.y + 20, nm, "STOP", "START", run, !run, SW_PISTOL, NULL, L_AMB, pm->tripped);
        if (s > 0 && !run) {
            pm->tripped = 0;
            pm->motor_on = 1;
            if (pm->speed_set < 0.1) pm->speed_set = 1.0;
            logmsg("PRIMARY PUMP %d STARTED (RSP)", i + 1);
        } else if (s < 0 && (run || pm->tripped)) {
            pm->motor_on = 0;
            pm->tripped = 0;
            logmsg("PRIMARY PUMP %d STOPPED (RSP)", i + 1);
        }
        if (lampbutton((Rectangle){cx - 36, bb.y + 124, 72, 26}, "PONY", L_WHT, pm->pony_on)) {
            pm->pony_on = !pm->pony_on;
            logmsg("PONY MOTOR %d %s (RSP)", i + 1, pm->pony_on ? "ARMED" : "OFF");
        }
        readout(cx - 30, bb.y + 158, 11, 5, "%5.0f", P->loop[i].W);
    }
    text("KG/S", px + 358, bb.y + 178, 10, INK);

    /* decay heat removal */
    float dy = bb.y + 206;
    demarc((Rectangle){px, dy, 400, bb.y + bb.height - dy - 4}, "DECAY HEAT REMOVAL - DRACS DAMPERS");
    for (int i = 0; i < 3; i++) {
        char nm[24];
        snprintf(nm, sizeof nm, "DRACS %c", 'A' + i);
        int open = P->dracs_damper[i] > 0.5;
        float cx = px + 52 + i * 98;
        int s = cswitch(cx, dy + 14, nm, "CLOSE", "OPEN", open, !open, SW_PISTOL, NULL);
        if (s) {
            P->dracs_man[i] = 1;
            P->dracs_damper_set[i] = s > 0 ? 1.0 : 0.0;
            logmsg("DRACS %c DAMPER %s (RSP)", 'A' + i, s > 0 ? "OPEN" : "CLOSED");
        }
        readout(cx - 24, dy + 118, 11, 3, "%3.0f", 100 * P->dracs_damper[i]);
        text("%", cx + 20, dy + 122, 10, INK);
    }
    if (lampbutton((Rectangle){px + 300, dy + 40, 90, 40}, "AUTO\nON TRIP", L_WHT, P->dracs_auto) && !P->dracs_auto) {
        P->dracs_auto = 1;
        for (int i = 0; i < 3; i++) P->dracs_man[i] = 0;
        logmsg("DRACS AUTO (RSP)");
    }

    /* the numbers the procedure asks for */
    float rx = bb.x + 710;
    tag(rx, bb.y + 12, "CORE OUT C");
    readout(rx, bb.y + 27, 18, 3, "%3.0f", C(P->T_core_out));
    tag(rx + 100, bb.y + 12, "CORE IN C");
    readout(rx + 100, bb.y + 27, 18, 3, "%3.0f", C(P->T_core_in));
    tag(rx, bb.y + 68, "CORE FLOW %");
    readout(rx, bb.y + 83, 18, 3, "%3.0f", 100 * P->W_core / rm_plant_nominal_flow());
    tag(rx + 100, bb.y + 68, "DRACS MW");
    readout(rx + 100, bb.y + 83, 18, 4, "%4.1f", P->Q_dracs / 1e6);
    tag(rx, bb.y + 124, "DECAY MW");
    readout(rx, bb.y + 139, 18, 3, "%3.0f", c->p_decay / 1e6);
    tag(rx + 100, bb.y + 124, "NA LEVEL MM");
    readout(rx + 100, bb.y + 139, 18, 4, "%4.0f", a->na_level);
    dymo(rx, bb.y + 190, "HOLD CORE OUTLET BELOW 550 C");
    dymo(rx, bb.y + 208, "PONY MOTORS ARMED, DRACS OPEN");
}

static void rsp2_face(Rectangle f)
{
    char b[32];
    for (int i = 0; i < 3; i++) {
        snprintf(b, sizeof b, "DG %d\nMW", i + 1);
        dial((Rectangle){f.x + i * 128 + 2, f.y, 120, 120}, b, P->tg.dg_mw[i], 0, 6, 6, 0, 4.5, 5.5, 6, NULL);
    }
    dial((Rectangle){f.x + 3 * 128 + 2, f.y, 120, 120}, "STEAM HEADER\nPRESS MPA", P->p_header / 1e6, 0, 20, 4, 13, 15,
         16.5, 20, NULL);
    double wf = 0;
    for (int i = 0; i < RM_NLOOPS; i++) wf += P->loop[i].sg.W_fw;
    static const char *x100[5] = {"0", "3", "6", "9", "12"};
    dial((Rectangle){f.x + 4 * 128 + 2, f.y, 120, 120}, "FEED FLOW\nKG/S X100", wf / 100, 0, 12, 4, 0, 0, 0, 0, x100);
    float hx = f.x + 5 * 128 + 12;
    plate_c(hx + 98, f.y, "H2 IN SODIUM PPM", 10);
    static const char *h2l[5] = {"0", ".25", ".5", ".75", "1"};
    for (int i = 0; i < RM_NLOOPS; i++) {
        snprintf(b, sizeof b, "SG %d", i + 1);
        edgewz((Rectangle){hx + i * 52, f.y + 24, 44, 100}, b, P->loop[i].sg.h2, 0, 1, 0, 0.15, RM_H2_ALARM, 1, 4, h2l);
    }

    /* the procedure card, laminated and screwed to the panel */
    float y2 = f.y + 160;
    Rectangle pr = {f.x, y2, 620, f.y + f.height - y2};
    DrawRectangleRec(pr, (Color){236, 232, 214, 255});
    DrawRectangleLinesEx(pr, 2, BEZEL);
    screw(pr.x + 8, pr.y + 8);
    screw(pr.x + pr.width - 8, pr.y + 8);
    screw(pr.x + 8, pr.y + pr.height - 8);
    screw(pr.x + pr.width - 8, pr.y + pr.height - 8);
    static const char *proc[] = {
        "EOP-9  CONTROL ROOM EVACUATION",
        "",
        "1. IF POSSIBLE, SCRAM THE REACTOR FROM THE MAIN CONTROL BOARD BEFORE LEAVING.",
        "2. AT THE RSP, TURN THE TRANSFER KEY TO RSP. THE MAIN BOARDS GO DEAD.",
        "3. SCRAM THE REACTOR IF IT IS NOT ALREADY. CONFIRM LOG POWER FALLING.",
        "4. TRIP THE TURBINE. CHECK THE DIESELS AND THE ESSENTIAL BUSES.",
        "5. ARM THE PONY MOTORS. OPEN THE DRACS DAMPERS.",
        "6. HOLD CORE OUTLET BELOW 550 C. KEEP FEED ON THE SGS OR ISOLATE THEM.",
        "7. AN SG WITH HIGH HYDROGEN: ISOLATE IT (RSP-2).",
        "8. WHEN THE FIRE IS OUT, THE CREW RETURNS TO THE CONTROL ROOM;",
        "   THEN TURN THE KEY BACK TO MCR.",
    };
    for (int i = 0; i < 11; i++) text(proc[i], pr.x + 22, pr.y + 16 + i * 22, 10, i ? INK : (Color){150, 20, 10, 255});

    float ix = f.x + 640, iw = f.x + f.width - ix;
    indicator((Rectangle){ix, y2, iw, 36}, "HOUSE BUSES\nLIVE", L_WHT, P->offsite_power);
    indicator((Rectangle){ix, y2 + 44, iw, 36}, "ESSENTIAL BUSES\nLIVE", L_WHT, rm_plant_essential_power(P));
    indicator((Rectangle){ix, y2 + 88, iw, 36}, "TURBINE\nTRIPPED", L_GRN, P->turbine_tripped);
    indicator((Rectangle){ix, y2 + 132, iw, 36}, "FEEDWATER\nIN SERVICE", L_RED, P->fw_on);
    int h2 = 0;
    for (int i = 0; i < RM_NLOOPS; i++) h2 |= P->loop[i].sg.h2 > RM_H2_ALARM;
    indicator((Rectangle){ix, y2 + 176, iw, 36}, "SG H2 HIGH", L_RED, h2 && blink_fast());
    indicator((Rectangle){ix, y2 + 220, iw, 36}, "DIESEL GEN\nRUNNING", L_WHT,
              P->diesel_running[0] || P->diesel_running[1] || P->diesel_running[2]);
}

static void rsp2_bench(Rectangle bb)
{
    /* emergency power */
    demarc((Rectangle){bb.x, bb.y + 8, 300, 176}, "EMERGENCY POWER");
    for (int i = 0; i < 3; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "DG %d", i + 1);
        float cx = bb.x + 52 + i * 98;
        int s = cswitch3(cx, bb.y + 20, nm, "STOP", "START", P->diesel_running[i], !P->diesel_running[i], SW_PISTOL,
                         &P->diesel_ptl[i], L_WHT, P->diesel_t[i] > 0 && !P->diesel_running[i]);
        if (s > 0 && !P->diesel_manual[i]) {
            P->diesel_manual[i] = 1;
            logmsg("DIESEL %d START (RSP)", i + 1);
        }
        if (s < 0) P->diesel_manual[i] = 0;
        readout(cx - 26, bb.y + 124, 12, 3, "%3.1f", P->tg.dg_mw[i]);
        text("MW", cx + 16, bb.y + 128, 10, INK);
    }
    text("RIGHT-CLICK: PULL TO LOCK", bb.x + 10, bb.y + 160, 10, INK);

    /* steam generator isolation */
    float sx = bb.x + 320;
    demarc((Rectangle){sx, bb.y + 8, 400, 176}, "STEAM GENERATOR ISOLATION");
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &P->loop[i].sg;
        char nm[16];
        snprintf(nm, sizeof nm, "SG %d ISOL", i + 1);
        float cx = sx + 52 + i * 98;
        int k = cswitch(cx, bb.y + 20, nm, "NORM", "ISOL", s->isolated, !s->isolated, SW_JHANDLE, NULL);
        if (k > 0 && !s->isolated) rm_plant_isolate_sg(P, i);
        if (k < 0 && s->isolated) logmsg("SG %d STAYS BLOWN DOWN UNTIL THE LOOP IS REFILLED", i + 1);
        readout(cx - 26, bb.y + 124, 12, 4, "%4.2f", fmin(s->h2, 99.99));
        text("H2", cx + 26, bb.y + 128, 10, INK);
    }
    text("ISOLATE: FEED AND STEAM SHUT, WATER SIDE BLOWN DOWN", sx + 10, bb.y + 160, 10, INK);

    /* feedwater and turbine */
    float fy = bb.y + 198;
    demarc((Rectangle){bb.x, fy, 400, bb.y + bb.height - fy - 4}, "FEEDWATER - TURBINE");
    int s = cswitch(bb.x + 60, fy + 14, "MDFP", "STOP", "START", P->tg.mdfp, !P->tg.mdfp, SW_PISTOL, NULL);
    if (s) {
        P->tg.mdfp = s > 0;
        logmsg("MOTOR FEED PUMP %s (RSP)", s > 0 ? "STARTED" : "STOPPED");
    }
    s = cswitch(bb.x + 160, fy + 14, "FEEDWATER", "OUT", "IN", P->fw_on, !P->fw_on, SW_PISTOL, NULL);
    if (s) {
        P->fw_on = s > 0;
        logmsg("FEEDWATER %s (RSP)", s > 0 ? "IN SERVICE" : "OUT OF SERVICE");
    }
    s = cswitch(bb.x + 260, fy + 14, "TURBINE", "TRIP", "-", !P->turbine_tripped, P->turbine_tripped, SW_PISTOL | SW_RED,
                NULL);
    if (s < 0 && !P->turbine_tripped) rm_plant_turbine_trip(P, "REMOTE SHUTDOWN PANEL");
    double wf = 0;
    for (int i = 0; i < RM_NLOOPS; i++) wf += P->loop[i].sg.W_fw;
    tag(bb.x + 310, fy + 20, "FEED KG/S");
    readout(bb.x + 310, fy + 35, 14, 4, "%4.0f", wf);
    tag(bb.x + 310, fy + 76, "STEAM MPA");
    readout(bb.x + 310, fy + 91, 14, 4, "%4.1f", P->p_header / 1e6);

    float lx = bb.x + 420;
    indicator((Rectangle){lx, fy + 10, 140, 34}, "HOUSE BUSES\nLIVE", L_WHT, P->offsite_power);
    indicator((Rectangle){lx, fy + 52, 140, 34}, "ESSENTIAL BUSES\nLIVE", L_WHT, rm_plant_essential_power(P));
    indicator((Rectangle){lx, fy + 94, 140, 34}, "RSP IN\nCONTROL", L_AMB, P->aux.rsp_control);
    dymo(lx + 160, fy + 14, "CONTROLS WORK ONLY WITH THE");
    dymo(lx + 160, fy + 32, "TRANSFER KEY AT RSP (RSP-1)");
}

void draw_rsp(void)
{
    static const secdef S[2] = {{"RSP-1", "REACTOR AND DECAY HEAT REMOVAL", 956, SEC_RSP, 6},
                                {"RSP-2", "EMERGENCY POWER, STEAM AND FEED", 956, -1, 0}};
    int ok = input_ok;
    secrect R[2];
    board_frame(S, 2, R);
    /* the second hood carries the unit plate instead of an annunciator box */
    float hx = R[1].face.x - 10;
    plate_c(hx + 478, HOOD_Y + 34, "RM-ELM UNIT 1   REMOTE SHUTDOWN PANEL", 20);
    ctext("OUTSIDE THE MAIN CONTROL ROOM - CONTROLS LIVE ONLY WITH THE TRANSFER KEY AT RSP", hx + 478, HOOD_Y + 76, 10,
          (Color){230, 228, 216, 255});
    rsp1_face(R[0].face);
    rsp2_face(R[1].face);
    rsp1_bench(R[0].bench, ok);
    input_ok = ok && P->aux.rsp_control;
    rsp2_bench(R[1].bench);
    input_ok = ok;
    board_strip();
}
