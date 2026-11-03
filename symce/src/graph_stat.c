/* Stat plots on SymCE's grapher (included by graph.c, not built alone).
 *
 * The OS keeps three plot records, 18 bytes from 0x0D0227E: byte 0 = type (low nibble: 0 Scatter, 1 xyLine,
 * 2 ModBox, 3 Hist, 4 Box, 5 NormProb) | mark << 4 (0 box, 1 cross, 2 dot); X list name (5 bytes, L1..L6 are
 * 0..5), Y list name, Freq list name (a literal 1 is '1'); byte 16 bit 0 = on; byte 17 = colour, as Y1's.
 * TI maps a point to its 265x165 grid with round((v - min) / delta), and draws the mark there; the marks are
 * 7x7 pixel glyphs (docs 11c), so they stay that size here and move to the grid's pixel in the 319x219 plot.
 * What this does not draw, or any list the OS would raise an error for, is TI's graph (sp_fall).
 */
#define SPREC(n) (0x0D0227Eu + 18u * (n))
#define SP_XL   1
#define SP_YL   6
#define SP_FQ   11
#define SP_ON   16
#define SP_COL  17

/* Finds the list named by the 5 bytes at nm (L1 is 00 00 00 00 00): its data, or 0. ChkFindSym, so an archived list
   is found where it is. */
const uint8_t *symce_lst(const uint8_t *nm);

/* A plot's lists, if it is one this draws and they are as the OS would take them: element counts of Scatter and
   xyLine must match and be 1..999. Returns the count, 0 for TI's graph. */
static unsigned splists(uint32_t rec, const uint8_t **x, const uint8_t **y)
{
    const uint8_t *a = symce_lst((const uint8_t *)(rec + SP_XL)), *b = symce_lst((const uint8_t *)(rec + SP_YL));
    unsigned n;
    if (!a || !b)
        return 0;
    n = (unsigned)(a[0] | a[1] << 8);
    if (n != (unsigned)(b[0] | b[1] << 8) || n < 1 || n > 999)
        return 0;
    *x = a + 2;
    *y = b + 2;
    return n;
}

/* ---- Histogram (type 3): bins of Xscl from Xmin, [left, right) each, a hollow thick rectangle per non-empty bin
   (count = sum of Freq, or the number of values) from the baseline y = 0 up, and a full-width baseline at y = 0, all in
   the plot colour, clipped to the grid (measured against TI's graph, statplot.py hist cases). ---- */
static int sgrid(const uint8_t *v, uint32_t min, uint32_t d);
#define SHB ((uint16_t *)0x0D0C800)     /* bin counts, in plotSScreen past the strips */
#define SH_MAX 320

/* floor of a TI real >= 0 as an int (cap 100000), -1 if negative */
static int rfl(const uint8_t *t)
{
    int e = t[1] - 0x80, k = 0;
    if (!(t[2] & 0xF0))
        return t[0] & 0x80 ? -1 : 0;              /* zero */
    if (t[0] & 0x80)
        return -1;
    if (e < 0)
        return 0;
    if (e > 4)
        return 100000;
    for (int i = 0; i <= e; i++)
        k = k * 10 + ((t[2 + i / 2] >> (i & 1 ? 0 : 4)) & 15);
    return k;
}

/* a TI real holding the integer n (0..99999999) */
static void ireal(uint8_t *t, unsigned n)
{
    uint8_t d[9];
    int nd = 0, i;
    memset(t, 0, 9);
    if (!n)
        return;
    for (unsigned m = n; m; m /= 10)
        d[nd++] = m % 10;
    t[1] = 0x80 + nd - 1;
    for (i = 0; i < nd; i++)
        t[2 + i / 2] |= d[nd - 1 - i] << (i & 1 ? 0 : 4);
}

/* Fills the bin counts (draw != 0) or only says whether TI's histogram is one this draws: Xscl > 0, 1..999 values,
   Freq 1 or a list of as many whole numbers 0..999, at most SH_MAX bins. Returns the bins, 0 = TI's graph. */
static unsigned shist(uint32_t rec, int draw)
{
    const uint8_t *x = symce_lst((const uint8_t *)(rec + SP_XL)), *f = 0;
    uint8_t t[9], u[9];
    unsigned n, nb;
    int k;
    if (!x)
        return 0;
    n = (unsigned)(x[0] | x[1] << 8);
    x += 2;
    if (n < 1 || n > 999 || OSB(ADDR_XSCL) & 0x80 || !(OSB(ADDR_XSCL + 2) & 0xF0))
        return 0;
    if (OSB(rec + SP_FQ) != '1' || OSB(rec + SP_FQ + 1)) {
        f = symce_lst((const uint8_t *)(rec + SP_FQ));
        if (!f || (unsigned)(f[0] | f[1] << 8) != n)
            return 0;
        f += 2;
    }
    symce_fp(FP_SUB, (const uint8_t *)ADDR_XMAX, (const uint8_t *)ADDR_XMIN, t);
    symce_fp(FP_DIV, t, (const uint8_t *)ADDR_XSCL, t);
    k = rfl(t);
    nb = (unsigned)k + 1;                          /* the bin whose left edge is Xmax is off the grid: skipped below */
    if (k < 0 || k >= SH_MAX)
        return 0;
    if (draw)
        memset(SHB, 0, nb * 2);
    for (unsigned i = 0; i < n; i++) {
        int w = 1;
        if (f) {
            w = rfl(f + 9 * i);
            if (w < 0 || w > 999)
                return 0;
            ireal(u, (unsigned)w);
            symce_fp(FP_SUB, f + 9 * i, u, u);      /* must be a whole number */
            if (u[2] & 0xF0)
                return 0;
        }
        if (!draw)
            continue;
        symce_fp(FP_SUB, x + 9 * i, (const uint8_t *)ADDR_XMIN, t);
        symce_fp(FP_DIV, t, (const uint8_t *)ADDR_XSCL, t);
        int b = rfl(t);
        if (b >= 0 && b < (int)nb) {
            if (SHB[b] + w > 60000)
                return 0;
            SHB[b] += w;
        }
    }
    return nb;
}

static void shist_draw(struct gw *w, uint32_t rec)
{
    uint8_t t[9], base[9], u[9];
    unsigned nb = shist(rec, 1);
    symce_dirty();                                  /* the bin counts live in plotSScreen */
    int gy0 = sgrid((const uint8_t *)"\0\0\0\0\0\0\0\0\0", ADDR_YMIN, ADDR_DY), py0 = fdiv((164 - gy0) * PH + 82, 164);
    pseg(w, 0, py0, 318, py0);
    for (unsigned b = 0; b < nb; b++) {
        int xa, xb, gt, pt;
        if (!SHB[b])
            continue;
        ireal(u, b);
        symce_fp(FP_MUL, u, (const uint8_t *)ADDR_XSCL, t);
        symce_fp(FP_ADD, t, (const uint8_t *)ADDR_XMIN, base);   /* left edge */
        xa = sgrid(base, ADDR_XMIN, ADDR_DX);
        ireal(u, b + 1);
        symce_fp(FP_MUL, u, (const uint8_t *)ADDR_XSCL, t);
        symce_fp(FP_ADD, t, (const uint8_t *)ADDR_XMIN, base);   /* right edge */
        xb = sgrid(base, ADDR_XMIN, ADDR_DX);
        ireal(u, SHB[b]);
        gt = sgrid(u, ADDR_YMIN, ADDR_DY);
        pt = fdiv((164 - gt) * PH + 82, 164);
        xa = fdiv(xa * 318 + 132, 264);
        xb = fdiv(xb * 318 + 132, 264);
        pseg(w, xa, py0, xa, pt);
        pseg(w, xb, py0, xb, pt);
        pseg(w, xa, pt, xb, pt);
    }
}

/* How many plots are on (0..3), or 4 if one is on that TI has to draw. */
/* ---- Box (4) and ModBox (2): quartiles of the values taken Freq times each (1-Var Stats' rule: Q1 and Q3 are the
   medians of the lower and upper halves, the middle value left out when the count is odd), a box and median, whiskers
   to the extremes (Box) or to the last values inside Q1 - 1.5 IQR .. Q3 + 1.5 IQR (ModBox), the rest marked. TI puts it
   at a fixed height whatever the window, drawn with the 2 pixel pen, lines clipped at the grid. ---- */

/* the X list (*x, *n values), its Freq weights (*f, 0 = all 1) and the sum of them; 0 for TI's graph: 1..999 values,
   weights whole 0..999, a sum of at least 1 */
static unsigned sbdata(uint32_t rec, const uint8_t **x, const uint8_t **f, unsigned *n)
{
    const uint8_t *a = symce_lst((const uint8_t *)(rec + SP_XL));
    unsigned tot = 0;
    *f = 0;
    if (!a)
        return 0;
    *n = (unsigned)(a[0] | a[1] << 8);
    *x = a + 2;
    if (*n < 1 || *n > 999)
        return 0;
    if (OSB(rec + SP_FQ) != '1' || OSB(rec + SP_FQ + 1)) {
        const uint8_t *b = symce_lst((const uint8_t *)(rec + SP_FQ));
        if (!b || (unsigned)(b[0] | b[1] << 8) != *n)
            return 0;
        *f = b + 2;
    }
    for (unsigned i = 0; i < *n; i++) {
        int w = 1;
        if (*f) {
            uint8_t u[9];
            w = rfl(*f + 9 * i);
            if (w < 0 || w > 999)
                return 0;
            ireal(u, (unsigned)w);
            symce_fp(FP_SUB, *f + 9 * i, u, u);      /* must be a whole number */
            if (u[2] & 0xF0)
                return 0;
        }
        tot += (unsigned)w;
    }
    return tot;
}

static unsigned snp_n(uint32_t rec, const uint8_t **x);

static uint8_t sp_state(void)
{
    uint8_t on = 0;
    for (uint8_t n = 0; n < 3; n++) {
        const uint8_t *x, *y;
        uint32_t r = SPREC(n);
        if (!(OSB(r + SP_ON) & 1))
            continue;
        const uint8_t *bf;
        unsigned bn;
        uint8_t ty = OSB(r) & 15;
        if (ty == 3 ? !shist(r, 0) : ty == 2 || ty == 4 ? !sbdata(r, &x, &bf, &bn) : ty == 5 ? !snp_n(r, &x) : ty > 1 || !splists(r, &x, &y))
            return 4;
        on++;
    }
    return on;
}

/* v in the window's units to the grid: round((v - min) / delta), as a float for rnd(); the OS's arithmetic when it is
   within a hair of .5 (the float cannot tell). */
static int sgridf(float v, uint32_t min, uint32_t d)
{
    float f = (v - real_os(min)) / real_os(d);
    return f < -20000.0f ? -20000 : f > 20000.0f ? 20000 : rnd(f);
}

static int sgrid(const uint8_t *v, uint32_t min, uint32_t d)
{
    float f = (real(v) - real_os(min)) / real_os(d), r = f - (float)(int)f;
    if (r > 0.4995f && r < 0.5005f || r > -0.5005f && r < -0.4995f) {
        uint8_t t[9];
        symce_fp(FP_SUB, v, (const uint8_t *)min, t);
        symce_fp(FP_DIV, t, (const uint8_t *)d, t);
        f = real(t);
    }
    return f < -20000.0f ? -20000 : f > 20000.0f ? 20000 : rnd(f);          /* far off: 24 bit ints stay clear of overflow */
}

/* The mark at (cx, cy): box 7x7 with a 2 pixel border, cross (a 3x3 block and arms of 3), dot 3x3. */
static void smark(int cx, int cy, uint8_t m, uint16_t c)
{
    for (int j = -3; j <= 3; j++)
        for (int i = -3; i <= 3; i++) {
            int aj = j < 0 ? -j : j, ai = i < 0 ? -i : i;
            if (m == 0 ? (aj > 1 || ai > 1) : m == 1 ? (aj <= 1 && ai <= 1) || !(ai && aj) : (aj <= 1 && ai <= 1))
                put(cx + i, cy + j, c);
        }
}

/* ---- Box / ModBox drawing ---- */
struct sbc {                                    /* (no writable statics: this runs from flash) */
    const uint8_t *x, *f;
    unsigned m;                                 /* values with a weight, sorted ascending as indexes in SHB */
};
static const uint8_t SB_TWO[9] = {0, 0x80, 0x20}, SB_ONE5[9] = {0, 0x80, 0x15};

/* compares two TI reals */
static int rcmp(const uint8_t *a, const uint8_t *b)
{
    int sa = !(a[2] & 0xF0) ? 0 : a[0] & 0x80 ? -1 : 1, sb = !(b[2] & 0xF0) ? 0 : b[0] & 0x80 ? -1 : 1, c;
    if (sa != sb)
        return sa < sb ? -1 : 1;
    if (!sa)
        return 0;
    c = a[1] != b[1] ? (a[1] < b[1] ? -1 : 1) : memcmp(a + 2, b + 2, 7);
    return sa < 0 ? -c : c;
}

/* the indexes of the values with a weight (all, if f is 0), sorted ascending, in SHB; their number */
static unsigned ssort(const uint8_t *x, const uint8_t *f, unsigned n)
{
    unsigned m = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned j;
        if (f && !rfl(f + 9 * i))
            continue;
        for (j = m++; j && rcmp(x + 9 * SHB[j - 1], x + 9 * i) > 0; j--)
            SHB[j] = SHB[j - 1];
        SHB[j] = (uint16_t)i;
    }
    return m;
}

/* the k-th (from 0) of the values, each repeated by its weight */
static const uint8_t *sbk(const struct sbc *c, unsigned k)
{
    for (unsigned i = 0; i < c->m; i++) {
        unsigned j = SHB[i], w = c->f ? (unsigned)rfl(c->f + 9 * j) : 1;
        if (k < w)
            return c->x + 9 * j;
        k -= w;
    }
    return c->x;
}

/* median of the len values from the a-th */
static void sbmed(const struct sbc *c, unsigned a, unsigned len, uint8_t *out)
{
    const uint8_t *p = sbk(c, a + len / 2);
    if (len & 1) {
        memcpy(out, p, 9);
        return;
    }
    symce_fp(FP_ADD, sbk(c, a + len / 2 - 1), p, out);
    symce_fp(FP_DIV, out, SB_TWO, out);
}

#define SBPX(g) fdiv((g) * 318 + 132, 264)
#define SBPY(g) fdiv((164 - (g)) * PH + 82, 164)

/* a vertical line at grid x g from grid y y0 to y1: none if g is off the grid (TI draws no pen from there) */
static void sbv(struct gw *w, int g, int y0, int y1)
{
    if (g >= 0 && g <= 264)
        pseg(w, SBPX(g), SBPY(y0), SBPX(g), SBPY(y1));
}

/* a horizontal line from grid x a to b at grid y, clipped to the grid */
static void sbh(struct gw *w, int a, int b, int y)
{
    if (a < 0)
        a = 0;
    if (b > 264)
        b = 264;
    if (a <= b)
        pseg(w, SBPX(a), SBPY(y), SBPX(b), SBPY(y));
}

static void sbox_draw(struct gw *w, uint32_t rec)
{
    const uint8_t *x, *f;
    unsigned n, tot = sbdata(rec, &x, &f, &n), h;
    uint8_t q1[9], md[9], q3[9], lo[9], hi[9], t[9];
    const uint8_t *wl, *wh;
    int mod = (OSB(rec) & 15) == 2, gq1, gq3, gl, gh;
    symce_dirty();
    struct sbc c = {x, f, 0};
    c.m = ssort(x, f, n);
    h = tot / 2;
    if (tot == 1) {
        memcpy(q1, x + 9 * SHB[0], 9);
        memcpy(q3, q1, 9);
    } else {
        sbmed(&c, 0, h, q1);
        sbmed(&c, tot - h, h, q3);
    }
    sbmed(&c, 0, tot, md);
    wl = sbk(&c, 0);
    wh = sbk(&c, tot - 1);
    if (mod) {
        symce_fp(FP_SUB, q3, q1, t);
        symce_fp(FP_MUL, t, SB_ONE5, t);
        symce_fp(FP_SUB, q1, t, lo);
        symce_fp(FP_ADD, q3, t, hi);
        wl = wh = 0;
        for (unsigned i = 0; i < c.m; i++) {
            const uint8_t *e = x + 9 * SHB[i];
            if (rcmp(e, lo) >= 0 && rcmp(e, hi) <= 0) {
                if (!wl)
                    wl = e;
                wh = e;
            }
        }
        if (!wl)
            wl = wh = md;
    }
    gq1 = sgrid(q1, ADDR_XMIN, ADDR_DX);
    gq3 = sgrid(q3, ADDR_XMIN, ADDR_DX);
    gl = sgrid(wl, ADDR_XMIN, ADDR_DX);
    gh = sgrid(wh, ADDR_XMIN, ADDR_DX);
    sbh(w, gq1, gq3, 152);
    sbh(w, gq1, gq3, 114);
    sbv(w, gq1, 152, 114);
    sbv(w, sgrid(md, ADDR_XMIN, ADDR_DX), 152, 114);
    sbv(w, gq3, 152, 114);
    sbh(w, gl, gq1, 133);
    sbh(w, gq3, gh, 133);
    sbv(w, gl, 135, 131);
    sbv(w, gh, 135, 131);
    if (mod)
        for (unsigned i = 0; i < c.m; i++) {
            const uint8_t *e = x + 9 * SHB[i];
            if (rcmp(e, lo) < 0 || rcmp(e, hi) > 0) {
                int g = sgrid(e, ADDR_XMIN, ADDR_DX);
                if (g >= 0 && g <= 264)
                    smark(SBPX(g), SBPY(133), OSB(rec) >> 4 & 3, w->pc);
            }
        }
}

/* ---- NormProb (type 5): the sorted values against the normal quantile of each one's place, invNorm((i - 0.5) / n), the
   values on the X or (record byte 16 bit 4) the Y axis, marked like a scatter (measured against TI's graph). ---- */
/* invNorm of 0 < p <= 0.5 (Acklam's rational approximation, relative error 1e-9: far inside a pixel) */
static float sinvn(float p)
{
    float q, r;
    if (p < 0.02425f) {
        q = sqrtf(-2.0f * logf(p));
        return (((((-7.784894002430293e-03f * q - 3.223964580411365e-01f) * q - 2.400758277161838f) * q - 2.549732539343734f) * q + 4.374664141464968f) * q + 2.938163982698783f) /
               ((((7.784695709041462e-03f * q + 3.224671290700398e-01f) * q + 2.445134137142996f) * q + 3.754408661907416f) * q + 1.0f);
    }
    q = p - 0.5f;
    r = q * q;
    return (((((-3.969683028665376e+01f * r + 2.209460984245205e+02f) * r - 2.759285104469687e+02f) * r + 1.383577518672690e+02f) * r - 3.066479806614716e+01f) * r + 2.506628277459239f) * q /
           (((((-5.447609879822406e+01f * r + 1.615858368580409e+02f) * r - 1.556989798598866e+02f) * r + 6.680131188771972e+01f) * r - 1.328068155288572e+01f) * r + 1.0f);
}

/* the values' count, or 0 for TI's graph: 1..999 values in the X list */
static unsigned snp_n(uint32_t rec, const uint8_t **x)
{
    const uint8_t *a = symce_lst((const uint8_t *)(rec + SP_XL));
    unsigned n;
    if (!a)
        return 0;
    n = (unsigned)(a[0] | a[1] << 8);
    *x = a + 2;
    return n > 999 ? 0 : n;
}

static void snorm_draw(struct gw *w, uint32_t rec)
{
    const uint8_t *x;
    unsigned n = snp_n(rec, &x);
    int ay = OSB(rec + SP_ON) & 0x10;
    symce_dirty();
    n = ssort(x, 0, n);
    for (unsigned i = 0; i < n; i++) {
        float k = (float)i + 0.5f, z = k * 2.0f < (float)n ? sinvn(k / (float)n) : -sinvn(((float)n - k) / (float)n);
        int gv = sgrid(x + 9 * SHB[i], ay ? ADDR_YMIN : ADDR_XMIN, ay ? ADDR_DY : ADDR_DX);
        int gz = sgridf(z, ay ? ADDR_XMIN : ADDR_YMIN, ay ? ADDR_DX : ADDR_DY);
        int gx = ay ? gz : gv, gy = ay ? gv : gz;
        if (gx >= 0 && gx <= 264 && gy >= 0 && gy <= 164)
            smark(SBPX(gx), SBPY(gy), OSB(rec) >> 4 & 3, w->pc);
    }
}

/* ZoomStat makes a window of the plots' data, and TI's error (ERR:WINDOW RANGE) of one with no width or height: is some
   list an on plot uses a single value (or several equal ones)? Then it is TI's. */
static int sp_degen(void)
{
    for (uint8_t n = 0; n < 3; n++) {
        uint32_t r = SPREC(n);
        if (!(OSB(r + SP_ON) & 1))
            continue;
        for (uint8_t l = 0; l < ((OSB(r) & 15) < 2 ? 2 : 1); l++) {
            const uint8_t *a = symce_lst((const uint8_t *)(r + (l ? SP_YL : SP_XL)));
            unsigned c = a ? (unsigned)(a[0] | a[1] << 8) : 0, i = 1;
            while (i < c && !rcmp(a + 2, a + 2 + 9 * i))
                i++;
            if (i >= c)
                return 1;
        }
    }
    return 0;
}

/* Plots 1, 2, 3 in that order, under the functions. */
static void splots(struct gw *w)
{
    for (uint8_t n = 0; n < 3; n++) {
        const uint8_t *x, *y;
        uint32_t r = SPREC(n);
        unsigned cnt;
        int ox = 0, oy = 0;
        if (!(OSB(r + SP_ON) & 1))
            continue;
        w->pc = pal(OSB(r + SP_COL));
        w->st = 1;
        if ((OSB(r) & 15) == 3) {
            shist_draw(w, r);
            continue;
        }
        if ((OSB(r) & 15) == 2 || (OSB(r) & 15) == 4) {
            sbox_draw(w, r);
            continue;
        }
        if ((OSB(r) & 15) == 5) {
            snorm_draw(w, r);
            continue;
        }
        if (!(cnt = splists(r, &x, &y)))
            continue;
        for (int pass = 0; pass < 2; pass++)     /* the line first for xyLine, then the marks on it */
            for (unsigned i = 0; i < cnt; i++) {
                int gx = sgrid(x + 9 * i, ADDR_XMIN, ADDR_DX), gy = sgrid(y + 9 * i, ADDR_YMIN, ADDR_DY);
                int px = fdiv(gx * 318 + 132, 264), py = fdiv((164 - gy) * PH + 82, 164);
                if (pass == 0 && (OSB(r) & 15) == 1 && i)
                    pseg(w, ox, oy, px, py);
                if (pass && gx >= 0 && gx <= 264 && gy >= 0 && gy <= 164)      /* TI draws no mark for a point off the grid */
                    smark(px, py, OSB(r) >> 4 & 3, w->pc);
                ox = px;
                oy = py;
            }
    }
}

/* The plot's colour. */
static uint16_t sp_color(const struct gw *w)
{
    return pal(OSB(SPREC(w->pt - 1) + SP_COL));
}

/* Can SymCE's TRACE walk every plot that is on? Function mode, ExprOff clear, Scatter and xyLine on L1..L6 so far.
   Anything else is TI's TRACE (graph_fall, graph_key). */
static uint8_t sp_trok(void)
{
    if (pmode() || (OSB(0x0D00098) & 1))
        return 0;
    for (uint8_t n = 0; n < 3; n++) {
        uint32_t r = SPREC(n);
        if (!(OSB(r + SP_ON) & 1))
            continue;
        if ((OSB(r) & 15) > 1)
            return 0;
        for (uint8_t k = 0; k < 2; k++) {
            uint32_t a = r + (k ? SP_YL : SP_XL);
            if (OSB(a) > 5 || OSB(a + 1) || OSB(a + 2) || OSB(a + 3) || OSB(a + 4))
                return 0;
        }
    }
    return 1;
}
