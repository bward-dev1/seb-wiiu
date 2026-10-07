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
#define MAX_BOOKS 8
#define MAX_LIVES 5
#define NLEVELS 10
#define GROUND_Y (H - 30)
#define SEB_TOP (GROUND_Y - 57)

#define STAR     0xffffffff
#define ORCHID   0x934ec5ff
#define INDIGO   0x370c77ff
#define AMBER    0xfbba44ff
#define BEAK     0xf99904ff
#define WHITE    0xfefefeff
#define GOLD     0xf5a623ff
#define DUST     0x8d8680ff

enum { S_TITLE, S_INTRO, S_PLAY, S_OVER, S_WIN };
enum { B_NORMAL, B_GOLD, B_DUSTY };

typedef struct {
    const char *name;
    int goal;          // points needed to clear the level
    float speed;       // base fall speed
    int spawn;         // average frames between books (lower = busier)
    int gold, dusty, wind;
    uint32_t bg, ground;
} Level;

static const Level LEVELS[NLEVELS] = {
    {"The Library",       8, 1.5f, 80, 0, 0, 0, 0x1b1235ff, 0x4a00e0ff},
    {"Reading Nook",     10, 1.8f, 72, 0, 0, 0, 0x2a1a55ff, 0x764ba2ff},
    {"Story Garden",     12, 2.0f, 68, 1, 0, 0, 0x0f3a3aff, 0x048e9bff},
    {"Dusty Attic",      12, 2.1f, 64, 0, 1, 0, 0x2d2a30ff, 0x636e72ff},
    {"Windy Hill",       14, 2.2f, 62, 1, 0, 1, 0x143a6bff, 0x4a90e2ff},
    {"Moonlit Library",  14, 2.4f, 58, 1, 1, 0, 0x0c0a2aff, 0x370c77ff},
    {"Sunset Shelves",   16, 2.5f, 56, 1, 1, 0, 0x5a2340ff, 0xd44654ff},
    {"Storm of Stories", 16, 2.7f, 52, 1, 1, 1, 0x1d2540ff, 0x667eeaff},
    {"Book Mountain",    18, 2.9f, 48, 1, 1, 1, 0x3a2a14ff, 0xac917aff},
    {"Seb's Big Night",  20, 3.1f, 44, 1, 1, 1, 0x1b1235ff, 0xf99904ff},
};

static const uint32_t BOOK_COLORS[] = {0x4a90e2ff, 0xd44654ff, 0x0ec5b1ff, 0xf5a623ff, 0x667eeaff};

typedef struct { float x, y, speed; int color, type, live; } Book;

static Book books[MAX_BOOKS];
static float sebX = W / 2;
static int state = S_TITLE;
static int level, points, lives, total, best, wind, windTimer, msgTimer;
static const char *msg = "";
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
static void ctext(Ctx *c, int row, const char *s) {
    int cols = c->screen == SCREEN_TV ? 100 : 66;
    int n = 0; while (s[n]) n++;
    text(c, (cols - n) / 2, row, s);
}

static void drawSeb(Ctx *c, int cx, int top) {
    int x = cx - 28;
    rect(c, x + 4, top, 10, 10, INDIGO);
    rect(c, x + 42, top, 10, 10, INDIGO);
    rect(c, x, top + 8, 56, 44, ORCHID);
    rect(c, x + 14, top + 28, 28, 22, AMBER);
    rect(c, x + 8, top + 14, 18, 18, WHITE);
    rect(c, x + 30, top + 14, 18, 18, WHITE);
    rect(c, x + 14, top + 20, 8, 8, INDIGO);
    rect(c, x + 34, top + 20, 8, 8, INDIGO);
    rect(c, x + 22, top + 30, 12, 8, BEAK);
    rect(c, x + 12, top + 52, 12, 5, BEAK);
    rect(c, x + 32, top + 52, 12, 5, BEAK);
}

static void drawBook(Ctx *c, Book *b) {
    int x = (int)b->x, y = (int)b->y;
    uint32_t col = b->type == B_GOLD ? GOLD : b->type == B_DUSTY ? DUST : BOOK_COLORS[b->color];
    rect(c, x, y, 30, 24, col);
    rect(c, x, y, 5, 24, INDIGO);
    rect(c, x + 9, y + 5, 16, 3, WHITE);
    rect(c, x + 9, y + 11, 12, 3, WHITE);
    if (b->type == B_GOLD) rect(c, x + 12, y - 5, 6, 5, 0xffc966ff);       // sparkle
    if (b->type == B_DUSTY) { rect(c, x + 4, y - 4, 6, 3, DUST); rect(c, x + 18, y - 6, 8, 3, DUST); }
}

static void draw(Ctx *c) {
    const Level *L = &LEVELS[level];
    OSScreenClearBufferEx(c->screen, L->bg);
    for (int i = 0; i < 24; i++) rect(c, (i * 97) % W, (i * 53) % 330, 2, 2, STAR);
    rect(c, 0, GROUND_Y, W, 30, L->ground);

    if (state == S_PLAY || state == S_INTRO) {
        for (int i = 0; i < MAX_BOOKS; i++) if (books[i].live) drawBook(c, &books[i]);
        drawSeb(c, (int)sebX, SEB_TOP);
        // progress bar toward the level goal
        rect(c, 20, 28, 204, 12, INDIGO);
        int fill = points * 200 / L->goal; if (fill > 200) fill = 200;
        rect(c, 22, 30, fill, 8, GOLD);
        char buf[96];
        snprintf(buf, sizeof buf, "Level %d: %s", level + 1, L->name);
        text(c, 2, 0, buf);
        snprintf(buf, sizeof buf, "Lives: %d   Books: %d   Best: %d", lives, total, best);
        text(c, 2, 1, buf);
        if (L->wind && state == S_PLAY) text(c, c->screen == SCREEN_TV ? 88 : 56, 0, wind < 0 ? "Wind <<" : wind > 0 ? "Wind >>" : "Calm");
        if (msgTimer > 0) ctext(c, 5, msg);
    }

    char buf[96];
    switch (state) {
    case S_TITLE:
        drawSeb(c, W / 2, 150);
        ctext(c, 4, "SEB'S BOOK CATCH");
        ctext(c, 9, "Help Seb catch the falling books through 10 levels.");
        ctext(c, 11, "Move: left stick or D-pad");
        ctext(c, 12, "Gold books count double. Grey dusty books cost a life.");
        ctext(c, 14, "Press A to start");
        break;
    case S_INTRO:
        snprintf(buf, sizeof buf, "LEVEL %d: %s", level + 1, L->name);
        ctext(c, 6, buf);
        snprintf(buf, sizeof buf, "Catch %d points worth of books.", L->goal);
        ctext(c, 8, buf);
        if (L->gold) ctext(c, 9, "Gold books are worth 2.");
        if (L->dusty) ctext(c, 10, "Dodge the grey dusty books.");
        if (L->wind) ctext(c, 11, "Watch out, the wind pushes the books.");
        ctext(c, 13, "Press A");
        break;
    case S_OVER:
        ctext(c, 6, "Seb needs a break.");
        snprintf(buf, sizeof buf, "You reached level %d and caught %d books.", level + 1, total);
        ctext(c, 8, buf);
        ctext(c, 10, "Press A to try again");
        break;
    case S_WIN:
        ctext(c, 5, "YOU DID IT!");
        ctext(c, 7, "Seb finished every level and the library is full of books.");
        snprintf(buf, sizeof buf, "Books caught: %d", total);
        ctext(c, 9, buf);
        ctext(c, 11, "Press A to play again");
        break;
    }
}

static void startLevel(int n) {
    level = n; points = 0; windTimer = 0; wind = 0; msgTimer = 0;
    for (int i = 0; i < MAX_BOOKS; i++) books[i].live = 0;
    sebX = W / 2; state = S_INTRO;
}

static void newGame(void) { lives = MAX_LIVES; total = 0; startLevel(0); }

static void loseLife(void) {
    if (--lives <= 0) state = S_OVER;
}

static void update(VPADStatus *st) {
    const Level *L = &LEVELS[level];
    float move = st->leftStick.x * 14.0f;
    if (st->hold & VPAD_BUTTON_LEFT) move = -14.0f;
    if (st->hold & VPAD_BUTTON_RIGHT) move = 14.0f;
    sebX += move;
    if (sebX < 30) sebX = 30;
    if (sebX > W - 30) sebX = W - 30;
    if (msgTimer > 0) msgTimer--;

    if (L->wind && --windTimer <= 0) { wind = rnd(3) - 1; windTimer = 240 + rnd(180); }

    if (rnd(L->spawn) == 0) {
        for (int i = 0; i < MAX_BOOKS; i++) if (!books[i].live) {
            int type = B_NORMAL, r = rnd(100);
            if (L->gold && r < 15) type = B_GOLD;
            else if (L->dusty && r < 35) type = B_DUSTY;
            books[i] = (Book){ (float)(20 + rnd(W - 70)), -24, L->speed + rnd(8) / 10.0f, rnd(5), type, 1 };
            break;
        }
    }
    for (int i = 0; i < MAX_BOOKS; i++) {
        Book *b = &books[i];
        if (!b->live) continue;
        b->y += b->speed;
        b->x += wind * 1.2f;
        if (b->x < 0) b->x = 0;
        if (b->x > W - 30) b->x = W - 30;
        // wide catch zone: Seb plus a margin either side
        if (b->y + 24 >= SEB_TOP && b->y < SEB_TOP + 45 && b->x + 30 > sebX - 42 && b->x < sebX + 42) {
            b->live = 0;
            if (b->type == B_DUSTY) { loseLife(); msg = "Oops, a dusty one."; msgTimer = 60; }
            else {
                int v = b->type == B_GOLD ? 2 : 1;
                points += v; total += v; if (total > best) best = total;
            }
        } else if (b->y > GROUND_Y) {
            b->live = 0;
            if (b->type != B_DUSTY) loseLife();   // dodging a dusty book is free
        }
        if (state == S_OVER) return;
    }
    if (points >= L->goal) {
        if (level + 1 >= NLEVELS) { state = S_WIN; return; }
        if (lives < MAX_LIVES) lives++;
        startLevel(level + 1);
        msg = "Level clear. Seb found a spare life."; msgTimer = 120;
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

    Ctx tv = { SCREEN_TV, 1280.0f / W }, drc = { SCREEN_DRC, 1.0f };
    while (WHBProcIsRunning()) {
        VPADStatus st; VPADReadError err;
        VPADRead(VPAD_CHAN_0, &st, 1, &err);
        if (err == VPAD_READ_SUCCESS) {
            int a = st.trigger & VPAD_BUTTON_A;
            if (state == S_PLAY) update(&st);
            else if (a) {
                if (state == S_INTRO) state = S_PLAY;
                else newGame();
            }
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
