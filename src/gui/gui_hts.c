/*
 * Heat transport board (panels 2-1 to 2-3), after the FFTF heat transport
 * system panel and the Clinch River steam generator protection panels:
 *
 *   2-1  PRIMARY HEAT TRANSPORT
 *        loop and core flow dials; pump speed and leg temperature edgewise
 *        meters in groups; reactor sodium level; sodium temperature and core
 *        flow recorders. On the benchboard, the primary loop mimic with a
 *        control switch and a speed M/A station under each pump, and the
 *        flow master.
 *   2-2  INTERMEDIATE LOOPS AND STEAM GENERATOR PROTECTION
 *        secondary flow and hydrogen-in-sodium meters, expansion tanks, the
 *        leak detection display and the hydrogen recorder. On the bench,
 *        the secondary loop mimics, pump switches and speed stations, and
 *        the guarded SG ISOLATE and LOOP DUMP pushbuttons.
 *   2-3  DECAY HEAT REMOVAL AND CONTAINMENT ISOLATION
 *        a heat dial over each DRACS damper loading station, the DRACS and
 *        decay heat recorder; containment isolation.
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- flow control ------------------------------------------------------------------------ */
/* Each pump speed M/A station in AUTO follows the flow master's output; in
 * MAN its raise/lower buttons set that pump's speed. The master in AUTO
 * holds core flow at its setpoint; in MAN its raise/lower buttons set the
 * output by hand. The secondary pumps follow the same master (flow ratio
 * one to one), so primary and intermediate flow move together. */
static int pp_auto[RM_NLOOPS] = {1, 1, 1, 1};
static int sp_auto[RM_NLOOPS] = {1, 1, 1, 1};
static int master_auto = 0;
static double master_sp = 100.0;    /* % rated core flow */
static double master_out = 1.0;     /* pump speed demand, fraction of rated */
static int ctl_init = 0;

static double core_flow_pct(void) { return 100.0 * P->W_core / rm_plant_nominal_flow(); }
static double C(double T_K) { return T_K - 273.15; }

void controls_step(void)
{
    if (!ctl_init) {
        double s = 0;
        for (int i = 0; i < RM_NLOOPS; i++) s += P->loop[i].ppump.speed_set / RM_NLOOPS;
        master_out = s;
        master_sp = fmin(105.0, fmax(10.0, round(core_flow_pct())));
        ctl_init = 1;
    }
    int following = 0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_pump *pm = &P->loop[i].ppump;
        following |= pp_auto[i] && pm->motor_on && !pm->tripped;
    }
    /* integral action only while a running pump follows it: no windup on natural circulation */
    if (master_auto && following) master_out = fmin(1.05, fmax(0.10, master_out + 0.001 * (master_sp - core_flow_pct()) * DT));
    for (int i = 0; i < RM_NLOOPS; i++) {
        if (pp_auto[i]) P->loop[i].ppump.speed_set = master_out;
        if (sp_auto[i]) P->loop[i].spump.speed_set = master_out;
    }
}

/* pump control switch: turn right to start, left to stop (a start resets a trip) */
static void pump_switch(float cx, float y, const char *name, rm_pump *pm, const char *what, int n)
{
    int run = pm->motor_on && !pm->tripped;
    int s = cswitch3(cx, y, name, "STOP", "START", run, !run, SW_PISTOL, NULL, L_AMB, pm->tripped);
    if (s > 0 && !run) {
        pm->tripped = 0;
        pm->motor_on = 1;
        if (pm->speed_set < 0.1) pm->speed_set = 1.0;
        logmsg("%s PUMP %d STARTED", what, n);
    } else if (s < 0 && (run || pm->tripped)) {
        pm->motor_on = 0;
        pm->tripped = 0;
        logmsg("%s PUMP %d STOPPED", what, n);
    }
}

static void speed_station(float x, float y, const char *name, rm_pump *pm, int *aut, const char *what)
{
    int ev = mastation((Rectangle){x, y, MA_W, MA_H}, name, "% SPEED", 100 * pm->speed, 100 * pm->speed_set, 0, 125,
                       pm->speed_set, *aut);
    if ((ev & MA_AUTO) && !*aut) {
        *aut = 1;
        logmsg("%s SPEED CONTROL AUTO - FOLLOWS THE FLOW MASTER", what);
    }
    if ((ev & MA_MAN) && *aut) {
        *aut = 0;
        logmsg("%s SPEED CONTROL MANUAL", what);
    }
    if (!*aut && (ev & MA_UP)) pm->speed_set = fmin(1.05, pm->speed_set + 0.01);
    if (!*aut && (ev & MA_DOWN)) pm->speed_set = fmax(0.10, pm->speed_set - 0.01);
    if (*aut && (ev & (MA_UP | MA_DOWN))) logmsg("%s IS ON AUTO - USE THE FLOW MASTER, OR PRESS M", what);
}

/* ---- 2-1 primary heat transport --------------------------------------------------------------- */
static const char *const KG4[5] = {"0", "1", "2", "3", "4"};

/* edgewise meters in groups, each group under its nameplate */
static float group_x(float x0, int k, int g) { return x0 + k * 45.0f + g * 11.0f; }

static void pht_face(Rectangle f)
{
    char b[40];
    float pitch = f.width / 5;
    for (int i = 0; i < RM_NLOOPS; i++) {
        snprintf(b, sizeof b, "LOOP %d FLOW\nKG/S X1000", i + 1);
        dial((Rectangle){f.x + i * pitch + 6, f.y, pitch - 12, pitch - 12}, b, P->loop[i].W / 1000.0, 0, 4, 4, 2.5, 3.1,
             0, 0, KG4);
    }
    dial((Rectangle){f.x + 4 * pitch + 6, f.y, pitch - 12, pitch - 12}, "CORE FLOW\n% RATED", core_flow_pct(), 0, 125, 5,
         95, 105, 0, 70, NULL);

    /* pump speeds, hot legs, cold legs, reactor */
    float y2 = f.y + 150;
    static const char *gname[4] = {"PUMP SPEED %", "HOT LEG C", "COLD LEG C", "REACTOR"};
    for (int g = 0; g < 4; g++) {
        float x0 = group_x(f.x, g * 4, g), x1 = group_x(f.x, g == 3 ? 13 : g * 4 + 3, g) + 40;
        plate_c((x0 + x1) / 2, y2 - 22, gname[g], 10);
    }
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        snprintf(b, sizeof b, "PP-%d", i + 1);
        edgewz((Rectangle){group_x(f.x, i, 0), y2, 40, 100}, b, 100 * l->ppump.speed, 0, 125, 90, 105, 0, 0, 5, NULL);
        snprintf(b, sizeof b, "HL %d", i + 1);
        edgewz((Rectangle){group_x(f.x, 4 + i, 1), y2, 40, 100}, b, C(rm_na_T(l->hot.h[RM_PIPE_N - 1])), 300, 700, 500,
               560, 600, 700, 4, NULL);
        snprintf(b, sizeof b, "CL %d", i + 1);
        edgewz((Rectangle){group_x(f.x, 8 + i, 2), y2, 40, 100}, b, C(rm_na_T(l->cold.h[RM_PIPE_N - 1])), 200, 500, 360,
               410, 420, 500, 3, NULL);
    }
    double prog = 2.5 * (0.5 * (P->T_core_in + P->T_core_out) - 653.15);
    edgewz((Rectangle){group_x(f.x, 12, 3), y2, 40, 100}, "NA LVL\nMM", P->aux.na_level, -400, 400, prog - 100, prog + 100,
           -400, -300, 4, NULL);
    edgewz((Rectangle){group_x(f.x, 13, 3), y2, 40, 100}, "CORE\nDT C", P->T_core_out - P->T_core_in, 0, 250, 150, 190, 0, 0,
           5, NULL);

    float y3 = f.y + 282;
    mpr_draw((Rectangle){f.x, y3, f.width / 2 - 5, f.y + f.height - y3}, &MP_NA, "SODIUM TEMPERATURES", "300 C", "700 C");
    strip((Rectangle){f.x + f.width / 2 + 5, y3, f.width / 2 - 5, f.y + f.height - y3}, TR_FLOW, 0, 125, "CORE FLOW %",
          TR_INLET, 300, 500, "CORE IN C");
}

static void pht_bench(Rectangle bb)
{
    const float hy = bb.y + 12, cy = bb.y + 90, py = bb.y + 72;
    const float colw = 131;
    float lastx = bb.x + 40 + 3 * colw;
    /* reactor vessel and the hot and cold leg headers */
    sym_tank((Rectangle){bb.x + 2, bb.y + 4, 30, 92}, MIM_PNA, "RX");
    Vector2 hot[2] = {{bb.x + 32, hy}, {lastx + 16, hy}};
    mpipe(hot, 2, MIM_PNA, 5, 1);
    Vector2 cold[2] = {{lastx + 76, cy}, {bb.x + 32, cy}};
    mpipe(cold, 2, MIM_PNA, 5, 1);
    text("HOT LEG", lastx + 24, hy - 5, 10, INK);
    text("COLD LEG", lastx + 80, cy - 16, 10, INK);

    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        rm_pump *pm = &l->ppump;
        float x = bb.x + 40 + i * colw;
        char b[32];
        /* the loop on the mimic: header, IHX shell, pump, back to the cold header */
        Vector2 d[2] = {{x + 16, hy}, {x + 16, hy + 6}};
        mpipe(d, 2, MIM_PNA, 5, 0);
        sym_hx((Rectangle){x + 4, hy + 6, 24, 40}, MIM_PNA, MIM_SNA, NULL);
        snprintf(b, sizeof b, "IHX %d", i + 1);
        text(b, x + 32, hy + 18, 10, INK);
        Vector2 r[3] = {{x + 16, hy + 46}, {x + 16, py}, {x + 40, py}};
        mpipe(r, 3, MIM_PNA, 5, 0);
        Vector2 o[3] = {{x + 60, py}, {x + 76, py}, {x + 76, cy}};
        mpipe(o, 3, MIM_PNA, 5, 0);
        int run = pm->motor_on && !pm->tripped;
        sym_pump((Vector2){x + 50, py}, 9, MIM_PNA, run, 0);

        snprintf(b, sizeof b, "PP-%d", i + 1);
        pump_switch(x + 50, bb.y + 98, b, pm, "PRIMARY", i + 1);
        if (lampbutton((Rectangle){x + 93, bb.y + 136, 38, 26}, "PONY", L_WHT, pm->pony_on)) {
            pm->pony_on = !pm->pony_on;
            logmsg("PONY MOTOR %d %s", i + 1, pm->pony_on ? "ARMED" : "OFF");
        }
        lamp(x + 100, bb.y + 176, 4, L_RED, pm->pony_on && !run && pm->speed > 0.05);
        text("RUN", x + 108, bb.y + 171, 10, INK);
        snprintf(b, sizeof b, "PP-%d SPEED", i + 1);
        char w[24];
        snprintf(w, sizeof w, "PRIMARY PUMP %d", i + 1);
        speed_station(x + 18, bb.y + bb.height - MA_H, b, pm, &pp_auto[i], w);
    }

    /* flow master and the core readouts */
    float mx = bb.x + bb.width - MA_W;
    int allstop = 1;
    for (int i = 0; i < RM_NLOOPS; i++) allstop &= !(P->loop[i].ppump.motor_on && !P->loop[i].ppump.tripped);
    indicator((Rectangle){mx, bb.y + 4, MA_W, 28}, "NATURAL\nCIRCULATION", L_WHT, allstop && P->W_core > 1.0);
    dymo(mx, bb.y + 38, "PONY MOTORS HOLD");
    dymo(mx, bb.y + 54, "10% ON DIESELS");
    tag(mx, bb.y + 104, "CORE FLOW %");
    readout(mx, bb.y + 119, 16, 3, "%3.0f", core_flow_pct());
    tag(mx, bb.y + 150, "CORE IN / OUT C");
    readout(mx, bb.y + 165, 12, 3, "%3.0f", C(P->T_core_in));
    readout(mx + 48, bb.y + 165, 12, 3, "%3.0f", C(P->T_core_out));
    int ev = mastation((Rectangle){mx, bb.y + bb.height - MA_H, MA_W, MA_H}, "FLOW MASTER", "% FLOW", core_flow_pct(),
                       master_sp, 0, 125, master_out, master_auto);
    if ((ev & MA_AUTO) && !master_auto) {
        master_auto = 1;
        logmsg("FLOW MASTER AUTO - HOLDS CORE FLOW AT %.0f%%", master_sp);
    }
    if ((ev & MA_MAN) && master_auto) {
        master_auto = 0;
        logmsg("FLOW MASTER MANUAL");
    }
    if (master_auto && (ev & MA_UP)) master_sp = fmin(105.0, master_sp + 1.0);
    if (master_auto && (ev & MA_DOWN)) master_sp = fmax(10.0, master_sp - 1.0);
    if (!master_auto && (ev & MA_UP)) master_out = fmin(1.05, master_out + 0.01);
    if (!master_auto && (ev & MA_DOWN)) master_out = fmax(0.10, master_out - 0.01);
}

/* ---- 2-2 intermediate loops and SG protection ---------------------------------------------- */
static void iht_face(Rectangle f)
{
    char b[40];
    float cw = f.width / RM_NLOOPS;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        float x = f.x + i * cw;
        snprintf(b, sizeof b, "SEC LOOP %d\nKG/S X1000", i + 1);
        dial((Rectangle){x + 2, f.y, 120, 120}, b, l->Ws / 1000.0, 0, 4, 4, 2.5, 3.1, 0, 0, KG4);
        static const char *h2l[5] = {"0", ".25", ".5", ".75", "1"};
        edgewz((Rectangle){x + 130, f.y, 40, 106}, "H2 PPM", l->sg.h2, 0, 1, 0, 0.15, RM_H2_ALARM, 1, 4, h2l);
    }
    float y2 = f.y + 154;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        float x = f.x + i * cw + 1;
        snprintf(b, sizeof b, "LOOP %d", i + 1);
        plate_c(x + 87, y2 - 22, b, 10);
        edgewz((Rectangle){x, y2, 38, 96}, "EXP\nLVL %", l->exp_level, 0, 100, 30, 70, 85, 100, 4, NULL);
        edgewz((Rectangle){x + 44, y2, 38, 96}, "EXP P\nMPA", l->exp_p / 1e6, 0, 1, 0.1, 0.3, 0.5, 1, 4, NULL);
        edgewz((Rectangle){x + 88, y2, 38, 96}, "HOT\nLEG C", C(rm_na_T(l->shot.h[RM_PIPE_N - 1])), 200, 600, 450, 530, 0,
               0, 4, NULL);
        edgewz((Rectangle){x + 132, y2, 38, 96}, "COLD\nLEG C", C(rm_na_T(l->scold.h[RM_PIPE_N - 1])), 200, 600, 300, 380,
               0, 0, 4, NULL);
    }

    /* leak detection: where the sodium is getting out, or the water in */
    float y3 = f.y + 290;
    demarc((Rectangle){f.x, y3, 404, f.y + f.height - y3}, "SG LEAK DETECTION");
    static const char *hd[6] = {"NA\nLEAK", "DISC\nBURST", "N2\nPURGE", "DUMPED", "REFILL", "FIRE"};
    static const Color hc[6] = {{240, 60, 44, 255}, {240, 60, 44, 255}, {248, 244, 228, 255},
                                {250, 186, 60, 255}, {248, 244, 228, 255}, {240, 60, 44, 255}};
    text("INVENT %", f.x + 346, y3 + 10, 10, INK);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        float ry = y3 + 22 + i * 30;
        snprintf(b, sizeof b, "SG %d", i + 1);
        text(b, f.x + 8, ry + 8, 10, INK);
        int on[6] = {l->na_leak > 0.001, l->sg.disc_burst, l->sg.n2_purge, l->dumped, l->refill_t > 0,
                     P->aux.fire[RM_FIRE_SG1 + i]};
        for (int k = 0; k < 6; k++) {
            int lit = on[k] && (k != 0 && k != 5 ? 1 : blink_fast());
            indicator((Rectangle){f.x + 42 + k * 50, ry, 47, 26}, hd[k], hc[k], lit);
        }
        readout(f.x + 350, ry + 1, 10, 3, "%3.0f", 100 * l->sec_inventory);
    }
    mpr_draw((Rectangle){f.x + 414, y3, f.width - 414, f.y + f.height - y3}, &MP_H2, "HYDROGEN IN SODIUM", "0",
             "1 PPM");
}

static void iht_bench(Rectangle bb)
{
    float cw = bb.width / RM_NLOOPS;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        rm_sg *s = &l->sg;
        rm_pump *pm = &l->spump;
        float x = bb.x + i * cw;
        char b[32];
        /* mimic: IHX tubes, hot leg over to the SG shell, cold leg back through the pump */
        float top = bb.y + 8, bot = bb.y + 86;
        sym_hx((Rectangle){x + 6, top + 12, 22, 50}, MIM_PNA, MIM_SNA, NULL);
        snprintf(b, sizeof b, "IHX %d", i + 1);
        text(b, x + 32, top + 28, 10, INK);
        Vector2 hl[4] = {{x + 17, top + 12}, {x + 17, top}, {x + 136, top}, {x + 136, top + 12}};
        mpipe(hl, 4, MIM_SNA, 5, 1);
        sym_hx((Rectangle){x + 124, top + 12, 24, 56}, MIM_SNA, MIM_WTR, NULL);
        snprintf(b, sizeof b, "SG %d", i + 1);
        text(b, x + 94, top + 30, 10, INK);
        Vector2 cl[3] = {{x + 136, top + 68}, {x + 136, bot}, {x + 60, bot}};
        mpipe(cl, 3, MIM_SNA, 5, 1);
        Vector2 cl2[3] = {{x + 40, bot}, {x + 17, bot}, {x + 17, top + 62}};
        mpipe(cl2, 3, MIM_SNA, 5, 1);
        int run = pm->motor_on && !pm->tripped;
        sym_pump((Vector2){x + 50, bot}, 9, MIM_SNA, run, 2);
        Vector2 fw[2] = {{x + cw - 4, top + 60}, {x + 148, top + 60}};
        mpipe(fw, 2, MIM_WTR, 4, 0);
        Vector2 st[2] = {{x + 148, top + 20}, {x + cw - 4, top + 20}};
        mpipe(st, 2, MIM_STM, 4, 0);
        tri((Vector2){x + 150, top + 60}, (Vector2){x + 157, top + 55}, (Vector2){x + 157, top + 65}, MIM_WTR);
        tri((Vector2){x + cw - 2, top + 20}, (Vector2){x + cw - 9, top + 15}, (Vector2){x + cw - 9, top + 25}, MIM_STM);

        snprintf(b, sizeof b, "SP-%d", i + 1);
        pump_switch(x + 50, bb.y + 98, b, pm, "SECONDARY", i + 1);
        /* the protection pushbuttons, under hinged guards */
        if (pbguard((Rectangle){x + 94, bb.y + 106, 80, 32}, "SG\nISOLATE", L_AMB, s->isolated)) {
            if (!s->isolated) rm_plant_isolate_sg(P, i);
        }
        if (pbguard((Rectangle){x + 94, bb.y + 154, 80, 32}, s->disc_burst ? "DISC\nBURST" : "LOOP\nDUMP",
                    s->disc_burst ? L_RED : L_AMB, l->dumped)) {
            if (!l->dumped) rm_plant_dump_loop(P, i);
        }
        snprintf(b, sizeof b, "SP-%d SPEED", i + 1);
        char w[24];
        snprintf(w, sizeof w, "SECONDARY PUMP %d", i + 1);
        speed_station(x + 2, bb.y + bb.height - MA_H, b, pm, &sp_auto[i], w);
        float rx = x + 106;
        if (lampbutton((Rectangle){rx, bb.y + 214, 70, 30}, "REFILL\nLOOP", L_WHT, l->refill_t > 0)) {
            if (!l->dumped) logmsg("LOOP %d IS FULL - NOTHING TO REFILL", i + 1);
            else rm_plant_refill_loop(P, i);
        }
        tag(rx, bb.y + 252, "REFILL S");
        readout(rx, bb.y + 267, 11, 4, "%4.0f", l->refill_t);
        tag(rx, bb.y + 300, "FLOW KG/S");
        readout(rx, bb.y + 315, 11, 4, "%4.0f", l->Ws);
    }
}

/* ---- 2-3 decay heat removal and containment isolation -------------------------------------------- */
static void dhr_face(Rectangle f)
{
    char b[40];
    float pitch = f.width / 3;
    for (int i = 0; i < 3; i++) {
        snprintf(b, sizeof b, "DRACS %c\nHEAT MW", 'A' + i);
        dial((Rectangle){f.x + i * pitch + 6, f.y, pitch - 12, pitch - 12}, b, P->Q_dracs_train[i] / 1e6, 0, 30, 6, 0, 0,
             0, 0, NULL);
    }
    float y = f.y + pitch + 2;
    struct { const char *t; const char *fmt; double v; } rd[6] = {
        {"DRACS MW", "%4.1f", P->Q_dracs / 1e6},
        {"DECAY HEAT MW", "%4.1f", P->core.p_decay / 1e6},
        {"NAT CIRC KG/S", "%4.0f", P->W_dracs},
        {"NA TO COOLERS C", "%3.0f", C(rm_na_T(P->h_outplenum))},
        {"NA RETURN C", "%3.0f", C(rm_na_T(P->h_dracs_out > 0 ? P->h_dracs_out : P->h_outplenum))},
        {"AIR INLET C", "%3.0f", C(P->T_air)},
    };
    for (int k = 0; k < 6; k++) {
        float x = f.x + (k % 3) * pitch + 6, yy = y + (k / 3) * 48;
        tag(x, yy, rd[k].t);
        readout(x, yy + 15, 15, 4, rd[k].fmt, rd[k].v);
    }
    float y3 = y + 100;
    strip((Rectangle){f.x, y3, f.width, f.y + f.height - y3}, TR_DRACS, 0, 150, "DRACS MW", TR_DECAY, 0, 150, "DECAY MW");
}

static void dhr_bench(Rectangle bb)
{
    /* damper loading stations: AUTO opens the damper on a reactor trip */
    for (int i = 0; i < 3; i++) {
        char nm[24];
        snprintf(nm, sizeof nm, "DRACS %c DAMPER", 'A' + i);
        int ev = mastation((Rectangle){bb.x + 2 + i * 104, bb.y + 4, MA_W, MA_H}, nm, "% OPEN", 100 * P->dracs_damper[i],
                           100 * P->dracs_damper_set[i], 0, 100, P->dracs_damper_set[i], P->dracs_man[i] ? 0 : 1);
        if ((ev & MA_AUTO) && P->dracs_man[i]) {
            P->dracs_man[i] = 0;
            logmsg("DRACS %c DAMPER AUTO", 'A' + i);
        }
        if ((ev & MA_MAN) && !P->dracs_man[i]) {
            P->dracs_man[i] = 1;
            logmsg("DRACS %c DAMPER MANUAL", 'A' + i);
        }
        if (P->dracs_man[i] && (ev & MA_UP)) P->dracs_damper_set[i] = fmin(1.0, P->dracs_damper_set[i] + 0.02);
        if (P->dracs_man[i] && (ev & MA_DOWN)) P->dracs_damper_set[i] = fmax(0.0, P->dracs_damper_set[i] - 0.02);
        if (!P->dracs_man[i] && (ev & (MA_UP | MA_DOWN))) logmsg("DRACS %c IS ON AUTO - PRESS M TO MOVE IT", 'A' + i);
    }
    float ax = bb.x + 318, aw = bb.x + bb.width - ax;
    demarc((Rectangle){ax, bb.y + 10, aw, 172}, "DRACS AUTO");
    if (keysw(ax + aw / 2, bb.y + 62, "OFF", "ON TRIP", P->dracs_auto)) {
        P->dracs_auto = !P->dracs_auto;
        if (P->dracs_auto)
            for (int i = 0; i < 3; i++) P->dracs_man[i] = 0;
        logmsg(P->dracs_auto ? "DRACS AUTO ARMED - ALL DAMPERS OPEN ON A REACTOR TRIP" : "DRACS AUTO OFF");
    }
    indicator((Rectangle){ax + 10, bb.y + 90, aw - 20, 26}, "AUTO ARMED", L_WHT, P->dracs_auto);
    int cooling = P->Q_dracs > 1e6;
    indicator((Rectangle){ax + 10, bb.y + 120, aw - 20, 26}, "DRACS COOLING", L_WHT, cooling);
    indicator((Rectangle){ax + 10, bb.y + 150, aw - 20, 26}, "AIR LOW - FAIL OPEN", L_AMB, P->aux.air_p < 3.0);

    float y = bb.y + 206;
    demarc((Rectangle){bb.x, y, 332, bb.y + bb.height - y - 4}, "CONTAINMENT ISOLATION");
    if (pbguard((Rectangle){bb.x + 12, y + 24, 112, 40}, "CONTAINMENT\nISOLATE", L_AMB, P->containment_isolated) &&
        !P->containment_isolated) {
        P->containment_isolated = 1;
        logmsg("CONTAINMENT ISOLATION - PURIFICATION LINES SHUT");
    }
    if (lampbutton((Rectangle){bb.x + 136, y + 24, 80, 40}, "CIS\nRESET", L_WHT, 0) && P->containment_isolated) {
        P->containment_isolated = 0;
        logmsg("CONTAINMENT ISOLATION RESET");
    }
    int hirad = P->aux.rad[RM_RAD_STACK] > 50.0 || P->aux.rad[RM_RAD_HALL] > 100.0;
    indicator((Rectangle){bb.x + 228, y + 24, 92, 40}, "HIGH RAD\nCIS SIGNAL", L_RED, hirad);
    indicator((Rectangle){bb.x + 12, y + 76, 100, 30}, "ISOLATED", L_AMB, P->containment_isolated);
    indicator((Rectangle){bb.x + 116, y + 76, 100, 30}, "PURIF LINES\nSHUT", L_WHT, P->containment_isolated);
    indicator((Rectangle){bb.x + 220, y + 76, 100, 30}, "COLD TRAPS\nOFF", L_WHT,
              P->containment_isolated || !P->aux.prim_cold_trap);
    dymo(bb.x + 12, y + 124, "AUTO CIS: STACK > 50 OR HALL > 100");
    mimic_legend(bb.x + bb.width - 114, bb.y + bb.height - 80);
}

void draw_hts(void)
{
    static const secdef S[3] = {{"2-1", "PRIMARY HEAT TRANSPORT", 680, SEC_PHT, 7},
                                {"2-2", "INTERMEDIATE LOOPS AND STEAM GENERATOR PROTECTION", 740, SEC_IHT, 7},
                                {"2-3", "DECAY HEAT REMOVAL AND CONTAINMENT ISOLATION", 492, SEC_DHR, 5}};
    secrect R[3];
    board_frame(S, 3, R);
    pht_face(R[0].face);
    pht_bench(R[0].bench);
    iht_face(R[1].face);
    iht_bench(R[1].bench);
    dhr_face(R[2].face);
    dhr_bench(R[2].bench);
    board_strip();
}
