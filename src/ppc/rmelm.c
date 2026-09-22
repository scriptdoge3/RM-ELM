/*
 * RM-ELM plant process computer - first, deliberately basic terminal.
 *
 * Amber-phosphor style full-screen display of the reactor core with a
 * command prompt. The core runs in real time (or faster with RUN n).
 * Only the core exists so far: coolant flow and inlet temperature are
 * set directly by the operator until the sodium loops are built.
 *
 * POSIX terminals only for now (Linux, macOS, WSL).
 */
#define _POSIX_C_SOURCE 200809L
#include "rm_core.h"
#include "rm_kernels.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define AMBER "\033[38;2;255;176;0m"
#define DIM "\033[38;2;150;100;0m"
#define BRIGHT "\033[1;38;2;255;210;90m"
#define INV "\033[7m"
#define RST "\033[0m"
#define DT 0.05

static struct termios saved;

static void restore_term(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    printf(RST "\033[?25h\033[2J\033[H");
    fflush(stdout);
}

static void raw_term(void)
{
    tcgetattr(STDIN_FILENO, &saved);
    atexit(restore_term);
    struct termios t = saved;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    printf("\033[?25l\033[2J");
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- message log ---- */
#define NLOG 7
static char logbuf[NLOG][96];
static void logmsg(const rm_core *c, const char *fmt, ...)
{
    for (int i = 0; i < NLOG - 1; i++) memcpy(logbuf[i], logbuf[i + 1], sizeof logbuf[i]);
    char msg[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    int t = (int)c->t;
    snprintf(logbuf[NLOG - 1], sizeof logbuf[0], "%02d:%02d:%02d %s", t / 3600, t / 60 % 60, t % 60, msg);
}

/* ---- plant state the operator sets directly (until loops exist) ---- */
static double flow_pct = 100.0, tin_c = 380.0;
static double w_nom[1024], wb_nom[1024];
static int speed = 1;
static double trend[64];
static int ntrend = 0;
static double period = INFINITY, n_prev = 0;
static int tripped_logged = 0;

static const char *bank_name[RM_NBANKS] = {"REG", "A", "B", "C", "D", "SAFE"};

static int bank_of(const char *s)
{
    for (int b = 0; b < RM_NBANKS; b++)
        if (strcasecmp(s, bank_name[b]) == 0) return b;
    if (strcasecmp(s, "SAFETY") == 0) return BANK_SAFETY;
    return -1;
}

static void apply_plant(rm_core *c)
{
    for (int ch = 0; ch < c->nchan; ch++) {
        c->th.W[ch] = w_nom[ch] * flow_pct / 100.0;
        c->th.W_byp[ch] = wb_nom[ch] * flow_pct / 100.0;
        c->th.T_in[ch] = tin_c + 273.15;
    }
}

static void protection(rm_core *c)
{
    const char *why = NULL;
    if (c->p_thermal > 1.18 * RM_P_RATED) why = "HIGH NEUTRON POWER 118%";
    else if (period > 0 && period < 8.0 && c->pks.n > 1e-4) why = "SHORT PERIOD < 8 S";
    else if (rm_core_max_clad_T(c) > 973.0) why = "HIGH CLAD TEMPERATURE 700 C";
    else if (c->p_thermal > 0.2 * RM_P_RATED && flow_pct < 0.8 * 100.0 * c->p_thermal / RM_P_RATED)
        why = "POWER/FLOW MISMATCH";
    if (why && !c->scram) {
        rm_core_scram(c);
        logmsg(c, "*** REACTOR TRIP: %s ***", why);
        tripped_logged = 1;
    }
}

static void bar(double frac, int w)
{
    int n = (int)(frac * w + 0.5);
    if (n < 0) n = 0;
    if (n > w) n = w;
    for (int i = 0; i < w; i++) fputs(i < n ? "\xe2\x96\x88" : DIM "\xc2\xb7" AMBER, stdout);
}

static void draw(rm_core *c, const char *cmd)
{
    int t = (int)c->t;
    double pct = 100.0 * c->p_thermal / RM_P_RATED;
    double rho = c->rho, beta = 0;
    for (int i = 0; i < 6; i++) beta += c->pk.beta[i];
    double tout = 0, tmax = 0;
    for (int ch = 0; ch < c->nchan; ch++) {
        tout += c->th.T_mixed_out[ch];
        if (c->th.T_mixed_out[ch] > tmax) tmax = c->th.T_mixed_out[ch];
    }
    tout /= c->nchan;

    printf("\033[H" AMBER);
    printf(INV " RM-ELM UNIT 1  PLANT PROCESS COMPUTER            T+%02d:%02d:%02d   SPEED x%-2d " RST AMBER "\033[K\n",
           t / 3600, t / 60 % 60, t % 60, speed);
    printf("\033[K\n");
    printf(" REACTOR THERMAL POWER  " BRIGHT "%8.1f MWt  %6.2f %%" AMBER "   ", c->p_thermal / 1e6, pct);
    bar(pct / 120.0, 22);
    printf("\033[K\n");
    printf("   FISSION %8.1f MW   DECAY HEAT %6.1f MW   NEUTRON LEVEL %9.3e\033[K\n",
           c->p_fission / 1e6, c->p_decay / 1e6, c->pks.n);
    if (isfinite(period) && fabs(period) < 1e4)
        printf("   REACTIVITY %+8.1f pcm  (%+6.3f $)   PERIOD %+8.1f s\033[K\n", 1e5 * rho, rho / beta, period);
    else
        printf("   REACTIVITY %+8.1f pcm  (%+6.3f $)   PERIOD   INFINITE\033[K\n", 1e5 * rho, rho / beta);
    printf("   DOPPLER COEF %6.2f pcm/K   PEAKING %5.2f   KINETICS %s\033[K\n",
           1e5 * c->rho_doppler_coef, c->peak_factor, rm_kernels_backend_name());
    printf("\033[K\n");
    printf(" PRIMARY SODIUM  FLOW %5.1f %%   INLET %6.1f C   OUTLET MEAN %6.1f C   MAX %6.1f C\033[K\n",
           flow_pct, tin_c, tout - 273.15, tmax - 273.15);
    printf(" FUEL  MEAN %6.0f C   CENTRELINE MAX %6.0f C      CLAD MAX %6.0f C\033[K\n",
           rm_core_mean_fuel_T(c) - 273.15, rm_core_max_fuel_T(c) - 273.15, rm_core_max_clad_T(c) - 273.15);
    printf("\033[K\n");
    printf(" CONTROL RODS (cm inserted of 160)     SCRAM: %s\033[K\n", c->scram ? BRIGHT INV " TRIPPED " RST AMBER : "no");
    printf("  ");
    for (int b = 0; b < RM_NBANKS; b++) printf(" %-4s %5.1f  ", bank_name[b], rm_core_bank_pos(c, b));
    printf("\033[K\n\033[K\n");
    /* power trend */
    printf(" POWER TREND (last %d s, 0-120%%)\033[K\n", ntrend);
    static const char *blk[] = {" ", "\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84",
                                "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88"};
    printf("  ");
    for (int i = 0; i < 64; i++) {
        if (i < 64 - ntrend) { printf(" "); continue; }
        double v = trend[i] / 120.0;
        int k = (int)(v * 8 + 0.5);
        if (k < 0) k = 0;
        if (k > 8) k = 8;
        printf("%s", blk[k]);
    }
    printf("\033[K\n\033[K\n");
    printf(" MESSAGES\033[K\n");
    for (int i = 0; i < NLOG; i++) printf("  %s\033[K\n", logbuf[i]);
    printf("\033[K\n");
    printf(" > %s" BRIGHT "_" AMBER "\033[K\n", cmd);
    printf(DIM " HELP for commands" AMBER "\033[K");
    fflush(stdout);
}

static void help(rm_core *c)
{
    logmsg(c, "ROD <REG|A|B|C|D|SAFE|ALL> <cm>   drive bank to depth (0=out 160=in)");
    logmsg(c, "SCRAM   RESET   FLOW <%%>   TIN <degC>   RUN <1-8>   QUIT");
}

static void command(rm_core *c, char *line, int *quit)
{
    char a[32] = "", b[32] = "", d[32] = "";
    int n = sscanf(line, "%31s %31s %31s", a, b, d);
    if (n <= 0) return;
    for (char *p = a; *p; p++) *p = (char)toupper((unsigned char)*p);
    if (!strcmp(a, "QUIT") || !strcmp(a, "EXIT")) {
        *quit = 1;
    } else if (!strcmp(a, "HELP")) {
        help(c);
    } else if (!strcmp(a, "SCRAM")) {
        rm_core_scram(c);
        logmsg(c, "*** MANUAL SCRAM ***");
    } else if (!strcmp(a, "RESET")) {
        if (!c->scram) logmsg(c, "NO TRIP TO RESET");
        else {
            rm_core_reset_scram(c);
            tripped_logged = 0;
            logmsg(c, "TRIP RESET - RODS REMAIN INSERTED");
        }
    } else if (!strcmp(a, "ROD") && n == 3) {
        if (c->scram) { logmsg(c, "ROD MOTION BLOCKED: TRIP NOT RESET"); return; }
        double pos = atof(d);
        if (!strcasecmp(b, "ALL")) {
            for (int k = 0; k < RM_NBANKS; k++) rm_core_bank_move(c, k, pos);
            logmsg(c, "ALL BANKS -> %.1f cm", pos);
        } else {
            int bk = bank_of(b);
            if (bk < 0) { logmsg(c, "UNKNOWN BANK %s", b); return; }
            rm_core_bank_move(c, bk, pos);
            logmsg(c, "BANK %s -> %.1f cm (%.1f cm/s)", bank_name[bk], pos, c->bank_speed[bk]);
        }
    } else if (!strcmp(a, "FLOW") && n >= 2) {
        double f = atof(b);
        if (f < 5) f = 5;
        if (f > 110) f = 110;
        flow_pct = f;
        logmsg(c, "PRIMARY FLOW SET %.1f %%", f);
    } else if (!strcmp(a, "TIN") && n >= 2) {
        double t = atof(b);
        if (t < 200) t = 200;
        if (t > 500) t = 500;
        tin_c = t;
        logmsg(c, "CORE INLET TEMPERATURE SET %.1f C", t);
    } else if (!strcmp(a, "RUN") && n >= 2) {
        int s = atoi(b);
        if (s < 1) s = 1;
        if (s > 8) s = 8;
        speed = s;
        logmsg(c, "SIMULATION SPEED x%d", s);
    } else {
        logmsg(c, "?SYNTAX: %s", line);
    }
}

int main(void)
{
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "rmelm needs an interactive terminal\n");
        return 1;
    }
    rm_core *c = malloc(sizeof *c);
    printf(AMBER "RM-ELM PLANT PROCESS COMPUTER\nLOADING CORE MODEL AND CONVERGING FULL-POWER STATE ...\n" RST);
    fflush(stdout);
    rm_core_init(c);
    rm_core_steady(c, 1.0, 1, 1);
    for (int ch = 0; ch < c->nchan && ch < 1024; ch++) {
        w_nom[ch] = c->th.W[ch];
        wb_nom[ch] = c->th.W_byp[ch];
    }
    logmsg(c, "CORE AT RATED POWER, SHIM BANKS HOLDING CRITICALITY");
    logmsg(c, "TYPE HELP FOR COMMANDS");

    raw_term();
    char cmd[80] = "";
    int len = 0, quit = 0;
    double next = now(), last_draw = 0, last_trend = 0;
    n_prev = c->pks.n;
    while (!quit) {
        /* keyboard */
        char ch;
        while (read(STDIN_FILENO, &ch, 1) == 1) {
            if (ch == '\n' || ch == '\r') {
                cmd[len] = 0;
                command(c, cmd, &quit);
                len = 0;
                cmd[0] = 0;
            } else if (ch == 127 || ch == 8) {
                if (len) cmd[--len] = 0;
            } else if (ch == 3) {
                quit = 1;
            } else if (isprint((unsigned char)ch) && len < 70) {
                cmd[len++] = (char)toupper((unsigned char)ch);
                cmd[len] = 0;
            }
        }
        /* simulation */
        double tnow = now();
        int steps = 0;
        while (tnow >= next && steps < 8 * speed) {
            for (int s = 0; s < speed; s++) {
                apply_plant(c);
                double n0 = c->pks.n;
                rm_core_step(c, DT);
                if (n0 > 0 && c->pks.n > 0) {
                    double inst = DT / log(c->pks.n / n0);
                    period = 1.0 / (0.9 / period + 0.1 / inst);
                }
                protection(c);
                steps++;
            }
            next += DT;
        }
        if (tnow - next > 1.0) next = tnow; /* can't keep up: don't spiral */
        if (c->t - last_trend >= 1.0) {
            memmove(trend, trend + 1, sizeof(double) * 63);
            trend[63] = 100.0 * c->p_thermal / RM_P_RATED;
            if (ntrend < 64) ntrend++;
            last_trend = c->t;
        }
        if (tnow - last_draw > 0.25) {
            draw(c, cmd);
            last_draw = tnow;
        }
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(STDIN_FILENO, &fs);
        struct timeval tv = {0, 10000};
        select(STDIN_FILENO + 1, &fs, NULL, NULL, &tv);
    }
    rm_core_free(c);
    free(c);
    return 0;
}
