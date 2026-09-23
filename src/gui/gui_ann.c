/*
 * Annunciators: one window box over each board section, and the ISA
 * sequence of the period (ISA-18.1 "ringback"):
 *
 *   alarm comes in    -> window flashes fast (4 Hz), horn sounds
 *   SILENCE           -> horn stops, window keeps flashing
 *   ACKNOWLEDGE       -> window steady on (horn stops too)
 *   alarm clears      -> window flashes slowly (1 Hz, ringback)
 *   RESET             -> window goes dark
 *
 * An alarm that clears before it is acknowledged stays locked in. TEST
 * lights every window while it is held. Windows are white engraved lenses
 * backlit in their priority colour (red trip, amber alarm, white status);
 * each box has row letters and column numbers so a window can be called
 * out as "1-2 B4". Each board has its own horn tone, so the operator can
 * tell which board is calling.
 *
 * Also here: the reactor trip functions (shared by the first-out box and
 * the PPS trip status matrix).
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NANN 256
enum { ST_CLEAR, ST_ALERT, ST_ACKED, ST_RINGBACK };

static struct {
    const char *l1, *l2;
    int sec;
    Color c;
    int on;
    int st;
    double t_in;              /* when it last came in */
} A[NANN];
static int nann;
static int silenced, testing;
static int started;
static Sound horn[NBOARDS];
static int have_horn;
static int horn_bd = -1;

static const char *SEC_ID[NSEC] = {"1-1", "1-2", "1-3", "2-1", "2-2", "2-3", "3-1", "3-2", "3-3",
                                   "4-1", "4-2", "5-1", "5-2", "5-3", "RSP"};
static const int SEC_BOARD[NSEC] = {BOARD_REACTOR, BOARD_REACTOR, BOARD_REACTOR, BOARD_HTS,  BOARD_HTS,
                                    BOARD_HTS,     BOARD_TG,      BOARD_TG,      BOARD_TG,   BOARD_ELEC,
                                    BOARD_ELEC,    BOARD_AUX,     BOARD_AUX,     BOARD_AUX,  BOARD_RSP};

int sec_board(int sec) { return sec >= 0 && sec < NSEC ? SEC_BOARD[sec] : -1; }

static int ann_fast(void) { return ((int)(GetTime() * 8)) & 1; }
static int ann_slow(void) { return ((int)(GetTime() * 2)) & 1; }

static void an(int sec, Color c, const char *l1, const char *l2, int cond)
{
    if (nann >= NANN) return;
    A[nann].l1 = l1;
    A[nann].l2 = l2;
    A[nann].sec = sec;
    A[nann].c = c;
    A[nann].on = cond != 0;
    nann++;
}

void ann_init_audio(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    /* a harsh 1970s annunciator buzz, pitched differently for each board */
    static const double base[NBOARDS] = {410, 340, 300, 520, 610, 700};
    const int rate = 22050, n = rate / 2;
    short *d = malloc(sizeof(short) * n);
    if (!d) return;
    for (int b = 0; b < NBOARDS; b++) {
        for (int i = 0; i < n; i++) {
            double t = (double)i / rate;
            double s = (fmod(t * base[b], 1.0) < 0.5 ? 1.0 : -1.0) * 0.55 + sin(2 * PI * 2 * base[b] * t) * 0.25;
            d[i] = (short)(s * 9000);
        }
        Wave w = {.frameCount = (unsigned)n, .sampleRate = (unsigned)rate, .sampleSize = 16, .channels = 1, .data = d};
        horn[b] = LoadSoundFromWave(w);
        SetSoundVolume(horn[b], 0.35f);
    }
    free(d);
    have_horn = 1;
}

/* ---- reactor trip functions ---------------------------------------------------------- */
const char *const trip_l1[NTRIP] = {"MANUAL", "MODE SWITCH", "LOSS OF", "PERIOD", "POWER/FLOW", "PRIMARY",
                                    "PRI PUMPS", "CORE OUTLET", "CLADDING", "STEAM", "TURBINE TRIP", "DND",
                                    "REACTOR NA", "IRM", "APRM", "NA-WATER"};
const char *const trip_l2[NTRIP] = {"SCRAM", "SHUTDOWN", "OFFSITE PWR", "< 10 S", "HIGH", "FLOW LOW",
                                    "2 OF 4 OFF", "TEMP HIGH", "TEMP HIGH", "PRESS HIGH", "> 50% POWER", "HIGH",
                                    "LEVEL LOW", "HI-HI", "HI-HI", "REACTION"};

static int rps_of(int ch) { return (ch & 1) * 2 + ((ch >> 1) & 1); }

/* is trip function k calling for a trip, on RPS channel ch (A1 A2 B1 B2), or any channel if ch < 0 */
int trip_active(int k, int ch)
{
    rm_core *c = &P->core;
    double pw = c->p_thermal / RM_P_RATED;
    double flow = P->W_core / rm_plant_nominal_flow();
    int start = P->mode == RM_MODE_STARTUP || P->mode == RM_MODE_REFUEL;
    switch (k) {
    case 0: return 0;
    case 1: return P->mode == RM_MODE_SHUTDOWN;
    case 2: return !P->offsite_power && pw > 0.05;
    case 3: return P->period > 0 && P->period < 10.0 && c->pks.n > 1e-4;
    case 4: return pw > 0.15 && pw / fmax(flow, 0.01) > 1.15;
    case 5: return pw > 0.10 && flow < 0.70;
    case 6: {
        int off = 0;
        for (int i = 0; i < RM_NLOOPS; i++) off += P->loop[i].ppump.tripped || !P->loop[i].ppump.motor_on;
        return off >= 2 && pw > 0.05;
    }
    case 7: return P->T_core_out > 873.15;
    case 8: return rm_core_max_clad_T(c) > 973.15;
    case 9: return P->p_header > 16.5e6;
    case 10: return P->turbine_tripped && pw > 0.50;
    case 11: return P->aux.dnd > 2000.0;
    case 12: return P->aux.na_level < -300.0;
    case 13:
        if (!start) return 0;
        for (int i = 0; i < 8; i++) {
            if (P->nms.irm_bypass[i & 1] == i) continue;
            if (rm_plant_irm_ch(P, i) > RM_IRM_TRIP && (ch < 0 || rps_of(i) == ch)) return 1;
        }
        return 0;
    case 14: {
        double sp = P->mode == RM_MODE_RUN ? 115.0 : 15.0;
        for (int i = 0; i < 6; i++) {
            if (P->nms.aprm_bypass[i & 1] == i) continue;
            if (rm_plant_aprm_ch(P, i) > sp && (ch < 0 || rps_of(i) == ch)) return 1;
        }
        return 0;
    }
    case 15:
        for (int i = 0; i < RM_NLOOPS; i++)
            if (P->loop[i].sg.disc_burst) return 1;
        return 0;
    }
    return 0;
}

/* which trip function the first-out string names, -1 none */
int trip_first(void)
{
    const char *f = P->first_out;
    if (!f[0]) return -1;
    static const char *const pre[NTRIP] = {"MANUAL", "MODE SWITCH", "LOSS OF OFFSITE", "SHORT PERIOD", "POWER/FLOW",
                                           "LOW PRIMARY FLOW", "PRIMARY PUMP TRIP", "HIGH CORE OUTLET",
                                           "HIGH CLAD", "HIGH STEAM", "TURBINE TRIP", "DELAYED NEUTRON",
                                           "REACTOR SODIUM", "IRM", "APRM", "SODIUM-WATER"};
    for (int k = 0; k < NTRIP; k++)
        if (!strncmp(f, pre[k], strlen(pre[k]))) return k;
    return -1;
}

/* ---- every window on every board, in a fixed order ------------------------------------- */
static void conditions(void)
{
    rm_core *c = &P->core;
    rm_aux *a = &P->aux;
    rm_tg *t = &P->tg;
    rm_nms *nm = &P->nms;
    nann = 0;
    double pw = c->p_thermal / RM_P_RATED;
    double flow = P->W_core / rm_plant_nominal_flow();
    int start = P->mode == RM_MODE_STARTUP || P->mode == RM_MODE_REFUEL;
    double aprm = rm_plant_aprm(P);
    double cps = rm_plant_srm_cps(P);

    int irm_hh = 0, irm_up = 0, irm_dn = 0, aprm_hh = trip_active(14, -1);
    for (int i = 0; i < 8; i++) {
        if (nm->irm_bypass[i & 1] == i) continue;
        double v = rm_plant_irm_ch(P, i);
        irm_hh |= start && v > RM_IRM_TRIP;
        irm_up |= start && v > 108.0;
        irm_dn |= start && nm->irm_range[i] > 1 && v < RM_IRM_DOWNSCALE;
    }
    int half = (nm->div_trip[0] != nm->div_trip[1]) && !c->scram;
    int chan_byp = nm->bypass[0] || nm->bypass[1] || nm->bypass[2] || nm->bypass[3];
    int vital_lost = !(t->inverter[0] && t->batt[0] > 0.02) || !(t->inverter[1] && t->batt[1] > 0.02);

    int drift = 0;
    for (int k = 0; k < c->nctrl; k++) drift |= c->rod_drift[k] != 0.0 && !c->scram;
    double reg = rm_core_bank_pos(c, BANK_REG);
    char rwhy[80];
    int rwm_block = nm->sel_rod >= 0 && !rm_plant_rod_permit(P, -1, nm->sel_rod, 0.0, rwhy, sizeof rwhy) &&
                    strncmp(rwhy, "RWM", 3) == 0;
    int rbm_hi = !nm->rbm_bypass && nm->sel_rod >= 0 && aprm > 30.0 && nm->rbm > 1.08;
    int online = G.on_line && P->generator_breaker;

    int ptrip[RM_NLOOPS], strip_ = 0, h2[RM_NLOOPS], iso = 0, burst = 0, dumped = 0, leak = 0, fire_na = 0;
    int exp_hi = 0, exp_p = 0, purge = 0, refill = 0, brg = 0, pony = 0, loopflow = 0, cold_low = 0, allstop = 1;
    int plug = 0, ct_out = !a->prim_cold_trap, heat_low = 0, msiv = 0, fiv = 0, steam_hot = 0, fw_man = !P->auto_fw;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &P->loop[i];
        ptrip[i] = l->ppump.tripped;
        strip_ |= l->spump.tripped;
        h2[i] = l->sg.h2 > RM_H2_ALARM;
        iso |= l->sg.isolated;
        burst |= l->sg.disc_burst;
        dumped |= l->dumped;
        leak |= l->na_leak > 0.001;
        fire_na |= a->fire[RM_FIRE_SG1 + i];
        exp_hi |= !l->dumped && (l->exp_level > 85.0 || l->exp_level < 15.0);
        exp_p |= l->exp_p > 0.5e6;
        purge |= l->sg.n2_purge;
        refill |= l->refill_t > 0;
        brg |= l->brg_T > 80.0;
        pony |= l->ppump.pony_on && !(l->ppump.motor_on && !l->ppump.tripped) && l->ppump.speed > 0.05;
        loopflow |= l->W < 0.8 * rm_plant_nominal_flow() / RM_NLOOPS && pw > 0.05;
        cold_low |= rm_na_T(l->cold.h[RM_PIPE_N - 1]) < 523.15;
        allstop &= !(l->ppump.motor_on && !l->ppump.tripped);
        plug |= rm_plant_plugging_T(P, i) > 160.0;
        ct_out |= !l->cold_trap;
        heat_low |= rm_na_T(l->scold.h[RM_PIPE_N - 1]) < 423.15 || rm_na_T(l->cold.h[RM_PIPE_N - 1]) < 423.15;
        msiv |= !l->sg.msiv_open;
        fiv |= !l->sg.fiv_open;
        steam_hot |= l->sg.T_steam > 783.15;
        fw_man |= l->sg.fw_manual;
    }
    plug |= rm_plant_plugging_T(P, -1) > 160.0;
    double prog_level = 2.5 * (0.5 * (P->T_core_in + P->T_core_out) - 653.15);
    int fires = 0, heat_off = 0, oos = 0, ptl = 0, dg_run[3];
    for (int f = 0; f < 8; f++) fires |= a->fire[f];
    for (int i = 0; i < 6; i++) heat_off |= !a->heat_auto[i] && !a->heat_on[i];
    for (int i = 0; i < 3; i++) {
        oos |= !P->diesel_avail[i];
        ptl |= P->diesel_ptl[i];
        dg_run[i] = P->diesel_running[i];
    }
    double wfw = 0;
    for (int i = 0; i < RM_NLOOPS; i++) wfw += P->loop[i].sg.W_fw;
    double quad_max = 0, quad_min = 1e9;
    for (int q = 0; q < 4; q++) {
        quad_max = fmax(quad_max, nm->quad[q]);
        quad_min = fmin(quad_min, nm->quad[q]);
    }

    /* ---- 1-1 nuclear instrumentation and protection ---- */
    int S = SEC_NI;
    an(S, L_AMB, "SRM", "HIGH", cps > 1e5 && nm->srm_pos > 0.5);
    an(S, L_AMB, "SRM", "DOWNSCALE", start && nm->srm_pos > 0.99 && rm_plant_srm_ch(P, 0) < 3.0);
    an(S, L_AMB, "STARTUP RATE", "HIGH", P->period > 0 && P->period < 30 && c->pks.n > 1e-9);
    an(S, L_RED, "IRM", "HI-HI TRIP", irm_hh);
    an(S, L_AMB, "IRM", "UPSCALE", irm_up);
    an(S, L_AMB, "IRM", "DOWNSCALE", irm_dn);
    an(S, L_RED, "APRM", "HI-HI TRIP", aprm_hh);
    an(S, L_AMB, "APRM", "UPSCALE", aprm > (P->mode == RM_MODE_RUN ? 110.0 : 12.0));
    an(S, L_AMB, "APRM", "DOWNSCALE", P->mode == RM_MODE_RUN && aprm < 5.0);
    an(S, L_WHT, "APRM SETDOWN", "IN EFFECT", P->mode != RM_MODE_RUN);
    an(S, L_RED, "RPS DIV A", "TRIP", nm->div_trip[0]);
    an(S, L_RED, "RPS DIV B", "TRIP", nm->div_trip[1]);
    an(S, L_AMB, "HALF", "SCRAM", half);
    an(S, L_AMB, "RPS CHANNEL", "BYPASSED", chan_byp);
    an(S, L_RED, "RPS", "BYPASSED", P->rps_bypass);
    an(S, L_AMB, "NI DETECTOR", "NOT FULL IN", start && (nm->srm_pos < 0.99 || nm->irm_pos < 0.99));
    an(S, L_RED, "NI POWER", "LOSS", vital_lost);
    an(S, L_AMB, "FLUX/FLOW", "HIGH", pw > 0.15 && pw / fmax(flow, 0.01) > 1.10);

    /* ---- 1-2 reactor control ---- */
    S = SEC_RC;
    an(S, L_RED, "REACTOR", "SCRAM", c->scram);
    an(S, L_RED, "ROD", "DRIFT", drift);
    an(S, L_AMB, "ROD WITHDRAWAL", "BLOCK", rm_plant_rod_block(P, 0, NULL, 0));
    an(S, L_AMB, "RWM", "ROD BLOCK", rwm_block);
    an(S, L_AMB, "RBM", "UPSCALE", rbm_hi);
    an(S, L_AMB, "GROUP 6", "AT LIMIT", P->auto_rod && !c->scram && (reg < 0.5 || reg > RM_ACTIVE_H - 0.5));
    an(S, L_WHT, "AUTO ROD", "CONTROL", P->auto_rod && !c->scram);
    an(S, L_WHT, "MODE SWITCH", "SHUTDOWN", P->mode == RM_MODE_SHUTDOWN);
    an(S, L_WHT, "MODE SWITCH", "REFUEL", P->mode == RM_MODE_REFUEL);
    an(S, L_AMB, "REACTOR POWER", "DEVIATION", P->auto_rod && !c->scram && fabs(pw - P->power_set) > 0.05);
    an(S, L_RED, "OFFSITE", "POWER LOST", !P->offsite_power);
    an(S, L_AMB, "LOAD", "DEVIATION", online && fabs(rm_game_deviation(&G, P)) > 0.05);
    an(S, L_RED, "DND", "FAILED FUEL", a->dnd > 500.0);
    an(S, L_WHT, "DRACS", "COOLING", P->Q_dracs > 1e6);
    an(S, L_RED, "CONTROL RM", "FIRE / SMOKE", a->fire[RM_FIRE_CR]);
    an(S, L_AMB, "HTS BOARD", "TROUBLE", ann_board_alerting(BOARD_HTS));
    an(S, L_AMB, "T-G BOARD", "TROUBLE", ann_board_alerting(BOARD_TG));
    an(S, L_AMB, "ELEC BOARD", "TROUBLE", ann_board_alerting(BOARD_ELEC));
    an(S, L_AMB, "AUX BOARD", "TROUBLE", ann_board_alerting(BOARD_AUX));

    /* ---- 1-3 core monitoring ---- */
    S = SEC_CM;
    an(S, L_RED, "FUEL", "TEMP HIGH", CS_fuel_max > 1300.0);
    an(S, L_RED, "CLADDING", "TEMP HIGH", rm_core_max_clad_T(c) > 923.15);
    an(S, L_AMB, "CORE OUTLET", "TEMP HIGH", P->T_core_out > 833.15);
    an(S, L_RED, "BOILING", "MARGIN LOW", CS_margin_min < 200);
    an(S, L_AMB, "QUADRANT", "POWER TILT", pw > 0.2 && quad_max / fmax(quad_min, 1e-6) > 1.10);

    /* ---- 2-1 primary heat transport ---- */
    S = SEC_PHT;
    an(S, L_AMB, "PRI PUMP 1", "TRIP", ptrip[0]);
    an(S, L_AMB, "PRI PUMP 2", "TRIP", ptrip[1]);
    an(S, L_AMB, "PRI PUMP 3", "TRIP", ptrip[2]);
    an(S, L_AMB, "PRI PUMP 4", "TRIP", ptrip[3]);
    an(S, L_AMB, "CORE FLOW", "LOW", flow < 0.9);
    an(S, L_AMB, "PRI LOOP", "FLOW LOW", loopflow);
    an(S, L_WHT, "PONY MOTOR", "RUNNING", pony);
    an(S, L_AMB, "PRI PUMP", "BEARING HOT", brg);
    an(S, L_AMB, "CORE INLET", "TEMP HIGH", P->T_core_in > 693.15);
    an(S, L_AMB, "CORE OUTLET", "TEMP HIGH", P->T_core_out > 833.15);
    an(S, L_AMB, "REACTOR NA", "LEVEL ABN", fabs(a->na_level - prog_level) > 100.0);
    an(S, L_AMB, "COLD LEG", "TEMP LOW", cold_low);
    an(S, L_WHT, "NATURAL", "CIRCULATION", allstop && P->W_core > 1.0);

    /* ---- 2-2 intermediate loops and steam generator protection ---- */
    S = SEC_IHT;
    an(S, L_AMB, "SEC PUMP", "TRIP", strip_);
    an(S, L_RED, "SG 1", "H2 HIGH", h2[0]);
    an(S, L_RED, "SG 2", "H2 HIGH", h2[1]);
    an(S, L_RED, "SG 3", "H2 HIGH", h2[2]);
    an(S, L_RED, "SG 4", "H2 HIGH", h2[3]);
    an(S, L_RED, "RUPTURE DISC", "BURST", burst);
    an(S, L_AMB, "SG", "ISOLATED", iso);
    an(S, L_AMB, "SECONDARY", "LOOP DUMPED", dumped);
    an(S, L_RED, "SEC NA", "LEAK", leak);
    an(S, L_RED, "SODIUM FIRE", "SG CELL", fire_na);
    an(S, L_AMB, "EXP TANK", "LEVEL ABN", exp_hi);
    an(S, L_AMB, "EXP TANK", "PRESS HIGH", exp_p);
    an(S, L_WHT, "N2 PURGE", "ON", purge);
    an(S, L_WHT, "LOOP", "REFILLING", refill);

    /* ---- 2-3 decay heat removal and containment isolation ---- */
    S = SEC_DHR;
    an(S, L_WHT, "DRACS A", "DAMPER OPEN", P->dracs_damper[0] > 0.5);
    an(S, L_WHT, "DRACS B", "DAMPER OPEN", P->dracs_damper[1] > 0.5);
    an(S, L_WHT, "DRACS C", "DAMPER OPEN", P->dracs_damper[2] > 0.5);
    an(S, L_WHT, "DRACS", "COOLING", P->Q_dracs > 1e6);
    an(S, L_AMB, "DECAY HEAT", "> DRACS", c->scram && allstop && c->p_decay > 1.2 * P->Q_dracs);
    an(S, L_WHT, "DRACS", "MANUAL", !P->dracs_auto || P->dracs_man[0] || P->dracs_man[1] || P->dracs_man[2]);
    an(S, L_AMB, "CONTAINMENT", "ISOLATED", P->containment_isolated);
    an(S, L_RED, "HIGH RAD", "CIS SIGNAL", a->rad[RM_RAD_STACK] > 50.0 || a->rad[RM_RAD_HALL] > 100.0);
    an(S, L_AMB, "INSTR AIR LOW", "DAMPERS OPEN", a->air_p < 3.0);

    /* ---- 3-1 feedwater and main steam ---- */
    S = SEC_FW;
    an(S, L_AMB, "FEEDWATER", "OUT OF SERV", !P->fw_on);
    an(S, L_AMB, "FEED PUMP", "CAPACITY LOW", P->fw_on && t->fw_cap < 0.95 * fmax(pw, 0.2));
    an(S, L_AMB, "FEED/STEAM", "MISMATCH", P->fw_on && fabs(wfw - (P->W_turbine + P->W_bypass + P->W_relief)) > 120.0);
    an(S, L_AMB, "STEAM PRESS", "LOW", P->p_header < 12e6);
    an(S, L_RED, "STEAM PRESS", "HIGH", P->p_header > 15.5e6);
    an(S, L_AMB, "STEAM SAFETY", "VALVE OPEN", P->W_relief > 0);
    an(S, L_AMB, "MSIV", "CLOSED", msiv);
    an(S, L_AMB, "FEED ISOL", "VALVE CLOSED", fiv);
    an(S, L_AMB, "HOTWELL", "LEVEL HIGH", t->hotwell > 1.4);
    an(S, L_AMB, "DEAERATOR", "LEVEL ABN", t->da_level < 1.5 || t->da_level > 2.6);
    an(S, L_AMB, "HP HEATERS", "OUT OF SERV", !t->heaters_in);
    an(S, L_WHT, "FEED CONTROL", "MANUAL", fw_man);
    an(S, L_RED, "INSTRUMENT", "AIR LOW", a->air_p < 5.0);
    an(S, L_AMB, "SG STEAM", "TEMP HIGH", steam_hot);

    /* ---- 3-2 turbine and condenser ---- */
    S = SEC_TURB;
    an(S, L_AMB, "TURBINE", "TRIP", P->turbine_tripped);
    an(S, L_RED, "TURBINE", "OVERSPEED", t->speed > 1950.0);
    an(S, L_RED, "VIBRATION", "HIGH", t->vib > 5.0);
    an(S, L_AMB, "ECCENTRICITY", "HIGH", t->ecc > 2.0 && t->speed < 600.0);
    an(S, L_RED, "BEARING", "TEMP HIGH", t->brg_T > 95.0);
    an(S, L_RED, "LUBE OIL", "PRESS LOW", t->lube_p < 1.0);
    an(S, L_AMB, "TURN GEAR OFF", "ROTOR STOPPED", !t->turning_gear && t->speed < 5.0);
    an(S, L_WHT, "AUX OIL PUMP", "RUNNING", t->aux_oil_pump && P->offsite_power);
    an(S, L_RED, "CONDENSER", "VACUUM LOW", P->p_cond > 15e3);
    an(S, L_AMB, "CIRC WATER", "PUMP OFF", !t->cw[0] || !t->cw[1] || !t->cw[2]);
    an(S, L_WHT, "TURBINE", "BYPASS OPEN", P->bypass_valve > 0.02);
    an(S, L_WHT, "TURBINE ON", "SPEED CONTROL", !P->turbine_tripped && !P->generator_breaker);

    /* ---- 3-3 generator ---- */
    S = SEC_GEN;
    an(S, L_AMB, "GEN BREAKER", "OPEN", !P->generator_breaker);
    an(S, L_AMB, "GEN FIELD", "BKR OPEN", !t->field_breaker);
    an(S, L_AMB, "VOLTAGE REG", "MANUAL", !t->avr_auto);
    an(S, L_AMB, "GENERATOR", "OVEREXCITED", t->mvar > 300.0);
    an(S, L_AMB, "GENERATOR", "UNDEREXCITED", t->mvar < -200.0);
    an(S, L_WHT, "AUTO SYNC", "IN SERVICE", t->auto_sync);
    an(S, L_AMB, "LOAD", "DEVIATION", online && fabs(rm_game_deviation(&G, P)) > 0.05);
    an(S, L_RED, "GRID", "LOST", !P->grid_ok || !P->grid_breaker);

    /* ---- 4-1 electrical distribution ---- */
    S = SEC_DIST;
    an(S, L_RED, "345 KV GRID", "LOST", !P->grid_ok);
    an(S, L_AMB, "SWYD BREAKER", "OPEN", !P->grid_breaker);
    an(S, L_AMB, "GEN BREAKER", "OPEN", !P->generator_breaker);
    an(S, L_WHT, "HOUSE LOAD", "ON SAT", !t->aux_on_uat && P->offsite_power);
    an(S, L_RED, "FAST TRANSFER", "FAILURE", t->transfer_fail && !P->offsite_power);
    an(S, L_RED, "6.9 KV BUSES", "DEAD", !P->offsite_power);
    an(S, L_RED, "ESS BUS A", "UNDERVOLTAGE", !P->offsite_power && !dg_run[0]);
    an(S, L_RED, "ESS BUS B", "UNDERVOLTAGE", !P->offsite_power && !dg_run[1]);
    an(S, L_RED, "ESS BUS C", "UNDERVOLTAGE", !P->offsite_power && !dg_run[2]);
    an(S, L_AMB, "ESS BUS A", "ON DIESEL", !P->offsite_power && dg_run[0]);
    an(S, L_AMB, "ESS BUS B", "ON DIESEL", !P->offsite_power && dg_run[1]);
    an(S, L_AMB, "ESS BUS C", "ON DIESEL", !P->offsite_power && dg_run[2]);

    /* ---- 4-2 emergency power and DC ---- */
    S = SEC_EPWR;
    an(S, L_WHT, "DG 1", "RUNNING", dg_run[0]);
    an(S, L_WHT, "DG 2", "RUNNING", dg_run[1]);
    an(S, L_WHT, "DG 3", "RUNNING", dg_run[2]);
    an(S, L_AMB, "DIESEL GEN", "OUT OF SERV", oos);
    an(S, L_RED, "DIESEL GEN", "FAIL TO START", !P->offsite_power && oos);
    an(S, L_AMB, "DIESEL", "PULL TO LOCK", ptl);
    an(S, L_RED, "BATTERY A", "DISCHARGING", !t->charger[0] || !rm_plant_essential_power(P));
    an(S, L_RED, "BATTERY B", "DISCHARGING", !t->charger[1] || !rm_plant_essential_power(P));
    an(S, L_AMB, "BATTERY", "CHARGER OFF", !t->charger[0] || !t->charger[1]);
    an(S, L_RED, "VITAL AC", "INVERTER OFF", vital_lost);
    an(S, L_RED, "BATTERY", "CHARGE LOW", t->batt[0] < 0.3 || t->batt[1] < 0.3);

    /* ---- 5-1 sodium auxiliaries ---- */
    S = SEC_NAAUX;
    an(S, L_AMB, "COVER GAS", "PRESSURE ABN", a->gas_p > 0.14e6 || a->gas_p < 0.105e6);
    an(S, L_RED, "COVER GAS", "ACTIVITY HIGH", a->gas_act > 50.0);
    an(S, L_RED, "DND", "FAILED FUEL", a->dnd > 500.0);
    an(S, L_AMB, "PLUGGING", "TEMP HIGH", plug);
    an(S, L_AMB, "COLD TRAP", "OUT OF SERV", ct_out);
    an(S, L_AMB, "TRACE HEAT", "PIPE TEMP LOW", heat_low);
    an(S, L_WHT, "TRACE HEAT", "CIRCUIT OFF", heat_off);
    an(S, L_RED, "PRI NA LEAK", "GUARD VESSEL", a->prim_leak > 0.0005);
    an(S, L_RED, "SEC NA", "LEAK", leak);
    an(S, L_AMB, "PRIMARY NA", "INVENTORY LOW", a->prim_inventory < 0.995);
    an(S, L_AMB, "GAS CLEANUP", "OFF", !a->cleanup);

    /* ---- 5-2 containment and radiation ---- */
    S = SEC_CONT;
    an(S, L_AMB, "CONTAINMENT", "PRESS HIGH", a->cont_p > 103.0);
    an(S, L_AMB, "CONTAINMENT", "TEMP HIGH", a->cont_T > 45.0);
    an(S, L_RED, "CELL O2", "HIGH", a->cell_o2 > 4.0);
    an(S, L_AMB, "PRIMARY CELL", "TEMP HIGH", a->cell_T > 80.0);
    an(S, L_RED, "STACK", "RAD HIGH", a->rad[RM_RAD_STACK] > 10.0);
    an(S, L_RED, "AREA", "RAD HIGH", a->rad[RM_RAD_HALL] > 25.0);
    an(S, L_AMB, "COVER GAS", "RAD HIGH", a->rad[RM_RAD_GAS] > 50.0);
    an(S, L_RED, "CONTROL RM", "RAD HIGH", a->rad[RM_RAD_CR] > 1.0);
    an(S, L_AMB, "CR HVAC", "EMERGENCY", a->hvac_emerg);
    an(S, L_AMB, "CELL N2", "SUPPLY SHUT", !a->n2_supply);
    an(S, L_AMB, "CONTAINMENT", "ISOLATED", P->containment_isolated);

    /* ---- 5-3 plant services and fire protection ---- */
    S = SEC_SERV;
    an(S, L_AMB, "CCW", "TEMP HIGH", a->ccw_T > 40.0);
    an(S, L_AMB, "CCW PUMP", "OFF", !a->ccw[0] || !a->ccw[1]);
    an(S, L_AMB, "SERVICE WATER", "PUMP OFF", !a->sw[0] || !a->sw[1]);
    an(S, L_RED, "INSTRUMENT", "AIR LOW", a->air_p < 5.0);
    an(S, L_AMB, "AIR", "COMPRESSOR OFF", !a->air_comp[0] || !a->air_comp[1]);
    an(S, L_WHT, "FIRE PUMP", "RUNNING", a->fire_pump[0] || a->fire_pump[1]);
    an(S, L_RED, "FIRE", "SG CELL", fire_na);
    an(S, L_RED, "FIRE", "PRIMARY CELL", a->fire[RM_FIRE_CELL]);
    an(S, L_RED, "FIRE", "TURBINE HALL", a->fire[RM_FIRE_TURB]);
    an(S, L_RED, "FIRE", "CABLE ROOM", a->fire[RM_FIRE_CABLE]);
    an(S, L_RED, "FIRE", "CONTROL ROOM", a->fire[RM_FIRE_CR]);
    an(S, L_RED, "FIRE", "DETECTED", fires);

    /* ---- remote shutdown panel ---- */
    S = SEC_RSP;
    an(S, L_RED, "CONTROL ROOM", "EVACUATED", a->evacuated);
    an(S, L_RED, "REACTOR", "NOT SHUT DOWN", !c->scram && a->evacuated);
    an(S, L_AMB, "RSP", "IN CONTROL", a->rsp_control);
    an(S, L_RED, "OFFSITE", "POWER LOST", !P->offsite_power);
    an(S, L_AMB, "CORE OUTLET", "TEMP HIGH", P->T_core_out > 833.15);
    an(S, L_WHT, "DRACS", "COOLING", P->Q_dracs > 1e6);
    an(S, L_WHT, "DIESEL GEN", "RUNNING", dg_run[0] || dg_run[1] || dg_run[2]);
    an(S, L_RED, "SG", "H2 HIGH", h2[0] || h2[1] || h2[2] || h2[3]);
}

void ann_eval(void)
{
    conditions();
    int alerting = 0, new_alarm = -1;
    double now = GetTime();
    for (int i = 0; i < nann; i++) {
        int on = A[i].on;
        switch (A[i].st) {
        case ST_CLEAR:
        case ST_RINGBACK:
            if (on) {
                A[i].st = ST_ALERT;
                A[i].t_in = now;
                new_alarm = i;
            }
            break;
        case ST_ACKED:
            if (!on) A[i].st = ST_RINGBACK;
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
        alerting = 0;
        new_alarm = -1;
    }
    if (new_alarm >= 0) {
        silenced = 0;
        horn_bd = sec_board(A[new_alarm].sec);
    }
    if (have_horn) {
        for (int b = 0; b < NBOARDS; b++) {
            int want = alerting && !silenced && b == horn_bd;
            if (want && !IsSoundPlaying(horn[b])) PlaySound(horn[b]);
            if (!want && IsSoundPlaying(horn[b])) StopSound(horn[b]);
        }
    }
}

void ann_ack_all_on_start(void) { started = 0; }

int ann_board_alerting(int bd)
{
    for (int i = 0; i < nann; i++)
        if (sec_board(A[i].sec) == bd && A[i].st == ST_ALERT) return 1;
    return 0;
}

static int lit(int i)
{
    if (testing) return 1;
    switch (A[i].st) {
    case ST_ALERT: return ann_fast();
    case ST_ACKED: return 1;
    case ST_RINGBACK: return ann_slow();
    default: return 0;
    }
}

/* a section's window box: row letters down the left, column numbers along
 * the bottom, spare windows blank */
void ann_box(Rectangle r, int sec, int cols)
{
    const int rows = 3;
    DrawRectangleRec((Rectangle){r.x - 2, r.y - 2, r.width + 4, r.height + 4}, (Color){14, 14, 12, 255});
    DrawRectangleRec(r, (Color){26, 26, 24, 255});
    float lx = 16, by = 14;
    Rectangle g = {r.x + lx, r.y + 3, r.width - lx - 3, r.height - by - 4};
    float w = g.width / cols, h = g.height / rows;
    int k = 0;
    for (int i = 0; i < nann; i++) {
        if (A[i].sec != sec) continue;
        if (k >= rows * cols) break;
        Rectangle t = {g.x + (k % cols) * w + 1, g.y + (k / cols) * h + 1, w - 2, h - 2};
        window(t, A[i].l1, A[i].l2, A[i].c, lit(i));
        k++;
    }
    for (; k < rows * cols; k++) {
        Rectangle t = {g.x + (k % cols) * w + 1, g.y + (k / cols) * h + 1, w - 2, h - 2};
        window(t, NULL, NULL, L_WHT, testing);
    }
    Color lab = {226, 224, 214, 255};
    for (int rr = 0; rr < rows; rr++) {
        char s[2] = {(char)('A' + rr), 0};
        ctext(s, r.x + lx / 2, g.y + rr * h + h / 2 - 5, 10, lab);
    }
    for (int cc = 0; cc < cols; cc++) {
        char s[12];
        snprintf(s, sizeof s, "%d", cc + 1);
        ctext(s, g.x + cc * w + w / 2, r.y + r.height - by + 1, 10, lab);
    }
    text(SEC_ID[sec], r.x + 2, r.y + r.height - by + 1, 10, (Color){250, 190, 60, 255});
}

/* SILENCE / ACKNOWLEDGE / RESET / TEST pushbuttons with their coloured collars */
void ann_controls(float x, float y)
{
    int ok = input_ok;
    input_ok = 1;                 /* the alarm system stays with whoever is on the panel */
    demarc((Rectangle){x - 8, y - 4, 262, 40}, NULL);
    if (pbround((Vector2){x + 16, y + 12}, 9, NULL, (Color){30, 30, 28, 255}, (Color){30, 30, 28, 255}, 0)) silenced = 1;
    text("SILENCE", x + 30, y + 7, 10, INK);
    if (pbround((Vector2){x + 94, y + 12}, 9, NULL, (Color){30, 30, 28, 255}, (Color){40, 150, 60, 255}, 0)) {
        for (int i = 0; i < nann; i++)
            if (A[i].st == ST_ALERT) A[i].st = A[i].on ? ST_ACKED : ST_RINGBACK;
        silenced = 1;
    }
    text("ACK", x + 108, y + 7, 10, INK);
    if (pbround((Vector2){x + 150, y + 12}, 9, NULL, (Color){30, 30, 28, 255}, (Color){200, 40, 30, 255}, 0)) {
        for (int i = 0; i < nann; i++)
            if (A[i].st == ST_RINGBACK) A[i].st = ST_CLEAR;
    }
    text("RESET", x + 164, y + 7, 10, INK);
    Vector2 tc = {x + 212, y + 12};
    testing = CheckCollisionPointCircle(GetMousePosition(), tc, 12) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    pbround(tc, 9, NULL, (Color){236, 234, 224, 255}, (Color){30, 30, 28, 255}, testing);
    text("TEST", x + 226, y + 7, 10, INK);
    input_ok = ok;
}

/* reactor trip first-out box: the first trip to come in flashes; any other
 * trip function still calling is steady */
void firstout_box(Rectangle r)
{
    DrawRectangleRec((Rectangle){r.x - 2, r.y - 2, r.width + 4, r.height + 4}, (Color){14, 14, 12, 255});
    DrawRectangleRec(r, (Color){26, 26, 24, 255});
    ctext("REACTOR TRIP  -  FIRST OUT", r.x + r.width / 2, r.y + 3, 10, (Color){250, 190, 60, 255});
    int cols = 4, rows = 4, f = trip_first();
    float w = (r.width - 6) / cols, h = (r.height - 20) / rows;
    for (int k = 0; k < NTRIP; k++) {
        Rectangle t = {r.x + 3 + (k % cols) * w + 1, r.y + 17 + (k / cols) * h + 1, w - 2, h - 2};
        int on = testing || (k == f ? ann_fast() : trip_active(k, -1));
        window(t, trip_l1[k], trip_l2[k], L_RED, on);
    }
}
