/*
 * RM-ELM graphical control room: shared state and the 1978 panel widget kit.
 *
 * The boards follow the layout of US control rooms of the period (the TMI-2
 * and Shoreham human-factors reviews, the FFTF and Clinch River sodium
 * plants): annunciator window boxes along the top of each system section,
 * a vertical section of meters and recorders, and a sloped benchboard with
 * the control switches laid out on a colour-coded mimic.
 *
 * Lamp convention is the utility one of the period: RED = running / open /
 * closed breaker (energised), GREEN = stopped / shut / open breaker, WHITE =
 * status, AMBER = abnormal or automatic action, BLUE = AUTO / ready.
 */
#ifndef RMELM_GUI_H
#define RMELM_GUI_H

#include "raylib.h"
#include "rm_game.h"
#include "rm_plant.h"
#include "rm_sodium.h"

/* the boards are drawn on a fixed canvas and scaled to fill the screen */
#define CW 1920
#define CH 1080
#define DT 0.05

/* ---- palette ------------------------------------------------------------ */
static const Color WALL = {58, 60, 56, 255};
static const Color PAINT = {156, 174, 158, 255};     /* institutional green */
static const Color PAINT_HI = {192, 208, 194, 255};
static const Color PAINT_LO = {98, 112, 100, 255};
static const Color HOOD = {112, 128, 116, 255};      /* the annunciator hood over the board */
static const Color DESK = {146, 164, 148, 255};      /* benchboard slope catches more light */
static const Color INK = {32, 38, 34, 255};
static const Color BEZEL = {30, 30, 28, 255};
static const Color FACE = {238, 230, 206, 255};      /* meter face / chart paper */
static const Color LED = {255, 46, 24, 255};
static const Color L_RED = {240, 60, 44, 255};
static const Color L_GRN = {60, 220, 90, 255};
static const Color L_AMB = {250, 186, 60, 255};
static const Color L_WHT = {248, 244, 228, 255};
static const Color L_BLU = {90, 150, 250, 255};
static const Color PEN_VIO = {120, 50, 170, 255};
static const Color PEN_RED = {200, 30, 30, 255};
static const Color PEN_GRN = {30, 120, 50, 255};
static const Color PEN_BLU = {30, 70, 190, 255};
/* mimic colour code (a legend plate on each board says so) */
static const Color MIM_PNA = {196, 40, 30, 255};     /* primary sodium */
static const Color MIM_SNA = {226, 132, 20, 255};    /* intermediate sodium */
static const Color MIM_WTR = {30, 120, 60, 255};     /* feed / condensate */
static const Color MIM_STM = {40, 80, 170, 255};     /* steam */
static const Color MIM_AIR = {120, 120, 120, 255};   /* air / gas */
static const Color MIM_ELE = {24, 24, 22, 255};      /* electrical bus */
static const Color MIM_ON = {190, 30, 24, 255};
static const Color MIM_OFF = {40, 90, 50, 255};

/* ---- shared state --------------------------------------------------------- */
extern rm_plant *P;
extern rm_game G;
extern int input_ok;            /* controls respond (off when the board is transferred away) */

enum { BOARD_REACTOR, BOARD_HTS, BOARD_TG, BOARD_ELEC, BOARD_AUX, BOARD_RSP, NBOARDS };
extern int board;

/* annunciator sections: one window box over each board section */
enum {
    SEC_NI, SEC_RC, SEC_CM,          /* reactor board */
    SEC_PHT, SEC_IHT, SEC_DHR,       /* heat transport board */
    SEC_FW, SEC_TURB, SEC_GEN,       /* turbine-generator board */
    SEC_DIST, SEC_EPWR,              /* electrical board */
    SEC_NAAUX, SEC_CONT, SEC_SERV,   /* auxiliary board */
    SEC_RSP,                         /* remote shutdown panel */
    NSEC
};
int sec_board(int sec);

/* trends, one sample every 0.5 s */
#define NTR 480
enum {
    TR_POWER, TR_OUTLET, TR_INLET, TR_PHDR, TR_GENMW, TR_SPEED, TR_GASACT, TR_DND, TR_LOGPWR, TR_FLOW,
    TR_PERIOD, TR_FEED, TR_DRACS, TR_DECAY, TR_NET, TR_DEMAND, TR_DGMW, TR_BATT, TR_N
};
extern float trend[TR_N][NTR];
extern int ntr;
extern long nsamp;

/* multipoint dot-printing recorder: prints one point per sample, in turn */
typedef struct {
    int n;
    const char *lab[6];
    float v[NTR];               /* 0..100 % of scale */
    unsigned char k[NTR];       /* which point */
} mpr;
void mpr_push(mpr *m, const float *vals);
extern mpr MP_NA, MP_TURB, MP_H2, MP_SG, MP_RAD;

void logmsg(const char *fmt, ...);
extern double last_log_time;
#define NLOG 8
extern char logs[NLOG][100];

/* ---- drawing primitives (gui_widgets.c) ----------------------------------- */
Color dimlens(Color c);
Color alpha(Color c, unsigned char a);
Color mixc(Color a, Color b, float t);
void tri(Vector2 a, Vector2 b, Vector2 c, Color col);   /* either winding */
void text(const char *s, float x, float y, int size, Color c);
void textf(float x, float y, int size, Color c, const char *fmt, ...);
void ctext(const char *s, float cx, float y, int size, Color c);
void screw(float x, float y);
float plate(float x, float y, const char *s, int size);
void plate_c(float cx, float y, const char *s, int size);
float tag(float x, float y, const char *s);             /* engraved component label */
void tag_c(float cx, float y, const char *s);
float dymo(float x, float y, const char *s);            /* operator-added label tape */
void dymo_c(float cx, float y, const char *s);
void steel(Rectangle r, const char *title);
void desk(Rectangle r);
float readout(float x, float y, float h, int ndig, const char *fmt, ...);
float segw(const char *s, float h);
float seg_char(float x, float y, float h, char ch, Color on);
void meter(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj);
void meterl(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj,
            const char *const *labs);
/* 250-degree round switchboard meter with a green normal band and red limit band */
void dial(Rectangle r, const char *label, double v, double lo, double hi, int nmaj, double g_lo, double g_hi,
          double r_lo, double r_hi, const char *const *labs);
/* vertical edgewise meter (GE style): label under the window */
void edgew(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj,
           const char *const *labs);
void edgewz(Rectangle r, const char *label, double v, double lo, double hi, double g_lo, double g_hi, double red_lo,
            double red_hi, int nmaj, const char *const *labs);
void lamp(float x, float y, float rad, Color lens, int lit);
int lampbutton(Rectangle r, const char *legend, Color lens, int lit);
void indicator(Rectangle r, const char *legend, Color lens, int lit);   /* engraved legend lamp */
void window(Rectangle t, const char *l1, const char *l2, Color lens, int lit);
int rotary(Vector2 c, float rad, int n, const char *const *leg, int cur, float a0, float a1, float lr);
void tooltip(const char *tip, Rectangle within);
int clicked(Rectangle r);
int held_repeat(Rectangle r);   /* fires on press, then repeats while held */
int blink_fast(void);
int blink_slow(void);

/* control switch on a square escutcheon: pistol grip or J-handle, with its
 * indicating lights above. Spring return to NORMAL-AFTER; the target flag
 * remembers the last operation. Returns -1 on a turn to the left
 * (STOP/TRIP/SHUT), +1 to the right (START/CLOSE/OPEN). ptl != NULL gives a
 * pull-to-lock position (right click): locked in STOP, no start. */
enum { SW_PISTOL = 0, SW_JHANDLE = 1, SW_SHAPE = 3, SW_GREEN = 4, SW_RED = 8 };
int cswitch(float cx, float y, const char *name, const char *ll, const char *rl, int red, int green, int style, int *ptl);
/* with a third lamp between the red and green ones */
int cswitch3(float cx, float y, const char *name, const char *ll, const char *rl, int red, int green, int style,
             int *ptl, Color c3, int lit3);
#define CSW_W 82
#define CSW_H 100
/* key-lock selector switch: returns 1 when turned (the caller toggles) */
int keysw(float cx, float cy, const char *l0, const char *l1, int state);
/* round pushbutton with a coloured collar */
int pbround(Vector2 c, float r, const char *label, Color cap, Color collar, int lit);
/* pushbutton under a hinged guard: the first click lifts the guard */
int pbguard(Rectangle r, const char *legend, Color lens, int lit);

/* Manual/Automatic control station (Bailey style): process indicator with a
 * setpoint index, output meter, A and M buttons, raise/lower. mode: 1 auto,
 * 0 manual, -1 a manual loading station with no A/M. Returns MA_* bits. */
enum { MA_AUTO = 1, MA_MAN = 2, MA_UP = 4, MA_DOWN = 8 };
int mastation(Rectangle r, const char *name, const char *unit, double pv, double sp, double lo, double hi, double out,
              int mode);
#define MA_W 96
#define MA_H 178

/* mimic: pipes with flow arrows, component symbols, demarcation boxes */
void mimic(Vector2 a, Vector2 b, float w, Color c);
void mimic_poly(const Vector2 *pts, int n, float w, Color c);
void mpipe(const Vector2 *pts, int n, Color c, float w, int arrows);
void sym_pump(Vector2 c, float r, Color col, int running, int dir);   /* dir 0 right, 1 down, 2 left, 3 up */
void sym_valve(Vector2 c, float s, int vertical, Color col, int open);
void sym_hx(Rectangle r, Color a, Color b, const char *name);
void sym_tank(Rectangle r, Color col, const char *name);
void sym_breaker(Vector2 p, int closed);
void sym_xfmr(Vector2 p, Color col, int vertical);
void demarc(Rectangle r, const char *label);
void mimic_legend(float x, float y);

/* recorders */
void strip(Rectangle fr, int ch_a, float lo_a, float hi_a, const char *name_a, int ch_b, float lo_b, float hi_b,
           const char *name_b);
void mpr_draw(Rectangle fr, const mpr *m, const char *title, const char *scale_lo, const char *scale_hi);

/* ---- board frame (gui_board.c) --------------------------------------------- */
#define HOOD_Y 48
#define HOOD_H 120
#define FACE_Y 170        /* vertical section */
#define FACE_Y2 640
#define BENCH_Y 648       /* benchboard */
#define BENCH_Y2 1030
#define STRIP_Y 1034      /* front lip: typer and annunciator controls */
typedef struct {
    const char *id, *name;
    int w;                /* px */
    int sec;              /* annunciator section, -1 none */
    int cols;             /* annunciator box columns */
} secdef;
typedef struct {
    Rectangle face;       /* inside the vertical section, below the section plate */
    Rectangle bench;
} secrect;
void board_frame(const secdef *d, int n, secrect *out);
void board_strip(void);   /* typer and annunciator response controls along the front lip */

/* annunciators (gui_ann.c) */
void ann_eval(void);            /* evaluates every window's condition, runs the sequence */
void ann_box(Rectangle r, int sec, int cols);
void ann_controls(float x, float y);
int ann_board_alerting(int bd);
void ann_init_audio(void);
void ann_ack_all_on_start(void);
void firstout_box(Rectangle r);
/* reactor trip functions: shared by the first-out box and the PPS trip status matrix */
#define NTRIP 16
extern const char *const trip_l1[NTRIP];
extern const char *const trip_l2[NTRIP];
int trip_active(int k, int ch);   /* ch: RPS channel A1 A2 B1 B2 = 0..3, -1 any */
int trip_first(void);             /* the first-out trip function, -1 none */

/* the boards */
void core_statistics(void);
void number_rods(void);
void draw_reactor(void);
void draw_hts(void);
void draw_tg(void);
void draw_elec(void);
void draw_auxb(void);
void draw_rsp(void);
void controls_step(void);       /* GUI-side automatic controllers, every frame */
extern double CS_margin_min;
extern double CS_fuel_max;

#endif
