/*
 * RM-ELM graphical control room: shared state and the 1978 panel widget kit.
 *
 * Lamp convention is the US one of the period: RED = running / closed /
 * energised, GREEN = stopped / open.
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
static const Color DESK = {126, 144, 128, 255};
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
/* mimic bus colours */
static const Color MIM_ON = {190, 30, 24, 255};
static const Color MIM_OFF = {40, 90, 50, 255};
static const Color MIM_NA = {220, 150, 20, 255};     /* sodium lines */
static const Color MIM_WTR = {40, 90, 180, 255};     /* water / steam lines */

/* ---- shared state --------------------------------------------------------- */
extern rm_plant *P;
extern rm_game G;
extern int input_ok;            /* controls respond (off when the board is transferred away) */

enum { BOARD_MAIN, BOARD_TG, BOARD_AUX, BOARD_RSP, NBOARDS };
extern int board;

/* trends, one sample every 0.5 s */
#define NTR 480
enum {
    TR_POWER, TR_OUTLET, TR_INLET, TR_PHDR, TR_GENMW, TR_SPEED, TR_GASACT, TR_DND, TR_LOGPWR, TR_FLOW, TR_N
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

void logmsg(const char *fmt, ...);
extern double last_log_time;
#define NLOG 8
extern char logs[NLOG][100];

/* ---- drawing primitives (gui_widgets.c) ----------------------------------- */
Color dimlens(Color c);
Color alpha(Color c, unsigned char a);
void text(const char *s, float x, float y, int size, Color c);
void textf(float x, float y, int size, Color c, const char *fmt, ...);
void ctext(const char *s, float cx, float y, int size, Color c);
void screw(float x, float y);
float plate(float x, float y, const char *s, int size);
void plate_c(float cx, float y, const char *s, int size);
float dymo(float x, float y, const char *s);
void dymo_c(float cx, float y, const char *s);
void steel(Rectangle r, const char *title);
void desk(Rectangle r);
float readout(float x, float y, float h, int ndig, const char *fmt, ...);
float segw(const char *s, float h);
float seg_char(float x, float y, float h, char ch, Color on);
void meter(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj);
void meterl(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj,
            const char *const *labs);
/* vertical edgewise meter (GE style): label under the window */
void edgew(Rectangle r, const char *label, double v, double lo, double hi, double red_lo, double red_hi, int nmaj,
           const char *const *labs);
void lamp(float x, float y, float rad, Color lens, int lit);
int lampbutton(Rectangle r, const char *legend, Color lens, int lit);
void window(Rectangle t, const char *l1, const char *l2, Color lens, int lit);
int rotary(Vector2 c, float rad, int n, const char *const *leg, int cur, float a0, float a1, float lr);
void map_lamp(Vector2 p, float rad, double f, int blink);
void tooltip(const char *tip, Rectangle within);
int clicked(Rectangle r);
int blink_fast(void);
int blink_slow(void);

/* control switch: pistol grip (style 0) or J-handle breaker switch (style 1).
 * Spring return to NORMAL-AFTER; the flag remembers the last operation.
 * Returns -1 on a turn to the left (STOP/TRIP), +1 to the right
 * (START/CLOSE). ptl != NULL gives the switch a pull-to-lock position
 * (right click): locked in STOP, no start. */
enum { SW_PISTOL, SW_JHANDLE };
int cswitch(float cx, float y, const char *name, const char *ll, const char *rl, int red, int green, int style, int *ptl);
#define CSW_H 92
/* key-lock selector switch: returns 1 when turned (the caller toggles) */
int keysw(float cx, float cy, const char *l0, const char *l1, int state);

/* mimic bus lines and demarcation boxes */
void mimic(Vector2 a, Vector2 b, float w, Color c);
void mimic_poly(const Vector2 *pts, int n, float w, Color c);
void demarc(Rectangle r, const char *label);

/* recorders */
void strip(Rectangle fr, int ch_a, float lo_a, float hi_a, const char *name_a, int ch_b, float lo_b, float hi_b,
           const char *name_b);
void mpr_draw(Rectangle fr, const mpr *m, const char *title, const char *scale_lo, const char *scale_hi);

/* annunciators (gui_ann.c) */
void ann_eval(void);            /* evaluates every window's condition, runs the sequence */
void ann_box(Rectangle r, int bd, int cols, float h);
void ann_controls(float x, float y);
int ann_board_alerting(int bd);
void ann_init_audio(void);
void ann_ack_all_on_start(void);

/* the boards */
void core_statistics(void);
void number_rods(void);
void draw_main(void);
void draw_tg(void);
void draw_auxb(void);
void draw_rsp(void);
extern double CS_margin_min;

#endif
