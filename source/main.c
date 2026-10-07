// Seb's Book Catch: a small Wii U game for KiddReads.
// Move Seb with the left stick or D-pad and catch the falling books.
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/proc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define W 854
#define H 480
#define MAX_BOOKS 6

#define BG       0x1b1235ff
#define STAR     0xffffffff
#define ORCHID   0x934ec5ff
#define INDIGO   0x370c77ff
#define AMBER    0xfbba44ff
#define BEAK     0xf99904ff
#define WHITE    0xfefefeff
#define GROUND   0x4a00e0ff

static const uint32_t BOOK_COLORS[] = {0x4a90e2ff, 0xd44654ff, 0x0ec5b1ff, 0xf5a623ff, 0x667eeaff};

typedef struct { float x, y, speed; int color; int live; } Book;

static Book books[MAX_BOOKS];
static float sebX = W / 2;
static int score, lives, best, playing;
static uint32_t rng = 12345;

static int rnd(int n) { rng = rng * 1664525u + 1013904223u; return (int)((rng >> 8) % (uint32_t)n); }

typedef struct { int screen; float s; } Ctx;

static void rect(Ctx *c, int x, int y, int w, int h, uint32_t col) {
    int x0 = (int)(x * c->s), y0 = (int)(y * c->s);
    int x1 = (int)((x + w) * c->s), y1 = (int)((y + h) * c->s);
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++)
            OSScreenPutPixelEx(c->screen, i, j, col);
}

static void text(Ctx *c, int col, int row, const char *s) { OSScreenPutFontEx(c->screen, col, row, s); }

static void drawSeb(Ctx *c, int cx, int top) {
    int x = cx - 28;
    rect(c, x + 4, top, 10, 10, INDIGO);        // ear tufts
    rect(c, x + 42, top, 10, 10, INDIGO);
    rect(c, x, top + 8, 56, 44, ORCHID);        // body
    rect(c, x + 14, top + 28, 28, 22, AMBER);   // belly
    rect(c, x + 8, top + 14, 18, 18, WHITE);    // eyes
    rect(c, x + 30, top + 14, 18, 18, WHITE);
    rect(c, x + 14, top + 20, 8, 8, INDIGO);
    rect(c, x + 34, top + 20, 8, 8, INDIGO);
    rect(c, x + 22, top + 30, 12, 8, BEAK);     // beak
    rect(c, x + 12, top + 52, 12, 5, BEAK);     // feet
    rect(c, x + 32, top + 52, 12, 5, BEAK);
}

static void drawBook(Ctx *c, Book *b) {
    int x = (int)b->x, y = (int)b->y;
    rect(c, x, y, 28, 22, BOOK_COLORS[b->color]);
    rect(c, x, y, 5, 22, INDIGO);               // spine
    rect(c, x + 8, y + 4, 16, 3, WHITE);        // title line
    rect(c, x + 8, y + 10, 12, 3, WHITE);
}

static void draw(Ctx *c) {
    OSScreenClearBufferEx(c->screen, BG);
    for (int i = 0; i < 24; i++) rect(c, (i * 97) % W, (i * 53) % 330, 2, 2, STAR);
    rect(c, 0, H - 30, W, 30, GROUND);
    for (int i = 0; i < MAX_BOOKS; i++) if (books[i].live) drawBook(c, &books[i]);
    drawSeb(c, (int)sebX, H - 30 - 57);

    char buf[64];
    int cols = c->screen == SCREEN_TV ? 100 : 66;
    snprintf(buf, sizeof buf, "Books: %d   Best: %d   Lives: %d", score, best, lives);
    text(c, 2, 0, buf);
    if (!playing) {
        text(c, cols / 2 - 10, 6, "SEB'S BOOK CATCH");
        text(c, cols / 2 - 16, 8, "Catch the falling books for Seb.");
        text(c, cols / 2 - 15, 10, "Move: left stick or D-pad");
        text(c, cols / 2 - 10, 12, lives == 0 ? "Press A to play again" : "Press A to start");
    }
}

static void reset(void) {
    for (int i = 0; i < MAX_BOOKS; i++) books[i].live = 0;
    score = 0; lives = 3; sebX = W / 2; playing = 1;
}

static void update(VPADStatus *st) {
    float move = st->leftStick.x * 9.0f;
    if (st->hold & VPAD_BUTTON_LEFT) move = -9.0f;
    if (st->hold & VPAD_BUTTON_RIGHT) move = 9.0f;
    sebX += move;
    if (sebX < 30) sebX = 30;
    if (sebX > W - 30) sebX = W - 30;

    int spawnEvery = 45 - (score > 30 ? 30 : score);
    if (rnd(spawnEvery) == 0) {
        for (int i = 0; i < MAX_BOOKS; i++) if (!books[i].live) {
            books[i] = (Book){ (float)(20 + rnd(W - 60)), -24, 2.5f + score * 0.05f + rnd(20) / 10.0f, rnd(5), 1 };
            break;
        }
    }
    for (int i = 0; i < MAX_BOOKS; i++) {
        Book *b = &books[i];
        if (!b->live) continue;
        b->y += b->speed;
        int sebTop = H - 30 - 57;
        if (b->y + 22 >= sebTop && b->y < sebTop + 40 && b->x + 28 > sebX - 28 && b->x < sebX + 28) {
            b->live = 0; score++; if (score > best) best = score;
        } else if (b->y > H - 30) {
            b->live = 0;
            if (--lives <= 0) playing = 0;
        }
    }
}

int main(void) {
    WHBProcInit();
    VPADInit();
    OSScreenInit();
    size_t tvSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    size_t drcSize = OSScreenGetBufferSizeEx(SCREEN_DRC);
    void *tvBuf = MEMAllocFromDefaultHeapEx(tvSize, 0x100);
    void *drcBuf = MEMAllocFromDefaultHeapEx(drcSize, 0x100);
    OSScreenSetBufferEx(SCREEN_TV, tvBuf);
    OSScreenSetBufferEx(SCREEN_DRC, drcBuf);
    OSScreenEnableEx(SCREEN_TV, 1);
    OSScreenEnableEx(SCREEN_DRC, 1);
    rng ^= (uint32_t)OSGetTime();
    lives = 3;

    Ctx tv = { SCREEN_TV, 1280.0f / W }, drc = { SCREEN_DRC, 1.0f };
    while (WHBProcIsRunning()) {
        VPADStatus st; VPADReadError err;
        VPADRead(VPAD_CHAN_0, &st, 1, &err);
        if (err == VPAD_READ_SUCCESS) {
            if (playing) update(&st);
            else if (st.trigger & VPAD_BUTTON_A) reset();
        }
        draw(&tv); draw(&drc);
        DCFlushRange(tvBuf, tvSize);
        DCFlushRange(drcBuf, drcSize);
        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);
        OSSleepTicks(OSMillisecondsToTicks(8));
    }

    MEMFreeToDefaultHeap(tvBuf);
    MEMFreeToDefaultHeap(drcBuf);
    OSScreenShutdown();
    WHBProcShutdown();
    return 0;
}
