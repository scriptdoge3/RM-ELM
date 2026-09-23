/*
 * The board frame shared by every board: sections side by side, each with
 * its annunciator window box on the hood along the top, an engraved section
 * plate at the head of the vertical section, and the fold down to the
 * sloped benchboard. Along the front lip: the alarm typer and the
 * annunciator response pushbuttons.
 */
#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void board_frame(const secdef *d, int n, secrect *out)
{
    int tot = 0;
    for (int i = 0; i < n; i++) tot += d[i].w;
    float x0 = (CW - tot) / 2.0f, x = x0;
    for (int i = 0; i < n; i++) {
        float w = (float)d[i].w;
        /* hood with the annunciator box */
        DrawRectangleRec((Rectangle){x, HOOD_Y, w, HOOD_H}, HOOD);
        DrawRectangleRec((Rectangle){x, HOOD_Y, w, 2}, mixc(HOOD, WHITE, 0.25f));
        if (d[i].sec >= 0) ann_box((Rectangle){x + 12, HOOD_Y + 6, w - 24, HOOD_H - 12}, d[i].sec, d[i].cols);
        /* vertical section, shaded under the hood */
        DrawRectangleRec((Rectangle){x, FACE_Y - 2, w, FACE_Y2 - FACE_Y + 2}, PAINT);
        DrawRectangleGradientV((int)x, (int)FACE_Y - 2, (int)w, 18, alpha(BLACK, 80), alpha(BLACK, 0));
        /* fold to the benchboard */
        DrawRectangleRec((Rectangle){x, FACE_Y2, w, BENCH_Y - FACE_Y2}, mixc(PAINT, BLACK, 0.40f));
        DrawRectangleRec((Rectangle){x, BENCH_Y - 2, w, 2}, mixc(DESK, WHITE, 0.35f));
        DrawRectangleRec((Rectangle){x, BENCH_Y, w, STRIP_Y - BENCH_Y}, DESK);
        DrawRectangleGradientV((int)x, BENCH_Y, (int)w, 30, alpha(WHITE, 26), alpha(WHITE, 0));
        /* seams between the sections */
        if (i) {
            DrawRectangleRec((Rectangle){x - 1, HOOD_Y, 2, STRIP_Y - HOOD_Y}, (Color){20, 22, 20, 255});
            DrawRectangleRec((Rectangle){x + 1, FACE_Y, 1, STRIP_Y - FACE_Y}, alpha(WHITE, 50));
        }
        screw(x + 10, FACE_Y + 10);
        screw(x + w - 10, FACE_Y + 10);
        screw(x + 10, FACE_Y2 - 10);
        screw(x + w - 10, FACE_Y2 - 10);
        char t[80];
        snprintf(t, sizeof t, "%s   %s", d[i].id, d[i].name);
        plate_c(x + w / 2, FACE_Y + 5, t, 12);
        if (out) {
            out[i].face = (Rectangle){x + 10, FACE_Y + 30, w - 20, FACE_Y2 - FACE_Y - 34};
            out[i].bench = (Rectangle){x + 10, BENCH_Y + 6, w - 20, BENCH_Y2 - BENCH_Y - 6};
        }
        x += w;
    }
    /* front lip of the benchboard */
    DrawRectangleRec((Rectangle){x0, STRIP_Y, (float)tot, CH - STRIP_Y}, mixc(DESK, BLACK, 0.30f));
    DrawRectangleRec((Rectangle){x0, STRIP_Y, (float)tot, 2}, mixc(DESK, WHITE, 0.30f));
}

/* the alarm typer along the front lip, and the annunciator response buttons */
void board_strip(void)
{
    Rectangle pp = {120, STRIP_Y + 6, CW - 420, CH - STRIP_Y - 10};
    text("ALARM", 16, STRIP_Y + 8, 10, (Color){230, 228, 216, 255});
    text("TYPER", 16, STRIP_Y + 20, 10, (Color){230, 228, 216, 255});
    lamp(78, STRIP_Y + 20, 5, L_AMB, GetTime() - last_log_time < 3.0);
    text("NEW", 88, STRIP_Y + 15, 10, (Color){230, 228, 216, 255});
    DrawRectangleRec((Rectangle){pp.x - 3, pp.y - 2, pp.width + 6, pp.height + 4}, BEZEL);
    DrawRectangleRec(pp, (Color){240, 236, 220, 255});
    DrawRectangleRec((Rectangle){pp.x + 16, pp.y + 1, pp.width - 32, 12}, (Color){206, 228, 204, 255});
    for (float yh = pp.y + 6; yh < pp.y + pp.height; yh += 12) {
        DrawCircleV((Vector2){pp.x + 7, yh}, 2.5f, (Color){60, 60, 56, 255});
        DrawCircleV((Vector2){pp.x + pp.width - 7, yh}, 2.5f, (Color){60, 60, 56, 255});
    }
    const int nl = 3;
    for (int i = 0; i < nl; i++) {
        int k = NLOG - nl + i;
        char line[100];
        snprintf(line, sizeof line, "%s", logs[k]);
        for (size_t m = strlen(line); m > 0 && MeasureText(line, 10) > pp.width - 40; m--) line[m - 1] = 0;
        text(line, pp.x + 20, pp.y + 2 + i * 12, 10, k == NLOG - 1 ? (Color){20, 20, 40, 255} : (Color){70, 70, 90, 255});
    }
    ann_controls(CW - 280, STRIP_Y + 6);
}
