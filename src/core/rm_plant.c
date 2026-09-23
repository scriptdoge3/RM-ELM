#include "rm_plant.h"
#include "rm_if97.h"
#include "rm_sodium.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- design point ------------------------------------------------------ */
#define W_P0 (RM_P_RATED / (1270.0 * 160.0) / RM_NLOOPS)   /* primary kg/s per loop */
#define W_S0 2744.0                                         /* secondary kg/s per loop */
#define T_COLD0 653.15
#define T_HOT0 813.15
#define TS_COLD0 618.15
#define TS_HOT0 783.15
#define DP_CORE0 0.45e6
#define DP_LOOP0 0.15e6
#define H_THERMAL 8.0        /* m, core centre to IHX centre */
#define LOOP_INERTIA 150.0   /* sum of L/A per loop, 1/m */
#define P_STEAM0 14.0e6
#define T_STEAM0 753.15      /* 480 C */
#define T_FW0 513.15         /* 240 C */
#define V_HEADER 200.0
#define ETA_TURB 0.71
#define ETA_GEN 0.985
#define W_CW 40000.0
#define P_HOUSE0 45.0e6
#define SG_TUBE_VOL 10.0     /* m3 water side per SG */
#define G 9.80665

static double W_T0 = 0.0;    /* nominal total steam flow, set at init */
static double chan_share[1024], byp_share[1024];

double rm_na_T_of_h(double h) { return rm_na_T(h); }

static double na_cp_h(double h)
{
    double T = rm_na_T(h);
    if (T <= RM_NA_TMELT + 1e-6) return 1.0e7;   /* freezing plateau */
    return rm_na_cp(T);
}

/* ---- pipes and plena ---------------------------------------------------- */
static void pipe_init(rm_pipe *p, double mass, double wall_mass, double T)
{
    p->mass = mass;
    p->wall_C = wall_mass * 500.0 / RM_PIPE_N;
    for (int i = 0; i < RM_PIPE_N; i++) {
        p->h[i] = rm_na_h(T);
        p->wall_T[i] = T;
    }
}

/* returns outlet enthalpy */
static double pipe_step(rm_pipe *p, double W, double h_in, double dt)
{
    double m = p->mass / RM_PIPE_N;
    double w = W > 0 ? W : 0.0;
    double hup = h_in;
    for (int i = 0; i < RM_PIPE_N; i++) {
        double cp = na_cp_h(p->h[i]);
        double M = m + p->wall_C / cp;
        p->h[i] = (M / dt * p->h[i] + w * hup) / (M / dt + w);
        p->wall_T[i] = rm_na_T(p->h[i]);
        hup = p->h[i];
    }
    return p->h[RM_PIPE_N - 1];
}

/* ---- IHX ---------------------------------------------------------------- */
static void ihx_init(rm_ihx *x)
{
    x->UA0 = 17.7e6;
    x->mp = 15000.0 / RM_HX_N;
    x->ms = 10000.0 / RM_HX_N;
    x->mw_C = 30000.0 * 500.0 / RM_HX_N;
    for (int k = 0; k < RM_HX_N; k++) {
        double f = (k + 0.5) / RM_HX_N;
        double Tp = T_HOT0 + (T_COLD0 - T_HOT0) * f;
        double Ts = TS_HOT0 + (TS_COLD0 - TS_HOT0) * f;
        x->hp[k] = rm_na_h(Tp);
        x->hs[k] = rm_na_h(Ts);
        x->Tw[k] = 0.5 * (Tp + Ts);
    }
}

/* primary flows k = 0 -> N-1, secondary flows k = N-1 -> 0 */
static void ihx_step(rm_ihx *x, double Wp, double hp_in, double Ws, double hs_in, double dt,
                     double *hp_out, double *hs_out)
{
    double wp = Wp > 0 ? Wp : 0.0, ws = Ws > 0 ? Ws : 0.0;
    double fp = pow(fmax(wp / W_P0, 0.02), 0.3), fs = pow(fmax(ws / W_S0, 0.02), 0.3);
    double UAp = 2.0 * x->UA0 / RM_HX_N * fp, UAs = 2.0 * x->UA0 / RM_HX_N * fs;
    double hp_old[RM_HX_N], hs_old[RM_HX_N], Tw_old[RM_HX_N];
    memcpy(hp_old, x->hp, sizeof hp_old);
    memcpy(hs_old, x->hs, sizeof hs_old);
    memcpy(Tw_old, x->Tw, sizeof Tw_old);
    for (int it = 0; it < 4; it++) {
        for (int k = 0; k < RM_HX_N; k++) {
            double Tp = rm_na_T(x->hp[k]), Ts = rm_na_T(x->hs[k]);
            x->Tw[k] = (x->mw_C / dt * Tw_old[k] + UAp * Tp + UAs * Ts) / (x->mw_C / dt + UAp + UAs);
        }
        double hup = hp_in;
        for (int k = 0; k < RM_HX_N; k++) {
            double cp = na_cp_h(hp_old[k]);
            double Told = rm_na_T(hp_old[k]);
            x->hp[k] = (x->mp / dt * hp_old[k] + wp * hup + UAp * (x->Tw[k] - Told + hp_old[k] / cp)) /
                       (x->mp / dt + wp + UAp / cp);
            hup = x->hp[k];
        }
        hup = hs_in;
        for (int k = RM_HX_N - 1; k >= 0; k--) {
            double cp = na_cp_h(hs_old[k]);
            double Told = rm_na_T(hs_old[k]);
            x->hs[k] = (x->ms / dt * hs_old[k] + ws * hup + UAs * (x->Tw[k] - Told + hs_old[k] / cp)) /
                       (x->ms / dt + ws + UAs / cp);
            hup = x->hs[k];
        }
    }
    double Q = 0;
    for (int k = 0; k < RM_HX_N; k++) Q += UAs * (x->Tw[k] - rm_na_T(x->hs[k]));
    x->Q = Q;
    *hp_out = x->hp[RM_HX_N - 1];
    *hs_out = x->hs[0];
}

/* ---- steam generator ---------------------------------------------------- */
static double h_water_side(const rm_if97_state *st, double wf)
{
    double f = pow(fmax(wf, 0.05), 0.8);
    if (st->region == 1) return 9000.0 * f;
    if (st->region == 4) {
        if (st->x < 0.75) return 35000.0;
        double s = (st->x - 0.75) / 0.25;
        return 35000.0 * (1 - s) + 3500.0 * f * s;
    }
    return 3500.0 * f;
}

static void sg_init(rm_sg *s, double hfw, double hst)
{
    s->area = 3200.0;
    s->mna = 25000.0 / RM_HX_N;
    s->mw_C = 40000.0 * 500.0 / RM_HX_N;
    s->fw_valve = 0.8;
    for (int k = 0; k < RM_HX_N; k++) {
        double f = (k + 0.5) / RM_HX_N;
        double Tna = TS_HOT0 + (TS_COLD0 - TS_HOT0) * f;
        s->hna[k] = rm_na_h(Tna);
        s->hw[k] = hst + (hfw - hst) * f;
        s->Tw[k] = Tna - 20.0;
        s->mwater[k] = 400.0;
    }
}

/* sodium flows k = 0 -> N-1 (down), water flows k = N-1 -> 0 (up) */
static double sg_step(rm_sg *s, double Wna, double hna_in, double p, double hfw, double dt)
{
    double w = Wna > 0 ? Wna : 0.0;
    double wf = s->isolated ? 0.0 : s->W_fw;
    double Ak = s->area / RM_HX_N;
    double hna_old[RM_HX_N], hw_old[RM_HX_N], Tw_old[RM_HX_N];
    memcpy(hna_old, s->hna, sizeof hna_old);
    memcpy(hw_old, s->hw, sizeof hw_old);
    memcpy(Tw_old, s->Tw, sizeof Tw_old);
    rm_if97_sat sat;
    rm_if97_sat_p(p, &sat);
    rm_if97_state st[RM_HX_N];
    for (int k = 0; k < RM_HX_N; k++) rm_if97_ph_sat(&sat, hw_old[k], &st[k]);
    double Gna = 25000.0 * Ak * pow(fmax(w / W_S0, 0.02), 0.3);
    double Q = 0;
    for (int it = 0; it < 3; it++) {
        for (int k = 0; k < RM_HX_N; k++) {
            double Gw = h_water_side(&st[k], wf / (W_T0 / RM_NLOOPS)) * Ak;
            double Tna = rm_na_T(s->hna[k]);
            s->Tw[k] = (s->mw_C / dt * Tw_old[k] + Gna * Tna + Gw * st[k].T) / (s->mw_C / dt + Gna + Gw);
        }
        double hup = hna_in;
        for (int k = 0; k < RM_HX_N; k++) {
            double cp = na_cp_h(hna_old[k]);
            double Told = rm_na_T(hna_old[k]);
            s->hna[k] = (s->mna / dt * hna_old[k] + w * hup + Gna * (s->Tw[k] - Told + hna_old[k] / cp)) /
                        (s->mna / dt + w + Gna / cp);
            hup = s->hna[k];
        }
        hup = hfw;
        Q = 0;
        for (int k = RM_HX_N - 1; k >= 0; k--) {
            double Gw = h_water_side(&st[k], wf / (W_T0 / RM_NLOOPS)) * Ak;
            double M = st[k].rho * SG_TUBE_VOL / RM_HX_N;
            double cpw = st[k].region == 4 ? 1e9 : st[k].cp;
            s->hw[k] = (M / dt * hw_old[k] + wf * hup + Gw * (s->Tw[k] - st[k].T + hw_old[k] / cpw)) /
                       (M / dt + wf + Gw / cpw);
            Q += Gw * (s->Tw[k] - st[k].T);
            hup = s->hw[k];
        }
        for (int k = 0; k < RM_HX_N; k++) rm_if97_ph_sat(&sat, s->hw[k], &st[k]);
    }
    s->Q = Q;
    s->W_steam = wf;
    s->h_steam = s->hw[0];
    s->T_steam = st[0].T;
    return s->hna[RM_HX_N - 1];
}

/* ---- pumps -------------------------------------------------------------- */
static void pump_step(rm_pump *p, double dt, int main_power, int ess_power)
{
    if (p->motor_on && !p->tripped && main_power) {
        p->speed += (p->speed_set - p->speed) * dt / 5.0;
    } else {
        /* flywheel coastdown: half speed after ~12 s */
        p->speed -= p->speed * p->speed / 12.0 * dt;
        if (p->pony_on && ess_power && p->speed < 0.10) p->speed = 0.10;
    }
    if (p->speed < 0) p->speed = 0;
}

static double pump_head(const rm_pump *p, double W)
{
    double dp0 = DP_CORE0 + DP_LOOP0;
    double s = p->speed, q = W / W_P0;
    return dp0 * (1.25 * s * s - 0.25 * q * fabs(q));
}

/* ---- init ---------------------------------------------------------------- */
int rm_plant_init(rm_plant *p)
{
    memset(p, 0, sizeof *p);
    if (rm_core_init(&p->core) != 0) return -1;
    double hfw = rm_if97_h_pT(15.0e6, T_FW0);
    double hst = rm_if97_h_pT(P_STEAM0, T_STEAM0);
    W_T0 = RM_P_RATED / (hst - hfw);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        pipe_init(&l->hot, 10400.0, 20000.0, T_HOT0);
        pipe_init(&l->cold, 15000.0, 30000.0, T_COLD0);
        ihx_init(&l->ihx);
        pipe_init(&l->shot, 16800.0, 30000.0, TS_HOT0);
        pipe_init(&l->scold, 16800.0, 30000.0, TS_COLD0);
        sg_init(&l->sg, hfw, hst);
        l->W = W_P0;
        l->Ws = W_S0;
        l->ppump.speed = l->ppump.speed_set = 1.0;
        l->ppump.motor_on = l->ppump.pony_on = 1;
        l->spump.speed = l->spump.speed_set = 1.0;
        l->spump.motor_on = 1;
        l->sg.W_fw = W_T0 / RM_NLOOPS;
    }
    p->m_inplenum = 40000.0;
    p->m_outplenum = 200000.0;
    p->h_inplenum = rm_na_h(T_COLD0);
    p->h_outplenum = rm_na_h(T_HOT0);
    p->W_core = W_P0 * RM_NLOOPS;
    p->cover_gas_p = 0.12e6;
    p->p_header = P_STEAM0;
    p->V_header = V_HEADER;
    p->h_header = hst;
    rm_if97_state st;
    rm_if97_ph(P_STEAM0, hst, &st);
    p->m_header = st.rho * V_HEADER;
    p->turbine_valve = 0.9;
    p->generator_breaker = 1;
    p->auto_fw = p->auto_turbine = 1;
    p->auto_rod = 1;
    p->power_set = 1.0;
    p->offsite_power = 1;
    for (int i = 0; i < 3; i++) p->diesel_avail[i] = 1;
    p->fw_pump = 1.0;
    p->cw_pump = 1.0;
    p->dracs_auto = 1;
    p->T_air = 293.15;
    p->p_set = P_STEAM0;
    p->T_steam_set = T_STEAM0;
    p->T_cw_in = 293.15;
    p->T_fw = T_FW0;
    p->p_cond = 6.0e3;
    p->period = INFINITY;
    return 0;
}

void rm_plant_free(rm_plant *p) { rm_core_free(&p->core); }

/* ---- step ---------------------------------------------------------------- */
static void hydraulics(rm_plant *p, double dt)
{
    const int nsub = 5;
    double h = dt / nsub;
    double K_core = DP_CORE0 / ((W_P0 * RM_NLOOPS) * (W_P0 * RM_NLOOPS));
    double K_loop = DP_LOOP0 / (W_P0 * W_P0);
    double rho_cold = rm_na_rho(fmax(rm_na_T(p->h_inplenum), RM_NA_TMELT));
    double rho_hot = rm_na_rho(fmax(rm_na_T(p->h_outplenum), RM_NA_TMELT));
    double rho_dr = rm_na_rho(fmax(rm_na_T(p->h_dracs_out > 0 ? p->h_dracs_out : p->h_outplenum), RM_NA_TMELT));
    for (int s = 0; s < nsub; s++) {
        double Wc = p->W_dracs;
        for (int i = 0; i < RM_NLOOPS; i++) Wc += p->loop[i].W;
        /* laminar floor keeps natural circulation from being over-resisted at low flow */
        double dpc = K_core * Wc * fabs(Wc) + 0.004 * DP_CORE0 * Wc / (W_P0 * RM_NLOOPS);
        for (int i = 0; i < RM_NLOOPS; i++) {
            rm_loop *l = &p->loop[i];
            double buoy = G * H_THERMAL * (rho_cold - rho_hot);
            double dp = pump_head(&l->ppump, l->W) - K_loop * l->W * fabs(l->W) - dpc + buoy;
            l->W += h * dp / LOOP_INERTIA;
            if (l->W < 0 && !l->check_valve_stuck_open) l->W = 0.0;
        }
        /* DRACS path: cold column from the coolers (6 m above the core
         * centre) down to the inlet plenum; a fluidic diode stops reverse
         * (core-bypass) flow while the pumps run */
        double head = G * 6.0 * (rho_dr - rho_hot);
        double dpd = head - 0.02 * p->W_dracs * fabs(p->W_dracs) - dpc;
        p->W_dracs += h * dpd / 80.0;
        if (p->W_dracs < 0) p->W_dracs = 0;
    }
    p->W_core = p->W_dracs;
    for (int i = 0; i < RM_NLOOPS; i++) p->W_core += p->loop[i].W;
}

static void steam_side(rm_plant *p, double dt)
{
    /* header mass balance */
    double Wsg = 0, Hsg = 0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        Wsg += p->loop[i].sg.W_steam;
        Hsg += p->loop[i].sg.W_steam * p->loop[i].sg.h_steam;
    }
    double ratio = p->p_header / P_STEAM0;
    double valve = p->turbine_tripped ? 0.0 : p->turbine_valve;
    p->W_turbine = W_T0 / 0.9 * valve * ratio;
    /* no condenser vacuum without circulating water: bypass interlocked shut */
    if (p->cw_pump < 0.5) p->bypass_valve = 0.0;
    p->W_bypass = 0.4 * W_T0 * p->bypass_valve * ratio;
    /* SG safety valves: full-flow capacity, lifting at 16 MPa */
    p->W_relief = p->p_header > 16.0e6 ? 1.1 * W_T0 * fmin(1.0, (p->p_header - 16.0e6) / 0.3e6) : 0.0;
    double Wout = p->W_turbine + p->W_bypass + p->W_relief;
    double m_old = p->m_header;
    if (Wsg > 1e-6) p->h_header += (Hsg / Wsg - p->h_header) * fmin(1.0, Wsg * dt / p->m_header);
    p->m_header += (Wsg - Wout) * dt;
    if (p->m_header < 1.0) p->m_header = 1.0;
    p->p_header *= p->m_header / m_old;
    if (p->p_header < 0.1e6) p->p_header = 0.1e6;

    /* turbine and condenser */
    rm_if97_state in;
    rm_if97_ph(p->p_header, p->h_header, &in);
    double hs_out = rm_if97_h_ps(p->p_cond, in.s);
    double dh = (p->h_header - hs_out) * ETA_TURB;
    if (dh < 0) dh = 0;
    p->P_mech = p->W_turbine * dh;
    p->P_gen = p->generator_breaker ? ETA_GEN * p->P_mech : 0.0;
    double pumps = 0;
    for (int i = 0; i < RM_NLOOPS; i++)
        pumps += 3.0e6 * pow(p->loop[i].ppump.speed, 3) + 1.5e6 * pow(p->loop[i].spump.speed, 3);
    p->P_house = 25.0e6 + pumps;
    p->P_net = p->P_gen - p->P_house;
    double Qc = (p->W_turbine + p->W_bypass) * (p->h_header - 150.0e3) - p->P_mech;
    double Tcw_out = p->T_cw_in + fmax(Qc, 0.0) / (W_CW * 4180.0 * fmax(p->cw_pump, 0.02));
    if (Tcw_out > 373.0) Tcw_out = 373.0;
    p->p_cond = rm_if97_psat(Tcw_out + 5.0);
    double load = fmin(1.0, p->W_turbine / W_T0);
    p->T_fw += ((423.15 + 90.0 * load) - p->T_fw) * dt / 30.0;

    /* controls */
    double err = (p->p_header - p->p_set) / p->p_set;
    if (p->auto_turbine && !p->turbine_tripped) {
        p->tv_int += err * dt;
        p->turbine_valve = 0.9 + 4.0 * err + 0.4 * p->tv_int;
        if (p->turbine_valve < 0) p->turbine_valve = 0;
        if (p->turbine_valve > 1) p->turbine_valve = 1;
    }
    /* bypass to the condenser opens above set + 0.4 MPa (set + 0.1 MPa with
     * the turbine off), proportional band 0.3 MPa, 1 s actuator */
    double bp_set = p->p_set + (p->turbine_tripped ? 0.1e6 : 0.4e6);
    double bp_target = (p->p_header - bp_set) / 0.3e6;
    if (bp_target < 0) bp_target = 0;
    if (bp_target > 1) bp_target = 1;
    p->bypass_valve += (bp_target - p->bypass_valve) * fmin(1.0, dt / 1.0);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &p->loop[i].sg;
        if (p->auto_fw && !s->isolated) {
            double e = (s->T_steam - p->T_steam_set) / 100.0;
            p->fw_int[i] += e * dt;
            s->fw_valve += (0.02 * e + 0.004 * p->fw_int[i] * 0.0) * dt * 10.0;
            if (s->fw_valve < 0.02) s->fw_valve = 0.02;
            if (s->fw_valve > 1.3) s->fw_valve = 1.3;
        }
        s->W_fw = s->isolated ? 0.0 : s->fw_valve / 0.8 * W_T0 / RM_NLOOPS * p->fw_pump;
    }
}

static double dracs_capacity(const rm_plant *p, double T_na)
{
    double dT = T_na - p->T_air;
    if (dT < 0) dT = 0;
    double Q = 0;
    /* 23 MW per train with 520 K between sodium and air, damper fully open */
    for (int i = 0; i < 3; i++) Q += 23.0e6 * pow(dT / 520.0, 1.3) * p->dracs_damper[i];
    return Q;
}

static void plant_thermal(rm_plant *p, double dt, int with_neutronics)
{
    rm_core *c = &p->core;
    /* core inlet from the inlet plenum */
    double Tin = rm_na_T(p->h_inplenum);
    for (int ch = 0; ch < c->nchan; ch++) {
        c->th.W[ch] = p->W_core * chan_share[ch];
        c->th.W_byp[ch] = p->W_core * byp_share[ch];
        c->th.T_in[ch] = Tin;
    }
    if (with_neutronics) rm_core_step(c, dt);
    else rm_coreth_step(&c->th, dt);
    /* mixed core outlet */
    double hs = 0, ws = 0;
    int top = RM_NZ_ACT - 1;
    for (int ch = 0; ch < c->nchan; ch++) {
        int n = top * c->nchan + ch;
        hs += c->th.W[ch] * c->th.h_cool[n] + c->th.W_byp[ch] * c->th.h_byp[n];
        ws += c->th.W[ch] + c->th.W_byp[ch];
    }
    double h_core_out = ws > 1e-9 ? hs / ws : p->h_outplenum;
    p->T_core_in = Tin;
    p->T_core_out = rm_na_T(h_core_out);
    /* outlet plenum (well mixed, vessel internals add heat capacity) */
    double wc = p->W_core > 0 ? p->W_core : 0;
    double M = p->m_outplenum + 150000.0 * 500.0 / na_cp_h(p->h_outplenum);
    p->h_outplenum = (M / dt * p->h_outplenum + wc * h_core_out) / (M / dt + wc);
    /* loops */
    double hcold_mix = 0, wsum = 0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        double h1 = pipe_step(&l->hot, l->W, p->h_outplenum, dt);
        double hp_out, hs_out;
        ihx_step(&l->ihx, l->W, h1, l->Ws, l->scold.h[RM_PIPE_N - 1], dt,
                 &hp_out, &hs_out);
        double h2 = pipe_step(&l->cold, l->W, hp_out, dt);
        hcold_mix += (l->W > 0 ? l->W : 0) * h2;
        wsum += l->W > 0 ? l->W : 0;
        /* secondary */
        double hs1 = pipe_step(&l->shot, l->Ws, hs_out, dt);
        double hsg_out = sg_step(&l->sg, l->Ws, hs1, p->p_header + 0.8e6 * pow(l->sg.W_fw / (W_T0 / RM_NLOOPS), 2),
                                 rm_if97_h_pT(15.5e6, p->T_fw), dt);
        pipe_step(&l->scold, l->Ws, hsg_out, dt);
    }
    /* DRACS stream: takes outlet-plenum sodium, gives up Q, returns to the inlet plenum */
    double Tq = rm_na_T(p->h_outplenum);
    double Q = dracs_capacity(p, Tq);
    double hmin = rm_na_h(p->T_air + 30.0);
    double qmax = p->W_dracs * (p->h_outplenum - hmin);
    if (Q > qmax) Q = qmax > 0 ? qmax : 0;
    p->Q_dracs = Q;
    /* the column inside the coolers: even stagnant sodium there is chilled
     * through an open damper, which is what starts the flow */
    double Weff = p->W_dracs > 60.0 ? p->W_dracs : 60.0;
    double Qc = dracs_capacity(p, Tq);
    double qcmax = Weff * (p->h_outplenum - hmin);
    if (Qc > qcmax) Qc = qcmax > 0 ? qcmax : 0;
    p->h_dracs_out = p->h_outplenum - Qc / Weff;
    double h_ret = p->W_dracs > 1e-6 ? p->h_outplenum - Q / p->W_dracs : p->h_outplenum;
    hcold_mix += p->W_dracs * h_ret;
    wsum += p->W_dracs;
    /* heat leaking through the idle coolers still comes off the plenum */
    if (p->W_dracs < 1e-6) {
        double qidle = 0.02 * dracs_capacity(p, Tq);
        double Mo = p->m_outplenum + 150000.0 * 500.0 / na_cp_h(p->h_outplenum);
        p->h_outplenum -= qidle * dt / Mo;
    }
    double Mi = p->m_inplenum + 60000.0 * 500.0 / na_cp_h(p->h_inplenum);
    if (wsum > 1e-9) p->h_inplenum = (Mi / dt * p->h_inplenum + wsum * hcold_mix / wsum) / (Mi / dt + wsum);
}

double rm_plant_nominal_flow(void) { return W_P0 * RM_NLOOPS; }

int rm_plant_essential_power(const rm_plant *p)
{
    if (p->offsite_power) return 1;
    for (int i = 0; i < 3; i++)
        if (p->diesel_running[i]) return 1;
    return 0;
}

static void electrical(rm_plant *p, double dt)
{
    if (p->offsite_power) {
        p->diesel_timer = 0;
        for (int i = 0; i < 3; i++) p->diesel_running[i] = 0;
    } else {
        p->diesel_timer += dt;
        /* start on undervoltage, up to speed and loaded after 10 s */
        for (int i = 0; i < 3; i++)
            p->diesel_running[i] = p->diesel_avail[i] && p->diesel_timer > 10.0;
        /* the main generator can't hold the grid on its own here */
        p->turbine_tripped = 1;
        p->generator_breaker = 0;
    }
    /* feedwater and circulating water pumps are on the main buses */
    double fw_target = p->offsite_power ? 1.0 : 0.0;
    p->fw_pump += (fw_target - p->fw_pump) * fmin(1.0, dt / (fw_target > p->fw_pump ? 10.0 : 4.0));
    p->cw_pump += ((p->offsite_power ? 1.0 : 0.0) - p->cw_pump) * fmin(1.0, dt / 20.0);
}

/* sodium-to-air decay heat removal. Hot sodium from the outlet plenum passes
 * down through the DRACS heat exchangers and returns, colder and denser,
 * to the core inlet plenum: that cold column is what drives in-vessel
 * natural circulation when the pumps are off. Natural draft on the air
 * side, so it works without power. */
static void dracs_dampers(rm_plant *p, double dt)
{
    rm_core *c = &p->core;
    for (int i = 0; i < 3; i++) {
        if (p->dracs_auto && c->scram) p->dracs_damper_set[i] = 1.0;
        p->dracs_damper[i] += (p->dracs_damper_set[i] - p->dracs_damper[i]) * fmin(1.0, dt / 20.0);
    }
}

static void trip(rm_plant *p, const char *why)
{
    rm_core *c = &p->core;
    if (c->scram) return;
    rm_core_scram(c);
    strncpy(p->first_out, why, sizeof p->first_out - 1);
    p->first_out[sizeof p->first_out - 1] = 0;
    /* reactor trip -> turbine trip */
    p->turbine_tripped = 1;
    p->generator_breaker = 0;
}

void rm_plant_manual_scram(rm_plant *p) { trip(p, "MANUAL SCRAM"); }

static void protection(rm_plant *p)
{
    rm_core *c = &p->core;
    double pw = c->p_thermal / RM_P_RATED;
    double flow = p->W_core / (W_P0 * RM_NLOOPS);
    int pumps_off = 0;
    for (int i = 0; i < RM_NLOOPS; i++)
        if (p->loop[i].ppump.tripped || !p->loop[i].ppump.motor_on) pumps_off++;
    if (p->rps_bypass || c->scram) return;
    if (!p->offsite_power && pw > 0.05) trip(p, "LOSS OF OFFSITE POWER");
    else if (pw > 1.15) trip(p, "HIGH POWER 115%");
    else if (p->period > 0 && p->period < 10.0 && c->pks.n > 1e-4) trip(p, "SHORT PERIOD 10 S");
    else if (pw > 0.15 && pw / fmax(flow, 0.01) > 1.15) trip(p, "POWER/FLOW 1.15");
    else if (pw > 0.10 && flow < 0.70) trip(p, "LOW PRIMARY FLOW 70%");
    else if (pumps_off >= 2 && pw > 0.05) trip(p, "PRIMARY PUMP TRIP (2/4)");
    else if (p->T_core_out > 873.15) trip(p, "HIGH CORE OUTLET 600 C");
    else if (rm_core_max_clad_T(c) > 973.15) trip(p, "HIGH CLAD TEMP 700 C");
    else if (p->p_header > 16.5e6) trip(p, "HIGH STEAM PRESSURE");
    else if (p->turbine_tripped && pw > 0.50) trip(p, "TURBINE TRIP > 50% POWER");
}

void rm_plant_step(rm_plant *p, double dt)
{
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        int ess = rm_plant_essential_power(p);
        pump_step(&l->ppump, dt, p->offsite_power, ess);
        pump_step(&l->spump, dt, p->offsite_power, ess);
        l->Ws = W_S0 * fmax(l->spump.speed, 0.02);
    }
    /* automatic rod control: regulating bank drives against the power
     * error with a 1% deadband, never while tripped */
    rm_core *c = &p->core;
    if (p->auto_rod && !c->scram) {
        double err = c->p_thermal / RM_P_RATED - p->power_set;
        double pos = rm_core_bank_pos(c, BANK_REG);
        if (err > 0.005) rm_core_bank_move(c, BANK_REG, pos + 2.0);
        else if (err < -0.005) rm_core_bank_move(c, BANK_REG, pos - 2.0);
        else rm_core_bank_move(c, BANK_REG, pos);
    }
    electrical(p, dt);
    dracs_dampers(p, dt);
    hydraulics(p, dt);
    plant_thermal(p, dt, 1);
    steam_side(p, dt);
    /* period from the neutron level */
    double n = p->core.pks.n;
    if (p->n_last > 0 && n > 0) {
        double inst = dt / log(n / p->n_last);
        if (!isfinite(p->period)) p->period = inst;
        else p->period = 1.0 / (0.9 / p->period + 0.1 / inst);
    }
    p->n_last = n;
    protection(p);
    p->t += dt;
}

void rm_plant_steady(rm_plant *p)
{
    rm_core *c = &p->core;
    rm_core_steady(c, 1.0, 1, 1);
    double wt = 0;
    for (int ch = 0; ch < c->nchan; ch++) wt += c->th.W[ch] + c->th.W_byp[ch];
    for (int ch = 0; ch < c->nchan && ch < 1024; ch++) {
        chan_share[ch] = c->th.W[ch] / wt;
        byp_share[ch] = c->th.W_byp[ch] / wt;
    }
    /* thermal plant to equilibrium with the core power frozen */
    for (int it = 0; it < 3000; it++) {
        hydraulics(p, 0.5);
        plant_thermal(p, 0.5, 0);
        steam_side(p, 0.5);
    }
    /* re-find criticality at the settled temperatures, keep the TH state */
    rm_core_steady(c, 1.0, 0, 1);
    for (int it = 0; it < 400; it++) {
        hydraulics(p, 0.25);
        plant_thermal(p, 0.25, 0);
        steam_side(p, 0.25);
    }
}
