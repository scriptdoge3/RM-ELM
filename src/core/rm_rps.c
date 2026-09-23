/*
 * Reactor protection system and neutron monitoring.
 *
 * The RPS has two divisions, A and B, each with two trip channels (A1, A2,
 * B1, B2). Plant-wide trip signals open every channel. The per-channel
 * nuclear instruments (IRM A-H, APRM A-F) only open the channel they feed,
 * so one instrument alone gives a half scram; the rods go in when both
 * divisions have tripped. One channel per division may be bypassed.
 *
 * Neutron monitoring: four SRMs, eight IRMs with their own 10-position
 * range switches, six APRMs. The SRM and IRM detectors are driven in and
 * out of the core; withdrawn they see a millionth of the flux. Each
 * instrument sits in one core quadrant and reads that quadrant's power.
 *
 * Rod control: rod blocks, the rod worth minimizer (below 20% power the
 * shim banks must be withdrawn together, after the safety bank) and the
 * rod block monitor (local power around the selected rod).
 */
#include "rm_plant_int.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned char chan_quad[1024];
static double quad_n[4];
static int quad_ready;

static void quad_setup(const rm_plant *p)
{
    const rm_core *c = &p->core;
    const rm_hexgrid *g = &c->dif.grid;
    for (int i = 0; i < 4; i++) quad_n[i] = 0;
    for (int ch = 0; ch < c->nchan && ch < 1024; ch++) {
        double x, y;
        rm_hexgrid_xy(g, c->col_of_chan[ch], 1.0, &x, &y);
        int q = (x >= 0 ? 0 : 1) + (y >= 0 ? 0 : 2);
        chan_quad[ch] = (unsigned char)q;
        quad_n[q] += 1;
    }
    quad_ready = 1;
}

void rm_rps_init(rm_plant *p, int hot)
{
    rm_nms *m = &p->nms;
    memset(m, 0, sizeof *m);
    for (int i = 0; i < 8; i++) m->irm_range[i] = hot ? 1 : 10;
    m->irm_bypass[0] = m->irm_bypass[1] = -1;
    m->aprm_bypass[0] = m->aprm_bypass[1] = -1;
    m->srm_pos = m->irm_pos = hot ? 1.0 : 0.0;
    for (int i = 0; i < 4; i++) m->quad[i] = 1.0;
    m->sel_rod = -1;
    m->rbm = m->rbm_ref = 1.0;
    quad_ready = 0;
}

/* a withdrawn detector sees a millionth of the in-core flux */
static double posf(double pos) { return pow(10.0, -6.0 * (1.0 - pos)); }
static int vital(const rm_plant *p, int div) { return p->tg.inverter[div] && p->tg.batt[div] > 0.02; }

double rm_plant_srm_ch(const rm_plant *p, int ch)
{
    if (!vital(p, ch & 1)) return 0.0;
    return 3.0e11 * p->core.pks.n * p->nms.quad[ch & 3] * posf(p->nms.srm_pos) + 0.3;
}
double rm_plant_srm_cps(const rm_plant *p) { return rm_plant_srm_ch(p, 0); }

/* IRM ranges are half a decade apart; range 10 reads full scale (125) at 40% */
static double irm_full(int range) { return 0.40 / pow(10.0, (10 - range) / 2.0); }
double rm_plant_irm_ch(const rm_plant *p, int ch)
{
    if (!vital(p, ch & 1)) return 0.0;
    return 125.0 * p->core.pks.n * p->nms.quad[ch & 3] / irm_full(p->nms.irm_range[ch]) * posf(p->nms.irm_pos);
}
double rm_plant_irm(const rm_plant *p) { return rm_plant_irm_ch(p, 0); }

double rm_plant_aprm_ch(const rm_plant *p, int ch)
{
    if (!vital(p, ch & 1)) return 0.0;
    return 100.0 * p->core.pks.n * p->nms.quad[ch & 3];
}
double rm_plant_aprm(const rm_plant *p)
{
    double s = 0;
    int n = 0;
    for (int ch = 0; ch < 6; ch++) {
        if (p->nms.aprm_bypass[ch & 1] == ch) continue;
        double v = rm_plant_aprm_ch(p, ch);
        if (v <= 0) continue;
        s += v;
        n++;
    }
    return n ? s / n : 0.0;
}

/* instrument channel -> RPS trip channel: even channels feed division A */
static int rps_of(int ch) { return (ch & 1) * 2 + ((ch >> 1) & 1); }

/* ---- RPS logic -------------------------------------------------------------- */
static void scram_now(rm_plant *p, const char *why)
{
    rm_core *c = &p->core;
    if (c->scram) return;
    rm_core_scram(c);
    strncpy(p->first_out, why, sizeof p->first_out - 1);
    p->first_out[sizeof p->first_out - 1] = 0;
    /* reactor trip -> turbine trip; rod control drops to manual */
    p->turbine_tripped = 1;
    p->generator_breaker = 0;
    p->auto_rod = 0;
}

static void div_eval(rm_plant *p, const char *why)
{
    rm_nms *m = &p->nms;
    for (int d = 0; d < 2; d++) {
        int t = m->trip[2 * d] || m->trip[2 * d + 1];
        if (t && !m->div_trip[d]) {
            m->div_trip[d] = 1;
            snprintf(m->div_why[d], sizeof m->div_why[d], "%s", why);
            if (!(m->div_trip[0] && m->div_trip[1]))
                rm_plant_msg(p, "RPS DIVISION %c TRIPPED (HALF SCRAM): %s", 'A' + d, why);
        }
    }
    if (m->div_trip[0] && m->div_trip[1]) {
        /* first out: the division that tripped first */
        const char *f = strcmp(m->div_why[0], why) ? m->div_why[0] : m->div_why[1];
        scram_now(p, f[0] ? f : why);
    }
}

void rm_plant_trip(rm_plant *p, const char *why)
{
    rm_nms *m = &p->nms;
    /* a plant-wide signal opens every channel at once */
    for (int ch = 0; ch < 4; ch++) m->trip[ch] = 1;
    for (int d = 0; d < 2; d++)
        if (!m->div_trip[d]) {
            m->div_trip[d] = 1;
            snprintf(m->div_why[d], sizeof m->div_why[d], "%s", why);
        }
    scram_now(p, why);
}

static void rps_channel(rm_plant *p, int ch, const char *why)
{
    rm_nms *m = &p->nms;
    if (m->bypass[ch] || m->trip[ch]) return;
    m->trip[ch] = 1;
    div_eval(p, why);
}

void rm_plant_manual_scram_div(rm_plant *p, int div)
{
    rm_nms *m = &p->nms;
    m->trip[2 * div] = m->trip[2 * div + 1] = 1;
    div_eval(p, "MANUAL SCRAM");
}

void rm_plant_manual_scram(rm_plant *p) { rm_plant_trip(p, "MANUAL SCRAM"); }

void rm_plant_rps_reset(rm_plant *p, int div)
{
    rm_nms *m = &p->nms;
    m->trip[2 * div] = m->trip[2 * div + 1] = 0;
    m->div_trip[div] = 0;
    m->div_why[div][0] = 0;
    if (!m->div_trip[0] && !m->div_trip[1] && p->core.scram) {
        rm_core_reset_scram(&p->core);
        p->first_out[0] = 0;
        rm_plant_msg(p, "RPS RESET - RODS REMAIN INSERTED");
    }
}

int rm_plant_set_mode(rm_plant *p, int mode, char *why, int nwhy)
{
    static const char *name[4] = {"SHUTDOWN", "REFUEL", "STARTUP", "RUN"};
    if (mode == p->mode) return 1;
    if (mode == RM_MODE_RUN && rm_plant_aprm(p) < 5.0) {
        snprintf(why, nwhy, "RUN REFUSED: APRM DOWNSCALE (BELOW 5%%)");
        return 0;
    }
    p->mode = mode;
    rm_plant_msg(p, "REACTOR MODE SWITCH IN %s", name[mode]);
    if (mode == RM_MODE_SHUTDOWN) rm_plant_trip(p, "MODE SWITCH IN SHUTDOWN");
    if (why && nwhy) why[0] = 0;
    return 1;
}

/* ---- rod blocks -------------------------------------------------------------- */
int rm_plant_rod_block(const rm_plant *p, int single, char *why, int nwhy)
{
    const rm_nms *m = &p->nms;
    const char *r = NULL;
    char buf[80];
    int start = p->mode == RM_MODE_STARTUP || p->mode == RM_MODE_REFUEL;
    if (p->core.scram) r = "ROD BLOCK: REACTOR TRIPPED";
    else if (p->mode == RM_MODE_SHUTDOWN) r = "ROD BLOCK: MODE SWITCH IN SHUTDOWN";
    else if (p->mode == RM_MODE_REFUEL && !single) r = "ROD BLOCK: REFUEL MODE - ONE ROD AT A TIME";
    else if (start && m->irm_pos < 0.99) r = "ROD BLOCK: IRM DETECTORS NOT FULL IN";
    else if (start && m->srm_pos < 0.99 && m->irm_range[0] <= 2) r = "ROD BLOCK: SRM DETECTORS NOT FULL IN";
    else if (start && m->srm_pos > 0.99 && rm_plant_srm_ch(p, 0) < 3.0) r = "ROD BLOCK: SRM DOWNSCALE";
    else if (start) {
        for (int ch = 0; ch < 8 && !r; ch++) {
            if (m->irm_bypass[ch & 1] == ch) continue;
            double v = rm_plant_irm_ch(p, ch);
            if (m->irm_range[ch] > 1 && v < RM_IRM_DOWNSCALE) {
                snprintf(buf, sizeof buf, "ROD BLOCK: IRM %c DOWNSCALE - RANGE DOWN", 'A' + ch);
                r = buf;
            } else if (v > 108.0) {
                snprintf(buf, sizeof buf, "ROD BLOCK: IRM %c UPSCALE - RANGE UP", 'A' + ch);
                r = buf;
            }
        }
    }
    if (r && why) snprintf(why, nwhy, "%s", r);
    return r != NULL;
}

int rm_plant_rod_permit(const rm_plant *p, int bank, int rod, double target, char *why, int nwhy)
{
    const rm_core *c = &p->core;
    const rm_nms *m = &p->nms;
    double now = rod >= 0 ? c->rod_ins[rod] : rm_core_bank_pos(c, bank);
    if (target >= now - 1e-6) return 1;                  /* insertion is always allowed */
    if (rm_plant_rod_block(p, rod >= 0, why, nwhy)) return 0;
    int bk = rod >= 0 ? c->ctrl_bank[rod] : bank;
    /* rod worth minimizer: banked withdrawal sequence below 20% power */
    if (!m->rwm_bypass && rm_plant_aprm(p) < 20.0 && bk >= BANK_SHIM_A && bk <= BANK_SHIM_D) {
        if (rm_core_bank_pos(c, BANK_SAFETY) > 0.5) {
            snprintf(why, nwhy, "RWM: WITHDRAW GROUP 1 (SAFETY) FULLY FIRST");
            return 0;
        }
        /* compare with where the other shim groups are heading, so they can be driven together */
        double most_in = 0;
        for (int b = BANK_SHIM_A; b <= BANK_SHIM_D; b++) {
            if (b == bk && rod < 0) continue;
            double t = 0;
            int n = 0;
            for (int k = 0; k < c->nctrl; k++)
                if (c->ctrl_bank[k] == b) {
                    t += c->rod_target[k];
                    n++;
                }
            most_in = fmax(most_in, n ? t / n : 0.0);
        }
        if (target < most_in - 20.0) {
            snprintf(why, nwhy, "RWM: SEQUENCE ERROR - KEEP GROUPS 2-5 WITHIN 5 NOTCHES");
            return 0;
        }
    }
    /* rod block monitor on the selected rod */
    if (rod >= 0 && rod == m->sel_rod && !m->rbm_bypass && rm_plant_aprm(p) > 30.0 && m->rbm > 1.08) {
        snprintf(why, nwhy, "ROD BLOCK: RBM UPSCALE");
        return 0;
    }
    return 1;
}

/* local power around a rod: the six channels next to it */
static double local_power(const rm_plant *p, int rod)
{
    const rm_core *c = &p->core;
    const rm_hexgrid *g = &c->dif.grid;
    int col = c->ctrl_col[rod];
    double s = 0, tot = 0;
    for (int k = 0; k < 6; k++) {
        int nb = g->nbr[col * 6 + k];
        if (nb < 0 || c->coltype[nb] != COL_FUEL) continue;
        int ch = c->chan_of_col[nb];
        for (int z = 0; z < RM_NZ_ACT; z++) s += c->th.q_node[core_fnode(c, ch, z)];
    }
    for (int ch = 0; ch < c->nchan; ch++)
        for (int z = 0; z < RM_NZ_ACT; z += 4) tot += c->th.q_node[core_fnode(c, ch, z)];
    return tot > 0 ? s / tot : 0.0;
}

void rm_plant_select_rod(rm_plant *p, int rod)
{
    p->nms.sel_rod = rod;
    if (rod >= 0) {
        p->nms.rbm_ref = local_power(p, rod);
        p->nms.rbm = 1.0;
    }
}

void rm_rps_step(rm_plant *p, double dt)
{
    rm_nms *m = &p->nms;
    rm_core *c = &p->core;
    if (!quad_ready) quad_setup(p);
    /* detector drives: a minute end to end */
    m->srm_pos = fmin(1.0, fmax(0.0, m->srm_pos + m->srm_drive * dt / 60.0));
    m->irm_pos = fmin(1.0, fmax(0.0, m->irm_pos + m->irm_drive * dt / 60.0));
    if ((m->srm_drive > 0 && m->srm_pos >= 1.0) || (m->srm_drive < 0 && m->srm_pos <= 0.0)) m->srm_drive = 0;
    if ((m->irm_drive > 0 && m->irm_pos >= 1.0) || (m->irm_drive < 0 && m->irm_pos <= 0.0)) m->irm_drive = 0;
    /* quadrant powers */
    double qs[4] = {0, 0, 0, 0}, tot = 0;
    for (int ch = 0; ch < c->nchan && ch < 1024; ch++) {
        double pw = 0;
        for (int z = 0; z < RM_NZ_ACT; z += 2) pw += c->th.q_node[core_fnode(c, ch, z)];
        qs[chan_quad[ch]] += pw;
        tot += pw;
    }
    for (int q = 0; q < 4; q++) m->quad[q] = tot > 0 ? (qs[q] / quad_n[q]) / (tot / c->nchan) : 1.0;
    if (m->sel_rod >= 0 && m->rbm_ref > 0) m->rbm = local_power(p, m->sel_rod) / m->rbm_ref;
}

/* ---- protection ----------------------------------------------------------------- */
void rm_rps_protection(rm_plant *p)
{
    rm_core *c = &p->core;
    rm_nms *m = &p->nms;
    double pw = c->p_thermal / RM_P_RATED;
    double flow = p->W_core / (W_P0 * RM_NLOOPS);
    int pumps_off = 0;
    for (int i = 0; i < RM_NLOOPS; i++)
        if (p->loop[i].ppump.tripped || !p->loop[i].ppump.motor_on) pumps_off++;
    if (p->rps_bypass || c->scram) return;

    /* plant-wide signals open every channel */
    const char *why = NULL;
    if (!p->offsite_power && pw > 0.05) why = "LOSS OF OFFSITE POWER";
    else if (p->period > 0 && p->period < 10.0 && c->pks.n > 1e-4) why = "SHORT PERIOD 10 S";
    else if (pw > 0.15 && pw / fmax(flow, 0.01) > 1.15) why = "POWER/FLOW 1.15";
    else if (pw > 0.10 && flow < 0.70) why = "LOW PRIMARY FLOW 70%";
    else if (pumps_off >= 2 && pw > 0.05) why = "PRIMARY PUMP TRIP (2/4)";
    else if (p->T_core_out > 873.15) why = "HIGH CORE OUTLET 600 C";
    else if (rm_core_max_clad_T(c) > 973.15) why = "HIGH CLAD TEMP 700 C";
    else if (p->p_header > 16.5e6) why = "HIGH STEAM PRESSURE";
    else if (p->turbine_tripped && pw > 0.50) why = "TURBINE TRIP > 50% POWER";
    else if (p->aux.dnd > 2000.0) why = "DELAYED NEUTRON DETECTION HIGH";
    else if (p->aux.na_level < -300.0) why = "REACTOR SODIUM LEVEL LOW";
    if (why) {
        rm_plant_trip(p, why);
        return;
    }

    /* per-channel nuclear instrument trips: each opens only its own channel */
    char buf[48];
    if (p->mode == RM_MODE_STARTUP || p->mode == RM_MODE_REFUEL) {
        for (int ch = 0; ch < 8; ch++) {
            if (m->irm_bypass[ch & 1] == ch) continue;
            if (rm_plant_irm_ch(p, ch) > RM_IRM_TRIP) {
                snprintf(buf, sizeof buf, "IRM %c HIGH", 'A' + ch);
                rps_channel(p, rps_of(ch), buf);
            }
        }
    }
    double sp = p->mode == RM_MODE_RUN ? 115.0 : 15.0;
    for (int ch = 0; ch < 6; ch++) {
        if (m->aprm_bypass[ch & 1] == ch) continue;
        if (rm_plant_aprm_ch(p, ch) > sp) {
            snprintf(buf, sizeof buf, p->mode == RM_MODE_RUN ? "APRM %c HIGH 115%%" : "APRM %c HIGH (SETDOWN 15%%)",
                     'A' + ch);
            rps_channel(p, rps_of(ch), buf);
        }
    }
}
