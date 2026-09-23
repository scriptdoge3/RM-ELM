/*
 * RM-ELM graphical control room (raylib), drawn as a 1978 control room:
 * each board is a row of system sections, each with its annunciator window
 * box on the hood, a vertical section of switchboard and edgewise meters and
 * recorders, and a sloped benchboard with the control switches, M/A
 * stations and pushbuttons laid out on a colour-coded mimic. Engraved
 * nameplates and operator label tape, red LED readouts, pistol-grip and
 * J-handle control switches, key switches, guarded pushbuttons, an alarm
 * typer along the front lip.
 *
 * Six boards, picked from the header (or F1-F6):
 *   REACTOR           1-1 nuclear instrumentation and protection, 1-2 reactor
 *                     control, 1-3 core monitoring
 *   HEAT TRANSPORT    2-1 primary, 2-2 intermediate loops and SG protection,
 *                     2-3 decay heat removal and containment isolation
 *   TURBINE-GEN/FEED  3-1 feedwater and main steam, 3-2 turbine and
 *                     condenser, 3-3 generator
 *   ELECTRICAL        4-1 distribution, 4-2 emergency power and DC
 *   AUXILIARY         5-1 sodium auxiliaries, 5-2 containment and radiation,
 *                     5-3 plant services and fire protection
 *   REMOTE SHUTDOWN   the panel outside the control room, for an evacuation
 */
#include "gui.h"

#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- shared state -------------------------------------------------------------- */
rm_plant *P;
rm_game G;
int input_ok = 1;
int board = BOARD_REACTOR;
float trend[TR_N][NTR];
int ntr = 0;
long nsamp = 0;
char logs[NLOG][100];
double last_log_time = -100;
mpr MP_NA = {6, {"OUT", "IN", "HL1", "HL2", "HL3", "HL4"}, {0}, {0}};
mpr MP_TURB = {5, {"RPM", "VIB", "ECC", "BRG", "OIL"}, {0}, {0}};
mpr MP_H2 = {4, {"SG1", "SG2", "SG3", "SG4"}, {0}, {0}};
mpr MP_SG = {4, {"SG1", "SG2", "SG3", "SG4"}, {0}, {0}};
mpr MP_RAD = {6, {"CR", "HALL", "GAS", "STACK", "SEC NA", "STEAM"}, {0}, {0}};

static volatile int loaded = 0;
static int paused = 0;
static int start_hot = 0;               /* start from hot shutdown instead of rated power */
static int random_failures = 1;
static unsigned msg_seen = 0;
static char last_first_out[48] = "";

void logmsg(const char *fmt, ...)
{
    for (int i = 0; i < NLOG - 1; i++) memcpy(logs[i], logs[i + 1], sizeof logs[i]);
    char m[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    for (char *q = m; *q; q++) *q = (char)toupper((unsigned char)*q);   /* teleprinters had one case */
    int t = P ? (int)P->t : 0;
    snprintf(logs[NLOG - 1], sizeof logs[0], "%02d:%02d:%02d  %s", t / 3600, t / 60 % 60, t % 60, m);
    last_log_time = GetTime();
}

static void *loader(void *arg)
{
    (void)arg;
    rm_plant_init(P);
    if (start_hot) rm_plant_hot_standby(P);
    else rm_plant_steady(P);
    loaded = 1;
    return NULL;
}

/* ---- trends ---------------------------------------------------------------------- */
static void sample(void)
{
    rm_core *c = &P->core;
    float v[TR_N];
    v[TR_POWER] = (float)(100 * c->p_thermal / RM_P_RATED);
    v[TR_OUTLET] = (float)(P->T_core_out - 273.15);
    v[TR_INLET] = (float)(P->T_core_in - 273.15);
    v[TR_PHDR] = (float)(P->p_header / 1e6);
    v[TR_GENMW] = (float)(P->P_gen / 1e6);
    v[TR_SPEED] = (float)P->tg.speed;
    v[TR_GASACT] = (float)log10(fmax(P->aux.gas_act, 0.01));
    v[TR_DND] = (float)log10(fmax(P->aux.dnd, 1.0));
    v[TR_LOGPWR] = (float)log10(fmax(c->p_thermal / RM_P_RATED, 1e-10));
    v[TR_FLOW] = (float)(100 * P->W_core / rm_plant_nominal_flow());
    double sur = isfinite(P->period) && fabs(P->period) < 1e4 ? 26.06 / P->period : 0.0;
    v[TR_PERIOD] = (float)fmin(5.0, fmax(-1.0, sur));
    double wf = 0, dg = 0;
    for (int i = 0; i < RM_NLOOPS; i++) wf += P->loop[i].sg.W_fw;
    for (int i = 0; i < 3; i++) dg += P->tg.dg_mw[i];
    v[TR_FEED] = (float)wf;
    v[TR_DRACS] = (float)(P->Q_dracs / 1e6);
    v[TR_DECAY] = (float)(c->p_decay / 1e6);
    v[TR_NET] = (float)fmax(P->P_net / 1e6, 0.0);
    v[TR_DEMAND] = (float)(G.on_line ? G.demand : 0.0);
    v[TR_DGMW] = (float)dg;
    v[TR_BATT] = (float)(100 * P->tg.batt[0]);
    for (int k = 0; k < TR_N; k++) {
        memmove(trend[k], trend[k] + 1, sizeof(float) * (NTR - 1));
        trend[k][NTR - 1] = v[k];
    }
    float na[6] = {v[TR_OUTLET], v[TR_INLET]};
    for (int i = 0; i < RM_NLOOPS; i++) na[2 + i] = (float)(rm_na_T(P->loop[i].hot.h[RM_PIPE_N - 1]) - 273.15);
    for (int i = 0; i < 6; i++) na[i] = (na[i] - 300.0f) / 4.0f;
    mpr_push(&MP_NA, na);
    rm_tg *t = &P->tg;
    float tb[5] = {(float)(t->speed / 20.0), (float)(t->vib * 10.0), (float)(t->ecc * 10.0), (float)(t->brg_T / 1.2),
                   (float)(t->lube_p / 0.03)};
    mpr_push(&MP_TURB, tb);
    float h2[4], sg[4], rad[6];
    for (int i = 0; i < RM_NLOOPS; i++) {
        h2[i] = (float)(100 * P->loop[i].sg.h2);
        sg[i] = (float)((P->loop[i].sg.T_steam - 573.15) / 3.0);
    }
    mpr_push(&MP_H2, h2);
    mpr_push(&MP_SG, sg);
    static const int rk[6] = {RM_RAD_CR, RM_RAD_HALL, RM_RAD_GAS, RM_RAD_STACK, RM_RAD_SECNA, RM_RAD_STEAM};
    for (int i = 0; i < 6; i++) rad[i] = (float)((log10(fmax(P->aux.rad[rk[i]], 0.01)) + 2.0) / 6.0 * 100.0);
    mpr_push(&MP_RAD, rad);
    if (ntr < NTR) ntr++;
    nsamp++;
}

/* ---- header with the board selector -------------------------------------------------- */
static const char *board_name[NBOARDS] = {"REACTOR BOARD", "HEAT TRANSPORT BOARD", "TURBINE-GENERATOR BOARD",
                                          "ELECTRICAL BOARD", "AUXILIARY BOARD", "REMOTE SHUTDOWN PANEL"};

static void header(void)
{
    input_ok = 1;
    char title[64];
    snprintf(title, sizeof title, "RM-ELM UNIT 1  %s", board_name[board]);
    plate(8, 8, title, 12);
    int t = (int)P->t;
    const float x0 = 390, dx = 118;
    dymo(x0, 4, "PLANT TIME");
    readout(x0, 18, 16, 6, "%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    dymo(x0 + dx, 4, "DISPATCH MWE");
    if (G.on_line) readout(x0 + dx, 18, 16, 4, "%4.0f", G.demand);
    else readout(x0 + dx, 18, 16, 4, "----");
    dymo(x0 + 2 * dx, 4, "ORDERED MWE");
    if (G.on_line) readout(x0 + 2 * dx, 18, 16, 4, "%4.0f", G.demand_target);
    else readout(x0 + 2 * dx, 18, 16, 4, "----");
    dymo(x0 + 3 * dx, 4, "NET MWE");
    readout(x0 + 3 * dx, 18, 16, 4, "%4.0f", fmax(P->P_net / 1e6, 0.0));
    dymo(x0 + 4 * dx, 4, "REACTOR PCT");
    readout(x0 + 4 * dx, 18, 16, 4, "%5.1f", 100 * P->core.p_thermal / RM_P_RATED);
    dymo(x0 + 5 * dx, 4, "SCORE");
    readout(x0 + 5 * dx, 18, 16, 6, "%6.0f", fmax(fmin(G.score, 999999.0), -99999.0));
    dymo(x0 + 6 * dx, 4, "MWH SENT");
    readout(x0 + 6 * dx, 18, 16, 6, "%6.0f", fmin(G.mwh, 999999.0));

    static const char *tab[NBOARDS] = {"F1 REACTOR",     "F2 HEAT\nTRANSPORT", "F3 TURBINE\nGEN-FEED",
                                       "F4 ELEC-\nTRICAL", "F5 AUX-\nILIARY",    "F6 REMOTE\nSHUTDOWN"};
    for (int b = 0; b < NBOARDS; b++) {
        int alert = b != board && ann_board_alerting(b);
        Color lens = alert ? L_AMB : L_WHT;
        if (lampbutton((Rectangle){1186 + b * 90, 4, 86, 38}, tab[b], lens, b == board || (alert && blink_fast()))) board = b;
    }
    if (lampbutton((Rectangle){1730, 6, 56, 34}, "HOLD", L_AMB, paused)) paused = !paused;
    dymo(1794, 6, "F11 FULL SCREEN");
    dymo(1794, 24, "F12 PHOTO");
}

/* smoke in the control room: thickens as the fire burns, then the crew is out */
static void smoke_overlay(void)
{
    rm_aux *a = &P->aux;
    if (a->evacuated) {
        DrawRectangle(0, 46, CW, CH - 46, (Color){70, 70, 66, 200});
        for (int i = 0; i < 14; i++) {
            float ph = (float)(GetTime() * 0.2 + i * 0.7);
            DrawCircleV((Vector2){(float)fmod(i * 353.0 + GetTime() * 12.0, CW), 200.0f + 60.0f * sinf(ph) + i * 55.0f},
                        160, (Color){110, 110, 104, 60});
        }
        Rectangle b = {CW / 2 - 330, CH / 2 - 70, 660, 140};
        DrawRectangleRec(b, (Color){22, 22, 20, 240});
        DrawRectangleLinesEx(b, 3, L_RED);
        ctext("CONTROL ROOM EVACUATED - SMOKE", CW / 2, b.y + 26, 20, L_RED);
        ctext("THE MAIN BOARDS CANNOT BE WORKED FROM HERE.", CW / 2, b.y + 66, 10, L_WHT);
        ctext("GO TO THE REMOTE SHUTDOWN PANEL (F6) AND TAKE CONTROL WITH THE TRANSFER KEY.", CW / 2, b.y + 84, 10, L_WHT);
        return;
    }
    if (a->fire[RM_FIRE_CR]) {
        unsigned char al = (unsigned char)fmin(150.0, 30.0 + a->fire_t[RM_FIRE_CR] * 2.0);
        DrawRectangle(0, 46, CW, CH - 46, (Color){90, 90, 84, al});
    }
    if (a->rsp_control) {
        Rectangle b = {CW / 2 - 300, 50, 600, 28};
        DrawRectangleRec(b, (Color){30, 20, 10, 230});
        ctext("CONTROL TRANSFERRED TO THE REMOTE SHUTDOWN PANEL - MAIN BOARDS DEAD", CW / 2, b.y + 9, 10, L_AMB);
    }
}

static void loading_screen(void)
{
    ClearBackground(WALL);
    Rectangle b = {CW / 2 - 300, CH / 2 - 160, 600, 320};
    steel(b, "RM-ELM   UNIT 1");
    int blink = ((int)(GetTime() * 2)) & 1;
    readout(b.x + 190, b.y + 60, 60, 4, blink ? "8888" : "    ");
    ctext(start_hot ? "PLANT COMPUTER SETTING UP HOT SHUTDOWN" : "PLANT COMPUTER CONVERGING TO RATED POWER",
          b.x + b.width / 2, b.y + 170, 10, INK);
    ctext("STAND BY", b.x + b.width / 2, b.y + 186, 10, INK);
    int k = (int)(GetTime() * 6) % 8;
    for (int i = 0; i < 8; i++) lamp(b.x + 160 + i * 40, b.y + 240, 8, i % 2 ? L_AMB : L_WHT, i == k);
}

/* returns 1 once a start has been chosen */
static int start_menu(void)
{
    int go = 0;
    ClearBackground(WALL);
    Rectangle b = {CW / 2 - 380, CH / 2 - 280, 760, 560};
    steel(b, "RM-ELM   UNIT 1   SHIFT TURNOVER");
    float x = b.x + 40, y = b.y + 50;
    const char *brief[] = {
        "YOU HAVE THE WATCH. THE LOAD DISPATCHER WILL ORDER NET OUTPUT IN MWE:",
        "FOLLOW IT WITH THE POWER DEMAND. POINTS FOR EVERY MWH SENT ON TARGET,",
        "PENALTIES FOR TRIPS. EQUIPMENT CAN FAIL AT ANY TIME - WATCH THE",
        "ANNUNCIATORS (SILENCE, ACK, RESET), THE ALARM TYPER AND ALL FIVE BOARDS.",
        "",
        "HOT SHUTDOWN START: MODE SWITCH TO STARTUP, WITHDRAW ROD GROUP 1, THEN",
        "GROUPS 2-5 TOGETHER (GANG DRIVE ALL). RANGE THE IRMS UP AS POWER RISES",
        "(THEY TRIP ABOVE 120). START FEEDWATER, GO TO RUN BETWEEN 5 AND 15% APRM.",
        "ON THE TURBINE-GENERATOR BOARD: LATCH, ROLL TO 1800 RPM, CLOSE THE FIELD",
        "BREAKER AND SYNCHRONISE (AUTO SYNC OR BY HAND). THEN RAISE POWER.",
    };
    for (int i = 0; i < 10; i++) text(brief[i], x, y + i * 16, 10, INK);
    y += 190;
    if (lampbutton((Rectangle){x, y, 320, 70}, "START AT\nRATED POWER", L_WHT, !start_hot)) {
        start_hot = 0;
        go = 1;
    }
    if (lampbutton((Rectangle){x + 340, y, 320, 70}, "START FROM\nHOT SHUTDOWN", L_AMB, start_hot)) {
        start_hot = 1;
        go = 1;
    }
    y += 100;
    dymo(x, y + 12, "RANDOM EQUIPMENT FAILURES");
    if (lampbutton((Rectangle){x + 190, y, 70, 36}, "ON", L_RED, random_failures)) random_failures = 1;
    if (lampbutton((Rectangle){x + 264, y, 70, 36}, "OFF", L_GRN, !random_failures)) random_failures = 0;
    text("ESC QUITS", b.x + b.width - 100, b.y + b.height - 24, 10, INK);
    return go;
}

static void draw_board(void)
{
    ClearBackground(WALL);
    header();
    int mcr_ok = !(P->aux.evacuated || P->aux.rsp_control);
    input_ok = mcr_ok;
    switch (board) {
    case BOARD_REACTOR: draw_reactor(); break;
    case BOARD_HTS: draw_hts(); break;
    case BOARD_TG: draw_tg(); break;
    case BOARD_ELEC: draw_elec(); break;
    case BOARD_AUX: draw_auxb(); break;
    default:
        input_ok = 1;
        draw_rsp();
        break;
    }
    input_ok = 1;
    if (board != BOARD_RSP) smoke_overlay();
}

int main(void)
{
    /* no MSAA: many drivers refuse a multisampled GLX config, and the flat
     * 1978 panel art does not need it. Size 0 = the monitor's size. */
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(0, 0, "RM-ELM control room");
    if (!IsWindowReady()) {
        fprintf(stderr,
                "rmelm_gui: could not open an OpenGL 2.1 window.\n"
                "Check that your graphics driver works (glxinfo -B),\n"
                "or play the terminal version: ./build/rmelm\n");
        return 1;
    }
    /* RMELM_WINDOWED=1 keeps a normal window (and scripted screenshots) */
    if (!getenv("RMELM_WINDOWED")) ToggleBorderlessWindowed();
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    if (!getenv("RMELM_MUTE")) ann_init_audio();
    RenderTexture2D canvas = LoadRenderTexture(CW, CH);
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_BILINEAR);
    P = calloc(1, sizeof *P);
    pthread_t th;
    double acc = 0;
    int shot = getenv("RMELM_SCREENSHOT") != NULL;
    double shot_at = shot ? atof(getenv("RMELM_SCREENSHOT")) : 0;
    /* RMELM_START=rated|hot skips the menu (screenshots, scripted runs) */
    const char *st = getenv("RMELM_START");
    int started = 0, quit = 0, ready = 0, was_evac = 0;
    if (st || shot) {
        start_hot = st && st[0] == 'h';
        started = 1;
    }
    /* RMELM_BOARD=reactor|hts|tg|elec|aux|rsp picks the board shown first */
    const char *bd = getenv("RMELM_BOARD");
    if (bd) {
        if (!strcmp(bd, "rsp") || !strncmp(bd, "remote", 6)) board = BOARD_RSP;
        else if (bd[0] == 'h') board = BOARD_HTS;
        else if (bd[0] == 't') board = BOARD_TG;
        else if (bd[0] == 'e') board = BOARD_ELEC;
        else if (bd[0] == 'a') board = BOARD_AUX;
        else board = BOARD_REACTOR;
    }
    if (getenv("RMELM_FAILURES")) random_failures = atoi(getenv("RMELM_FAILURES"));
    if (started) pthread_create(&th, NULL, loader, NULL);
    while (!WindowShouldClose() && !quit) {
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_ESCAPE) && !started) quit = 1;
        for (int b = 0; b < NBOARDS; b++)
            if (IsKeyPressed(KEY_F1 + b)) board = b;
        /* letterbox the canvas and map the mouse back onto it */
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float sc = fminf(sw / CW, sh / CH);
        float ox = (sw - CW * sc) / 2, oy = (sh - CH * sc) / 2;
        SetMouseOffset((int)-ox, (int)-oy);
        SetMouseScale(1.0f / sc, 1.0f / sc);

        if (started && loaded && !ready) {
            ready = 1;
            pthread_join(th, NULL);
            number_rods();
            rm_game_init(&G, P, (unsigned)time(NULL), random_failures);
            msg_seen = P->nmsg;
            if (start_hot) {
                logmsg("Unit in hot shutdown: all rods in, sodium at 380 C");
                logmsg("Mode switch to STARTUP, then withdraw rod group 1, then groups 2-5");
            } else {
                logmsg("Unit at rated power, turbine on line");
                logmsg("Follow the load dispatcher's orders");
            }
            core_statistics();
            ann_ack_all_on_start();
            ann_eval();
        }
        if (started && loaded && !paused) {
            acc += GetFrameTime();
            int n = 0;
            while (acc >= DT && n < 8) {
                rm_plant_step(P, DT);
                controls_step();
                rm_game_step(&G, P, DT);
                acc -= DT;
                n++;
            }
            if (acc > 1.0) acc = 0;
        }
        if (ready) {
            while (msg_seen < P->nmsg) logmsg("%s", P->msg[msg_seen++ % 16]);
            static double last = -1;
            if (P->t - last >= 0.5) {
                sample();
                last = P->t;
            }
            if (P->first_out[0] && strcmp(P->first_out, last_first_out)) {
                strcpy(last_first_out, P->first_out);
                if (strcmp(P->first_out, "MANUAL SCRAM")) logmsg("REACTOR TRIP - first out: %s", P->first_out);
            }
            if (!P->first_out[0]) last_first_out[0] = 0;
            if (P->aux.evacuated && !was_evac) board = BOARD_RSP;
            was_evac = P->aux.evacuated;
            core_statistics();
            ann_eval();
        }

        BeginTextureMode(canvas);
        if (!started) {
            if (start_menu()) {
                started = 1;
                pthread_create(&th, NULL, loader, NULL);
            }
        } else if (!ready) {
            loading_screen();
        } else {
            draw_board();
        }
        EndTextureMode();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(canvas.texture, (Rectangle){0, 0, CW, -CH}, (Rectangle){ox, oy, CW * sc, CH * sc},
                       (Vector2){0, 0}, 0, WHITE);
        EndDrawing();

        if (!ready) continue;
        /* raylib itself saves screenshotNNN.png on F12 */
        if (IsKeyPressed(KEY_F12)) logmsg("Photo saved in the working directory");
        if (shot && loaded && P->t >= shot_at) {
            TakeScreenshot("rmelm_screenshot.png");
            break;
        }
    }
    UnloadRenderTexture(canvas);
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    return 0;
}
