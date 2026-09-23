/*
 * Remote shutdown panel: a small panel in a separate room, with just enough
 * to trip the reactor and hold it in hot shutdown when the main control room
 * has to be abandoned. Its controls only work once the transfer key switch
 * has taken control from the main control room, and while it has control
 * the main boards are dead.
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void draw_log(Rectangle r);

void draw_rsp(void)
{
    rm_core *c = &P->core;
    rm_aux *a = &P->aux;
    int ok = input_ok;
    Rectangle r = {140, 52, 1640, 1020};
    steel(r, "REMOTE SHUTDOWN PANEL - UNIT 1");

    /* ---- transfer and status ---- */
    input_ok = 1;
    float x = r.x + 20, y = r.y + 34;
    demarc((Rectangle){x, y + 6, 420, 150}, "CONTROL TRANSFER");
    if (keysw(x + 70, y + 70, "MCR", "RSP", a->rsp_control)) {
        a->rsp_control = !a->rsp_control;
        logmsg(a->rsp_control ? "CONTROL TRANSFERRED TO THE REMOTE SHUTDOWN PANEL"
                              : "CONTROL RETURNED TO THE MAIN CONTROL ROOM");
    }
    text("TRANSFER", x + 44, y + 92, 10, INK);
    lamp(x + 170, y + 40, 7, L_RED, a->rsp_control);
    text("RSP IN CONTROL", x + 184, y + 34, 10, INK);
    lamp(x + 170, y + 64, 7, L_RED, a->evacuated && blink_fast());
    text("MAIN CONTROL ROOM EVACUATED", x + 184, y + 58, 10, INK);
    lamp(x + 170, y + 88, 7, L_AMB, a->fire[RM_FIRE_CR]);
    text("CONTROL ROOM FIRE", x + 184, y + 82, 10, INK);
    int can_return = a->evacuated && !a->fire[RM_FIRE_CR];
    if (lampbutton((Rectangle){x + 170, y + 104, 230, 36}, "RETURN TO\nMAIN CONTROL ROOM", L_WHT, can_return) && can_return) {
        a->evacuated = 0;
        logmsg("CREW BACK IN THE MAIN CONTROL ROOM - TRANSFER CONTROL BACK WITH THE KEY");
    }
    input_ok = ok && a->rsp_control;

    /* ---- reactor ---- */
    x = r.x + 20;
    y = r.y + 210;
    demarc((Rectangle){x, y, 420, 420}, "REACTOR");
    Vector2 sc = {x + 80, y + 80};
    Vector2 m = GetMousePosition();
    int hov = input_ok && CheckCollisionPointCircle(m, sc, 32);
    DrawCircleV(sc, 42, (Color){238, 196, 30, 255});
    DrawCircleV(sc, hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 28.0f : 32.0f, (Color){206, 28, 22, 255});
    DrawCircleV((Vector2){sc.x - 10, sc.y - 11}, 10, alpha(WHITE, 70));
    ctext("REACTOR SCRAM", sc.x, sc.y + 48, 10, INK);
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        rm_plant_manual_scram(P);
        logmsg("REACTOR SCRAMMED FROM THE REMOTE SHUTDOWN PANEL");
    }
    lamp(x + 180, y + 50, 7, L_RED, c->scram);
    text("ALL RODS IN / SCRAMMED", x + 194, y + 44, 10, INK);
    lamp(x + 180, y + 74, 7, L_AMB, !c->scram && blink_fast());
    text("REACTOR CRITICAL/NOT TRIPPED", x + 194, y + 68, 10, INK);
    static const char *lpl[6] = {"-8", "-6", "-4", "-2", "0", "2"};
    edgew((Rectangle){x + 20, y + 150, 48, 220}, "LOG PWR %", log10(fmax(100 * c->p_thermal / RM_P_RATED, 1e-8)), -8, 2, 1.9,
          2, 5, lpl);
    static const char *srml[5] = {".1", "10", "1E3", "1E5", ""};
    edgew((Rectangle){x + 90, y + 150, 48, 220}, "SRM CPS", log10(fmax(rm_plant_srm_cps(P), 0.1)), -1, 5, 4.5, 5, 3, srml);
    float rx = x + 170;
    dymo(rx, y + 150, "PERIOD S");
    if (isfinite(P->period) && fabs(P->period) < 999) readout(rx, y + 166, 16, 4, "%5.0f", P->period);
    else readout(rx, y + 166, 16, 4, "----");
    dymo(rx, y + 206, "CORE OUT / IN C");
    readout(rx, y + 222, 16, 3, "%3.0f", P->T_core_out - 273.15);
    readout(rx + 64, y + 222, 16, 3, "%3.0f", P->T_core_in - 273.15);
    dymo(rx, y + 262, "CORE FLOW %");
    readout(rx, y + 278, 16, 3, "%3.0f", 100 * P->W_core / rm_plant_nominal_flow());
    dymo(rx, y + 318, "NA LEVEL MM");
    readout(rx, y + 334, 16, 4, "%4.0f", a->na_level);
    dymo(rx + 120, y + 262, "DECAY MW");
    readout(rx + 120, y + 278, 16, 3, "%3.0f", c->p_decay / 1e6);

    /* ---- primary pumps and DRACS ---- */
    x = r.x + 460;
    y = r.y + 34;
    demarc((Rectangle){x, y + 6, 560, 300}, "PRIMARY PUMPS");
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_pump *pm = &P->loop[i].ppump;
        char nm[16];
        snprintf(nm, sizeof nm, "PP-%d", i + 1);
        int run = pm->motor_on && !pm->tripped;
        float cx = x + 70 + i * 132;
        int s = cswitch(cx, y + 24, nm, "STOP", "START", run, !run, SW_PISTOL, NULL);
        if (s > 0 && !run) {
            pm->tripped = 0;
            pm->motor_on = 1;
            if (pm->speed_set < 0.1) pm->speed_set = 1.0;
            logmsg("PRIMARY PUMP %d STARTED (RSP)", i + 1);
        } else if (s < 0 && run) {
            pm->motor_on = 0;
            logmsg("PRIMARY PUMP %d STOPPED (RSP)", i + 1);
        }
        readout(cx - 30, y + 122, 14, 3, "%3.0f", 100 * pm->speed);
        text("%", cx + 22, y + 126, 10, INK);
        if (lampbutton((Rectangle){cx - 40, y + 154, 80, 28}, "PONY", L_WHT, pm->pony_on)) {
            pm->pony_on = !pm->pony_on;
            logmsg("PONY MOTOR %d %s (RSP)", i + 1, pm->pony_on ? "ARMED" : "OFF");
        }
        readout(cx - 40, y + 190, 12, 5, "%5.0f", P->loop[i].W);
        text("KG/S", cx + 40, y + 194, 10, INK);
    }
    text("KEEP THE PONY MOTORS ARMED: THEY HOLD 10% FLOW ON DIESEL POWER.", x + 14, y + 240, 10, INK);

    y = r.y + 360;
    demarc((Rectangle){x, y, 560, 270}, "DECAY HEAT REMOVAL");
    for (int i = 0; i < 3; i++) {
        char nm[24];
        snprintf(nm, sizeof nm, "DRACS %c", 'A' + i);
        int open = P->dracs_damper[i] > 0.5;
        float cx = x + 90 + i * 180;
        int s = cswitch(cx, y + 20, nm, "CLOSE", "OPEN", open, !open, SW_PISTOL, NULL);
        if (s) {
            P->dracs_auto = 0;
            P->dracs_damper_set[i] = s > 0 ? 1.0 : 0.0;
            logmsg("DRACS %c DAMPER %s (RSP)", 'A' + i, s > 0 ? "OPEN" : "CLOSED");
        }
        readout(cx - 30, y + 120, 14, 4, "%4.1f", P->Q_dracs_train[i] / 1e6);
        text("MW", cx + 30, y + 124, 10, INK);
    }
    dymo(x + 20, y + 170, "TOTAL DRACS MW");
    readout(x + 20, y + 186, 18, 4, "%4.1f", P->Q_dracs / 1e6);
    dymo(x + 180, y + 170, "NAT CIRC KG/S");
    readout(x + 180, y + 186, 18, 4, "%4.0f", P->W_dracs);
    if (lampbutton((Rectangle){x + 340, y + 176, 110, 36}, "AUTO\nON TRIP", L_WHT, P->dracs_auto) && !P->dracs_auto) {
        P->dracs_auto = 1;
        logmsg("DRACS AUTO (RSP)");
    }

    /* ---- electrical, steam generators, feed ---- */
    x = r.x + 1040;
    y = r.y + 34;
    demarc((Rectangle){x, y + 6, 580, 200}, "EMERGENCY POWER");
    for (int i = 0; i < 3; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "DG %d", i + 1);
        float cx = x + 80 + i * 150;
        int s = cswitch(cx, y + 24, nm, "STOP", "START", P->diesel_running[i], !P->diesel_running[i], SW_PISTOL,
                        &P->diesel_ptl[i]);
        if (s > 0) {
            P->diesel_manual[i] = 1;
            logmsg("DIESEL %d START (RSP)", i + 1);
        }
        if (s < 0) P->diesel_manual[i] = 0;
        readout(cx - 30, y + 122, 14, 3, "%3.1f", P->tg.dg_mw[i]);
        text("MW", cx + 22, y + 126, 10, INK);
    }
    lamp(x + 30, y + 176, 7, L_RED, P->offsite_power);
    text("HOUSE BUSES LIVE", x + 44, y + 170, 10, INK);
    lamp(x + 230, y + 176, 7, L_RED, rm_plant_essential_power(P));
    text("ESSENTIAL BUSES LIVE", x + 244, y + 170, 10, INK);

    y = r.y + 250;
    demarc((Rectangle){x, y, 580, 190}, "STEAM GENERATORS");
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &P->loop[i].sg;
        char nm[16];
        snprintf(nm, sizeof nm, "SG %d ISOL", i + 1);
        float cx = x + 72 + i * 140;
        int k = cswitch(cx, y + 16, nm, "NORM", "ISOL", s->isolated, !s->isolated, SW_JHANDLE, NULL);
        if (k > 0 && !s->isolated) rm_plant_isolate_sg(P, i);
        readout(cx - 30, y + 116, 12, 4, "%4.2f", fmin(s->h2, 99.99));
        text("H2", cx + 26, y + 120, 10, INK);
    }
    dymo(x + 20, y + 150, "STEAM MPA");
    readout(x + 100, y + 146, 14, 4, "%4.1f", P->p_header / 1e6);

    y = r.y + 460;
    demarc((Rectangle){x, y, 580, 170}, "FEEDWATER - TURBINE");
    int s = cswitch(x + 80, y + 16, "MDFP", "STOP", "START", P->tg.mdfp, !P->tg.mdfp, SW_PISTOL, NULL);
    if (s) {
        P->tg.mdfp = s > 0;
        logmsg("MOTOR FEED PUMP %s (RSP)", s > 0 ? "STARTED" : "STOPPED");
    }
    s = cswitch(x + 200, y + 16, "FEEDWATER", "OUT", "IN", P->fw_on, !P->fw_on, SW_PISTOL, NULL);
    if (s) {
        P->fw_on = s > 0;
        logmsg("FEEDWATER %s (RSP)", s > 0 ? "IN SERVICE" : "OUT OF SERVICE");
    }
    s = cswitch(x + 320, y + 16, "TURBINE", "TRIP", "-", !P->turbine_tripped, P->turbine_tripped, SW_PISTOL, NULL);
    if (s < 0) rm_plant_turbine_trip(P, "REMOTE SHUTDOWN PANEL");
    dymo(x + 400, y + 30, "FEED KG/S");
    double wf = 0;
    for (int i = 0; i < RM_NLOOPS; i++) wf += P->loop[i].sg.W_fw;
    readout(x + 400, y + 46, 14, 4, "%4.0f", wf);

    /* ---- annunciators, procedure and typer ---- */
    input_ok = ok;
    y = r.y + 650;
    ann_box((Rectangle){r.x + 20, y, 600, 80}, BOARD_RSP, 3, 34);
    ann_controls(r.x + 20, y + 90);
    Rectangle pr = {r.x + 660, y, 960, 170};
    DrawRectangleRec(pr, (Color){236, 232, 214, 255});
    DrawRectangleLinesEx(pr, 2, BEZEL);
    static const char *proc[] = {
        "EOP-9  CONTROL ROOM EVACUATION",
        "1. IF POSSIBLE, SCRAM THE REACTOR FROM THE MAIN CONTROL BOARD BEFORE LEAVING.",
        "2. AT THE RSP, TURN THE TRANSFER KEY TO RSP. THE MAIN BOARDS GO DEAD.",
        "3. SCRAM THE REACTOR IF IT IS NOT ALREADY. CONFIRM LOG POWER FALLING.",
        "4. TRIP THE TURBINE. CHECK DIESELS AND ESSENTIAL BUSES.",
        "5. ARM THE PONY MOTORS. OPEN THE DRACS DAMPERS. HOLD CORE OUTLET BELOW 550 C.",
        "6. WHEN THE FIRE IS OUT: RETURN TO THE CONTROL ROOM, THEN TURN THE KEY BACK TO MCR.",
    };
    for (int i = 0; i < 7; i++) text(proc[i], pr.x + 14, pr.y + 12 + i * 20, 10, i ? INK : (Color){150, 20, 10, 255});
    draw_log((Rectangle){r.x + 20, r.y + 840, r.width - 40, 160});
    input_ok = ok;
}
