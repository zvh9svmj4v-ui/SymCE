/* SymCE's TABLE: 2nd GRAPH from the grapher or home. The same look as TI's (columns X, Y1.., TblStart
   and DeltaTbl as TBLSET left them, Indpnt Auto), the same numbers (each cell is the OS's own evaluation of
   Yn at X, formatted by the OS's table routine), and quicker: a scroll moves the rows already drawn and
   evaluates one row, a column scroll evaluates nothing it has already. TI's table does the rest: the
   modes other than Function, Indpnt or Depend Ask, a split screen, typing into the table, ENTER on a
   header, + in the X column. The cells are kept in SB, 9 bytes a real: X first, then the Y columns by
   position in fn[]; byte 0 is 0xEE for a cell the OS could not evaluate.
   Included by graph.c. */
#define TBL_START ((const uint8_t *)0x0D02267)    /* TblStart, TblDelta: TI reals */
#define TBL_DELTA ((const uint8_t *)0x0D02270)
#define ADDR_TBLF 0x0D00093                       /* tblFlags: 10 Indpnt Ask, 20 Depend Ask */
#define TROWS 11
#define TCELL(k, r) (SB + ((k) * TROWS + (r)) * 9)
#define TBAD 0xEE
#define TSEL ((volatile uint8_t *)0x0D0EED0)                /* the selection TI keeps between visits: 0xA5, column, row, header, first column */
#define TBODY 49                                  /* first row of the cells; a row is 14 px, the last 13 */
#define TBOT 204                                  /* under the black line */

/* Does TI's table have to show this? */
static uint8_t tb_fall(void)
{
    return pmode() || (OSB(ADDR_TBLF) & 0x30) || (OSB(ADDR_SG) & 7);
}

static void tb_tbox(int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; j++) {
        uint16_t *v = VRAM + (y + j) * VS + x;
        if (w > 0)
            symce_fill(v, w, c);
    }
}

/* The X of row r: TblStart + r DeltaTbl. */
static void tb_trx(int r, uint8_t *out)
{
    uint8_t t[9];
    itor(r, t);
    symce_fp(FP_MUL, t, TBL_DELTA, t);
    symce_fp(FP_ADD, t, TBL_START, out);
}

/* Column k (0 = X), rows a..b, from the OS. */
static void tb_tbcalc(struct gw *w, uint8_t k, uint8_t a, uint8_t b)
{
    for (uint8_t r = a; r <= b; r++) {
        uint8_t x[9], *c = TCELL(k, r);
        tb_trx(r, x);
        if (!k)
            memcpy(c, x, 9);
        else if (symce_osy(w->tbl.fn[k - 1], x, c, 0x58))
            c[0] = TBAD;
    }
    w->tbl.ok |= 1 << k;
}

/* The column's place on the screen 0..4, or -1. */
static int tb_tcx(const struct gw *w, uint8_t k)
{
    if (!k)
        return 0;
    return k - 1 < w->tbl.c0 || k - 1 >= w->tbl.c0 + 4 ? -1 : k - w->tbl.c0;
}

static int tb_tcl(int sc)
{
    return sc ? 64 + 60 * (sc - 1) : 0;
}

static uint16_t tb_tcol(const struct gw *w, uint8_t k)
{
    return k ? pal(w->col[w->tbl.fn[k - 1]]) : 0;
}

static void tb_tcell(struct gw *w, uint8_t k, uint8_t r)
{
    int sc = tb_tcx(w, k), x = tb_tcl(sc), y = TBODY + 14 * r;
    uint8_t hl = !w->tbl.hdr && w->tbl.c == k && w->tbl.r == r;
    char s[16];
    const uint8_t *c = TCELL(k, r);
    if (sc < 0)
        return;
    if (c[0] == TBAD)
        strcpy(s, "ERROR");
    else
        symce_fmtw(c, s, 6);
    if (hl)
        tb_tbox(x, y, sc ? 60 : 64, 13, 0);
    else
        tb_tbox(x, y, sc ? 58 : 61, 13, 0xFFFF);
    symce_text(x + (sc ? 8 : 12), y, hl ? 0xFFFF : tb_tcol(w, k), hl ? 0 : 0xFFFF, s, 0);
    tb_tbox(x, y + 13, sc ? 58 : 61, 1, r < TROWS - 1 ? 0xE71C : 0);   /* the glyphs' cell reached the line under */
}

static void tb_tcellcol(struct gw *w, uint8_t k)
{
    for (uint8_t r = 0; r < TROWS; r++)
        tb_tcell(w, k, r);
}

/* The text of the column's name: Y1.. */
static void tb_tname(const struct gw *w, uint8_t k, char *s)
{
    uint8_t t[2];
    if (!k) {
        s[0] = 'X';
        s[1] = 0;
        return;
    }
    t[0] = 0x5E;
    t[1] = (uint8_t)(0x10 + w->tbl.fn[k - 1]);
    symce_tok(t, s);
}

static void tb_thead(struct gw *w, uint8_t k)
{
    int sc = tb_tcx(w, k), x = tb_tcl(sc);
    uint8_t hl = w->tbl.hdr && w->tbl.c == k;
    char s[16];
    if (sc < 0)
        return;
    tb_tname(w, k, s);
    if (hl)
        tb_tbox(x, 30, sc ? 60 : 64, 17, 0);
    else
        tb_tbox(x, 30, sc ? 58 : 61, 17, 0xFFFF);
    symce_text(sc ? x + 20 : 24, 31, hl ? 0xFFFF : tb_tcol(w, k), hl ? 0 : 0xFFFF, s, 1);
    if (!sc)
        tb_tbox(0, 46, 64, 1, 0);
}

/* The status bar's second line, and the line under the table: what the selected cell is. */
static void tb_tinfo(struct gw *w)
{
    static const char bx[] = "PRESS  \xC1" "ENTER]  TO  EDIT", bu[] = "PRESS  \xF3  TO  EDIT  FUNCTION", bp[] = "PRESS  +  FOR  \xBE" "Tbl";
    char s[40], v[24];
    uint8_t k = w->tbl.c;
    TSEL[0] = 0xA5;
    TSEL[1] = k;
    TSEL[2] = w->tbl.r;
    TSEL[3] = w->tbl.hdr;
    TSEL[4] = w->tbl.c0;
    const uint8_t *c = TCELL(k, w->tbl.r);
    tb_tbox(0, 12, 300, 12, 0x52AA);
    tb_tbox(0, 24, 320, 6, 0x52AA);
    symce_text(2, 14, 0xE71C, 0x52AA, w->tbl.hdr ? bx : k ? bu : bp, 0);
    tb_tbox(0, TBOT, 320, 36, 0xFFFF);
    tb_tname(w, k, s);
    if (w->tbl.hdr) {
        if (k) {
            uint8_t n = w->len[w->tbl.fn[k - 1]];
            unsigned l = strlen(s), i = 0;
            const uint8_t *t = w->tok[w->tbl.fn[k - 1]];
            symce_text(2, 219, tb_tcol(w, k), 0xFFFF, s, 1);
            tb_tbox(2 + 12 * l, 219, 10, 14, tb_tcol(w, k));     /* the selected equals sign: a block with two white slots */
            tb_tbox(4 + 12 * l, 223, 6, 2, 0xFFFF);
            tb_tbox(4 + 12 * l, 227, 6, 2, 0xFFFF);
            l++;
            while (i < n && i < MAXTOK && l < 24) {
                i += symce_tok(t + i, v);
                symce_text(2 + 12 * l, 219, tb_tcol(w, k), 0xFFFF, v, 1);
                l += strlen(v);
            }
        }
        return;
    }
    strcat(s, "=");
    if (c[0] == TBAD)
        strcat(s, "ERROR");
    else
        symce_fmtn(c, s + strlen(s), 15 + (c[0] >> 7) + (c[1] < 0x80) * 2);   /* 14 digits, the point, the sign, and the 0 of 0.x */
    symce_text(2, 219, tb_tcol(w, k), 0xFFFF, s, 1);
}

static void tb_tframe(struct gw *w)
{
    symce_rom(0x21A3C);                          /* DrawStatusBar */
    tb_tbox(0, 30, 320, 210, 0xFFFF);
    for (int r = 0; r < TROWS - 1; r++)
        for (int s = 0; s < 5; s++)
            tb_tbox(tb_tcl(s), TBODY + 13 + 14 * r, s ? 58 : 61, 1, 0xE71C);
    tb_tbox(0, 47, 320, 2, 0);
    tb_tbox(0, 202, 320, 2, 0);
    tb_tbox(0, 46, 64, 1, 0);
    tb_tbox(61, 30, 3, 173, 0);
    for (int s = 1; s < 5; s++)
        tb_tbox(tb_tcl(s) + 58, 30, 2, 173, 0);
    for (uint8_t k = 0; k <= w->tbl.nc; k++)
        tb_thead(w, k);
}

static void tb_tall(struct gw *w)
{
    tb_tframe(w);
    tb_tcell(w, 0, 0);
    for (uint8_t k = 0; k <= w->tbl.nc; k++)
        tb_tcellcol(w, k);
    tb_tinfo(w);
}

static void tb_topen(struct gw *w)
{
    struct tbl *t = &w->tbl;
    t->on = 1;
    t->nc = 0;
    for (uint8_t i = 0; i < 10; i++)
        if (w->len[i])
            t->fn[t->nc++] = i;
    t->c = t->r = t->hdr = t->c0 = 0;
    if (TSEL[0] == 0xA5 && TSEL[1] <= t->nc && TSEL[2] < TROWS && TSEL[3] <= 1 && !(TSEL[3] && !TSEL[1]) &&
        TSEL[4] <= (t->nc > 4 ? t->nc - 4 : 0) && (!TSEL[1] || (TSEL[1] > TSEL[4] && TSEL[1] <= TSEL[4] + 4))) {
        t->c = TSEL[1];
        t->r = TSEL[2];
        t->hdr = TSEL[3];
        t->c0 = TSEL[4];
    }
    t->ok = 0;
    w->mode = M_PLAIN;
    w->hk = 0;
    tb_tbcalc(w, 0, 0, TROWS - 1);
    for (uint8_t k = 1; k <= t->nc; k++)
        if (tb_tcx(w, k) >= 0)
            tb_tbcalc(w, k, 0, TROWS - 1);
    tb_tframe(w);
    tb_tcell(w, 0, 0);
    for (uint8_t k = 0; k <= t->nc; k++)
        tb_tcellcol(w, k);
    tb_tinfo(w);
}

/* Leave: X as it was. */
static void tb_tleave(struct gw *w)
{
    symce_stox(w->var[23]);
    w->tbl.on = 0;
}

/* Scroll the rows: dir > 0 shows the next X (the rows move up). */
static void tb_tscroll(struct gw *w, int dir)
{
    struct tbl *t = &w->tbl;
    uint8_t d[9];
    uint16_t *v = VRAM;
    uint8_t keep = t->r;
    t->r = 99;
    tb_tcell(w, t->c, keep);                        /* the highlight off */
    symce_fp(dir > 0 ? FP_ADD : FP_SUB, TBL_START, TBL_DELTA, d);
    memcpy((void *)TBL_START, d, 9);
    if (dir > 0)
        memmove(v + TBODY * VS, v + (TBODY + 14) * VS, 139 * VS * 2);
    else
        memmove(v + (TBODY + 14) * VS, v + TBODY * VS, 139 * VS * 2);
    for (uint8_t k = 0; k <= t->nc; k++) {
        uint8_t *c = TCELL(k, 0);
        if (k && !(t->ok & (1 << k)))
            continue;
        if (dir > 0) {
            memmove(c, c + 9, (TROWS - 1) * 9);
            if (tb_tcx(w, k) >= 0 || !k)
                tb_tbcalc(w, k, TROWS - 1, TROWS - 1);
            else
                t->ok &= ~(1 << k);
        } else {
            memmove(c + 9, c, (TROWS - 1) * 9);
            if (tb_tcx(w, k) >= 0 || !k)
                tb_tbcalc(w, k, 0, 0);
            else
                t->ok &= ~(1 << k);
        }
    }
    {
        uint8_t row = dir > 0 ? TROWS - 1 : 0, g = dir > 0 ? TROWS - 2 : 0;
        if (dir > 0)
            for (int s = 0; s < 5; s++)
                tb_tbox(tb_tcl(s), TBODY + 13 + 14 * g, s ? 58 : 61, 1, 0xE71C);
        for (uint8_t k = 0; k <= t->nc; k++)
            tb_tcell(w, k, row);
    }
    t->r = keep;
    tb_tcell(w, t->c, keep);
}

static void tb_tmove(struct gw *w, uint8_t key)
{
    struct tbl *t = &w->tbl;
    uint8_t c = t->c, r = t->r, h = t->hdr;
    if (key == 1) {                              /* down */
        if (h)
            h = 0, r = 0;
        else if (r < TROWS - 1)
            r++;
        else {
            tb_tscroll(w, 1);
            tb_tinfo(w);
            return;
        }
    } else if (key == 4) {                       /* up */
        if (h)
            return;
        if (r)
            r--;
        else if (!c) {
            tb_tscroll(w, -1);
            tb_tinfo(w);
            return;
        } else
            h = 1;
    } else if (key == 2) {                       /* left */
        if (!c)
            return;
        c--;
    } else {                                     /* right */
        if (c >= t->nc)
            return;
        c++;
    }
    {
        uint8_t oc = t->c, or_ = t->r, oh = t->hdr, was = t->c0;
        t->c = c;
        t->r = r;
        t->hdr = h;
        if (c && c - 1 < t->c0)
            t->c0 = c - 1;
        if (c && c - 1 >= t->c0 + 4)
            t->c0 = c - 4;
        if (t->c0 != was) {                      /* the columns move: the Y columns are drawn again */
            for (uint8_t k = 1; k <= t->nc; k++)
                if (tb_tcx(w, k) >= 0) {
                    if (!(t->ok & (1 << k)))
                        tb_tbcalc(w, k, 0, TROWS - 1);
                    tb_thead(w, k);
                    tb_tcellcol(w, k);
                }
        } else {
            t->c = 99;                           /* the old cell, plain */
            t->hdr = 0;
            if (oh)
                tb_thead(w, oc);
            else
                tb_tcell(w, oc, or_);
            t->c = c;
            t->hdr = h;
            if (h)
                tb_thead(w, c);
            else
                tb_tcell(w, c, r);
        }
        tb_tinfo(w);
    }
}

static uint8_t tb_tkey(struct gw *w, uint8_t key)
{
    uint8_t sec = w->sec;
    const uint8_t *t;
    if (key == 0x36) {
        w->sec = 1;
        return 0;
    }
    if (key == 0x30)
        return 0;
    w->sec = 0;
    if (sec && key == 0x37) {                    /* 2nd QUIT */
        tb_tleave(w);
        return 1;
    }
    if (!sec && key >= 1 && key <= 4) {
        w->hk = key;
        w->th = w->tr = now();
        tb_tmove(w, key);
        return 0;
    }
    if (!sec && key == 0x0F)                     /* CLEAR does nothing in TI's table (measured) */
        return 0;
    if (!sec && key == 0x31) {                   /* GRAPH: SymCE's graph */
        tb_tleave(w);
        plain(w);
        render(w);
        return 0;
    }
    if (!sec && key == 0x32) {                   /* TRACE */
        tb_tleave(w);
        plain(w);
        render(w);
        tstart(w);
        return 0;
    }
    if (sec && key == 0x31)                      /* 2nd GRAPH: here already */
        return 0;
    if (!sec && ((key == 0x09 && w->tbl.hdr) || (key == 0x0A && !w->tbl.c) || (!w->tbl.hdr && etok(key, 0)))) {
        tb_tleave(w);                               /* editing: TI's table (the key itself is not replayed; TI opens on its own remembered cell) */
        return 0x4A;
    }
    for (t = sec ? hand2 : hand1; *t; t += 2)
        if (*t == key) {
            tb_tleave(w);
            return t[1];
        }
    return 0;
}

/* A held arrow repeats. */
static void tb_ttick(struct gw *w)
{
    unsigned t = now();
    if (!w->hk)
        return;
    if (!(*(volatile uint8_t *)ADDR_KB7 & (1 << (w->hk - 1))))
        w->hk = 0;
    else if (((t - w->th) & 0xFFFFFF) >= HOLD && ((t - w->tr) & 0xFFFFFF) >= REPT) {
        w->tr = t;
        tb_tmove(w, w->hk);
    }
}
