/*
 * Annunciators: the window boxes of every board and the ISA sequence of the
 * period (ISA-18.1 "ringback"):
 *
 *   alarm comes in    -> window flashes fast, horn sounds
 *   SILENCE           -> horn stops, window keeps flashing
 *   ACKNOWLEDGE       -> window steady on (horn stops too)
 *   alarm clears      -> window flashes slowly (ringback)
 *   RESET             -> window goes dark
 *
 * An alarm that clears before it is acknowledged stays locked in. TEST
 * lights every window while it is held.
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NANN 160
enum { ST_CLEAR, ST_ALERT, ST_ACKED, ST_RINGBACK };

static struct {
    const char *l1, *l2;
    int bd, red;
    Color c;
    int on;
    int st;
} A[NANN];
static int nann;
static int silenced, testing;
static int started;
static Sound horn;
static int have_horn;

static void an(int bd, int red, Color c, const char *l1, const char *l2, int cond)
{
    if (nann >= NANN) return;
    A[nann].l1 = l1;
    A[nann].l2 = l2;
    A[nann].bd = bd;
    A[nann].red = red;
    A[nann].c = c;
    A[nann].on = cond != 0;
    nann++;
}

void ann_init_audio(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    /* a 1970s annunciator horn: a harsh buzz near 400 Hz */
    const int rate = 22050, n = rate / 2;
    short *d = malloc(sizeof(short) * n);
    if (!d) return;
    for (int i = 0; i < n; i++) {
        double t = (double)i / rate;
        double s = (fmod(t * 410.0, 1.0) < 0.5 ? 1.0 : -1.0) * 0.55 + sin(2 * PI * 820.0 * t) * 0.25;
        d[i] = (short)(s * 9000);
    }
    Wave w = {.frameCount = (unsigned)n, .sampleRate = (unsigned)rate, .sampleSize = 16, .channels = 1, .data = d};
    horn = LoadSoundFromWave(w);
    free(d);
    SetSoundVolume(horn, 0.35f);
    have_horn = 1;
}

/* every window on every board, in a fixed order (the sequence state is kept by position) */
static void conditions(void)
{
    rm_core *c = &P->core;
    rm_aux *a = &P->aux;
    rm_tg *t = &P->tg;
    nann = 0;
    int pumps_off = 0, h2 = 0, iso = 0, oos = 0, drift = 0, leak = 0, burst = 0, dumped = 0, fire_na = 0, exp_hi = 0;
    int ppump_brg = 0, plug = 0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        pumps_off += !l->ppump.motor_on || l->ppump.tripped || !l->spump.motor_on || l->spump.tripped;
        h2 |= l->sg.h2 > RM_H2_ALARM;
        iso |= l->sg.isolated || !l->sg.fiv_open || !l->sg.msiv_open;
        leak |= l->na_leak > 0.001;
        burst |= l->sg.disc_burst;
        dumped |= l->dumped;
        fire_na |= a->fire[RM_FIRE_SG1 + i];
        exp_hi |= !l->dumped && (l->exp_level > 85.0 || l->exp_level < 15.0);
        ppump_brg |= l->brg_T > 80.0;
        plug |= rm_plant_plugging_T(P, i) > 160.0;
    }
    plug |= rm_plant_plugging_T(P, -1) > 160.0;
    for (int i = 0; i < 3; i++) oos |= !P->diesel_avail[i];
    for (int k = 0; k < c->nctrl; k++) drift |= c->rod_drift[k] != 0.0 && !c->scram;
    double reg = rm_core_bank_pos(c, BANK_REG);
    int online = G.on_line && P->generator_breaker;
    int half = (P->nms.div_trip[0] != P->nms.div_trip[1]) && !c->scram;
    int chan_byp = P->nms.bypass[0] || P->nms.bypass[1] || P->nms.bypass[2] || P->nms.bypass[3];
    double aprm = rm_plant_aprm(P);
    char rwhy[80];
    int rwm_block = P->nms.sel_rod >= 0 && !rm_plant_rod_permit(P, -1, P->nms.sel_rod, 0.0, rwhy, sizeof rwhy) &&
                    strncmp(rwhy, "RWM", 3) == 0;
    int rbm_hi = !P->nms.rbm_bypass && P->nms.sel_rod >= 0 && aprm > 30.0 && P->nms.rbm > 1.08;
    int heat_low = 0;
    for (int i = 0; i < RM_NLOOPS; i++)
        if (rm_na_T(P->loop[i].scold.h[RM_PIPE_N - 1]) < 423.15 || rm_na_T(P->loop[i].cold.h[RM_PIPE_N - 1]) < 423.15)
            heat_low = 1;
    int fires = 0;
    /* programmed vessel level: the sodium expands with its mean temperature */
    double prog_level = 2.5 * (0.5 * (P->T_core_in + P->T_core_out) - 653.15);
    for (int f = 0; f < 8; f++) fires |= a->fire[f];

    /* ---- main board ---- */
    const int M = BOARD_MAIN;
    an(M, 1, L_RED, "REACTOR", "SCRAM", c->scram);
    an(M, 1, L_RED, "RPS DIV A", "TRIPPED", P->nms.div_trip[0]);
    an(M, 1, L_RED, "RPS DIV B", "TRIPPED", P->nms.div_trip[1]);
    an(M, 0, L_AMB, "HALF", "SCRAM", half);
    an(M, 1, L_RED, "NEUTRON", "FLUX HIGH", aprm > 110.0 || (P->mode != RM_MODE_RUN && aprm > 13.0));
    an(M, 1, L_RED, "PERIOD", "SHORT", isfinite(P->period) && P->period > 0 && P->period < 30);
    an(M, 1, L_RED, "CLADDING", "TEMP HIGH", rm_core_max_clad_T(c) > 923.15);
    an(M, 1, L_RED, "BOILING", "MARGIN LOW", CS_margin_min < 200);
    an(M, 1, L_RED, "RPS", "BYPASSED", P->rps_bypass);
    an(M, 0, L_AMB, "RPS CHAN", "BYPASSED", chan_byp);
    an(M, 0, L_AMB, "ROD WDL", "BLOCK", rm_plant_rod_block(P, 0, NULL, 0));
    an(M, 1, L_RED, "ROD", "DRIFT", drift);
    an(M, 0, L_AMB, "RWM", "ROD BLOCK", rwm_block);
    an(M, 0, L_AMB, "RBM", "UPSCALE", rbm_hi);
    an(M, 0, L_AMB, "GROUP 6 (REG)", "AT LIMIT", P->auto_rod && !c->scram && (reg < 0.5 || reg > RM_ACTIVE_H - 0.5));
    an(M, 0, L_AMB, "CORE FLOW", "LOW", P->W_core < 0.9 * rm_plant_nominal_flow());
    an(M, 0, L_AMB, "NA PUMP", "OFF", pumps_off > 0);
    an(M, 0, L_AMB, "CORE OUTLET", "TEMP HIGH", P->T_core_out > 833.15);
    an(M, 1, L_RED, "H2 IN SODIUM", "HIGH", h2);
    an(M, 0, L_AMB, "SG / STEAM", "ISOLATED", iso);
    an(M, 0, L_AMB, "TURBINE", "TRIP", P->turbine_tripped);
    an(M, 1, L_RED, "OFFSITE", "POWER LOST", !P->offsite_power);
    an(M, 0, L_WHT, "DRACS", "COOLING", P->Q_dracs > 1e6);
    an(M, 0, L_AMB, "CONTAINMENT", "ISOLATED", P->containment_isolated);
    an(M, 0, L_AMB, "LOAD", "DEVIATION", online && fabs(rm_game_deviation(&G, P)) > 0.05);
    an(M, 1, L_RED, "DND", "FAILED FUEL", a->dnd > 500.0);
    an(M, 1, L_RED, "FIRE", "PLANT", fires);
    an(M, 1, L_RED, "CONTROL RM", "FIRE / SMOKE", a->fire[RM_FIRE_CR]);
    an(M, 0, L_AMB, "TG BOARD", "ALARM", ann_board_alerting(BOARD_TG));
    an(M, 0, L_AMB, "AUX BOARD", "ALARM", ann_board_alerting(BOARD_AUX));

    /* ---- turbine-generator & electrical board ---- */
    const int T = BOARD_TG;
    an(T, 0, L_AMB, "TURBINE", "TRIP", P->turbine_tripped);
    an(T, 1, L_RED, "TURBINE", "OVERSPEED", t->speed > 1950.0);
    an(T, 1, L_RED, "VIBRATION", "HIGH", t->vib > 5.0);
    an(T, 0, L_AMB, "ECCENTRICITY", "HIGH", t->ecc > 2.0 && t->speed < 600.0);
    an(T, 1, L_RED, "BEARING", "TEMP HIGH", t->brg_T > 95.0);
    an(T, 1, L_RED, "LUBE OIL", "PRESS LOW", t->lube_p < 1.0);
    an(T, 0, L_AMB, "TURN GEAR OFF", "ROTOR STOPPED", !t->turning_gear && t->speed < 5.0);
    an(T, 0, L_AMB, "GEN BREAKER", "OPEN", !P->generator_breaker);
    an(T, 0, L_AMB, "GEN FIELD", "BREAKER OPEN", !t->field_breaker);
    an(T, 0, L_AMB, "AVR", "MANUAL", !t->avr_auto);
    an(T, 1, L_RED, "GRID", "LOST", !P->grid_ok || !P->grid_breaker);
    an(T, 0, L_AMB, "HOUSE LOAD", "ON STARTUP XFMR", !t->aux_on_uat && P->offsite_power);
    an(T, 1, L_RED, "HOUSE BUSES", "DEAD", !P->offsite_power);
    an(T, 0, L_AMB, "DIESEL GEN", "RUNNING", P->diesel_running[0] || P->diesel_running[1] || P->diesel_running[2]);
    an(T, 0, L_AMB, "DIESEL GEN", "OUT OF SERV", oos);
    an(T, 0, L_AMB, "DIESEL", "PULL TO LOCK", P->diesel_ptl[0] || P->diesel_ptl[1] || P->diesel_ptl[2]);
    an(T, 1, L_RED, "BATTERY", "DISCHARGING", (!t->charger[0] || !t->charger[1] || !rm_plant_essential_power(P)));
    an(T, 1, L_RED, "VITAL AC", "INVERTER OFF", !t->inverter[0] || !t->inverter[1] || t->batt[0] < 0.02 || t->batt[1] < 0.02);
    an(T, 0, L_AMB, "STEAM SAFETY", "VALVE OPEN", P->W_relief > 0);
    an(T, 0, L_AMB, "STEAM PRESS", "LOW", P->p_header < 12e6);
    an(T, 1, L_RED, "CONDENSER", "VACUUM LOW", P->p_cond > 15e3);
    an(T, 0, L_AMB, "CIRC WATER", "PUMP OFF", !t->cw[0] || !t->cw[1] || !t->cw[2]);
    an(T, 0, L_AMB, "FEED PUMP", "CAPACITY LOW", P->fw_on && t->fw_cap < 0.95 * fmax(P->core.p_thermal / RM_P_RATED, 0.2));
    an(T, 0, L_AMB, "HOTWELL", "LEVEL HIGH", t->hotwell > 1.4);
    an(T, 0, L_AMB, "FEED HEATERS", "OUT OF SERV", !t->heaters_in);
    an(T, 1, L_RED, "INSTRUMENT", "AIR LOW", a->air_p < 5.0);

    /* ---- auxiliary board ---- */
    const int X = BOARD_AUX;
    an(X, 1, L_RED, "H2 IN SODIUM", "HIGH", h2);
    an(X, 1, L_RED, "RUPTURE DISC", "BURST", burst);
    an(X, 0, L_AMB, "SECONDARY", "LOOP DUMPED", dumped);
    an(X, 1, L_RED, "SODIUM LEAK", "SECONDARY", leak);
    an(X, 1, L_RED, "SODIUM LEAK", "PRIMARY CELL", a->prim_leak > 0.0005);
    an(X, 1, L_RED, "SODIUM", "FIRE", fire_na || a->fire[RM_FIRE_CELL]);
    an(X, 0, L_AMB, "EXPANSION TANK", "LEVEL", exp_hi);
    an(X, 0, L_AMB, "REACTOR NA", "LEVEL ABN", fabs(a->na_level - prog_level) > 100.0);
    an(X, 0, L_AMB, "COVER GAS", "PRESSURE ABN", a->gas_p > 0.14e6 || a->gas_p < 0.105e6);
    an(X, 1, L_RED, "COVER GAS", "ACTIVITY HIGH", a->gas_act > 50.0);
    an(X, 1, L_RED, "DND", "FAILED FUEL", a->dnd > 500.0);
    an(X, 0, L_AMB, "PLUGGING", "TEMP HIGH", plug);
    an(X, 0, L_AMB, "TRACE HEAT", "PIPE TEMP LOW", heat_low);
    an(X, 0, L_AMB, "PRIM PUMP", "BEARING HOT", ppump_brg);
    an(X, 1, L_RED, "CELL O2", "HIGH", a->cell_o2 > 4.0);
    an(X, 0, L_AMB, "CONTAINMENT", "PRESS HIGH", a->cont_p > 103.0);
    an(X, 1, L_RED, "STACK", "RADIATION HIGH", a->rad[RM_RAD_STACK] > 10.0);
    an(X, 1, L_RED, "AREA", "RADIATION HIGH", a->rad[RM_RAD_HALL] > 25.0);
    an(X, 0, L_AMB, "CCW", "TEMP HIGH", a->ccw_T > 40.0);
    an(X, 0, L_AMB, "SERVICE WATER", "PUMP OFF", !a->sw[0] || !a->sw[1]);
    an(X, 1, L_RED, "INSTRUMENT", "AIR LOW", a->air_p < 5.0);
    an(X, 0, L_AMB, "CR HVAC", "EMERGENCY", a->hvac_emerg);
    an(X, 1, L_RED, "FIRE", "DETECTED", fires);
    an(X, 0, L_AMB, "FIRE PUMP", "OFF", !a->fire_pump[0] && !a->fire_pump[1] && fires);

    /* ---- remote shutdown panel ---- */
    const int R = BOARD_RSP;
    an(R, 1, L_RED, "CONTROL ROOM", "EVACUATED", a->evacuated);
    an(R, 1, L_RED, "REACTOR", "NOT SHUT DOWN", !c->scram && a->evacuated);
    an(R, 0, L_AMB, "RSP", "IN CONTROL", a->rsp_control);
    an(R, 1, L_RED, "OFFSITE", "POWER LOST", !P->offsite_power);
    an(R, 0, L_AMB, "CORE OUTLET", "TEMP HIGH", P->T_core_out > 833.15);
    an(R, 0, L_WHT, "DRACS", "COOLING", P->Q_dracs > 1e6);
}

void ann_eval(void)
{
    conditions();
    int alerting = 0, new_alarm = 0;
    for (int i = 0; i < nann; i++) {
        int on = A[i].on;
        switch (A[i].st) {
        case ST_CLEAR:
            if (on) {
                A[i].st = ST_ALERT;
                new_alarm = 1;
            }
            break;
        case ST_ACKED:
            if (!on) A[i].st = ST_RINGBACK;
            break;
        case ST_RINGBACK:
            if (on) {
                A[i].st = ST_ALERT;
                new_alarm = 1;
            }
            break;
        default:
            break;
        }
        alerting |= A[i].st == ST_ALERT;
    }
    if (started < 60) {
        /* the shift turns the board over with standing alarms acknowledged
         * (held for the first second, while the first plant steps settle) */
        for (int i = 0; i < nann; i++) A[i].st = A[i].on ? ST_ACKED : ST_CLEAR;
        started++;
        alerting = new_alarm = 0;
    }
    if (new_alarm) silenced = 0;
    if (have_horn) {
        if (alerting && !silenced) {
            if (!IsSoundPlaying(horn)) PlaySound(horn);
        } else if (IsSoundPlaying(horn)) {
            StopSound(horn);
        }
    }
}

void ann_ack_all_on_start(void) { started = 0; }

int ann_board_alerting(int bd)
{
    for (int i = 0; i < nann; i++)
        if (A[i].bd == bd && A[i].st == ST_ALERT) return 1;
    return 0;
}

static int lit(int i)
{
    if (testing) return 1;
    switch (A[i].st) {
    case ST_ALERT: return blink_fast();
    case ST_ACKED: return 1;
    case ST_RINGBACK: return blink_slow();
    default: return 0;
    }
}

void ann_box(Rectangle r, int bd, int cols, float h)
{
    int n = 0;
    for (int i = 0; i < nann; i++) n += A[i].bd == bd;
    float w = (r.width - 16) / cols;
    DrawRectangleRec(r, (Color){40, 40, 38, 255});
    int k = 0;
    for (int i = 0; i < nann; i++) {
        if (A[i].bd != bd) continue;
        Rectangle t = {r.x + 8 + (k % cols) * w, r.y + 6 + (k / cols) * (h + 2), w - 2, h};
        window(t, A[i].l1, A[i].l2, A[i].c, lit(i));
        k++;
    }
    (void)n;
}

/* SILENCE / ACKNOWLEDGE / RESET / TEST pushbuttons (they act on the whole control room) */
void ann_controls(float x, float y)
{
    int ok = input_ok;
    input_ok = 1;                 /* the alarm system stays with whoever is on the panel */
    if (lampbutton((Rectangle){x, y, 62, 30}, "SILENCE", (Color){80, 80, 76, 255}, 0)) silenced = 1;
    if (lampbutton((Rectangle){x + 66, y, 62, 30}, "ACK", L_WHT, 0)) {
        for (int i = 0; i < nann; i++)
            if (A[i].st == ST_ALERT) A[i].st = A[i].on ? ST_ACKED : ST_RINGBACK;
        silenced = 1;
    }
    if (lampbutton((Rectangle){x + 132, y, 62, 30}, "RESET", L_AMB, 0)) {
        for (int i = 0; i < nann; i++)
            if (A[i].st == ST_RINGBACK) A[i].st = ST_CLEAR;
    }
    Rectangle tb = {x + 198, y, 62, 30};
    testing = CheckCollisionPointRec(GetMousePosition(), tb) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    lampbutton(tb, "TEST", L_WHT, testing);
    input_ok = ok;
}
