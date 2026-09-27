/* SymCE menu: ALPHA+DOWN on the home screen, the TI-Nspire's Algebra and
 * Calculus menus, and a Geometry menu.
 *
 * Pops up, like the OS's own ALPHA+Y= shortcut menus, bottom left over three
 * tabs, Algebra, Calculus and Geometry,
 *
 *     1:solve(  2:factor(  3:expand(  4:deriv(  5:Polynomial Tools >
 *     6:comDenom(  7:Convert Expression >  8:Trigonometry >  9:Complex >
 *     0:Extract >
 *
 *     1:Derivative ... 9:Tangent Line  0:Normal Line  A:Arc Length
 *     B:Series >  C:Implicit Derivative  D:Numeric Calculations >
 *
 *     1:Distance  2:Midpoint ... 0:Partition Segment  A:Triangle >
 *     B:Circle >  C:Transformations >
 *
 * and types the chosen command at the cursor, exactly as if it had been
 * spelled with ALPHA letters: FACTOR( is the tokens 46 41 43 54 4F 52 10.
 * An item with > opens its submenu in place of this one (M, below). Keys:
 * 1-9, 0 and A-D (MATH APPS PRGM x⁻¹, no ALPHA, as in the OS's menus) pick;
 * UP/DOWN move, wrapping, scrolling past 10 items, and ENTER picks; RIGHT or
 * ENTER on a > item opens it; LEFT or RIGHT on another switches between the
 * tabs, wrapping; LEFT or CLEAR in a submenu goes back up, CLEAR at the top
 * closes. 2nd is ignored, so 2nd+MODE (QUIT) closes it through
 * MODE; any other key closes it.
 *
 * The hook body (hook.c) calls symce_menu at A=1, the key event, when the key
 * is ALPHA+DOWN (kAlphaDown, 0x08) and the cursor is on the entry itself --
 * not inside a MathPrint box, not on a history line -- and then swallows the
 * key. ALPHA+DOWN does nothing on the stock home screen (measured, both
 * modes, history line selected or not); ALPHA+GRAPH, the obvious spare, opens
 * the graph before any hook sees it.
 *
 * The same items are three tabs in 2nd MATH, after TEST LOGIC CONDITIONS:
 * menu_tab, called by the menu hook (mhook.c), hands the OS a table with
 * those tabs and draws their titles and items. Every item's key code is
 * 0x21, which no key makes, with the item's number as the prefix byte, plus
 * 0x10 for Calculus and 0x20 for Geometry: the OS closes the menu and hands
 * the home screen key 0x21 with keyExtend = that number (0x867CD: a prefix that is not
 * FF/FE/FC/FB/FA goes to keyExtend). A command is typed; a > item opens the
 * popup at that submenu.
 *
 * Like engine.c it is compiled C linked on its own and relocated with the app
 * (tools/relocs.py), and it runs from flash: no writable statics.
 *
 * - The popup is drawn straight into VRAM, text by the OS's VPutS in the large
 *   font, over pixels saved first to engineWork (the engine only runs at A=2)
 *   and put back on close. The cursor is already off: Mon turns it off before
 *   it hands a key to the home screen. Nothing of the OS's is touched.
 *   Having the OS redraw instead, (cxPPutAway) then _CxReDisp as around its
 *   own menus, is exact in Classic but in MathPrint reopens the entry with the
 *   cursor at its start (measured), so a pick mid-entry would land there.
 * - Keys come from GetCSC, raw.
 * - The command is typed by handing each letter's key code to the home
 *   screen's own key handler, (cxMain): the hook sees each one at A=1 as if
 *   typed, and the OS inserts it and draws it, MathPrint included.
 *
 * ponytail: the GetCSC loop does no APD; the OS turns off only once the menu
 * is closed. Add a timeout if a menu left open ever matters.
 */
#include <stdint.h>

#define VRAM     ((volatile uint16_t *)0xD40000)       /* 320x240, RGB565 */
#define SAVE     ((uint8_t *)0xD0EF00)                 /* engineWork: 3 colours, codes, literals */
#define FONTFL   (*(volatile uint8_t *)0xD000B2)       /* iy+0x32: bit 2 = VPutS in the large font */
#define TEXTFL   (*(volatile uint8_t *)0xD00085)       /* iy+5: bit 3 = textInverse */
#define PENCOL   (*(volatile unsigned *)0xD008D2)
#define PENROW   (*(volatile uint8_t *)0xD008D5)
#define CURCOL   (*(volatile uint8_t *)0xD00596)
#define DRAWFG   (*(volatile uint16_t *)0xD026AC)       /* what VPutMap draws with, even in the large font */
#define DRAWBG   (*(volatile uint16_t *)0xD026AA)
#define KEYEXT   (*(volatile uint8_t *)0xD0058E)        /* keyExtend */

#define BLACK    0x0000
#define WHITE    0xFFFF
#define GREY     0xE71C          /* the OS's popup grey, e0e0e0 (measured) */
#define DARK     0x18E3          /* the OS's highlight, 181c18 */

#define X0       4               /* popup, bottom left, like the OS's, over a tab */
#define LINE     17              /* the large font is 16 rows, 12 wide */

enum { SK_DOWN = 0x01, SK_LEFT = 0x02, SK_RIGHT = 0x03, SK_UP = 0x04,
       SK_ENTER = 0x09, SK_CLEAR = 0x0F, SK_2ND = 0x36 };
enum { K_LPAREN = 0x85, K_CAPA = 0x9A, K_ALPHADOWN = 0x08,
       K_SYMCE = 0x21 };         /* no key and no OS menu makes it */

#define CURAPP   (*(volatile uint8_t *)0xD007E0)        /* cxCurApp: 0x40 = home */
#define MENU     ((volatile uint8_t *)0xD00824)         /* menuCurrent, menuCurrentSub */
#define CELL     (*(volatile unsigned *)0xD0059C)       /* PutMap: VRAM of the last cell's bottom row */
#define PUTS     0x0207C0                               /* _PutS: HL = text, at curRow/curCol */
#define VPUTS    0x020834                               /* _VPutS: HL = text, at penCol/penRow */
#define ERASEEOL 0x020820                               /* _EraseEOL: from curCol to the row's end */

uint8_t os_getcsc(void);
void os_str(unsigned fn, const char *s);
void os_key(uint8_t k);

/* The menus, packed: each is its title, then each item's label and what it
   types, NUL-terminated, then an empty label. What an item types: "" is its
   label in capitals (solve( types SOLVE(); "\1".."\10" opens that menu
   instead; one byte from 0x80 is an OS key, which the OS types, a MathPrint
   template included: nDeriv( 0xC7, fnInt( 0xC8, fMin( 0xF1, fMax( 0xF2
   (MATH's own items, 0x87028). \5 in a label is the OS's > glyph (Lconvert,
   as in >Frac). Labels fit 25 columns with "n:" and a >: the Nspire's
   "Implicit Differentiation" and "Numerical Calculations" do not. */
static const char M[] =
    "Algebra\0"
        "solve(\0\0" "factor(\0\0" "expand(\0\0" "deriv(\0\0"
        "Polynomial Tools\0\1\0" "comDenom(\0\0" "Convert Expression\0\2\0"
        "Trigonometry\0\3\0" "Complex\0\4\0" "Extract\0\5\0" "\0"
    "Polynomial Tools\0"
        "Find Roots of Poly\0NROOTS(\0" "Real Roots of Poly\0POLYROOTS(\0"
        "Complex Roots of Poly\0CPOLYROOTS(\0" "Remainder of Poly\0POLYREMAINDER(\0"
        "Quotient of Poly\0POLYQUOTIENT(\0" "Greatest Common Divisor\0POLYGCD(\0"
        "Coefficients of Poly\0POLYCOEFFS(\0" "Degree of Poly\0POLYDEGREE(\0" "\0"
    "Convert Expression\0"
        "\5ln\0TOLN(\0" "\5logbase\0TOLOGBASE(\0" "\5exp\0TOEXP(\0"
        "\5sin\0TOSIN(\0" "\5cos\0TOCOS(\0" "\0"
    "Trigonometry\0" "tExpand(\0\0" "tCollect(\0\0" "\0"
    "Complex\0" "cSolve(\0\0" "cFactor(\0\0" "cZeros(\0\0" "\0"
    "Extract\0" "left(\0\0" "right(\0\0" "\0"
    "Calculus\0"
        "Derivative\0DERIV(\0" "Derivative at a Point\0DERIVAT(\0" "Integral\0INTEGRAL(\0"
        "Limit\0LIMIT(\0" "Sum\0SUM(\0" "Product\0PRODUCT(\0" "Function Minimum\0FMIN(\0"
        "Function Maximum\0FMAX(\0" "Tangent Line\0TANGENTLINE(\0" "Normal Line\0NORMALLINE(\0"
        "Arc Length\0ARCLEN(\0" "Series\0\7\0" "Implicit Derivative\0IMPDIF(\0"
        "Numeric Calculations\0\10\0" "\0"
    "Series\0"
        "Taylor Polynomial\0TAYLOR(\0" "Generalized Series\0SERIES(\0"
        "Dominant Term\0DOMINANTTERM(\0" "\0"
    "Numeric Calculations\0"
        "Numerical Derivative\0\xC7\0" "Central Diff Quotient\0CENTRALDIFF(\0"
        "Numerical Integral\0\xC8\0" "Numerical Function Min\0\xF1\0"
        "Numerical Function Max\0\xF2\0" "\0"
    "Geometry\0"
        "Distance\0DISTANCE(\0" "Midpoint\0MIDPOINT(\0" "Slope\0SLOPE(\0"
        "Line Through 2 Points\0LINE(\0" "Line: Point, Slope\0LINE(\0" "Parallel Line\0PARALLEL(\0"
        "Perpendicular Line\0PERPENDICULAR(\0" "Perpendicular Bisector\0PERPBISECTOR(\0"
        "Intersection of Lines\0INTERSECT(\0" "Partition Segment\0PARTITION(\0"
        "Triangle\0\12\0" "Circle\0\13\0" "Transformations\0\14\0" "\0"
    "Triangle\0"
        "Area of Polygon\0AREA(\0" "Perimeter of Polygon\0PERIMETER(\0" "Centroid\0CENTROID(\0"
        "Circumcenter\0CIRCUMCENTER(\0" "Orthocenter\0ORTHOCENTER(\0" "Incenter\0INCENTER(\0"
        "Hypotenuse\0HYPOT(\0" "Missing Leg\0LEG(\0" "\0"
    "Circle\0"
        "Circle: Center, Radius\0CIRCLE(\0" "Circle: Center, Point\0CIRCLE(\0"
        "Circle: 3 Points\0CIRCLE(\0" "Center, Radius of Eq\0CIRCLE(\0" "\0"
    "Transformations\0"
        "Reflect Over Line\0REFLECT(\0" "Rotate (Degrees)\0ROTATE(\0" "Dilate\0DILATE(\0";

static const char *next(const char *s)          /* past this string's NUL */
{
    while (*s++)
        ;
    return s;
}

static unsigned len(const char *s)
{
    return next(s) - s - 1;
}

static const char *menu(uint8_t m)              /* menu m's title */
{
    const char *s = M;
    for (; m; m--, s++)
        for (s = next(s); *s; s = next(next(s)))
            ;
    return s;
}

static const char *item(const char *t, unsigned i)   /* item i's label, "" past the end */
{
    for (t = next(t); *t && i; i--)
        t = next(next(t));
    return t;
}

static uint8_t sub(const char *s)               /* the menu item s opens, or 0 */
{
    uint8_t c = *next(s);
    return c < 'A' ? c : 0;
}

/* In asm, below: compiled C took half a second over the popup's pixels, and
   keys pressed meanwhile were lost. fill_px: h rows of 1 + bytes/2 pixels
   of colour c from p. save_px: the h rows of 4*w4 pixels from p (skip bytes
   from one row's end to the next's start) to SAVE at 2 bits a pixel: the
   first three colours met, or 3 for the next of the literals at lit. The home
   screen is two or three colours; the graph of a split screen can be more
   than fit before 0xD13E00, and then save_px says 0. restore_px puts them
   back. */
void fill_px(volatile uint16_t *p, unsigned bytes, unsigned h, uint16_t c);
int save_px(volatile uint16_t *p, unsigned skip, unsigned w4, unsigned h, uint16_t *lit);
void restore_px(volatile uint16_t *p, unsigned skip, unsigned w4, unsigned h, uint16_t *lit);

static void fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c)
{
    fill_px(VRAM + y * 320 + x, 2 * w - 2, h, c);
}

static void text(unsigned x, unsigned y, const char *s, uint16_t fg, uint16_t bg)
{
    PENCOL = x;
    PENROW = y;
    DRAWFG = fg;
    DRAWBG = bg;
    os_str(VPUTS, s);
}

/* GetCSC's scan codes for items 1-9, 0, then A-D: MATH APPS PRGM x⁻¹ */
static const char keys[] = "\x22\x1A\x12\x23\x1B\x13\x24\x1C\x14\x21\x2F\x27\x1F\x2E";

/* Menu t's size: the columns its widest line takes, its items in n. */
static unsigned size(const char *t, unsigned *n)
{
    unsigned cols = len(t), c;
    for (*n = 0, t = next(t); *t; t = next(next(t)), ++*n)
        if ((c = len(t) + 2 + 2 * !!sub(t)) > cols)
            cols = c;
    return cols;
}

#define MENUS 13                 /* in M */
#define CALC  6                  /* Calculus, the tab after Algebra (0) */
#define GEO   9                  /* Geometry, the last tab */
#define ROWS  10                 /* items shown at once; more scroll */

static const uint8_t tops[] = { 0, CALC, GEO };   /* the tabs' menus */

/* The popup, at menu m; returns the command picked (its label), or 0. The
   pixels under every menu's box and tabs are saved once, so moving between
   menus only puts them back before drawing the next. Algebra, Calculus and
   Geometry show the three tabs, the current one black (12 columns each and
   a 4 pixel gap: 312 pixels, as wide as the box); a submenu its own title. */
static const char *popup(uint8_t m)
{
    uint16_t fg = DRAWFG, bg = DRAWBG;
    uint8_t font = FONTFL;
    const char *r = 0, *t, *s;
    unsigned sel = 0, n, i, uc = 0, un = 0;
    for (i = 0; i < MENUS; i++) {
        unsigned c = size(menu(i), &n);
        uc = c > uc ? c : uc;
        un = n > un ? n : un;
    }
    un = un < ROWS ? un : ROWS;
    unsigned w4 = uc * 3 + 2, h = 6 + un * LINE + 20;      /* (12 uc + 8) / 4 */
    volatile uint16_t *p = VRAM + (240 - h) * 320 + X0;
    uint16_t *lit = (uint16_t *)(SAVE + 12 + w4 * h);
    if (!save_px(p, 640 - 8 * w4, w4, h, lit))
        return 0;                /* ponytail: too colourful to save, so no menu */
    FONTFL = font | 4;
    for (;;) {
        unsigned w = size(t = menu(m), &n) * 12 + 8, rows = n < ROWS ? n : ROWS,
                 y0 = 220 - 6 - rows * LINE, all = 1, top = 0, x = X0;
        uint8_t k, tab;                         /* m's tab, 3 for a submenu */
        for (tab = 0; tab < 3 && tops[tab] != m; tab++)
            ;
        fill(X0, y0, w, 220 - y0, BLACK);
        for (i = 0; i < (tab < 3 ? 3 : 1); i++) {   /* the tabs: black, or framed grey */
            const char *u = tab < 3 ? menu(tops[i]) : t;
            unsigned tw = len(u) * 12 + 8, on = u == t;
            fill(x, 220, tw, 20, BLACK);
            if (!on)
                fill(x + 2, 220, tw - 4, 18, GREY);
            text(x + 4, 222, u, on ? WHITE : BLACK, on ? BLACK : GREY);
            x += tw + 4;
        }
        for (;;) {
            if (sel < top || sel >= top + rows) {
                top = sel < top ? sel : sel - rows + 1;
                all = 1;
            }
            if (all)
                fill(X0 + 2, y0 + 2, w - 4, 216 - y0, GREY);
            unsigned y = y0 + 4;
            for (i = top, s = item(t, top); i < top + rows; i++, s = next(next(s)), y += LINE) {
                /* 1-9, 0, A..; ':' an arrow where more are above or below */
                char d[3] = {(char)(i < 9 ? '1' + i : i == 9 ? '0' : 'A' - 10 + i),
                             (char)(i == top && top ? 0x1E : i + 1 == top + rows && i + 1 < n ? 0x1F : ':'), 0};
                text(X0 + 4, y, d, i == sel ? WHITE : BLACK, i == sel ? DARK : GREY);
                if (all) {
                    text(PENCOL, y, s, BLACK, GREY);
                    if (sub(s))
                        text(X0 + w - 16, y, "\5", BLACK, GREY);
                }
            }
            all = 0;
            while (!(k = os_getcsc()))
                ;
            for (i = 0; i < sizeof keys - 1 && keys[i] != k; i++)
                ;
            if (i < sizeof keys - 1) {
                if (i >= n)
                    continue;
                sel = i;
                k = SK_ENTER;
            }
            if (k == SK_UP)
                sel = (sel ? sel : n) - 1;
            else if (k == SK_DOWN)
                sel = sel + 1 < n ? sel + 1 : 0;
            else if (!(k == SK_2ND || (k == SK_RIGHT && tab == 3 && !sub(item(t, sel)))))
                break;
        }
        restore_px(p, 640 - 8 * w4, w4, h, lit);
        s = item(t, sel);
        if (k == SK_ENTER || (k == SK_RIGHT && sub(s))) {
            if (!sub(s)) {
                r = s;
                break;
            }
            m = sub(s);
            sel = 0;
        } else if (tab < 3 && (k == SK_LEFT || k == SK_RIGHT)) {
            m = tops[(tab + (k == SK_RIGHT ? 1 : 2)) % 3];
            sel = 0;
        } else if (tab == 3 && (k == SK_LEFT || k == SK_CLEAR)) {
            uint8_t c = m;              /* up, onto the item that opened it */
            t = menu(m = m < CALC ? 0 : m < GEO ? CALC : GEO);
            for (sel = 0; sub(item(t, sel)) != c; sel++)
                ;
        } else
            break;
    }
    DRAWFG = fg;
    DRAWBG = bg;
    FONTFL = font;
    return r;
}

/* The homescreen hook, at A=1, for ALPHA+DOWN or the SymCE tab's key code
   0x21; B holds the key. Types the command picked, if any. */
void menu_key(unsigned bc)
{
    const char *s;
    uint8_t k = KEYEXT;
    if ((uint8_t)(bc >> 8) == K_ALPHADOWN)
        s = popup(0);
    else {
        if (k > 0x2F)
            return;
        s = item(menu(tops[k >> 4]), k & 15);
        if (!*s)
            return;
        if (sub(s))
            s = popup(sub(s));
    }
    if (s) {
        const char *u = next(s);
        for (u = *u ? u : s; *u; u++) {
            uint8_t c = *u;
            c -= c - 'a' < 26u ? 32 : 0;   /* capitals */
            os_key(c >= 0x80 ? c : c == '(' ? K_LPAREN : K_CAPA + (c - 'A'));
        }
    }
}

/* The SymCE tabs: 2nd MATH (TEST, menu 9) at home gets this table in place of
   the OS's, whose three tabs it copies (OS 5.8.4, 0x86F86): tab count, items
   per tab, title string ids, then each item's key code, prefix first. The
   SymCE tabs' items are all key 0x21, which no key makes, prefixed by the
   item's number, plus 0x10 for Calculus and 0x20 for Geometry; picking one
   closes the menu and hands 0x21 to the home screen with that byte in
   keyExtend, where menu_key does the rest.
   ponytail: a copy, not read out of ROM; os_check already pins the OS. */
static const uint8_t tabs[] = {
    6, 6, 4, 16, 10, 14, 13, 0x54, 0x33, 0x74, 0, 0, 0,
    0xFE, 0x0A, 0xFE, 0x0B, 0xFE, 0x0C, 0xFE, 0x0D, 0xFE, 0x0E, 0xFE, 0x0F,
    0xFE, 0x10, 0xFE, 0x11, 0xFE, 0x12, 0xFE, 0x13,
    0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8,
    0, 9, 0, 10, 0, 11, 0, 12, 0, 13, 0, 14, 0, 15, 0, 16,
    0, K_SYMCE, 1, K_SYMCE, 2, K_SYMCE, 3, K_SYMCE, 4, K_SYMCE,
    5, K_SYMCE, 6, K_SYMCE, 7, K_SYMCE, 8, K_SYMCE, 9, K_SYMCE,
    0x10, K_SYMCE, 0x11, K_SYMCE, 0x12, K_SYMCE, 0x13, K_SYMCE, 0x14, K_SYMCE,
    0x15, K_SYMCE, 0x16, K_SYMCE, 0x17, K_SYMCE, 0x18, K_SYMCE, 0x19, K_SYMCE,
    0x1A, K_SYMCE, 0x1B, K_SYMCE, 0x1C, K_SYMCE, 0x1D, K_SYMCE,
    0x20, K_SYMCE, 0x21, K_SYMCE, 0x22, K_SYMCE, 0x23, K_SYMCE, 0x24, K_SYMCE,
    0x25, K_SYMCE, 0x26, K_SYMCE, 0x27, K_SYMCE, 0x28, K_SYMCE, 0x29, K_SYMCE,
    0x2A, K_SYMCE, 0x2B, K_SYMCE, 0x2C, K_SYMCE,
};

/* The menu hook (mhook.c), every event: 0 leaves it to the OS. A=0 asks for
   the table; A=2 draws tab `index`'s title, A=1 item `index`'s text, each at
   the OS's cursor (the OS has drawn "n:" already). */
const void *menu_tab(uint8_t event, uint8_t index)
{
    if (MENU[0] != 9 || CURAPP != 0x40)
        return 0;
    if (event == 0)
        return tabs;
    if (event == 1 && MENU[1] >= 3) {
        const char *s = item(menu(tops[MENU[1] - 3]), index);
        os_str(PUTS, s);
        os_str(ERASEEOL, s);     /* the OS scrolls by redrawing over longer items */
        if (sub(s)) {
            CURCOL = 24;         /* not 25: PutS there would wrap the cursor */
            os_str(PUTS, "\5");
        }
        return tabs;
    }
    if (event == 2 && index < 5)
        return tabs;             /* every title at 5 */
    if (event == 2) {
        /* The large font's 26 columns do not hold six titles, so the OS
           draws none: all six go in VPutS's small font across the header
           row, the current one white on black, once the OS has drawn the
           space it puts before the last tab (CELL: that space's bottom row,
           so the header's row). VPutS inverts all by itself while the OS has
           textInverse set for the current tab, so that is off meanwhile. */
        /* ponytail: the full names measure 176 px; after CONDITIONS 146 are left */
        static const char names[] = "TEST\0LOGIC\0CONDITIONS\0Alg\0Calc\0Geo";
        unsigned y = ((CELL - 0xD40000) / 2 - 17 * 320) / 320, x = 2, i;
        const char *u = names;
        uint16_t fg = DRAWFG, bg = DRAWBG;
        uint8_t font = FONTFL, tf = TEXTFL;
        fill(0, y, 320, 18, WHITE);
        FONTFL = font & ~4;
        TEXTFL = tf & ~8;
        for (i = 0; i < 6; i++, u = next(u), x = PENCOL + 4) {
            text(x + 2, y + 3, u, BLACK, WHITE);
            if (MENU[1] == i) {
                fill(x, y, PENCOL + 2 - x, 18, BLACK);
                text(x + 2, y + 3, u, WHITE, BLACK);
            }
        }
        TEXTFL = tf;
        FONTFL = font;
        DRAWFG = fg;
        DRAWBG = bg;
        return tabs;
    }
    return 0;
}

/* The entry points, first in the image (src/engine.ld): the homescreen hook
   jumps to +0, the menu hook to +4. */
__asm__(
"	.section	.text._symce_menu,\"ax\",@progbits\n"
"	.globl	_symce_menu\n"
"_symce_menu:\n"
"	jp	_menu_key\n"
"	jp	_menu_tab\n"
);

/* The OS side, register arguments. Each sets iy to the OS's flags, which
   compiled C does not keep there, and keeps ix, which C expects kept. */
__asm__(
"	.section	.text._os_getcsc,\"ax\",@progbits\n"
"	.globl	_os_getcsc\n"
"_os_getcsc:\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	call	0x002014C\n"          /* _GetCSC: A = scan code, 0 if none */
"	pop	ix\n"
"	ret\n"
"	.section	.text._os_str,\"ax\",@progbits\n"
"	.globl	_os_str\n"
"_os_str:\n"                     /* call fn with HL = text */
"	pop	de\n"
"	pop	bc\n"
"	pop	hl\n"
"	push	hl\n"
"	push	bc\n"
"	push	de\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	call	.Ljpbc\n"
"	pop	ix\n"
"	ret\n"
".Ljpbc:\n"
"	push	bc\n"
"	ret\n"
"	.section	.text._os_key,\"ax\",@progbits\n"
"	.globl	_os_key\n"
"_os_key:\n"
"	pop	de\n"
"	pop	hl\n"
"	push	hl\n"
"	push	de\n"
"	ld	a, l\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	ld	hl, (0x0D007CA)\n"      /* cxMain: the home screen's key handler, A = key */
"	call	.Ljphl\n"
"	pop	ix\n"
"	ret\n"
".Ljphl:\n"
"	jp	(hl)\n"
);

/* The pixel loops (see fill_px). Arguments are on the stack, 3 bytes each;
   the spare bytes of an argument's slot are counters, which C allows (the
   slots are the callee's). ix and iy are kept. The app runs from flash, at
   10 cycles an instruction byte (KNOWLEDGE.md, port 0x1005), so the common
   case -- 4 pixels all colour 0, one code byte 0 -- is a few 24-bit compares
   or stores against colour 0 laid out as bytes lo hi lo (de) and hi lo hi
   (bc), kept at SAVE+6 and SAVE+9. */
__asm__(
"	.section	.text._fill_px,\"ax\",@progbits\n"
"	.globl	_fill_px\n"
"_fill_px:\n"                    /* (p, bytes, h, c); bytes > 0 */
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 p, +9 bytes, +12 h, +15 c */
"	ld	hl, (ix + 6)\n"
".Lfrow:\n"
"	ld	a, (ix + 15)\n"
"	ld	(hl), a\n"
"	inc	hl\n"
"	ld	a, (ix + 16)\n"
"	ld	(hl), a\n"
"	dec	hl\n"
"	push	hl\n"
"	push	hl\n"
"	pop	de\n"
"	inc	de\n"
"	inc	de\n"
"	ld	bc, (ix + 9)\n"
"	ldir\n"                       /* the first pixel, copied along the row */
"	pop	hl\n"
"	ld	bc, 640\n"
"	add	hl, bc\n"
"	dec	(ix + 12)\n"
"	jr	nz, .Lfrow\n"
"	pop	ix\n"
"	ret\n"
"	.section	.text._save_px,\"ax\",@progbits\n"
"	.globl	_save_px\n"
"_save_px:\n"                    /* (p, skip, w4, h, lit) -> hl = 0 if it did not fit */
"	push	ix\n"
"	push	iy\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+9 p, then the next code; +12 skip; +15 w4 */
"	ld	iy, (ix + 9)\n"          /* (+16 count, +17 colours); +18 h; +21 lit */
"	ld	hl, (iy + 0)\n"
"	ld	(0x0D0EF00), hl\n"       /* the first pixel, 6 times: all three colours */
"	ld	hl, 0x0D0EF00\n"         /* to start with, and the two patterns */
"	ld	de, 0x0D0EF02\n"
"	ld	bc, 10\n"
"	ldir\n"
"	ld	(ix + 9), de\n"          /* 0xD0EF0C: the codes */
"	ld	de, (0x0D0EF06)\n"
"	ld	bc, (0x0D0EF09)\n"
".Lsrow:\n"
"	ld	a, (ix + 15)\n"
"	ld	(ix + 16), a\n"
".Lsquad:\n"
"	ld	hl, (iy + 0)\n"
"	or	a, a\n"
"	sbc	hl, de\n"
"	jr	nz, .Lsslow\n"
"	ld	hl, (iy + 3)\n"
"	sbc	hl, bc\n"                /* nc after a zero sbc */
"	jr	nz, .Lsslow\n"
"	ld	hl, (iy + 5)\n"
"	sbc	hl, bc\n"
"	jr	nz, .Lsslow\n"
"	xor	a, a\n"
".Lsput:\n"                      /* a: the quad's code byte */
"	ld	hl, (ix + 9)\n"
"	ld	(hl), a\n"
"	inc	hl\n"
"	ld	(ix + 9), hl\n"
"	lea	iy, iy + 8\n"
"	dec	(ix + 16)\n"
"	jr	nz, .Lsquad\n"
"	push	bc\n"
"	ld	bc, (ix + 12)\n"
"	add	iy, bc\n"
"	pop	bc\n"
"	dec	(ix + 18)\n"
"	jr	nz, .Lsrow\n"
"	ld	hl, 1\n"
".Lsret:\n"
"	pop	iy\n"
"	pop	ix\n"
"	ret\n"
".Lsslow:\n"                     /* pixel by pixel: 2 bits each, from the top of c */
"	push	de\n"
"	push	bc\n"
"	ld	b, 4\n"
".Lspx:\n"
"	ld	de, (iy + 0)\n"
"	ld	a, (0x0D0EF00)\n"
"	cp	a, e\n"
"	jr	nz, .Ls1\n"
"	ld	a, (0x0D0EF01)\n"
"	cp	a, d\n"
"	jr	nz, .Ls1\n"
"	srl	c\n"                      /* 0: colour 0 */
"	srl	c\n"
"	jr	.Lsnext\n"
".Ls1:\n"
"	ld	a, (0x0D0EF02)\n"
"	cp	a, e\n"
"	jr	nz, .Ls2\n"
"	ld	a, (0x0D0EF03)\n"
"	cp	a, d\n"
"	jr	nz, .Ls2\n"
".Lsk1:\n"
"	scf\n"
"	rr	c\n"
"	srl	c\n"
"	jr	.Lsnext\n"
".Ls2:\n"
"	ld	a, (0x0D0EF04)\n"
"	cp	a, e\n"
"	jr	nz, .Lsnew\n"
"	ld	a, (0x0D0EF05)\n"
"	cp	a, d\n"
"	jr	nz, .Lsnew\n"
".Lsk2:\n"
"	srl	c\n"
"	scf\n"
"	rr	c\n"
".Lsnext:\n"
"	lea	iy, iy + 2\n"
"	djnz	.Lspx\n"
"	lea	iy, iy - 8\n"
"	ld	a, c\n"
"	pop	bc\n"
"	pop	de\n"
"	jp	.Lsput\n"                /* out of jr range; the blob is relocated */
".Lsnew:\n"                      /* a new colour: to slot 1 or 2 while free */
"	ld	a, (ix + 17)\n"
"	cp	a, 2\n"
"	jr	z, .Lslit\n"
"	inc	(ix + 17)\n"
"	or	a, a\n"
"	jr	nz, .Lsslot2\n"
"	ld	a, e\n"
"	ld	(0x0D0EF02), a\n"
"	ld	a, d\n"
"	ld	(0x0D0EF03), a\n"
"	jr	.Lsk1\n"
".Lsslot2:\n"
"	ld	a, e\n"
"	ld	(0x0D0EF04), a\n"
"	ld	a, d\n"
"	ld	(0x0D0EF05), a\n"
"	jr	.Lsk2\n"
".Lslit:\n"                      /* 3: the next literal, if one more fits */
"	ld	hl, 0x0D13DFE\n"
"	push	de\n"
"	ld	de, (ix + 21)\n"
"	or	a, a\n"
"	sbc	hl, de\n"
"	ex	de, hl\n"
"	pop	de\n"
"	jr	c, .Lsfull\n"
"	ld	(hl), e\n"
"	inc	hl\n"
"	ld	(hl), d\n"
"	inc	hl\n"
"	ld	(ix + 21), hl\n"
"	scf\n"
"	rr	c\n"
"	scf\n"
"	rr	c\n"
"	jr	.Lsnext\n"
".Lsfull:\n"
"	pop	bc\n"
"	pop	de\n"
"	or	a, a\n"
"	sbc	hl, hl\n"
"	jp	.Lsret\n"
"	.section	.text._restore_px,\"ax\",@progbits\n"
"	.globl	_restore_px\n"
"_restore_px:\n"                 /* (p, skip, w4, h, lit) */
"	push	ix\n"
"	push	iy\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"
"	ld	iy, (ix + 9)\n"
"	ld	de, (0x0D0EF06)\n"
"	ld	bc, (0x0D0EF09)\n"
"	ld	hl, 0x0D0EF0C\n"
".Lrrow:\n"
"	ld	a, (ix + 15)\n"
"	ld	(ix + 16), a\n"
".Lrquad:\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	or	a, a\n"
"	jr	nz, .Lrslow\n"
"	ld	(iy + 0), de\n"          /* 8 bytes of colour 0 in three 3-byte stores */
"	ld	(iy + 3), bc\n"
"	ld	(iy + 5), bc\n"
".Lrnext:\n"
"	lea	iy, iy + 8\n"
"	dec	(ix + 16)\n"
"	jr	nz, .Lrquad\n"
"	push	bc\n"
"	ld	bc, (ix + 12)\n"
"	add	iy, bc\n"
"	pop	bc\n"
"	dec	(ix + 18)\n"
"	jr	nz, .Lrrow\n"
"	pop	iy\n"
"	pop	ix\n"
"	ret\n"
".Lrslow:\n"
"	push	hl\n"
"	push	de\n"
"	push	bc\n"
"	ld	c, a\n"
"	ld	b, 4\n"
"	ld	hl, (ix + 21)\n"         /* the next literal */
".Lrpx:\n"
"	srl	c\n"
"	jr	c, .Lrodd\n"
"	srl	c\n"
"	ld	de, (0x0D0EF00)\n"
"	jr	nc, .Lrput\n"
"	ld	de, (0x0D0EF04)\n"
"	jr	.Lrput\n"
".Lrodd:\n"
"	srl	c\n"
"	ld	de, (0x0D0EF02)\n"
"	jr	nc, .Lrput\n"
"	ld	e, (hl)\n"
"	inc	hl\n"
"	ld	d, (hl)\n"
"	inc	hl\n"
".Lrput:\n"
"	ld	(iy + 0), e\n"
"	ld	(iy + 1), d\n"
"	lea	iy, iy + 2\n"
"	djnz	.Lrpx\n"
"	ld	(ix + 21), hl\n"
"	lea	iy, iy - 8\n"
"	pop	bc\n"
"	pop	de\n"
"	pop	hl\n"
"	jr	.Lrnext\n"
);
