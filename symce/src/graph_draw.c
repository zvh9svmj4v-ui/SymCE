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
    return r <= 0 ? 0 : r > 164 ? PH : (r * 218 - 109 + 163) / 164;
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
    if (d->dm != D_LINE && d->dm != D_HORZ && d->dm != D_VERT)
        return;
    d->cm = 0;
    d->nib = 1;
    d->n = 0;
    d->ovf = 0;
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
    w->mode = M_FREE;
    w->hk = 0;
    if (mget(18) == 1) {
        w->gx = mget(13) | mget(14) << 4 | mget(15) << 8;
        w->gy = mget(16) | mget(17) << 4;
    } else {
        w->gx = 132;
        w->gy = 82;
    }
    dr_title(w);
    fshow(w);
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
    case D_PTON:
    case D_PTOFF:
    case D_PTCHG:
        dmark(w, w->gx, w->gy, d->dm - D_PTON);
        break;
    }
    fshow(w);
}
