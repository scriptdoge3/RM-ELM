#include "rm_plant_int.h"
#include "rm_if97.h"
#include "rm_sodium.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double rm_W_T0 = 0.0;        /* nominal total steam flow, set at init */
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
        p->h[i] = (M / dt * p->h[i] + w * hup + p->q_ext / RM_PIPE_N) / (M / dt + w);
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
                     double *hp_out, double *hs_out, double sec_present)
{
    double wp = Wp > 0 ? Wp : 0.0, ws = Ws > 0 ? Ws : 0.0;
    double fp = pow(fmax(wp / W_P0, 0.02), 0.3), fs = pow(fmax(ws / W_S0, 0.02), 0.3) * sec_present;
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
static double sg_step(rm_sg *s, double Wna, double hna_in, double p, double hfw, double dt, double na_present)
{
    double w = Wna > 0 ? Wna : 0.0;
    double wf = s->isolated ? 0.0 : s->W_fw;
    double dry = s->isolated ? 0.0 : 1.0;   /* blown-down tubes transfer no heat */
    double Ak = s->area / RM_HX_N;
    double hna_old[RM_HX_N], hw_old[RM_HX_N], Tw_old[RM_HX_N];
    memcpy(hna_old, s->hna, sizeof hna_old);
    memcpy(hw_old, s->hw, sizeof hw_old);
    memcpy(Tw_old, s->Tw, sizeof Tw_old);
    rm_if97_sat sat;
    rm_if97_sat_p(p, &sat);
    rm_if97_state st[RM_HX_N];
    for (int k = 0; k < RM_HX_N; k++) rm_if97_ph_sat(&sat, hw_old[k], &st[k]);
    double Gna = 25000.0 * Ak * pow(fmax(w / W_S0, 0.02), 0.3) * na_present;
    double Q = 0;
    for (int it = 0; it < 3; it++) {
        for (int k = 0; k < RM_HX_N; k++) {
            double Gw = dry * h_water_side(&st[k], wf / (rm_W_T0 / RM_NLOOPS)) * Ak;
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
            double Gw = dry * h_water_side(&st[k], wf / (rm_W_T0 / RM_NLOOPS)) * Ak;
            double M = st[k].rho * SG_TUBE_VOL / RM_HX_N;
            double cpw = st[k].region == 4 ? 1e9 : st[k].cp;
            s->hw[k] = (M / dt * hw_old[k] + wf * hup + Gw * (s->Tw[k] - st[k].T + hw_old[k] / cpw)) /
                       (M / dt + wf + Gw / cpw);
            Q += Gw * (s->Tw[k] - st[k].T);
            hup = s->hw[k];
        }
        for (int k = 0; k < RM_HX_N; k++) rm_if97_ph_sat(&sat, s->hw[k], &st[k]);
    }
    /* sodium-water reaction heat goes into the sodium where the leak is */
    if (s->leak > 0) s->hna[RM_HX_N / 2] += s->leak * SWR_HEAT * dt / s->mna;
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

void rm_plant_msg(rm_plant *p, const char *fmt, ...)
{
    char *m = p->msg[p->nmsg % 16];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof p->msg[0], fmt, ap);
    va_end(ap);
    p->nmsg++;
}

/* ---- init ---------------------------------------------------------------- */
int rm_plant_init(rm_plant *p)
{
    memset(p, 0, sizeof *p);
    if (rm_core_init(&p->core) != 0) return -1;
    double hfw = rm_if97_h_pT(15.0e6, T_FW0);
    double hst = rm_if97_h_pT(P_STEAM0, T_STEAM0);
    rm_W_T0 = RM_P_RATED / (hst - hfw);
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
        l->sg.W_fw = rm_W_T0 / RM_NLOOPS;
        l->sg.h2 = RM_H2_BACKGROUND;
        l->sg.fiv_open = l->sg.msiv_open = 1;
    }
    p->fw_on = 1;
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
    p->offsite_power = p->grid_ok = p->grid_breaker = 1;
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
    p->mode = RM_MODE_RUN;
    p->fw_pump = 1.2;
    rm_rps_init(p, 0);
    rm_tg_init(p, 0);
    rm_aux_init(p, 0);
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
    p->W_turbine = rm_W_T0 / 0.9 * valve * ratio;
    /* no condenser vacuum without circulating water, and no instrument air
     * to hold the bypass open: interlocked shut */
    if (p->cw_pump < 0.5 || p->p_cond > 25e3 || p->aux.air_p < 4.0) p->bypass_valve = 0.0;
    p->W_bypass = 0.4 * rm_W_T0 * p->bypass_valve * ratio;
    /* SG safety valves: full-flow capacity, lifting at 16 MPa */
    p->W_relief = p->p_header > 16.0e6 ? 1.1 * rm_W_T0 * fmin(1.0, (p->p_header - 16.0e6) / 0.3e6) : 0.0;
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
    /* air in-leakage the vacuum pumps haven't removed adds its partial pressure */
    p->p_cond = rm_if97_psat(Tcw_out + 5.0) + p->tg.cond_air * 287.0 * 313.0 / 5000.0;
    double load = fmin(1.0, p->W_turbine / rm_W_T0);
    double Tfw = 423.15 + 90.0 * load - (p->tg.heaters_in ? 0.0 : 60.0 * load);
    p->T_fw += (Tfw - p->T_fw) * dt / 30.0;

    /* controls */
    double err = (p->p_header - p->p_set) / p->p_set;
    if (p->auto_turbine && !p->turbine_tripped && p->generator_breaker) {
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
    if (p->cw_pump < 0.5 || p->p_cond > 25e3 || p->aux.air_p < 4.0) bp_target = 0.0;
    p->bypass_valve += (bp_target - p->bypass_valve) * fmin(1.0, dt / 1.0);
    /* feedwater: feedforward from reactor power (a quarter of it per loop,
     * as design steam), trimmed to hold that loop's primary cold
     * leg at 380 C. That keeps the core inlet at its design value at any
     * power; steam temperature follows the sodium (~480 C at full power,
     * lower at part load). An override adds feed if the steam runs hot. */
    double hfw = rm_if97_h_pT(15.5e6, p->T_fw);
    double hst0 = rm_if97_h_pT(P_STEAM0, T_STEAM0);
    double dem[RM_NLOOPS], tot = 0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &p->loop[i].sg;
        int path = !s->isolated && s->fiv_open && s->msiv_open && !p->loop[i].dumped;
        /* air-operated feed regulating valves lock in place without instrument air */
        if (p->auto_fw && !s->fw_manual && p->fw_on && path && p->aux.air_p >= 4.0) {
            double Tc = rm_na_T(p->loop[i].cold.h[RM_PIPE_N - 1]);
            double e = (Tc - T_COLD0) / 100.0;
            double es = (s->T_steam - p->T_steam_set - 15.0) / 100.0;
            if (es > e) e = es;
            p->fw_int[i] += 0.002 * e * dt;
            if (p->fw_int[i] < -0.5) p->fw_int[i] = -0.5;
            if (p->fw_int[i] > 1.0) p->fw_int[i] = 1.0;
            double W = fmax(p->core.p_thermal, 0.0) / RM_NLOOPS / (hst0 - hfw) * (1.0 + p->fw_int[i] + 0.5 * e);
            double target = W / (rm_W_T0 / RM_NLOOPS) * 0.8 / fmax(p->fw_pump, 0.05);
            s->fw_valve += (target - s->fw_valve) * fmin(1.0, dt / 2.0);
            if (s->fw_valve < 0.0) s->fw_valve = 0.0;
            if (s->fw_valve > 1.3) s->fw_valve = 1.3;
        }
        dem[i] = (!path || !p->fw_on) ? 0.0 : s->fw_valve / 0.8;
        tot += dem[i] / RM_NLOOPS;
    }
    /* the running feed pumps can only deliver so much */
    double scale = tot > p->fw_pump ? p->fw_pump / tot : 1.0;
    for (int i = 0; i < RM_NLOOPS; i++) p->loop[i].sg.W_fw = dem[i] * scale * rm_W_T0 / RM_NLOOPS;
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
        double present = l->dumped ? 0.002 : 1.0;
        ihx_step(&l->ihx, l->W, h1, l->Ws, l->scold.h[RM_PIPE_N - 1], dt, &hp_out, &hs_out, present);
        double h2 = pipe_step(&l->cold, l->W, hp_out, dt);
        hcold_mix += (l->W > 0 ? l->W : 0) * h2;
        wsum += l->W > 0 ? l->W : 0;
        /* secondary */
        double hs1 = pipe_step(&l->shot, l->Ws, hs_out, dt);
        double hsg_out = sg_step(&l->sg, l->Ws, hs1, p->p_header + 0.8e6 * pow(l->sg.W_fw / (rm_W_T0 / RM_NLOOPS), 2),
                                 rm_if97_h_pT(15.5e6, p->T_fw), dt, present);
        pipe_step(&l->scold, l->Ws, hsg_out, dt);
    }
    /* DRACS stream: takes outlet-plenum sodium, gives up Q, returns to the inlet plenum */
    double Tq = rm_na_T(p->h_outplenum);
    double Q = dracs_capacity(p, Tq);
    double hmin = rm_na_h(p->T_air + 30.0);
    double qmax = p->W_dracs * (p->h_outplenum - hmin);
    if (Q > qmax) Q = qmax > 0 ? qmax : 0;
    p->Q_dracs = Q;
    {
        double tot = 0;
        for (int i = 0; i < 3; i++) tot += p->dracs_damper[i];
        for (int i = 0; i < 3; i++) p->Q_dracs_train[i] = tot > 1e-9 ? Q * p->dracs_damper[i] / tot : 0.0;
    }
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

/* sodium-to-air decay heat removal. Hot sodium from the outlet plenum passes
 * down through the DRACS heat exchangers and returns, colder and denser,
 * to the core inlet plenum: that cold column is what drives in-vessel
 * natural circulation when the pumps are off. Natural draft on the air
 * side, so it works without power. */
static void dracs_dampers(rm_plant *p, double dt)
{
    rm_core *c = &p->core;
    for (int i = 0; i < 3; i++) {
        if (p->dracs_auto && !p->dracs_man[i] && c->scram) p->dracs_damper_set[i] = 1.0;
        /* the damper actuators are held shut by air: they fail open */
        if (p->aux.air_p < 3.0) p->dracs_damper_set[i] = 1.0;
        p->dracs_damper[i] += (p->dracs_damper_set[i] - p->dracs_damper[i]) * fmin(1.0, dt / 20.0);
    }
}

/* the isolate-and-blow-down sequence: feed and steam isolation valves shut,
 * blowdown valves open, nitrogen purge on */
void rm_plant_isolate_sg(rm_plant *p, int i)
{
    rm_sg *s = &p->loop[i].sg;
    if (s->isolated) return;
    s->isolated = 1;
    s->fiv_open = s->msiv_open = 0;
    s->n2_purge = 1;
    s->W_fw = 0.0;
    rm_plant_msg(p, "SG %d ISOLATED, WATER SIDE BLOWN DOWN", i + 1);
}

void rm_plant_dump_loop(rm_plant *p, int i)
{
    rm_loop *l = &p->loop[i];
    if (l->dumped) return;
    l->dumped = 1;
    l->refill_t = 0;
    l->spump.tripped = 1;
    l->na_leak = 0;
    rm_plant_msg(p, "LOOP %d SECONDARY SODIUM DUMPED", i + 1);
}

void rm_plant_refill_loop(rm_plant *p, int i)
{
    rm_loop *l = &p->loop[i];
    if (!l->dumped || l->refill_t > 0) return;
    if (l->sg.leak > 1e-5) {
        rm_plant_msg(p, "LOOP %d REFILL REFUSED: SG %d STILL LEAKING", i + 1, i + 1);
        return;
    }
    l->refill_t = 1200.0;
    rm_plant_msg(p, "LOOP %d REFILL STARTED (20 MIN)", i + 1);
}

double rm_plant_essential_load(const rm_plant *p)
{
    double w = 2.5e6;                              /* chargers, instruments, ventilation */
    for (int i = 0; i < RM_NLOOPS; i++) w += p->loop[i].ppump.pony_on && p->loop[i].ppump.speed > 0.05 ? 0.4e6 : 0;
    for (int i = 0; i < 2; i++) w += (p->aux.ccw[i] ? 0.3e6 : 0) + (p->aux.sw[i] ? 0.4e6 : 0);
    for (int i = 0; i < 6; i++) w += p->aux.heat_kw[i] * 1e3;
    return w;
}

/* Tube leaks feed water into the secondary sodium. The reaction makes
 * hydrogen (picked up by the hydrogen meter) and heat, and the jet wastes
 * neighbouring tubes, so the leak grows until the SG is isolated. A big leak
 * bursts the sodium-side rupture disc: automatic isolation, the loop's
 * sodium goes to the dump tank and the reactor trips. */
static void sodium_water(rm_plant *p, double dt)
{
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        rm_sg *s = &l->sg;
        if (s->leak > 0) {
            if (s->isolated || l->dumped) {
                s->leak *= exp(-dt / 20.0);                 /* blowdown */
                if (s->leak < 1e-7) s->leak = 0.0;
            } else {
                s->leak *= exp(dt * log(2.0) / 90.0);       /* self-wastage: doubles in 90 s */
                if (s->leak > 30.0) s->leak = 30.0;
            }
            s->h2 += s->leak * (2.0 / 18.0) * dt / NA_SEC_MASS * 1e6;
        }
        /* cold trap takes hydrogen out slowly (its lines close on containment isolation) */
        if (!p->containment_isolated && l->cold_trap && !l->dumped) s->h2 -= (s->h2 - RM_H2_BACKGROUND) * dt / 10800.0;
        /* the expansion tank sees the reaction's pressure pulse; the disc goes at 1 MPa */
        l->exp_p = (l->dumped ? 0.12e6 : 0.3e6) + 0.35e6 * s->leak * !s->isolated;
        if (!s->disc_burst && s->leak > DISC_BURST_LEAK) {
            s->disc_burst = 1;
            rm_plant_msg(p, "SG %d RUPTURE DISC BURST - SODIUM-WATER REACTION", i + 1);
            rm_plant_isolate_sg(p, i);
            rm_plant_dump_loop(p, i);
            char why[48];
            snprintf(why, sizeof why, "SODIUM-WATER REACTION SG %d", i + 1);
            rm_plant_trip(p, why);
        }
    }
}

void rm_plant_step(rm_plant *p, double dt)
{
    sodium_water(p, dt);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        int ess = rm_plant_essential_power(p);
        pump_step(&l->ppump, dt, p->offsite_power, ess);
        pump_step(&l->spump, dt, p->offsite_power, ess);
        /* a loop short of sodium cavitates its pump; a dumped loop has none */
        double inv = fmin(1.0, fmax(0.0, (l->sec_inventory - 0.8) / 0.1));
        l->Ws = l->dumped ? 0.0 : W_S0 * fmax(l->spump.speed, 0.02) * inv;
    }
    /* automatic rod control: the regulating bank steers the reactor period.
     * Well below demand it raises power on a period no shorter than ~60 s
     * (a startup-rate limit, so it can also take the reactor up from the
     * source range); above demand it inserts; near demand it drives the
     * period back to infinity. Never while tripped. */
    rm_core *c = &p->core;
    if (p->auto_rod && !c->scram) {
        double pw = c->p_thermal / RM_P_RATED + 1e-12;
        double rel = pw / fmax(p->power_set, 1e-6);
        double per = p->period;
        int rising = isfinite(per) && per > 0, falling = isfinite(per) && per < 0;
        double pos = rm_core_bank_pos(c, BANK_REG), step = 0.0;
        if (rel < 0.97) {
            if (rising && per < 45.0) step = 0.5;                       /* too fast: ease in */
            else if (!rising || per > 90.0) step = -1.0;                /* withdraw */
        } else if (rel > 1.03) {
            if (!(falling && per > -60.0)) step = 1.0;                  /* insert */
        } else if (rel > 1.005) {
            if (!(falling && per > -300.0)) step = 0.3;
        } else if (rel < 0.995) {
            if (!(rising && per < 300.0)) step = -0.3;
        } else {
            if (rising && per < 600.0) step = 0.3;
            else if (falling && per > -600.0) step = -0.3;
        }
        if (step < 0 && rm_plant_rod_block(p, 0, NULL, 0)) step = 0.0;
        rm_core_bank_move(c, BANK_REG, pos + step);
    }
    rm_rps_step(p, dt);
    rm_tg_step(p, dt);
    rm_aux_step(p, dt);
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
    rm_rps_protection(p);
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
        rm_aux_pipe_heat(p);
        hydraulics(p, 0.5);
        plant_thermal(p, 0.5, 0);
        steam_side(p, 0.5);
    }
    /* re-find criticality at the settled temperatures, keep the TH state */
    rm_core_steady(c, 1.0, 0, 1);
    for (int it = 0; it < 400; it++) {
        rm_aux_pipe_heat(p);
        hydraulics(p, 0.25);
        plant_thermal(p, 0.25, 0);
        steam_side(p, 0.25);
    }
}

void rm_plant_hot_standby(rm_plant *p)
{
    rm_core *c = &p->core;
    /* flows, orificing and cross sections come from the rated-power solve */
    rm_core_steady(c, 1.0, 1, 1);
    double wt = 0;
    for (int ch = 0; ch < c->nchan; ch++) wt += c->th.W[ch] + c->th.W_byp[ch];
    for (int ch = 0; ch < c->nchan && ch < 1024; ch++) {
        chan_share[ch] = c->th.W[ch] / wt;
        byp_share[ch] = c->th.W_byp[ch] / wt;
    }
    const double T = T_COLD0;
    rm_core_shutdown(c, T);
    double hna = rm_na_h(T);
    double hst = rm_if97_h_pT(P_STEAM0, T);
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        pipe_init(&l->hot, 10400.0, 20000.0, T);
        pipe_init(&l->cold, 15000.0, 30000.0, T);
        pipe_init(&l->shot, 16800.0, 30000.0, T);
        pipe_init(&l->scold, 16800.0, 30000.0, T);
        for (int k = 0; k < RM_HX_N; k++) {
            l->ihx.hp[k] = l->ihx.hs[k] = hna;
            l->ihx.Tw[k] = T;
            l->sg.hna[k] = hna;
            l->sg.hw[k] = hst;
            l->sg.Tw[k] = T;
        }
        l->sg.W_fw = l->sg.W_steam = 0.0;
        l->sg.fw_valve = 0.02;
        l->sg.T_steam = T;
        l->sg.h_steam = hst;
    }
    p->h_inplenum = p->h_outplenum = hna;
    p->T_core_in = p->T_core_out = T;
    p->fw_on = 0;
    p->turbine_tripped = 1;
    p->generator_breaker = 0;
    p->turbine_valve = 0.0;
    p->bypass_valve = 0.0;
    p->tv_int = 0.0;
    p->p_header = P_STEAM0;
    p->h_header = hst;
    rm_if97_state st;
    rm_if97_ph(P_STEAM0, hst, &st);
    p->m_header = st.rho * V_HEADER;
    p->T_fw = 423.15;
    p->auto_rod = 0;
    p->power_set = 0.05;
    p->mode = RM_MODE_SHUTDOWN;
    rm_rps_init(p, 1);
    rm_tg_init(p, 1);
    rm_aux_init(p, 1);
    p->first_out[0] = 0;
    p->period = INFINITY;
    /* settle the loops (no fission power to speak of) */
    for (int it = 0; it < 200; it++) {
        hydraulics(p, 0.5);
        plant_thermal(p, 0.5, 0);
        steam_side(p, 0.5);
    }
    p->n_last = c->pks.n;
}
