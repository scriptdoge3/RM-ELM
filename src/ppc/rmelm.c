/*
 * RM-ELM plant process computer - amber-phosphor terminal front end.
 *
 * Full-screen display of the whole plant with a command prompt, in real
 * time: core, four sodium loops, steam generators and the turbine, with the
 * reactor protection system armed, the load dispatcher and random
 * equipment failures. `rmelm --hot` starts from hot shutdown,
 * `rmelm --no-failures` turns the random failures off.
 *
 * POSIX terminals only for now (Linux, macOS, WSL).
 */
#define _POSIX_C_SOURCE 200809L
#include "rm_if97.h"
#include "rm_kernels.h"
#include "rm_game.h"
#include "rm_plant.h"
#include "rm_sodium.h"

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

static rm_game G;
static double trend[64];
static int ntrend = 0;
static char last_first_out[48] = "";

static const char *bank_name[RM_NBANKS] = {"REG", "A", "B", "C", "D", "SAFE"};

static int bank_of(const char *s)
{
    for (int b = 0; b < RM_NBANKS; b++)
        if (strcasecmp(s, bank_name[b]) == 0) return b;
    if (strcasecmp(s, "SAFETY") == 0) return BANK_SAFETY;
    return -1;
}

static double C(double K) { return K - 273.15; }

static void bar(double frac, int w)
{
    int n = (int)(frac * w + 0.5);
    if (n < 0) n = 0;
    if (n > w) n = w;
    for (int i = 0; i < w; i++) fputs(i < n ? "\xe2\x96\x88" : DIM "\xc2\xb7" AMBER, stdout);
}

static void draw(rm_plant *p, const char *cmd)
{
    rm_core *c = &p->core;
    int t = (int)p->t;
    double pct = 100.0 * c->p_thermal / RM_P_RATED;
    double beta = 0;
    for (int i = 0; i < 6; i++) beta += c->pk.beta[i];

    printf("\033[H" AMBER);
    printf(INV " RM-ELM UNIT 1   PLANT PROCESS COMPUTER                          T+%02d:%02d:%02d " RST AMBER "\033[K\n",
           t / 3600, t / 60 % 60, t % 60);
    if (G.on_line)
        printf(" LOAD DISPATCH " BRIGHT "%5.0f MWe" AMBER " (ordered %4.0f)  NET %5.0f MWe  DEVIATION %+5.1f %%   SCORE " BRIGHT
               "%7.0f" AMBER "   %6.0f MWh\033[K\n",
               G.demand, G.demand_target, p->P_net / 1e6, 100 * rm_game_deviation(&G, p), G.score, G.mwh);
    else
        printf(" LOAD DISPATCH: AWAITING SYNCHRONISATION                          SCORE " BRIGHT "%7.0f" AMBER "\033[K\n", G.score);
    printf(" REACTOR POWER " BRIGHT "%7.1f MWt %6.2f %%" AMBER "  ", c->p_thermal / 1e6, pct);
    bar(pct / 120.0, 20);
    printf("   RPS: %s\033[K\n", c->scram ? BRIGHT INV " TRIPPED " RST AMBER : (p->rps_bypass ? "BYPASSED" : "armed"));
    printf("   FISSION %7.1f MW  DECAY %6.1f MW  REACTIVITY %+7.1f pcm (%+6.3f $)  ",
           c->p_fission / 1e6, c->p_decay / 1e6, 1e5 * c->rho, c->rho / beta);
    if (isfinite(p->period) && fabs(p->period) < 1e4) printf("PERIOD %+7.1f s\033[K\n", p->period);
    else printf("PERIOD   INFIN\033[K\n");
    static const char *mode_name[4] = {"SHUTDOWN", "REFUEL", "STARTUP", "RUN"};
    printf("   FIRST OUT: %-28s MODE %-8s  SRM %8.0f cps  IRM R%-2d %5.1f  APRM %5.1f %%\033[K\n",
           p->first_out[0] ? p->first_out : "-", mode_name[p->mode], rm_plant_srm_cps(p), p->nms.irm_range[0],
           rm_plant_irm(p), rm_plant_aprm(p));
    printf("   AUTO ROD %s %3.0f%%  RODS cm in:", p->auto_rod ? "ON " : "off", 100 * p->power_set);
    for (int b = 0; b < RM_NBANKS; b++) printf(" %s %5.1f", bank_name[b], rm_core_bank_pos(c, b));
    printf("\033[K\n");
    printf(" CORE  FLOW %6.0f kg/s (%5.1f %%)  INLET %5.1f C  OUTLET %5.1f C  FUEL MAX %5.0f C  CLAD MAX %5.0f C\033[K\n",
           p->W_core, 100.0 * p->W_core / rm_plant_nominal_flow(), C(p->T_core_in), C(p->T_core_out),
           C(rm_core_max_fuel_T(c)), C(rm_core_max_clad_T(c)));
    printf("\033[K\n");
    printf(DIM " LOOP  PRI PUMP   PRI FLOW  HOT LEG COLD LEG   SEC PUMP  SEC HOT SEC COLD   FW kg/s  STEAM C  SG MW  H2 ppm" AMBER "\033[K\n");
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        const char *ps = l->ppump.tripped ? "TRIP" : (l->ppump.motor_on ? "RUN " : (l->ppump.pony_on ? "PONY" : "OFF "));
        const char *ss = l->spump.tripped ? "TRIP" : (l->spump.motor_on ? "RUN " : "OFF ");
        printf("  %d    %s %3.0f%%  %6.0f   %6.1f   %6.1f    %s %3.0f%%  %6.1f   %6.1f   %7.1f  %6.1f %6.1f  %s%5.2f%s%s\033[K\n",
               i + 1, ps, 100 * l->ppump.speed, l->W, C(rm_na_T(l->hot.h[RM_PIPE_N - 1])),
               C(rm_na_T(l->cold.h[RM_PIPE_N - 1])), ss, 100 * l->spump.speed,
               C(rm_na_T(l->shot.h[RM_PIPE_N - 1])), C(rm_na_T(l->scold.h[RM_PIPE_N - 1])),
               l->sg.W_fw, C(l->sg.T_steam), l->sg.Q / 1e6, l->sg.h2 > RM_H2_ALARM ? BRIGHT INV : "", l->sg.h2,
               l->sg.h2 > RM_H2_ALARM ? RST AMBER : "",
               l->sg.disc_burst ? " DISC BURST" : (l->sg.isolated ? " ISOLATED" : ""));
    }
    printf("\033[K\n");
    printf(" FEEDWATER %s  STEAM %5.2f MPa  TURBINE %s VALVE %3.0f%%  BYPASS %3.0f%%  RELIEF %s  COND %4.1f kPa  FW %5.1f C\033[K\n",
           p->fw_on ? "ON " : "OFF", p->p_header / 1e6, p->turbine_tripped ? "TRIPPED" : "online ", 100 * p->turbine_valve,
           100 * p->bypass_valve, p->W_relief > 0 ? "OPEN" : "shut", p->p_cond / 1e3, C(p->T_fw));
    printf(" GENERATOR " BRIGHT "%6.1f MWe" AMBER " gross   HOUSE LOAD %5.1f MW   NET " BRIGHT "%6.1f MWe" AMBER "   BREAKER %s\033[K\n",
           p->P_gen / 1e6, p->P_house / 1e6, p->P_net / 1e6, p->generator_breaker ? "CLOSED" : "OPEN");
    printf(" GRID %s  DIESELS", p->offsite_power ? "ON " : BRIGHT INV "LOST" RST AMBER);
    for (int i = 0; i < 3; i++)
        printf(" %d:%s", i + 1, !p->diesel_avail[i] ? "FAIL" : (p->diesel_running[i] ? "RUN " : "stby"));
    printf("   DRACS %s dampers %3.0f/%3.0f/%3.0f %%  %5.1f MW  %5.0f kg/s\033[K\n",
           p->dracs_auto ? "AUTO" : "MAN ", 100 * p->dracs_damper[0], 100 * p->dracs_damper[1],
           100 * p->dracs_damper[2], p->Q_dracs / 1e6, p->W_dracs);
    printf(" POWER TREND (%d s)  ", ntrend);
    static const char *blk[] = {" ", "\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84",
                                "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88"};
    for (int i = 0; i < 64; i++) {
        if (i < 64 - ntrend) { printf(" "); continue; }
        int k = (int)(trend[i] / 120.0 * 8 + 0.5);
        if (k < 0) k = 0;
        if (k > 8) k = 8;
        printf("%s", blk[k]);
    }
    printf("\033[K\n\033[K\n MESSAGES\033[K\n");
    for (int i = 0; i < NLOG; i++) printf("  %s\033[K\n", logbuf[i]);
    printf(" > %s" BRIGHT "_" AMBER "\033[K\n", cmd);
    printf(DIM " HELP for commands" AMBER "\033[K");
    fflush(stdout);
}

static void help(rm_plant *p)
{
    rm_core *c = &p->core;
    logmsg(c, "ROD <REG|A|B|C|D|SAFE|ALL> <cm>  0=out 160=in    SCRAM   RESET");
    logmsg(c, "PUMP <P1-P4|S1-S4> <START|STOP|PONY|SPEED n>     TURB <TRIP|RESET>");
    logmsg(c, "AUTO <%%|OFF>  PSET <MPa>  FW <START|STOP|AUTO|MAN>  SG <1-4> ISOLATE");
    logmsg(c, "MODE <SD|REFUEL|STARTUP|RUN>  IRM <1-10>  DRACS <OPEN|CLOSE|AUTO>  RPS <ON|BYPASS>  QUIT");
}

static void command(rm_plant *p, char *line, int *quit)
{
    rm_core *c = &p->core;
    char a[32] = "", b[32] = "", d[32] = "", e[32] = "";
    int n = sscanf(line, "%31s %31s %31s %31s", a, b, d, e);
    if (n <= 0) return;
    if (!strcmp(a, "QUIT") || !strcmp(a, "EXIT")) {
        *quit = 1;
    } else if (!strcmp(a, "HELP")) {
        help(p);
    } else if (!strcmp(a, "SCRAM")) {
        rm_plant_manual_scram(p);
        logmsg(c, "*** MANUAL SCRAM ***");
    } else if (!strcmp(a, "RESET")) {
        if (!c->scram) logmsg(c, "NO TRIP TO RESET");
        else {
            rm_plant_rps_reset(p, 0);
            rm_plant_rps_reset(p, 1);
            last_first_out[0] = 0;
            logmsg(c, "RPS RESET - RODS REMAIN INSERTED");
        }
    } else if (!strcmp(a, "MODE") && n >= 2) {
        int md = !strncmp(b, "SD", 2) || !strncmp(b, "SHUT", 4) ? RM_MODE_SHUTDOWN
               : !strncmp(b, "REF", 3) ? RM_MODE_REFUEL : !strncmp(b, "START", 5) ? RM_MODE_STARTUP
               : !strcmp(b, "RUN") ? RM_MODE_RUN : -1;
        char why[80];
        if (md < 0) logmsg(c, "MODE SD|REFUEL|STARTUP|RUN");
        else if (!rm_plant_set_mode(p, md, why, sizeof why)) logmsg(c, "%s", why);
    } else if (!strcmp(a, "IRM") && n >= 2) {
        int rg = atoi(b);
        if (rg < 1 || rg > 10) { logmsg(c, "IRM <1-10>"); return; }
        for (int k = 0; k < 8; k++) p->nms.irm_range[k] = rg;
        logmsg(c, "IRM RANGE %d", rg);
    } else if (!strcmp(a, "ROD") && n == 3) {
        char why[80];
        int withdrawing = 0;
        {
            double tgt = atof(d);
            int bk0 = !strcmp(b, "ALL") ? -1 : bank_of(b);
            for (int k = 0; k < RM_NBANKS; k++)
                if ((bk0 < 0 || k == bk0) && tgt < rm_core_bank_pos(c, k) - 0.01) withdrawing = 1;
        }
        if (withdrawing && rm_plant_rod_block(p, 0, why, sizeof why)) { logmsg(c, "%s", why); return; }
        if (p->auto_rod && !strcmp(b, "REG")) { logmsg(c, "REG BANK IS IN AUTO - USE AUTO OFF FIRST"); return; }
        double pos = atof(d);
        if (!strcmp(b, "ALL")) {
            for (int k = 0; k < RM_NBANKS; k++) rm_core_bank_move(c, k, pos);
            logmsg(c, "ALL BANKS -> %.1f cm", pos);
        } else {
            int bk = bank_of(b);
            if (bk < 0) { logmsg(c, "UNKNOWN BANK %s", b); return; }
            if (!rm_plant_rod_permit(p, bk, -1, pos, why, sizeof why)) { logmsg(c, "%s", why); return; }
            rm_core_bank_move(c, bk, pos);
            logmsg(c, "BANK %s -> %.1f cm (%.1f cm/s)", bank_name[bk], pos, c->bank_speed[bk]);
        }
    } else if (!strcmp(a, "PUMP") && n >= 3) {
        int idx = atoi(b + 1) - 1;
        if ((b[0] != 'P' && b[0] != 'S') || idx < 0 || idx >= RM_NLOOPS) { logmsg(c, "PUMP P1-P4 OR S1-S4"); return; }
        rm_pump *pp = b[0] == 'P' ? &p->loop[idx].ppump : &p->loop[idx].spump;
        if (!strcmp(d, "START")) { pp->tripped = 0; pp->motor_on = 1; if (pp->speed_set < 0.2) pp->speed_set = 1.0; }
        else if (!strcmp(d, "STOP")) { pp->motor_on = 0; }
        else if (!strcmp(d, "PONY")) { pp->pony_on = !pp->pony_on; }
        else if (!strcmp(d, "SPEED") && n == 4) {
            double v = atof(e) / 100.0;
            pp->speed_set = v < 0.1 ? 0.1 : (v > 1.05 ? 1.05 : v);
        } else { logmsg(c, "?PUMP %s", d); return; }
        logmsg(c, "PUMP %s: %s%s", b, d, !strcmp(d, "PONY") ? (pp->pony_on ? " ON" : " OFF") : "");
    } else if (!strcmp(a, "TURB") && n >= 2) {
        if (!strcmp(b, "TRIP")) rm_plant_turbine_trip(p, "MANUAL");
        else if (!strcmp(b, "RESET")) {
            /* latch, roll to speed and let the auto-synchroniser close the breaker */
            char why[80];
            if (!p->tg.field_breaker) p->tg.field_breaker = 1;
            if (rm_plant_turbine_latch(p, why, sizeof why)) {
                p->tg.auto_sync = 1;
                p->tg.speed_target = 1800.0;
                logmsg(c, "TURBINE ROLLING TO 1800 RPM ON AUTO SYNC");
            } else logmsg(c, "%s", why);
        } else logmsg(c, "?TURB %s", b);
    } else if (!strcmp(a, "PSET") && n >= 2) {
        double v = atof(b);
        if (v < 8) v = 8;
        if (v > 15.5) v = 15.5;
        p->p_set = v * 1e6;
        logmsg(c, "STEAM PRESSURE SETPOINT %.2f MPa", v);
    } else if (!strcmp(a, "FW") && n >= 2) {
        if (!strcmp(b, "START") || !strcmp(b, "STOP")) {
            p->fw_on = !strcmp(b, "START");
            if (p->fw_on) {
                /* condensate pumps and both turbine-driven feed pumps (they pick up once there is steam) */
                p->tg.cond_pump[0] = p->tg.cond_pump[1] = 1;
                p->tg.tdfp[0] = p->tg.tdfp[1] = 1;
            }
            logmsg(c, "FEEDWATER PUMPS %s", p->fw_on ? "STARTED" : "STOPPED");
        } else {
            p->auto_fw = !strcmp(b, "AUTO");
            logmsg(c, "FEEDWATER CONTROL %s", p->auto_fw ? "AUTO" : "MANUAL (valves frozen)");
        }
    } else if (!strcmp(a, "SG") && n >= 3 && !strncmp(d, "ISOL", 4)) {
        int i = atoi(b) - 1;
        if (i < 0 || i >= RM_NLOOPS) { logmsg(c, "SG <1-4> ISOLATE"); return; }
        rm_plant_isolate_sg(p, i);
    } else if (!strcmp(a, "AUTO") && n >= 2) {
        if (!strcmp(b, "OFF")) {
            p->auto_rod = 0;
            logmsg(c, "AUTO ROD CONTROL OFF");
        } else {
            double v = atof(b);
            if (v < 1 || v > 110) { logmsg(c, "AUTO <1-110 %%> OR AUTO OFF"); return; }
            p->auto_rod = 1;
            p->power_set = v / 100.0;
            logmsg(c, "AUTO ROD CONTROL: REG BANK HOLDS %.0f %% POWER", v);
        }
    } else if (!strcmp(a, "DRACS") && n >= 2) {
        if (!strcmp(b, "AUTO")) {
            p->dracs_auto = 1;
            for (int i = 0; i < 3; i++) p->dracs_man[i] = 0;
        } else {
            p->dracs_auto = 0;
            for (int i = 0; i < 3; i++) p->dracs_damper_set[i] = !strcmp(b, "OPEN") ? 1.0 : 0.0;
        }
        logmsg(c, "DRACS DAMPERS %s", b);
    } else if (!strcmp(a, "RPS") && n >= 2) {
        p->rps_bypass = !strcmp(b, "BYPASS");
        logmsg(c, p->rps_bypass ? "*** RPS BYPASSED - TRIPS DISABLED ***" : "RPS ARMED");
    } else {
        logmsg(c, "?SYNTAX: %s", line);
    }
}

int main(int argc, char **argv)
{
    int hot = 0, failures = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hot")) hot = 1;
        else if (!strcmp(argv[i], "--no-failures")) failures = 0;
        else {
            fprintf(stderr, "usage: rmelm [--hot] [--no-failures]\n");
            return 1;
        }
    }
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "rmelm needs an interactive terminal\n");
        return 1;
    }
    rm_plant *p = malloc(sizeof *p);
    printf(AMBER "RM-ELM PLANT PROCESS COMPUTER\n"
           "LOADING CORE MODEL, SODIUM LOOPS AND STEAM PLANT\n"
           "%s (ABOUT 20 S) ...\n" RST, hot ? "SETTING UP HOT SHUTDOWN" : "CONVERGING RATED-POWER HEAT BALANCE");
    fflush(stdout);
    rm_plant_init(p);
    if (hot) rm_plant_hot_standby(p);
    else rm_plant_steady(p);
    rm_core *c = &p->core;
    rm_game_init(&G, p, (unsigned)time(NULL), failures);
    unsigned seen = p->nmsg;
    if (hot) {
        logmsg(c, "UNIT IN HOT SHUTDOWN: ALL RODS IN, SODIUM AT 380 C");
        logmsg(c, "MODE STARTUP, ROD SAFE 0, SHIMS OUT TO CRITICAL, IRM RANGE UP, RUN AT 5-15%%");
    } else {
        logmsg(c, "UNIT AT RATED POWER, TURBINE ON LINE - FOLLOW THE LOAD DISPATCHER");
    }
    logmsg(c, "TYPE HELP FOR COMMANDS");

    raw_term();
    char cmd[80] = "";
    int len = 0, quit = 0;
    double next = now(), last_draw = 0, last_trend = -1;
    while (!quit) {
        char ch;
        while (read(STDIN_FILENO, &ch, 1) == 1) {
            if (ch == '\n' || ch == '\r') {
                cmd[len] = 0;
                command(p, cmd, &quit);
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
        double tnow = now();
        int steps = 0;
        while (tnow >= next && steps < 8) {
            rm_plant_step(p, DT);
            rm_game_step(&G, p, DT);
            steps++;
            next += DT;
        }
        while (seen < p->nmsg) logmsg(c, "%s", p->msg[seen++ % 16]);
        if (tnow - next > 1.0) next = tnow;
        if (p->first_out[0] && strcmp(p->first_out, last_first_out)) {
            strcpy(last_first_out, p->first_out);
            if (strcmp(p->first_out, "MANUAL SCRAM"))
                logmsg(c, "*** REACTOR TRIP  FIRST OUT: %s ***", p->first_out);
        }
        if (p->t - last_trend >= 1.0) {
            memmove(trend, trend + 1, sizeof(double) * 63);
            trend[63] = 100.0 * c->p_thermal / RM_P_RATED;
            if (ntrend < 64) ntrend++;
            last_trend = p->t;
        }
        if (tnow - last_draw > 0.25) {
            draw(p, cmd);
            last_draw = tnow;
        }
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(STDIN_FILENO, &fs);
        struct timeval tv = {0, 10000};
        select(STDIN_FILENO + 1, &fs, NULL, NULL, &tv);
    }
    rm_plant_free(p);
    free(p);
    return 0;
}
