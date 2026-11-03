/* 2nd DRAW on SymCE's grapher (included by graph.c, below the zoom code).
 *
 * The drawing layer is TI's own: plotSScreen, 165 rows of 133 bytes, 4 bits a pixel (the high nibble the
 * even column), the top row the top of the plot (row = 164 - gy), on TI's 265x165 grid. Here it holds only
 * what DRAW put there: 0 nothing, otherwise a palette index (1 = the pen's blue, 11 = white, the Pt-Off
 * erase). render() paints it over the curves, cell by cell, so a drawing survives every redisplay.
 * The unused low nibble of each row's last byte keeps the layer's signature: a magic, a hash of what TI
 * clears a drawing for (window, equations, formats), whether anything is drawn, and the free cursor.
 * A hash that differs (or no magic: TI wrote the buffer) empties the layer, as TI's replot does.
 *
 * A pixel of the grid is a cell of the screen: the plot is 319x219, so a pixel is 1 or 2 columns and rows
 * (xlo, ylo). Previews (the rubber band) go to VRAM only, with the pixels they cover logged in 0xD11800
 * so the cursor's next move can put them back; a log that fills up redraws the picture instead.
 *
 * ILine and the thick IPoint are TI's (ROM 0x7B2F5, 0x7B501), measured, and exact.
 */
#define LAY   ((uint8_t *)0x0D09466)
#define LRB   133                /* bytes a row */
#define LAYN  (LRB * 165)
#define ULMAX 1638      /* ULOG ends at 0xD137FE: 0xD13800.. is symce_text's shadow park (graph.c). Estimate, not measured:
                           a cell is ~1.2 x 1.3 px and a thick point 3-5 cells, so a full-width Horizontal is ~1300 entries and a
                           full-height Vertical ~800 (both fit); a corner-to-corner Line can pass 1638, and then ovf redraws the
                           picture instead of undoing the log, so the pixels are the same. */
struct ulog { uint16_t c, x; uint8_t y; } __attribute__((packed));
#define ULOG  ((struct ulog *)0x0D11800)
_Static_assert(0x0D11800 + ULMAX * 5 <= 0x0D13800, "ULOG runs into the shadow park");

static const char *const dtit[12] = { "", "", "DRAW LINE SEGMENT", "DRAW HORIZONTAL LINE", "DRAW VERTICAL LINE",
                                      "DRAW TANGENT", "DRAW CIRCLE", "DRAW TEXT", "DRAW PEN", "DRAW PT-ON",
                                      "DRAW PT-OFF", "DRAW PT-CHANGE" };

/* The signature nibbles: 0..3 magic, 4..11 hash, 12 something drawn, 13..15 cursor gx, 16..17 gy, 18 cursor kept. */
static uint8_t mget(int i)
{
    return LAY[i * LRB + 132] & 15;
}

static void mset(int i, uint8_t v)
{
    uint8_t *p = LAY + i * LRB + 132;
    *p = (uint8_t)((*p & 0xF0) | (v & 15));
}

static uint8_t lget(int gx, int gy)
{
    uint8_t b = LAY[(164 - gy) * LRB + (gx >> 1)];
    return gx & 1 ? b & 15 : b >> 4;
}

static void lset(int gx, int gy, uint8_t v)
{
    uint8_t *p = LAY + (164 - gy) * LRB + (gx >> 1);
    *p = gx & 1 ? (uint8_t)((*p & 0xF0) | v) : (uint8_t)((*p & 0x0F) | (v << 4));
    mset(12, 1);
}

/* The first screen column / row of a grid column / row: the cell is [xlo(g), xlo(g + 1)). */
static int xlo(int g)
{
    return g <= 0 ? 0 : g > 264 ? PW : (g * 318 - 159 + 263) / 264;
}

static int ylo(int r)
{
    return r <= 0 ? 0 : r > 164 ? SH : (r * PH - (PH / 2) + 163) / 164;
}

static uint32_t dhash(const struct gw *w)
{
    uint32_t h = 2166136261u;
    const uint8_t *p;
#define HB(b) (h = (h ^ (uint8_t)(b)) * 16777619u)
    for (int i = 0; i < 36; i++)
        HB(w->win[i]);
    for (p = (const uint8_t *)ADDR_XRES; p < (const uint8_t *)ADDR_XRES + 9; p++)
        HB(*p);
    HB(OSB(ADDR_DB));
    HB(OSB(ADDR_MODE) & 0xF0);                   /* the graph mode; the low bits are OS state that moves on a quit */
    HB(OSB(ADDR_ASYM));
    HB(OSB(ADDR_GRIDC));
    HB(OSB(ADDR_AXESC));
    HB(w->deg);
#undef HB
    return h;
}

/* A GRAPH that finds TI's picture: TI plotted (a home Line(, Circle(, Text(, TI's own graph) since our last render, so the
   buffer may hold drawings we cannot read. TI's replot wipes our signature and clears the dirty bit (0xD00083 bit 0) that every
   render of ours sets. Then TI shows its own graph, and a marker (magic A5C4 + the window hash) keeps it TI's until the window
   changes (TI clears its drawings then too). */
static uint8_t dr_foreign(const struct gw *w)
{
    uint32_t h = dhash(w);
    int i, same = 1;
    if (OSB(0x0D0EF00 + 0xC0A))                  /* GWK: only a plain GRAPH key (0) asks */
        return 0;
    for (i = 0; i < 8; i++)
        same = same && mget(4 + i) == (uint8_t)((h >> (4 * i)) & 15);
    if (mget(0) == 0xA && mget(1) == 5 && mget(2) == 0xC) {
        if (mget(3) == 3)
            return 0;                            /* ours */
        if (mget(3) == 4) {                      /* TI's, until the window changes or an equation it plotted changes (TI replots) */
            if (OSB(ADDR_YEDIT) & 2) {
                mset(0, 0);
                return 0;
            }
            return same;
        }
    }
    if (OSB(0x0D00083) & 1)
        return 0;                                /* needs a replot: nothing of TI's to keep (fresh, or an edit) */
    mset(0, 0xA);
    mset(1, 5);
    mset(2, 0xC);
    mset(3, 4);
    for (i = 0; i < 8; i++)
        mset(4 + i, (uint8_t)((h >> (4 * i)) & 15));
    mset(12, 0);
    return 1;
}

/* Out of TI's hands, into ours: the buffer is ours (the OS graph is out of date), emptied if the signature is
   not this graph's. */
/* TI drops its drawing when a Y= edit changes, deletes or toggles an equation it had plotted, and keeps it when an equation is
   added (measured; the OS tracks that in 0xD00097 only for equations ITS plot drew). So each slot's equation keeps an 8 bit
   signature in the pad nibbles 19..38 (0 = none): a stored one that differs now means the picture is stale. */
static uint8_t esig(const struct gw *w, int i)
{
    uint32_t h = 2166136261u;
    unsigned n = w->len[i] < MAXTOK ? w->len[i] : MAXTOK;
    if (!w->len[i])
        return 0;
    h = (h ^ w->len[i]) * 16777619u;
    for (unsigned j = 0; j < n; j++)
        h = (h ^ w->tok[i][j]) * 16777619u;
    return (uint8_t)((h >> 24) ^ h) | 1;
}

static void dr_sync(struct gw *w)
{
    uint32_t h = dhash(w);
    int ok = mget(0) == 0xA && mget(1) == 5 && mget(2) == 0xC && mget(3) == 3;
    for (int i = 0; i < 8 && ok; i++)
        ok = mget(4 + i) == (uint8_t)((h >> (4 * i)) & 15);
    for (int i = 0; i < 10 && ok; i++) {
        uint8_t o = (uint8_t)(mget(19 + 2 * i) | mget(20 + 2 * i) << 4);
        ok = !o || o == esig(w, i);
    }
    symce_dirty();
    if (!ok) {
        memset(LAY, 0, LAYN);
        mset(0, 0xA);
        mset(1, 5);
        mset(2, 0xC);
        mset(3, 3);
        for (int i = 0; i < 8; i++)
            mset(4 + i, (uint8_t)((h >> (4 * i)) & 15));
    }
    for (int i = 0; i < 10; i++) {
        uint8_t e = esig(w, i);
        mset(19 + 2 * i, e & 15);
        mset(20 + 2 * i, e >> 4);
    }
}

/* TI cleared the drawing (ZStandard, ClrDraw ...): the next render empties the layer and forgets the cursor. */
static void dr_reset(void)
{
    mset(0, 0);
}

/* The layer over the picture. */
static void dr_overlay(void)
{
    if (!mget(12))
        return;
    for (int r = 0; r < 165; r++) {
        const uint8_t *row = LAY + r * LRB;
        int y0 = ylo(r), y1 = ylo(r + 1);
        for (int j = 0; j < 133; j++) {
            if (!row[j])
                continue;
            for (int h = 0; h < 2; h++) {
                int g = 2 * j + h;
                uint8_t n = h ? row[j] & 15 : row[j] >> 4;
                if (!n || g > 264)
                    continue;
                uint16_t c = pal(n);
                int x0 = xlo(g), x1 = xlo(g + 1);
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++)
                        VRAM[y * VS + x] = c;
            }
        }
    }
}

/* One pixel of the grid, drawn (the layer's too when committing). */
static void dcell(struct gw *w, int gx, int gy)
{
    struct dst *d = &w->dr;
    uint16_t c = pal(d->nib);
    int r, ya, yb, xa, xb;
    if ((unsigned)gx > 264 || (unsigned)gy > 164)
        return;
    r = 164 - gy;
    if (d->cm)
        lset(gx, gy, d->nib);
    xa = xlo(gx);
    xb = xlo(gx + 1);
    ya = ylo(r);
    yb = ylo(r + 1);
    for (int y = ya; y < yb; y++)
        for (int x = xa; x < xb; x++) {
            uint16_t *p = VRAM + y * VS + x;
            if (!d->cm) {
                if (d->n < ULMAX) {
                    struct ulog *e = ULOG + d->n++;
                    e->c = *p;
                    e->x = (uint16_t)x;
                    e->y = (uint8_t)y;
                } else
                    d->ovf = 1;
            }
            *p = c;
        }
}

/* TI's thick IPoint: the point, and its neighbours by where the line came from. */
static void dip(struct gw *w, int x, int y)
{
    struct dst *d = &w->dr;
    int px = d->px, py = d->py;
    dcell(w, x, y);
    if (y == py) {
        dcell(w, px, py - 1);
        if (x != px)
            dcell(w, x < px ? px - 1 : px + 1, py - 1);
    } else if (y > py) {
        dcell(w, px + 1, py);
        if (x == px)
            dcell(w, px + 1, py + 1);
        else if (x < px)
            dcell(w, px, py + 1);
        else
            dcell(w, px, py - 1);
    } else {
        dcell(w, px + 1, py);
        dcell(w, px, py - 1);
        if (x == px)
            dcell(w, px + 1, py - 1);
    }
    d->px = x;
    d->py = y;
}

/* TI's ILine (ROM 0x7B2F5): Bresenham, e = 2 minor - major, the major axis steps every point. */
static void dline(struct gw *w, int x0, int y0, int x1, int y1)
{
    int dy = y1 < y0 ? y0 - y1 : y1 - y0, dx = x1 < x0 ? x0 - x1 : x1 - x0;
    int sy = y1 < y0 ? -1 : 1, sx = x1 < x0 ? -1 : 1;
    int ymaj = !(dy < dx), major = ymaj ? dy : dx, minor = ymaj ? dx : dy;
    int e = 2 * minor - major, x = x0, y = y0;
    w->dr.px = x0;
    w->dr.py = y0;
    for (int i = 0; i <= major; i++) {
        dip(w, x, y);
        if (e < 0) {
            e += 2 * minor;
            if (ymaj)
                y += sy;
            else
                x += sx;
        } else {
            e += 2 * (minor - major);
            x += sx;
            y += sy;
        }
    }
}

/* Pen: the thin line from the last cursor spot to this one (single points, no neighbours). */
static void dlinet(struct gw *w, int x0, int y0, int x1, int y1)
{
    int dy = y1 < y0 ? y0 - y1 : y1 - y0, dx = x1 < x0 ? x0 - x1 : x1 - x0;
    int sy = y1 < y0 ? -1 : 1, sx = x1 < x0 ? -1 : 1;
    int ymaj = !(dy < dx), major = ymaj ? dy : dx, minor = ymaj ? dx : dy;
    int e = 2 * minor - major, x = x0, y = y0;
    for (int i = 0; i <= major; i++) {
        dcell(w, x, y);
        if (e < 0) {
            e += 2 * minor;
            if (ymaj)
                y += sy;
            else
                x += sx;
        } else {
            e += 2 * (minor - major);
            x += sx;
            y += sy;
        }
    }
}

/* TI's Circle( from the cursor (measured, pixel exact): 60 chords of the circle round the first point, radius sqrt(dx^2 + dy^2)
   in grid pixels (round, not an ellipse of the window's units), each vertex rounded half down, the chords cut to the grid
   (0..264, 0..164) as real lines before they are rounded, each drawn with the thick ILine. cos(6 i degrees) * 1e9, i = 0..15. */
static const int32_t CCOS[16] = { 1000000000, 994521895, 978147601, 951056516, 913545458, 866025404, 809016994, 743144825,
                                  669130606, 587785252, 500000000, 406736643, 309016994, 207911691, 104528463, 0 };

static int32_t ccos(int k)           /* cos(6 k degrees) * 1e9 */
{
    k %= 60;
    if (k > 30)
        k = 60 - k;
    return k <= 15 ? CCOS[k] : -CCOS[30 - k];
}

static int64_t isqrt64(int64_t n)
{
    int64_t r = 0, b = (int64_t)1 << 62;
    while (b > n)
        b >>= 2;
    for (; b; b >>= 2)
        if (n >= r + b) {
            n -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
    return r;
}

static int rhd(float v)              /* round, a tie going down */
{
    return (int)floorf(v + 0.4999f);
}

/* A segment of the real line (x0, y0)-(x1, y1) from the rounded (ax, ay) to (bx, by) as the thick ILine; with clip it is cut to the
   grid as reals first (Liang-Barsky) and the cut ends rounded, a tie going down. Wholly outside: nothing. */
static void dseg(struct gw *w, float x0, float y0, float x1, float y1, int ax, int ay, int bx, int by, int clip)
{
    float t0 = 0.0f, t1 = 1.0f, dx = x1 - x0, dy = y1 - y0;
    if (clip) {
        float p[4] = { -dx, dx, -dy, dy }, q[4] = { x0, 264 - x0, y0, 164 - y0 };
        for (int e = 0; e < 4; e++) {
            if (p[e] == 0) {
                if (q[e] < 0)
                    return;
                continue;
            }
            float t = q[e] / p[e];
            if (p[e] < 0) {
                if (t > t1)
                    return;
                if (t > t0)
                    t0 = t;
            } else if (t < t0)
                return;
            else if (t < t1)
                t1 = t;
        }
        if (t0 > 0.0f) {
            ax = rhd(x0 + dx * t0);
            ay = rhd(y0 + dy * t0);
        }
        if (t1 < 1.0f) {
            bx = rhd(x0 + dx * t1);
            by = rhd(y0 + dy * t1);
        }
    }
    dline(w, ax, ay, bx, by);
}

static void dcirc(struct gw *w, int cx, int cy, int px, int py)
{
    int64_t r6 = isqrt64(((int64_t)(px - cx) * (px - cx) + (int64_t)(py - cy) * (py - cy)) * 1000000000000LL);   /* r * 1e6 */
    int vx[61], vy[61];
    float fx[61], fy[61];
    for (int k = 0; k <= 60; k++) {
        int64_t ox = r6 * ccos(k), oy = r6 * ccos(k + 45);                    /* sin(6 k) = cos(6 k + 270 degrees) */
        int64_t ax = (int64_t)cx * 1000000000000000LL + ox + 499999999999999LL;
        int64_t ay = (int64_t)cy * 1000000000000000LL + oy + 499999999999999LL;
        vx[k] = (int)(ax / 1000000000000000LL - (ax % 1000000000000000LL < 0));
        vy[k] = (int)(ay / 1000000000000000LL - (ay % 1000000000000000LL < 0));
        fx[k] = (float)cx + (float)(ox / 1000000000) * 1e-6f;
        fy[k] = (float)cy + (float)(oy / 1000000000) * 1e-6f;
    }
    for (int k = 0; k < 60; k++) {
        float x0 = fx[k], y0 = fy[k], x1 = fx[k + 1], y1 = fy[k + 1];
        dseg(w, x0, y0, x1, y1, vx[k], vy[k], vx[k + 1], vy[k + 1],
             x0 < 0 || x0 > 264 || y0 < 0 || y0 > 164 || x1 < 0 || x1 > 264 || y1 < 0 || y1 > 164);
    }
}

/* Is the pixel on, as TI's Pt-Change reads it: the layer, else the picture. */
static int don(int gx, int gy)
{
    uint8_t v = lget(gx, gy);
    if (v)
        return v != 11;
    return VRAM[ylo(164 - gy) * VS + xlo(gx)] != 0xFFFF;
}

/* The marks: Pt-On and Pt-Off a 7x7 ring 2 thick, Pt-Change a 3x3 block that flips every pixel. */
static void dmark(struct gw *w, int cx, int cy, int kind)
{
    struct dst *d = &w->dr;
    d->cm = 1;
    if (kind == 2) {
        uint8_t on[9];
        for (int j = 0; j < 9; j++)
            on[j] = (unsigned)(cx + j % 3 - 1) <= 264 && (unsigned)(cy + j / 3 - 1) <= 164
                    ? (uint8_t)don(cx + j % 3 - 1, cy + j / 3 - 1) : 0;
        for (int j = 0; j < 9; j++) {
            d->nib = on[j] ? 11 : 1;
            dcell(w, cx + j % 3 - 1, cy + j / 3 - 1);
        }
        return;
    }
    d->nib = kind ? 11 : 1;
    for (int j = -3; j <= 3; j++)
        for (int i = -3; i <= 3; i++)
            if (j >= 2 || j <= -2 || i >= 2 || i <= -2)
                dcell(w, cx + i, cy + j);
}

/* The rubber band of the tool for the cursor, drawn under it. */
static void dr_prev(struct gw *w)
{
    struct dst *d = &w->dr;
    if (!d->dm)
        return;
    mset(13, (uint8_t)w->gx);
    mset(14, (uint8_t)(w->gx >> 4));
    mset(15, (uint8_t)(w->gx >> 8));
    mset(16, (uint8_t)w->gy);
    mset(17, (uint8_t)(w->gy >> 4));
    mset(18, 1);
    if (d->dm == D_PEN) {                        /* pen down: every cursor move draws for good */
        if (d->ds && (w->gx != d->x0 || w->gy != d->y0)) {
            d->cm = 1;
            d->nib = 1;
            dlinet(w, d->x0, d->y0, w->gx, w->gy);
            d->x0 = w->gx;
            d->y0 = w->gy;
        }
        return;
    }
    if (d->dm != D_LINE && d->dm != D_HORZ && d->dm != D_VERT && d->dm != D_CIRC)
        return;
    d->cm = 0;
    d->nib = 1;
    d->n = 0;
    d->ovf = 0;
    if (d->dm == D_CIRC) {                       /* no rubber band: the centre keeps the cursor's second frame (VRAM only) */
        if (!d->ds)
            return;
        for (int j = 0; j < 9; j++)
            for (int i = 0; i < 9; i++) {
                int x = gpx(d->x0) - 4 + i, y = gpy(d->y0) - 4 + j;
                if ((unsigned)x < PW && (unsigned)y < SH) {
                    uint16_t *p = VRAM + y * VS + x;
                    if (d->n < ULMAX) {
                        struct ulog *e = ULOG + d->n++;
                        e->c = *p;
                        e->x = (uint16_t)x;
                        e->y = (uint8_t)y;
                    } else
                        d->ovf = 1;
                    *p = spr[1][j][i] == '#' ? 0 : 0xFFFF;
                }
            }
        d->on = 1;
        return;
    }
    if (d->dm == D_LINE) {
        if (!d->ds)
            return;
        dline(w, d->x0, d->y0, w->gx, w->gy);
    } else if (d->dm == D_HORZ)
        dline(w, 0, w->gy, 264, w->gy);
    else
        dline(w, w->gx, 0, w->gx, 164);
    d->on = 1;
}

static void dr_title(struct gw *w)
{
    ztitle(w, dtit[w->dr.dm]);
}

/* The rubber band back to the picture (the cursor is already gone). */
static void dr_undo(struct gw *w)
{
    struct dst *d = &w->dr;
    if (!d->on)
        return;
    d->on = 0;
    if (d->ovf) {
        render(w);
        dr_title(w);
        return;
    }
    while (d->n) {
        const struct ulog *e = ULOG + --d->n;
        VRAM[e->y * VS + e->x] = e->c;
    }
}

/* A real's nearest integer from its BCD digits, a tie away from zero (a float cannot hold a 14 digit tie); past 1e6 it saturates. */
static int bint(const uint8_t *r)
{
    int e = (int)r[1] - 0x80, n = 0, i;
    if (!(r[2] & 0xF0))
        return 0;
    if (e > 5)
        return r[0] & 0x80 ? -1000000 : 1000000;
    for (i = 0; i <= e + 1 && i < 14; i++) {
        int d = i & 1 ? r[2 + i / 2] & 15 : r[2 + i / 2] >> 4;
        if (i <= e)
            n = n * 10 + d;
        else if (d >= 5)
            n++;
    }
    return r[0] & 0x80 ? -n : n;
}

/* TI's Tangent( (measured, pixel exact): the line y = m X + b (m = nDeriv at X, b = Y - m X) is plotted like a function, in BCD, at every
   second column k: X = Xmin + k/2 TraceStep, row = round((y - Ymin) 164/(Ymax - Ymin)); the points are joined by thick ILines, a
   segment with an end off the grid cut as reals first. */
static void dtang(struct gw *w, const uint8_t *m, const uint8_t *b)
{
    uint8_t ts[9], k[9], x[9], y[9], g[9], sc[9];
    int ra = 0, rb;
    float ga = 0, gb;
    itor(2, x);
    symce_fp(FP_MUL, (const uint8_t *)ADDR_DX, x, ts);
    symce_fp(FP_SUB, (const uint8_t *)(ADDR_YMIN + 9), (const uint8_t *)ADDR_YMIN, y);
    itor(164, k);
    symce_fp(FP_DIV, k, y, sc);
    w->dr.cm = 1;
    w->dr.nib = w->col[w->fn];
    for (int j = 0; j <= 132; j++) {
        itor(j, k);
        symce_fp(FP_MUL, ts, k, x);
        symce_fp(FP_ADD, (const uint8_t *)ADDR_XMIN, x, x);
        symce_fp(FP_MUL, m, x, y);
        symce_fp(FP_ADD, y, b, y);
        symce_fp(FP_SUB, y, (const uint8_t *)ADDR_YMIN, g);
        symce_fp(FP_MUL, g, sc, g);
        rb = bint(g);
        gb = real(g);
        gb = gb > 1e30f ? 1e30f : gb < -1e30f ? -1e30f : gb;
        if (j)
            dseg(w, 2 * j - 2, ga, 2 * j, gb, 2 * j - 2, ra, 2 * j, rb, (unsigned)ra > 164 || (unsigned)rb > 164);
        ra = rb;
        ga = gb;
    }
}

/* ENTER in Tangent(: the tangent at the trace's X, and its equation where TI puts it; the cursor stays. */
static void dtent(struct gw *w)
{
    uint8_t m[9], b[9], t[9];
    unsigned e;
    if (!w->yok)
        return;
    e = symce_calc(5, w->fn, w->cl, w->cr, w->xv, 0, m);
    if (e)
        cthrow(w, e);
    symce_fp(FP_MUL, m, w->xv, t);
    symce_fp(FP_SUB, w->yv, t, b);
    chide(w);
    for (int y = 0; y < w->sr; y++)              /* TI drops the header and the label */
        sbrest(y, 0, PW);
    dtang(w, m, b);
    strcpy(w->ptb, "X=");
    symce_fmt(w->xv, w->ptb + 2);
    strcpy(w->ptc, "y=");
    symce_fmt(m, w->ptc + 2);
    strcat(w->ptc, "X+");
    symce_fmt(b, w->ptc + strlen(w->ptc));
    w->pfg = pal(w->col[w->fn]);
    w->psv = 0;
    w->cres = 1;
    mset(40, 1);                                 /* the free cursor's spot is kept from here (measured) */
    tshow(w);
}

/* The free cursor where the layer's signature remembers it, else the centre. */
static void dr_home(struct gw *w)
{
    if (mget(18) == 1) {
        w->gx = mget(13) | mget(14) << 4 | mget(15) << 8;
        w->gy = mget(16) | mget(17) << 4;
    } else {
        w->gx = 132;
        w->gy = 82;
    }
}

/* A pick from the DRAW menu: 1 ClrDraw ... (D_*). */
static void dr_pick(struct gw *w, uint8_t n)
{
    struct dst *d = &w->dr;
    plain(w);
    if (n == D_CLR) {
        dr_reset();
        render(w);
        return;
    }
    render(w);
    d->dm = n;
    d->ds = 0;
    d->on = 0;
    w->hk = 0;
    if (n == D_TANG) {                           /* TRACE on the lowest function; ENTER draws (graph_fall hands TI the rest) */
        tstart(w);
        return;
    }
    w->mode = M_FREE;
    dr_home(w);
    if (n == D_TEXT) {                           /* TI keeps the whole 8 x 14 cell on the grid */
        w->gx = w->gx > 256 ? 256 : w->gx;
        w->gy = w->gy < 12 ? 12 : w->gy;
    }
    dr_title(w);
    fshow(w);
}

/* Text(: scan code, token (2 bytes: u v w and L1.. are two-byte tokens); plain, then 2nd. */
static const uint8_t ttab[] = { 0x21, 0x30, 0, 0x22, 0x31, 0, 0x1A, 0x32, 0, 0x12, 0x33, 0, 0x23, 0x34, 0, 0x1B, 0x35, 0,
                                0x13, 0x36, 0, 0x24, 0x37, 0, 0x1C, 0x38, 0, 0x14, 0x39, 0, 0x19, 0x3A, 0, 0x11, 0xB0, 0,
                                0x0A, 0x70, 0, 0x0B, 0x71, 0, 0x0C, 0x82, 0, 0x0D, 0x83, 0, 0x0E, 0xF0, 0, 0x1D, 0x10, 0,
                                0x15, 0x11, 0, 0x28, 0x58, 0, 0x25, 0x2B, 0, 0x26, 0xC2, 0, 0x1E, 0xC4, 0, 0x16, 0xC6, 0,
                                0x2C, 0xC0, 0, 0x2B, 0xBE, 0, 0x2D, 0x0D, 0, 0x2E, 0x0C, 0, 0x2A, 0x04, 0, 0 };
static const uint8_t ttab2[] = { 0x0E, 0xAC, 0, 0x2D, 0xBC, 0, 0x2C, 0xC1, 0, 0x2B, 0xBF, 0, 0x26, 0xC3, 0, 0x1E, 0xC5, 0,
                                 0x16, 0xC7, 0, 0x24, 0x5E, 0x80, 0x1C, 0x5E, 0x81, 0x14, 0x5E, 0x82, 0x22, 0x5D, 0,
                                 0x1A, 0x5D, 1, 0x12, 0x5D, 2, 0x23, 0x5D, 3, 0x1B, 0x5D, 4, 0x13, 0x5D, 5, 0x11, 0x72, 0,
                                 0x19, 0x2C, 0, 0x1D, 0x08, 0, 0x15, 0x09, 0, 0x0C, 0x06, 0, 0x0B, 0x07, 0, 0x25, 0x3B, 0,
                                 0x0D, 0xBB, 0x31, 0 };
/* ALPHA: A..Z, theta by key, then the punctuation pairs */
static const uint8_t atab[] = { 0x2F, 0x27, 0x1F, 0x2E, 0x26, 0x1E, 0x16, 0x0E, 0x2D, 0x25, 0x1D, 0x15, 0x0D, 0x2C, 0x24,
                                0x1C, 0x14, 0x0C, 0x2B, 0x23, 0x1B, 0x13, 0x0B, 0x2A, 0x22, 0x1A, 0x12, 0,
                                0x21, 0x29, 0x19, 0x3E, 0x11, 0xAF, 0x0A, 0x2A, 0 };

/* A token's characters go at the cursor, each a white cell with the glyph (the OS's own, via Load_Sfont), the
   cursor on by the advance. */
static void dr_ttext(struct gw *w, uint8_t *tk)
{
    struct dst *d = &w->dr;
    char s[16];
    symce_tok(tk, s);
    zhide(w);
    d->cm = 1;
    for (const char *c = s; *c; c++) {
        const uint8_t *g = symce_sglyph((uint8_t)*c);
        int a = g[0];
        if (w->gx + a > 264) {                   /* past the edge: nothing drawn, the cursor to the last column */
            w->gx = 263;
            continue;
        }
        for (int r = 0; r < 14; r++)
            for (int j = 0; j < a; j++) {
                uint8_t b = a <= 8 ? g[1 + r] >> (7 - j) : r < 12 ? g[1 + 2 * r + j / 8] >> (7 - j % 8) : 0;
                d->nib = b & 1 ? 1 : 11;
                dcell(w, w->gx + j, w->gy - 1 - r);
            }
        w->gx += a;
    }
    fshow(w);
}

/* A key typed in Text(: 1 if the key was Text's. */
static uint8_t dr_text(struct gw *w, uint8_t key, uint8_t sec)
{
    struct dst *d = &w->dr;
    uint8_t tk[2] = { 0, 0 }, i;
    if (key == 0x38)                             /* DEL */
        return 1;
    if (d->ds && !sec) {
        for (i = 0; i < 27; i++)
            if (atab[i] == key)
                tk[0] = 0x41 + i;
        for (const uint8_t *e = atab + 27; !tk[0] && *e; e += 2)
            if (*e == key)
                tk[0] = e[1];
        if (d->ds == 1)
            d->ds = 0;
    } else
        for (const uint8_t *e = sec ? ttab2 : ttab; *e; e += 3)
            if (*e == key) {
                tk[0] = e[1];
                tk[1] = e[2];
            }
    if (!tk[0])
        return 0;
    if (key == 0x28 && tk[0] == 0x58)            /* the X key (not ALPHA STO, which is X) */
        tk[0] = vtok(w);
    dr_ttext(w, tk);
    return 1;
}

/* Back from an OS menu opened in Text( (main.asm): the cursor as it was, the picked token (GWP) written there. */
static void dr_tpaste(struct gw *w)
{
    dr_pick(w, D_TEXT);
    w->gx = mget(13) | mget(14) << 4 | mget(15) << 8;
    w->gy = mget(16) | mget(17) << 4;
    if (*(uint8_t *)0x0D0FB14)                   /* none: CLEAR out of the menu */
        dr_ttext(w, (uint8_t *)0x0D0FB14);
}

/* ENTER with a DRAW cursor. */
static void dr_enter(struct gw *w)
{
    struct dst *d = &w->dr;
    zhide(w);
    d->cm = 1;
    d->nib = 1;
    switch (d->dm) {
    case D_LINE:
        if (!d->ds) {
            d->x0 = w->gx;
            d->y0 = w->gy;
            d->ds = 1;
        } else {
            dline(w, d->x0, d->y0, w->gx, w->gy);
            d->ds = 0;
        }
        break;
    case D_HORZ:
        dline(w, 0, w->gy, 264, w->gy);
        break;
    case D_VERT:
        dline(w, w->gx, 0, w->gx, 164);
        break;
    case D_CIRC:
        if (!d->ds) {
            d->x0 = w->gx;
            d->y0 = w->gy;
            d->ds = 1;
        } else {
            dcirc(w, d->x0, d->y0, w->gx, w->gy);
            d->ds = 0;
        }
        break;
    case D_PEN:
        d->ds ^= 1;
        if (d->ds) {                             /* pen down: a point here */
            d->x0 = w->gx;
            d->y0 = w->gy;
            dcell(w, w->gx, w->gy);
        }
        break;
    case D_PTON:
    case D_PTOFF:
    case D_PTCHG:
        dmark(w, w->gx, w->gy, d->dm - D_PTON);
        break;
    }
    fshow(w);
}
