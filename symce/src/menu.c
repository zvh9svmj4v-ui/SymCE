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
 *     B:Series >  C:Implicit Derivative  D:Numeric Calculations >  E:Differential Equation
 *
 *     1:Distance  2:Midpoint ... 0:Partition Segment  A:Triangle >
 *     B:Circle >  C:Transformations >
 *
 * and types the chosen command at the cursor as lowercase tokens: factor( is
 * BB B5 BB B0 BB B2 BB C4 BB BF BB C2 10 (hand-spelled capitals are variables).
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
 * - The command is typed by handing each letter's token (keyExtend 0xE2+n
 *   through cxMain at A=0xFC) to the home screen's own key handler: the hook
 *   sees each one at A=1 as if typed, and the OS inserts it and draws it,
 *   MathPrint included.
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

#define PSHADOW1 ((volatile uint8_t *)0xD031F6)         /* pixelShadow: glyph bits of VPutS, 40 bytes a row */
#define PSHADOW2 ((volatile uint8_t *)0xD07396)

#define BLACK    0x0000
#define WHITE    0xFFFF
#define GREY     0xE71C          /* the OS's popup grey, e0e0e0 (measured) */
#define DARK     0x18E3          /* the OS's highlight, 181c18 */

#define X0       4               /* popup, bottom left, like the OS's, over a tab */
#define LINE     17              /* the large font is 16 rows, 12 wide */

enum { SK_DOWN = 0x01, SK_LEFT = 0x02, SK_RIGHT = 0x03, SK_UP = 0x04,
       SK_ENTER = 0x09, SK_PLUS = 0x0A, SK_CLEAR = 0x0F, SK_2ND = 0x36 };
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
/* A key of the extended page: keyExtend = ext, then cxMain with A = 0xFC (kExtendEcho2). */
static void os_key2(uint8_t k, uint8_t ext)
{
    KEYEXT = ext;
    os_key(k);
}

/* The menus, packed: each is its title, then each item's label and what it
   types, NUL-terminated, then an empty label. What an item types: "" is its
   label (solve( types the lowercase tokens s o l v e and a bracket; "\1".."\10" opens that menu
   instead; one byte from 0x80 is an OS key, which the OS types, a MathPrint
   template included: nDeriv( 0xC7, fnInt( 0xC8, fMin( 0xF1, fMax( 0xF2
   (MATH's own items, 0x87028). \5 in a label is the OS's > glyph (Lconvert,
   as in >Frac). Labels fit 25 columns with "n:" and a >: the Nspire's
   "Implicit Differentiation" and "Numerical Calculations" do not. */
static const char M[] =
    "Algebra\0"
        "solve(\0\0" "factor(\0\0" "expand(\0\0" "deriv(\0\0"
        "Polynomial Tools\0\1\0" "comDenom(\0\0" "Convert Expression\0\2\0"
        "Trigonometry\0\3\0" "Complex\0\4\0" "Extract\0\5\0" "Matrix\0\15\0" "steps(\0\0" "Graph Ans\0\x7f\0" "\0"
    "Polynomial Tools\0"
        "Find Roots of Poly\0NROOTS(\0" "Real Roots of Poly\0POLYROOTS(\0"
        "Complex Roots of Poly\0CPOLYROOTS(\0" "Remainder of Poly\0POLYREMAINDER(\0"
        "Quotient of Poly\0POLYQUOTIENT(\0" "Greatest Common Divisor\0POLYGCD(\0"
        "Coefficients of Poly\0POLYCOEFFS(\0" "Degree of Poly\0POLYDEGREE(\0"
        "Partial Fractions\0PARTFRAC(\0" "\0"
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
        "Numeric Calculations\0\10\0" "Differential Equation\0DESOLVE(\0" "\0"
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
        "Reflect Over Line\0REFLECT(\0" "Rotate (Degrees)\0ROTATE(\0" "Dilate\0DILATE(\0" "\0"
    "Matrix\0"
        "Determinant\0MDET(\0" "Inverse\0MINV(\0" "Row Reduced Echelon\0MRREF(\0"
        "Transpose\0MTRANS(\0" "Dot Product\0DOTP(\0" "Cross Product\0CROSSP(\0"
        "Norm\0NORM(\0";

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

/* What + (Catalog Help) shows for each command: its name as typed (the OS key
   byte for the Numerical items), then syntax, what it does and an example,
   each NUL-terminated, '\n' between lines (26 columns of the large font fit).
   [ ] (glyphs 0xC1 and ']') mark an optional argument. Every example was run through the engine
   (the examples' answers are what it prints); \x10( is the root sign, \xD7 the
   imaginary i. A line past 9 in all would not fit the screen. */
static const char H[] =
    "SOLVE(\0" "SOLVE(eq,var)\nSOLVE({eq,..},{var,..})\0" "Exact solutions of an\nequation or inequality\n(< > \x17 \x19 \x18); lists of\nequations make a system.\0"
        "SOLVE({X+Y=3,X=2Y},{X,Y})\n-> X=2 and Y=1\0"
    "FACTOR(\0" "FACTOR(expr)\0" "Factors a polynomial or an\ninteger.\0"
        "FACTOR(X^2-4)\n-> (X-2)(X+2)\0"
    "EXPAND(\0" "EXPAND(expr)\0" "Multiplies out products\nand powers.\0"
        "EXPAND((X+1)^3)\n-> X^3+3X^2+3X+1\0"
    "DERIV(\0" "DERIV(expr\xC1,V\xC1,n]])\0" "Derivative of expr in V\n(default X), n times.\0"
        "DERIV(X^3,X,2)\n-> 6X\0"
    "STEPS(\0" "STEPS(command)\0" "Shows the worked steps of\nmost commands after the\nanswer.\0"
        "STEPS(SOLVE(2X+3=7,X))\n-> 2 steps, X=2\0"
    "COMDENOM(\0" "COMDENOM(expr)\0" "Puts a sum of fractions\nover one denominator.\0"
        "COMDENOM(1/(X+1)+1/(X-1))\n-> 2X/(X^2-1)\0"
    "NROOTS(\0" "NROOTS(poly\xC1,V])\0" "Decimal roots of a\npolynomial, complex too.\0"
        "NROOTS(X^2-4)\n-> {-2,2}\0"
    "POLYROOTS(\0" "POLYROOTS(poly\xC1,V])\0" "Exact real roots of a\npolynomial, as a list.\0"
        "POLYROOTS(X^2-2)\n-> {-\x10(2),\x10(2)}\0"
    "CPOLYROOTS(\0" "CPOLYROOTS(poly\xC1,V])\0" "All exact roots, complex\nones too, as a list.\0"
        "CPOLYROOTS(X^2+1)\n-> {-\xD7,\xD7}\0"
    "POLYREMAINDER(\0" "POLYREMAINDER(p,q)\0" "Remainder of p divided\nby q.\0"
        "POLYREMAINDER(X^2,X+1)\n-> 1\0"
    "POLYQUOTIENT(\0" "POLYQUOTIENT(p,q)\0" "Whole part of p divided\nby q.\0"
        "POLYQUOTIENT(X^2,2X+1)\n-> X/2-1/4\0"
    "POLYGCD(\0" "POLYGCD(p,q)\0" "Greatest common divisor of\ntwo polynomials.\0"
        "POLYGCD(4X+4,6X+6)\n-> 2X+2\0"
    "POLYCOEFFS(\0" "POLYCOEFFS(poly\xC1,V])\0" "Coefficients, highest\npower first, as a list.\0"
        "POLYCOEFFS(AX^2+BX+C)\n-> {A,B,C}\0"
    "POLYDEGREE(\0" "POLYDEGREE(poly\xC1,V])\0" "Highest power of V.\0"
        "POLYDEGREE(XY^2,Y)\n-> 2\0"
    "PARTFRAC(\0" "PARTFRAC(expr\xC1,V])\0" "Splits a fraction into\nsimple fractions.\0"
        "PARTFRAC(1/(X^2-1))\n-> 1/(2(X-1))-\n   1/(2(X+1))\0"
    "TOLN(\0" "TOLN(expr)\0" "Rewrites logs as ln.\0"
        "TOLN(log(X))\n-> ln(X)/ln(10)\0"
    "TOLOGBASE(\0" "TOLOGBASE(expr,base)\0" "Rewrites ln and log in\nthe given base.\0"
        "TOLOGBASE(ln(X),5)\n-> logBASE(X,5)/\n   logBASE(e,5)\0"
    "TOEXP(\0" "TOEXP(expr)\0" "Rewrites trig as e^( ).\nNeeds RADIAN.\0"
        "TOEXP(cos(X))\n-> (e^(X\xD7)+e^(-X\xD7))/2\0"
    "TOSIN(\0" "TOSIN(expr)\0" "Trades a cos square for\na sin square.\0"
        "TOSIN(cos(X)^2)\n-> 1-sin(X)^2\0"
    "TOCOS(\0" "TOCOS(expr)\0" "Trades a sin square for\na cos square.\0"
        "TOCOS(sin(X)^2)\n-> 1-cos(X)^2\0"
    "TEXPAND(\0" "TEXPAND(expr)\0" "Expands sin and cos of\nsums and multiples.\0"
        "TEXPAND(sin(2X))\n-> 2sin(X)cos(X)\0"
    "TCOLLECT(\0" "TCOLLECT(expr)\0" "Collects sin and cos\nproducts into one angle.\0"
        "TCOLLECT(sin(X)cos(X))\n-> sin(2X)/2\0"
    "CSOLVE(\0" "CSOLVE(eq,var)\nCSOLVE({eq,..},{var,..})\0" "Like SOLVE, with complex\nsolutions too.\0"
        "CSOLVE(X^2+2X+5=0,X)\n-> X=-1-2\xD7 or X=-1+2\xD7\0"
    "CFACTOR(\0" "CFACTOR(expr)\0" "Factors over the complex\nnumbers.\0"
        "CFACTOR(X^2+1)\n-> (X-\xD7)(X+\xD7)\0"
    "CZEROS(\0" "CZEROS(expr\xC1,V])\0" "Complex zeros of expr, as\na list.\0"
        "CZEROS(X^2+2X+5)\n-> {-1-2\xD7,-1+2\xD7}\0"
    "LEFT(\0" "LEFT(equation)\0" "The left side of an\nequation.\0"
        "LEFT(X^2+1=3X)\n-> X^2+1\0"
    "RIGHT(\0" "RIGHT(equation)\0" "The right side of an\nequation.\0"
        "RIGHT(X^2+1=3X)\n-> 3X\0"
    "DERIVAT(\0" "DERIVAT(expr,V,pt\xC1,n])\0" "Derivative at a point.\0"
        "DERIVAT(X^3,X,2)\n-> 12\0"
    "INTEGRAL(\0" "INTEGRAL(expr,V\xC1,a,b])\0" "Antiderivative, or the\ndefinite integral a to b.\0"
        "INTEGRAL(X^2,X,0,3)\n-> 9\0"
    "LIMIT(\0" "LIMIT(expr,V,pt\xC1,dir])\0" "Limit as V nears pt; dir\nis 1 from the right, -1\nfrom the left.\0"
        "LIMIT(sin(X)/X,X,0)\n-> 1\0"
    "SUM(\0" "SUM(expr,V,lo,hi)\0" "Sum of expr for V from\nlo to hi.\0"
        "SUM(X^2,X,1,3)\n-> 14\0"
    "PRODUCT(\0" "PRODUCT(expr,V,lo,hi)\0" "Product of expr for V\nfrom lo to hi.\0"
        "PRODUCT(X,X,1,5)\n-> 120\0"
    "FMIN(\0" "FMIN(expr,V\xC1,lo,hi])\0" "Where expr is least,\noptionally within lo, hi.\0"
        "FMIN(X^4-2X^2,X)\n-> X=-1 or X=1\0"
    "FMAX(\0" "FMAX(expr,V\xC1,lo,hi])\0" "Where expr is greatest,\noptionally within lo, hi.\0"
        "FMAX(X^3-3X,X,-3,3)\n-> X=3\0"
    "TANGENTLINE(\0" "TANGENTLINE(expr,V,pt)\0" "The tangent line to expr\nat pt.\0"
        "TANGENTLINE(X^2,X,1)\n-> 2X-1\0"
    "NORMALLINE(\0" "NORMALLINE(expr,V,pt)\0" "The normal line to expr\nat pt.\0"
        "NORMALLINE(X^2,X,1)\n-> 3/2-X/2\0"
    "ARCLEN(\0" "ARCLEN(expr,V,a,b)\0" "Arc length of expr from\na to b.\0"
        "ARCLEN(3X,X,0,1)\n-> \x10(10)\0"
    "TAYLOR(\0" "TAYLOR(expr,V,n\xC1,pt])\0" "Taylor polynomial of order\nn about pt (default 0).\0"
        "TAYLOR(e^(X),X,3)\n-> X^3/6+X^2/2+X+1\0"
    "SERIES(\0" "SERIES(expr,V,n\xC1,pt])\0" "Like TAYLOR, but a pole at\npt gives negative powers.\0"
        "SERIES(1/sin(X),X,3)\n-> 7X^3/360+X/6+1/X\0"
    "DOMINANTTERM(\0" "DOMINANTTERM(expr,V\xC1,pt])\0" "The leading term at pt\n(1E99 for infinity).\0"
        "DOMINANTTERM(sin(X)-X,X)\n-> -X^3/6\0"
    "DESOLVE(\0" "DESOLVE(eq,X,Y)\0" "Solves Y'=f or Y''=f.\nAdd ICs after and (2nd\nMATH LOGIC); ' is 2nd\nANGLE 2. Constants C, D.\0"
        "DESOLVE(Y'=2Y,X,Y)\n-> Y=Ce^(2X)\0"
    "IMPDIF(\0" "IMPDIF(eq,V,W\xC1,n])\0" "dW/dV from an equation\nin V and W, n times.\0"
        "IMPDIF(X^2+Y^2=1,X,Y)\n-> -X/Y\0"
    "CENTRALDIFF(\0" "CENTRALDIFF(expr,V,h)\0" "(f(V+h)-f(V-h))/(2h),\nexact, any step h.\0"
        "CENTRALDIFF(X^3,X,H)\n-> H^2+3X^2\0"
    "\xC7\0" "nDeriv(expr,V,pt\xC1,h])\0" "The calculator's own\nnumerical derivative.\0"
        "nDeriv(X^3,X,2)\0"
    "\xC8\0" "fnInt(expr,V,a,b\xC1,tol])\0" "The calculator's own\nnumerical integral.\0"
        "fnInt(X^2,X,0,1)\0"
    "\xF1\0" "fMin(expr,V,a,b\xC1,tol])\0" "The calculator's own\nnumerical minimum.\0"
        "fMin(X^2-4X,X,0,5)\0"
    "\xF2\0" "fMax(expr,V,a,b\xC1,tol])\0" "The calculator's own\nnumerical maximum.\0"
        "fMax(4X-X^2,X,0,5)\0"
    "DISTANCE(\0" "DISTANCE(p1,p2) or\nDISTANCE(point,line)\0" "Distance between two\npoints, or point to line.\0"
        "DISTANCE((1,1),(4,5))\n-> 5\0"
    "MIDPOINT(\0" "MIDPOINT(p1,p2)\0" "The point halfway\nbetween two points.\0"
        "MIDPOINT((1,2),(4,7))\n-> (5/2,9/2)\0"
    "SLOPE(\0" "SLOPE(p1,p2) or\nSLOPE(line)\0" "Slope between two points,\nor of a line.\0"
        "SLOPE((1,2),(3,8))\n-> 3\0"
    "LINE(\0" "LINE(p1,p2) or\nLINE(point,slope)\0" "Line through two points,\nor through a point with\na slope.\0"
        "LINE((0,1),(2,5))\n-> Y=2X+1\0"
    "PARALLEL(\0" "PARALLEL(point,line)\0" "Line through the point,\nparallel to the line.\0"
        "PARALLEL((1,2),Y=3X+1)\n-> Y=3X-1\0"
    "PERPENDICULAR(\0" "PERPENDICULAR(pt,line)\0" "Line through the point,\nperpendicular to the line.\0"
        "PERPENDICULAR((1,2),Y=X)\n-> Y=3-X\0"
    "PERPBISECTOR(\0" "PERPBISECTOR(p1,p2)\0" "Perpendicular bisector of\nthe segment p1 p2.\0"
        "PERPBISECTOR((0,0),(4,2))\n-> Y=5-2X\0"
    "INTERSECT(\0" "INTERSECT(line,line)\0" "Where two lines cross.\0"
        "INTERSECT(2X+3Y=7,X-Y=1)\n-> (2,1)\0"
    "PARTITION(\0" "PARTITION(p1,p2,m,n)\0" "The point dividing p1 p2\nin the ratio m:n.\0"
        "PARTITION((0,0),(5,5),2,3)\n-> (2,2)\0"
    "AREA(\0" "AREA(p1,p2,p3\xC1,...])\0" "Area of a polygon (3 to 6\nvertices, in order).\0"
        "AREA((0,0),(4,0),(0,3))\n-> 6\0"
    "PERIMETER(\0" "PERIMETER(p1,p2,p3\xC1,...])\0" "Perimeter of a polygon\n(3 to 6 vertices).\0"
        "PERIMETER((0,0),(3,0),\n(0,4))\n-> 12\0"
    "CENTROID(\0" "CENTROID(p1,p2,p3)\0" "Centre of gravity of a\ntriangle.\0"
        "CENTROID((0,0),(3,0),\n(0,3))\n-> (1,1)\0"
    "CIRCUMCENTER(\0" "CIRCUMCENTER(p1,p2,p3)\0" "Centre of the circle\nthrough three points.\0"
        "CIRCUMCENTER((0,0),(4,0),\n(0,6))\n-> (2,3)\0"
    "ORTHOCENTER(\0" "ORTHOCENTER(p1,p2,p3)\0" "Where the altitudes of a\ntriangle meet.\0"
        "ORTHOCENTER((0,0),(4,0),\n(1,3))\n-> (1,1)\0"
    "INCENTER(\0" "INCENTER(p1,p2,p3)\0" "Centre of the circle\ninside a triangle.\0"
        "INCENTER((0,0),(1,0),\n(0,1))\n-> (1-\x10(2)/2,1-\x10(2)/2)\0"
    "HYPOT(\0" "HYPOT(leg,leg)\0" "Hypotenuse of a right\ntriangle.\0"
        "HYPOT(3,4)\n-> 5\0"
    "LEG(\0" "LEG(hypotenuse,leg)\0" "The missing leg of a right\ntriangle.\0"
        "LEG(5,3)\n-> 4\0"
    "CIRCLE(\0" "CIRCLE(centre,radius)\nCIRCLE(p1,p2,p3)\nCIRCLE(equation)\0"
        "Circle through the givens,\nor an equation's centre\nand radius.\0"
        "CIRCLE((1,-2),3)\n-> (X-1)^2+(Y+2)^2=9\0"
    "REFLECT(\0" "REFLECT(point,line)\0" "Mirror image of a point in\na line.\0"
        "REFLECT((1,2),Y=X)\n-> (2,1)\0"
    "ROTATE(\0" "ROTATE(pt,deg\xC1,centre])\0" "Rotates a point anti-\nclockwise by degrees.\0"
        "ROTATE((1,0),90)\n-> (0,1)\0"
    "DILATE(\0" "DILATE(pt,factor\xC1,ctr])\0" "Scales a point from a\ncentre (default 0,0).\0"
        "DILATE((2,3),1/2,(1,1))\n-> (3/2,2)\0"
    "MDET(\0" "MDET(matrix)\0" "Determinant of a square\nmatrix, up to 4x4. Type\n[ ] with 2nd x and 2nd -.\0"
        "MDET([[A,1][2,A]])\n-> A^2-2\0"
    "MINV(\0" "MINV(matrix)\0" "Inverse of a square matrix,\nup to 4x4.\0"
        "MINV([[1,2][3,4]])\n-> [[-2,1][3/2,-1/2]]\0"
    "MRREF(\0" "MRREF(matrix)\0" "Row reduced echelon form,\nup to 20 entries.\0"
        "MRREF([[1,2,3][4,5,6]])\n-> [[1,0,-1][0,1,2]]\0"
    "MTRANS(\0" "MTRANS(matrix)\0" "Swaps the rows and\ncolumns.\0"
        "MTRANS([[1,2][3,4]])\n-> [[1,3][2,4]]\0"
    "DOTP(\0" "DOTP(vec,vec)\0" "Dot product of two vectors\nof the same length.\0"
        "DOTP([1,2,3],[4,5,6])\n-> 32\0"
    "CROSSP(\0" "CROSSP(vec,vec)\0" "Cross product of two\n3-vectors.\0"
        "CROSSP([1,0,0],[0,1,0])\n-> [0,0,1]\0"
    "NORM(\0" "NORM(vec)\0" "Length of a vector:\nthe root of its squares.\0"
        "NORM([3,4])\n-> 5\0";

/* In asm, below: compiled C took half a second over the popup's pixels, and
   keys pressed meanwhile were lost. fill_px: h rows of 1 + bytes/2 pixels
   of colour c from p. save_px: the h rows of 4*w4 pixels from p (skip bytes
   from one row's end to the next's start) to SAVE at 2 bits a pixel: the
   first three colours met, or 3 for the colour's one-byte index in a table at lit (+0 count, +1 up to 16
   colours, +33 the next index byte's address, indices from +36; the home screen with rainbow parentheses is
   white, black, blue and six more, so a byte instead of a 16-bit literal holds twice the pixels). When the
   indices pass 0xD13D5E (colTab, fhook.c, lives above) or a 17th colour turns up, the pixel is saved as 3
   with no index and restore_px gives black (lossy; MathPrint's paren_pass recolours). So save_px always
   returns 1. lit..0xD13D5E is 2270 pixels for steps_show, whose log (engine.c STOFF, 0xD13800) it copies away first. */
void fill_px(volatile uint16_t *p, unsigned bytes, unsigned h, uint16_t c);
int save_px(volatile uint16_t *p, unsigned skip, unsigned w4, unsigned h, uint16_t *lit);
void restore_px(volatile uint16_t *p, unsigned skip, unsigned w4, unsigned h, uint16_t *lit);
void sync_px(volatile uint16_t *p, volatile uint8_t *s1, volatile uint8_t *s2, unsigned rows);

static void fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c)
{
    if (w < 2) {                        /* fill_px's byte count 2w-2 is 0 for one pixel */
        for (; h; h--, y++)
            VRAM[y * 320 + x] = c;
        return;
    }
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

#define MENUS 14                 /* in M */
#define CALC  6                  /* Calculus, the tab after Algebra (0) */
#define GEO   9                  /* Geometry, the last tab */
#define ROWS  10                 /* items shown at once; more scroll */

static const uint8_t tops[] = { 0, CALC, GEO };   /* the tabs' menus */

/* The popup, at menu m; returns the command picked (its label), or 0. The
   pixels under every menu's box and tabs are saved once, so moving between
   menus only puts them back before drawing the next. Algebra, Calculus and
   Geometry show the three tabs, the current one black (12 columns each and
   a 4 pixel gap: 312 pixels, as wide as the box); a submenu its own title. */
static const char *hfind(const char *c);
static uint8_t help(const char *e);

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
            else if (k == SK_PLUS) {            /* help, for a command that has an entry */
                s = item(t, sel);
                if (!sub(s) && hfind(*next(s) ? next(s) : s))
                    break;
            } else if (!(k == SK_2ND || (k == SK_RIGHT && tab == 3 && !sub(item(t, sel)))))
                break;
        }
        restore_px(p, 640 - 8 * w4, w4, h, lit);
        s = item(t, sel);
        if (k == SK_PLUS) {             /* the help screen; CLEAR comes back to this item */
            uint8_t e = help(hfind(*next(s) ? next(s) : s));
            if (e == 5)
                r = s;
            if (e == 1 && save_px(p, 640 - 8 * w4, w4, h, lit))
                continue;
            break;
        }
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
            t = menu(m = m == 13 || m < CALC ? 0 : m < GEO ? CALC : GEO);
            for (sel = 0; sub(item(t, sel)) != c; sel++)
                ;
        } else
            break;
    }
    /* VPutS also records its glyphs in the OS's 1-bit pixel shadows (bit x of
       row y-30 is set where pixel (x, y) is not white: *measured*, exact on
       the home screen), and a later full-screen redraw of home (a menu,
       MODE ...) replays them: the popup's text would come back over it. The
       screen under the popup is back, so rebuild its bits from that (whole rows,
       in asm: C took longer than the keys that follow wait). */
    sync_px(VRAM + (240 - h) * 320, PSHADOW1 + (240 - h - 30) * 40, PSHADOW2 + (240 - h - 30) * 40, h);
    DRAWFG = fg;
    DRAWBG = bg;
    FONTFL = font;
    return r;
}

/* The homescreen hook, at A=1, for ALPHA+DOWN or the SymCE tab's key code
   0x21; B holds the key. Types the command picked, if any. */
uint8_t menu_key(unsigned bc)
{
    const char *s;
    uint8_t k = KEYEXT;
    if ((uint8_t)(bc >> 8) == K_ALPHADOWN)
        s = popup(0);
    else {
        if (k > 0x2F)
            return 0;
        s = item(menu(tops[k >> 4]), k & 15);
        if (!*s)
            return 0;
        if (sub(s))
            s = popup(sub(s));
    }
    if (s) {
        const char *u = next(s);
        if (*u == 0x7f)
            return 1;                  /* Graph Ans: the hook does it (main.asm graphAns) */
        for (u = *u ? u : s; *u; u++) {
            uint8_t c = *u;
            c += c - 'A' < 26u ? 32 : 0;   /* lowercase */
            if (c >= 0x80 || c == '(')
                os_key(c >= 0x80 ? c : K_LPAREN);
            else
                os_key2(0xFC, 0xE2 + (c - 'a'));   /* a lowercase token (BB xx): only menus type commands, so typing S O L V E by hand is variables */
        }
    }
    return 0;
}

/* + on a SymCE tab's item: its entry in H, for the screen below. */
static const char *hfind(const char *c)
{
    const char *h = H;
    for (; *h; h = next(next(next(next(h))))) {
        const char *a = h, *b = c;
        while (*a && ((uint8_t)*a == (uint8_t)*b || (uint8_t)*a == (uint8_t)*b - 32u * ((uint8_t)*b - 'a' < 26u)))
            a++, b++;
        if (!*a && !*b)
            return h;
    }
    return 0;
}

static unsigned hsec(unsigned y, const char *s)   /* s's lines from row y; the row after */
{
    do {
        char b[28];
        unsigned i = 0;
        for (; *s && *s != '\n' && i < 27; i++, s++)
            b[i] = *s;
        b[i] = 0;
        text(4, y, b, BLACK, WHITE);
        y += 16;
        s += *s == '\n';
    } while (*s);
    return y;
}

/* A screen like the OS's Catalog Help for the entry e of H, over everything
   below the status bar, the pixels under it saved like the popup's and put
   back on the way out. ENTER returns 5 (kEnter: the OS picks the item, which
   types the command), 2nd+MODE 0x40 (kQuit), CLEAR or ALPHA 1: back to the
   menu as it was. Too colourful to save: 1, and nothing is drawn. */
static uint8_t help(const char *e)
{
    uint16_t fg = DRAWFG, bg = DRAWBG;
    uint8_t font = FONTFL, tf = TEXTFL, r = 0, k;
    uint16_t *lit = (uint16_t *)(SAVE + 12 + 80 * 210);
    volatile uint16_t *p = VRAM + 30 * 320;
    unsigned y;
    if (!save_px(p, 0, 80, 210, lit))
        return 1;
    FONTFL = font | 4;
    TEXTFL = tf & ~8;
    fill(0, 30, 320, 210, WHITE);
    fill(0, 30, 320, 18, BLACK);
    text(88, 32, "CATALOG HELP", WHITE, BLACK);
    e = next(e);
    y = hsec(52, e);
    for (k = 0; k < 2; k++) {
        fill(4, y + 2, 312, 1, BLACK);
        e = next(e);
        y = hsec(y + 6, e);
    }
    fill(0, 220, 320, 20, BLACK);
    text(4, 222, "ENTER:paste CLEAR:back", WHITE, BLACK);
    while (!r) {
        while (!(k = os_getcsc()))
            ;
        if (k == SK_ENTER)
            r = 5;
        else if (k == SK_CLEAR || k == 0x30)
            r = 1;
        else if (k == SK_2ND) {
            while (!(k = os_getcsc()))
                ;
            r = k == 0x37 ? 0x40 : 0;       /* skMode */
        }
    }
    restore_px(p, 0, 80, 210, lit);
    sync_px(p, PSHADOW1, PSHADOW2, 210);
    DRAWFG = fg;
    DRAWBG = bg;
    FONTFL = font;
    TEXTFL = tf;
    return r;
}

/* ---- steps( : the worked solution, over everything below the status bar, after the answer ----
   engine.c's step log (STOFF into engineWork: 0xD13800, past the pool; header comment there) holds
   the entry, then each step as a rule number, its parameter and its expression, as tokens, then
   the answer. hook.c's .Lshow calls this after the OS drew the answer; the pixels under the screen
   are saved like help()'s and put back, so the home screen is as it was.
   The screen is a list of rows between a header and a footer bar: the problem, then for each step a
   divider, its title with a number badge, a grey "why", and the maths in 2D (a stacked fraction, a
   raised power, a root with its bar: s_mx), and last the answer in a green box. UP and DOWN scroll
   a row at a time (the rows have different heights); CLEAR, ENTER, ALPHA or 2nd leave. For a step
   that does the same to both sides (SUB ADD MUL DIV on an equation) the maths is two lines: the
   equation before with the move in red, then the result, the '=' of both under each other.
   The rule texts follow engine.c's SR_ enum, each [=]Title[\2Why]: % is the parameter, %1..%9 its
   comma-separated pieces, \1 the square glyph; a leading = shows the expression after "= " (a value,
   a derivative) rather than as it is (an equation). Words are in the small font, wrapped by the
   pixel width of each glyph (ROM 0xA3CA9, byte 0 of each 25-byte record), maths in the large. */
#define STLOG ((volatile uint8_t *)0xD13800)
static const char R[] =
    "Subtract % from both sides\2Taking % from both sides cancels it on the left. The same on both sides keeps it true.\0"
    "Add % to both sides\2Adding % to both sides cancels the subtraction. The same on both sides keeps it true.\0"
    "Divide both sides by %\2Dividing both sides by % leaves the unknown alone. The same on both sides keeps it true.\0"
    "Multiply both sides by %\002Multiplying by % clears the fraction (or the minus). Both sides, so it stays true.\0"
    "=Take each term on its own\2The derivative of a sum is the sum of the derivatives, so do one term at a time.\0"
    "=Power rule on %\2Bring the power down in front and lower the power by 1: X^n gives n*X^(n-1).\0"
    "=Chain rule on %\2Differentiate the outside, keep the inside, then multiply by the derivative of the inside.\0"
    "=Product rule on %\2For two things multiplied: (first)' x second + first x (second)'.\0"
    "=Add the results\2The parts were done one at a time. Add them back together for the whole answer.\0"
    "=Differentiate the top, %\2The quotient rule needs the derivative of the top.\0"
    "=Differentiate the bottom, %\2It also needs the derivative of the bottom.\0"
    "=Put it in the quotient rule\2(top' x bottom - top x bottom') / bottom\1, then simplify.\0"
    "Move everything to the left\2Subtract the right side from both sides, so the right is 0. Then we can factor.\0"
    "Factor the left side\2A product is 0 only when one of its factors is 0, so write it as a product.\0"
    "Set % equal to 0\2If A x B = 0 then A = 0 or B = 0, so each factor gives an answer.\0"
    "=Find the discriminant b\001-4ac\002Its sign tells how many real roots there are. Negative means none.\0"
    "It is negative: no real roots\2A negative number has no real square root, so the equation has no real answer.\0"
    "=Take out the common factor\2Every term shares it, so pull it to the front: ab + ac = a(b + c).\0"
    "% is a root, so a factor is\2If X = r makes the polynomial 0, then X - r divides it exactly.\0"
    "=Divide by the prime %\2To factor a number, keep dividing by primes until nothing is left.\0"
    "=First work out %\2The command inside is worked out first, and its answer replaces it.\0"
    "=Integrate %\2Power rule: add 1 to the power, then divide by the new power.\0"
    "=Put in the top limit %\2Put the top limit into the antiderivative.\0"
    "=Put in the bottom limit %\2Put the bottom limit into the antiderivative too.\0"
    "=Subtract the two results\2Subtract: F(top) - F(bottom). That is the value of the definite integral.\0"
    "=Keep the biggest power\2For a huge X the highest power wins, so the other terms hardly matter.\0"
    "=Put in %\2The function has no gap there, so putting the number in gives the limit.\0"
    "=Leading terms of top and bottom\2Close to the point, only the lowest powers of top and bottom matter. Compare those.\0"
    "Split into two parts: F = %\2The product rule needs the product split into F and G.\0"
    "Differentiate both parts\2Find F' and G', the derivative of each part.\0"
    "=Combine with the product rule\2The derivative of F x G is F'G + FG'. Put the pieces in and add.\0"
    "Split into top F = % and bottom G\2The quotient rule needs the top (F) and the bottom (G).\0"
    "=Derivative of the inside, %\2Chain rule: the derivative of the inside is multiplied in at the end.\0"
    "Multiply out and collect like terms\2Remove the brackets, then add up the terms of the same kind.\0"
    "Swap the sides\2Both sides are equal, so they can change places. The unknown goes on the left.\0"
    "=Differentiate again, number %\2A higher derivative is the derivative of the previous one.\0"
    "Put the pivot in equation %\2Swap two equations so the first has a number in front of the unknown (the pivot).\0"
    "Divide equation % so the pivot is 1\2Divide the whole equation by the pivot so the unknown has 1 in front. Same on both sides.\0"
    "Clear that unknown from equation %\2Subtract a multiple of the pivot equation to cancel this unknown. Same on both sides.\0"
    "Get the root alone\2Put the square root on its own side, so squaring it next works cleanly.\0"
    "Square both sides\2Squaring undoes the square root. It can add fake answers, so we check at the end.\0"
    "Root % fails the check\002Put it back in: the original is false. Squaring made a fake answer, so drop it.\0"
    "Let u = %\2Replace the inside with u. The integral may then be simpler.\0"
    "Rewrite in terms of u\002Everything is now in u: dX becomes du divided by the slope of the inside.\0"
    "Put u = % back\2The answer is in u. Replace u by its expression to answer in X.\0"
    "By parts: pick u = %\2Choose u as the part that gets simpler when you differentiate it.\0"
    "The rest is dv\2dv is whatever is left. You integrate it to get v.\0"
    "Split into partial fractions\2A fraction with a factored bottom is a sum of simpler fractions, one for each factor.\0"
    "=Integrate each fraction\2Each simple fraction has an easy integral, often a logarithm.\0"
    "Solve for y'\2Get y' on its own, so the equation is in a standard form.\0"
    "=Homogeneous part is C times\2First solve with 0 on the right. The solution is a constant C times this function.\0"
    "Separate and integrate both sides\2Put all the y's with dy and all the x's with dx, then integrate each side.\0"
    "Use the initial condition\2Put the given point in to find the constant C.\0"
    "Characteristic equation\2Try y = e^(rX). That turns the differential equation into an ordinary equation in r.\0"
    "Root of that equation\2Each root r gives a solution e^(rX).\0"
    "Complex roots: real part %, imaginary part\2A pair a +/- bi gives e^(aX) times cos(bX) and sin(bX).\0"
    "Homogeneous solution\2Combine the basic solutions with constants C and D.\0"
    "Add a particular solution\2Add one solution that fits the right side. General = homogeneous + particular.\0"
    "Solve equation % for one unknown\2Get one unknown alone, so it can go into the other equation.\0"
    "Put that in the other equation\2Replacing the unknown leaves one equation in one unknown.\0"
    /* stepsalg */
    "Find two numbers that multiply to %1 and add to %2: %3 and %4\002Multiply to a times c, add to b. They split the middle term.\0"
    "Split the middle term into %1 and %2\002Same value, since the two add up to the middle term. Now the four terms can be grouped.\0"
    "Factor each pair of terms\002Take the common factor out of the first two terms, then out of the last two.\0"
    "Factor out the common bracket\002Both parts have the same bracket. Take it out; what is left makes the other bracket.\0"
    "Put each number in a bracket\002The numbers go next to the variable: (X + first)(X + second). Multiply back to check.\0"
    "Use the quadratic formula (a=%1, b=%2, c=%3)\002It solves any aX\001+bX+c=0: X is (-b plus or minus the square root of b\001-4ac) over 2a.\0"
    "Work out b\001-4ac with the numbers\002The discriminant. Positive: two real roots. Zero: one. Negative: no real roots.\0"
    "Put the numbers into the formula\002Plus gives one root and minus gives the other.\0"
    "Simplify the square root\002Pull out any perfect square: root(a x b) = root(a) x root(b).\0"
    "A negative under the root: use i\002The root of -1 is called i. So the root of -n is i times the root of n.\0"
    "Simplify each fraction\002Divide the top and bottom by any common factor to get the roots.\0"
    "The discriminant is not a perfect square\002So the roots are not fractions, and it does not factor into whole-number brackets.\0"
    "Take the square root of both sides\002A number has two square roots, the positive one and the negative one.\0"
    "Rational root test: try factors of %1 over factors of %2\002A root p/q needs p to divide the constant term and q to divide the leading coefficient.\0"
    "Divide by %\002Long division by the factor leaves a smaller polynomial that is easier to factor.\0"
    /* stepsalg */
    "Multiply every term by every term\002Each term of the first part meets each term of the second. Write all the products.\0"
    "Write the power as a product\002A power means the same bracket times itself that many times.\0"
    "Multiply out %\002Clear the brackets in this term first.\0"
    "=Combine like terms\002Add the terms that have the same power of the letter.\0"
    "=Common denominator\002The smallest expression that every bottom divides into: each factor, highest power.\0"
    "Rewrite each fraction over it\002Multiply top and bottom of each fraction by what its bottom is missing. Same value.\0"
    "Add the tops\002Fractions with the same bottom add by adding their tops.\0"
    "Divide the leading terms %1 by %2: %3\002Leading term = highest power. Dividing them gives the next part of the answer.\0"
    "Multiply %1 by the divisor and subtract\002That removes the biggest term. What is left is the new remainder.\0"
    "=The quotient\002Stop: what is left has a lower power than the divisor. The quotient is the parts so far.\0"
    "=The remainder\002Its power is lower than the divisor's, so it cannot be divided any more.\0"
    "Divide %1 by %2: the remainder is\002The GCD (biggest common factor) of A and B equals that of B and the remainder.\0"
    "The remainder is 0, so the last divisor is the GCD\002Scale it so the leading coefficient is 1.\0"
    "=The degree is the highest power of %\002Read it off the expanded polynomial.\0"
    "=Coefficients, highest power first\002A power that is missing has coefficient 0.\0"
    "=The left side\002Everything before the equals sign.\0"
    "=The right side\002Everything after the equals sign.\0"
    "Find all % roots by repeated improvement\002No formula exists, so guess every root, then improve all guesses until they stop changing.\0"
    "Round and list the roots\002Rounded to 9 decimal places.\0"
    "Use the sum formulas\002sin(A+B) = sin A cos B + cos A sin B, and cos(A+B) = cos A cos B - sin A sin B.\0"
    "Use the double-angle formulas\002sin 2A = 2 sin A cos A, and cos 2A = cos A squared minus sin A squared.\0"
    "Use the triple-angle formulas\002sin 3A = 3 sin A - 4 sin A cubed, and cos 3A = 4 cos A cubed - 3 cos A.\0"
    "Use the power-reducing formulas\002sin A squared = (1 - cos 2A) / 2, and cos A squared = (1 + cos 2A) / 2.\0"
    "Use the product-to-sum formulas\002sin A cos B = (sin(A+B) + sin(A-B)) / 2. Two sines or two cosines work the same way.\0"
    "Change log to ln\002log(A) is ln(A) over ln(10), because log is base 10.\0"
    "Write ln as a log\002ln(A) = log(A) / log(e): the change-of-base rule, with e as the new base.\0"
    "Euler's formula\002e^(iA) = cos A + i sin A. Adding or subtracting e^(iA) and e^(-iA) isolates one of them.\0"
    "Use sin\001A + cos\001A = 1\002It is always true, so one of the two can be written with the other.\0"
    "=Replace and simplify\002Put the formula in and tidy up.\0"
    "=Nothing to change\002These formulas do not apply, so the expression stays as it is.\0"
    "=Factor the bottom\002Each factor of the bottom gets its own fraction.\0"
    "Find the numerators\002Put the fractions over one bottom and match the tops. That finds the unknown numerators.\0"
    "Split the absolute value into cases\002|U| = k means U = k or U = -k. |U| < k means -k < U < k. |U| > k means U < -k or U > k.\0"
    "Solve each case\002Each case is an ordinary equation or inequality.\0"
    "These values make a bottom 0, so they are not allowed\002You cannot divide by 0, so none of these can be an answer.\0"
    "=Multiply by the common denominator\002Every bottom cancels, leaving an equation with no fractions. Here it is moved to one side.\0"
    "Turn the inequality around\002Multiplying or dividing by a negative turns it around: < becomes > and back.\0"
    "Find the boundary values\002The sign of each side can only change where they are equal, or where a bottom is 0.\0"
    "Test a number in each region\002Try one number from each region. If it works, the whole region is part of the answer.\0"
    /* stepsgeo */
    "Put the points in the distance formula" "\x02" "d = sqrt((x2-x1)" "\x01" "+(y2-y1)" "\x01" "), Pythagoras on the run and the rise." "\0"
    "Subtract inside the brackets" "\x02" "Work out each difference first: the run and the rise." "\0"
    "Square and add" "\x02" "Square each number, then add them up." "\0"
    "Square and subtract" "\x02" "Square each number, then subtract." "\0"
    "Write the line as ax + by + c = 0" "\x02" "Move everything to one side so a, b and c can be read off." "\0"
    "Distance from a point to a line" "\x02" "d = |a x0 + b y0 + c| / sqrt(a" "\x01" "+b" "\x01" "), where (x0, y0) is the point." "\0"
    "Slope = rise over run" "\x02" "m = (y2 - y1) / (x2 - x1): how far up for each step across." "\0"
    "Slope of ax + by + c = 0" "\x02" "Solve for y: y = -(a/b)x - c/b, so the slope is -a/b." "\0"
    "Parallel: same slope, through the point" "\x02" "Parallel lines have the same slope m. Use y - y1 = m (x - x1)." "\0"
    "Perpendicular slope: flip and negate" "\x02" "Perpendicular slopes multiply to -1, so the new slope is -1/m." "\0"
    "Point-slope form through the point" "\x02" "y - y1 = m (x - x1) is the line with slope m through (x1, y1)." "\0"
    "Vertical line: no slope" "\x02" "A line parallel to a vertical line is vertical: X equals the point's x." "\0"
    "Perpendicular to a vertical line" "\x02" "A vertical line has no slope; its perpendicular is flat: Y equals the point's y." "\0"
    "Perpendicular to a flat line" "\x02" "A flat line has slope 0; its perpendicular is vertical: X equals the point's x." "\0"
    "Same x, so the line is vertical" "\x02" "The run is 0, so the slope cannot be found: the line is X = that x." "\0"
    "Both lines as ax + by + c = 0" "\x02" "Line them up so the coefficients can be compared." "\0"
    "Cancel Y between the lines" "\x02" "Multiply each line by the other's Y coefficient and subtract: Y drops out." "\0"
    "Solve for the unknown" "\x02" "Undo the adding first, then the multiplying." "\0"
    "Put X back into a line" "\x02" "Any line will do: it leaves an equation with only Y in it." "\0"
    "Put the point into ax + by + c" "\x02" "This number measures how far the point is from the line." "\0"
    "Size of the normal: a" "\x01" "+b" "\x01" "" "\x02" "(a, b) points straight across the line; a" "\x01" "+b" "\x01" " is its length squared." "\0"
    "Go across the line twice as far" "\x02" "P' = P - 2v/(a" "\x01" "+b" "\x01" ") (a, b), v = ax0+by0+c: as far again on the other side." "\0"
    "Midpoint: average the coordinates" "\x02" "M = ((x1+x2)/2, (y1+y2)/2): halfway across and halfway up." "\0"
    "Flat segment: the bisector is vertical" "\x02" "It passes through the midpoint, so it is X = the midpoint's x." "\0"
    "Upright segment: the bisector is flat" "\x02" "It passes through the midpoint, so it is Y = the midpoint's y." "\0"
    "Section formula: how far along" "\x02" "For the ratio m:n the point is t = m/(m+n) of the way from P to Q." "\0"
    "Move t of the way from P to Q" "\x02" "P + t (Q - P), done for x and for y." "\0"
    "Shoelace cross term" "\x02" "x * (next y) - (next x) * y for each pair of neighbouring corners, in order." "\0"
    "Add the cross terms" "\x02" "The total is twice the area, with a sign for the direction you went round." "\0"
    "Half the absolute value" "\x02" "Area = |total| / 2. The bars make it positive whichever way you went round." "\0"
    "Length of the next side" "\x02" "Distance formula between neighbouring corners." "\0"
    "Add the lengths" "\x02" "The perimeter is the sum of all the sides." "\0"
    "Centroid: average the corners" "\x02" "G = (sum of x / n, sum of y / n), n the number of corners." "\0"
    "Perpendicular bisector of a side" "\x02" "Points equally far from both ends: |PA|" "\x01" " = |PB|" "\x01" ", and the squares cancel." "\0"
    "Where the two lines cross" "\x02" "Solve the two equations together for X and Y." "\0"
    "Altitude from a corner" "\x02" "The line through the corner, perpendicular to the opposite side." "\0"
    "Side opposite each corner" "\x02" "a = |BC|, b = |CA|, c = |AB|: each side is named by the corner across from it." "\0"
    "Weighted average of the corners" "\x02" "x = (a x1 + b x2 + c x3) / (a+b+c), same for y: a corner counts its opposite side." "\0"
    "Pythagoras: c" "\x01" " = a" "\x01" " + b" "\x01" "" "\x02" "The hypotenuse is the square root of the sum of the squares of the legs." "\0"
    "Pythagoras: a" "\x01" " = c" "\x01" " - b" "\x01" "" "\x02" "A leg is the root of the hypotenuse squared minus the other leg squared." "\0"
    "Circle: (X-h)" "\x01" "+(Y-k)" "\x01" " = r" "\x01" "" "\x02" "Centre (h, k), radius r. Put the numbers in." "\0"
    "Radius squared from a point" "\x02" "r" "\x01" " = (x - h)" "\x01" " + (y - k)" "\x01" ", with (x, y) a point on the circle." "\0"
    "Divide through by the X" "\x01" " coefficient" "\x02" "So X" "\x01" " and Y" "\x01" " each have coefficient 1." "\0"
    "Group the X terms and the Y terms" "\x02" "Move the plain number to the right-hand side." "\0"
    "Complete the square in X and in Y" "\x02" "Add (half the X coefficient)" "\x01" " and (half the Y coefficient)" "\x01" " to both sides." "\0"
    "Factor each group into a square" "\x02" "X" "\x01" "+2pX+p" "\x01" " = (X+p)" "\x01" ". The right side is r" "\x01" ", the radius squared." "\0"
    "Subtract the centre" "\x02" "Move the centre to (0, 0) first so the formulas are simple." "\0"
    "Cosine and sine of the angle" "\x02" "These are exact for multiples of 30 and 45 degrees." "\0"
    "New x = x cos - y sin" "\x02" "x' = x cos(a) - y sin(a): the rotation formula for x." "\0"
    "New y = x sin + y cos" "\x02" "y' = x sin(a) + y cos(a): the rotation formula for y." "\0"
    "Add the centre back" "\x02" "Undo the first move: add the centre's coordinates." "\0"
    "Multiply by the scale factor k" "\x02" "A dilation stretches the distance from the centre by k, same direction." "\0"
    "=Write the answer" "\x02" "Put the result in its simplest form." "\0"
    "Write the answer" "\x02" "Put the result in its simplest form." "\0"
    "=Work it out" "\x02" "Do the arithmetic: multiply, add and simplify the numbers." "\0"
    "Determinant of 2x2: ad - bc" "\x02" "Multiply down the main diagonal, then subtract the product of the other diagonal." "\0"
    "Entry times its minor, with sign" "\x02" "The minor is what is left after deleting the entry's row and column. Signs go + - + - ." "\0"
    "Minor: cross out its row and column" "\x02" "What is left is a smaller matrix; its determinant is the minor." "\0"
    "=The determinant" "\x02" "Found by expanding along the first row, entry by entry." "\0"
    "Add the terms" "\x02" "The sum of the row's terms is the determinant." "\0"
    "Swap a and d, flip the signs of b and c\002This matrix is the adjugate: [[a,b][c,d]] becomes [[d,-b][-c,a]].\0"
    "Matrix of cofactors\002Each entry becomes its minor (cross out its row and column), with checkerboard signs + - +.\0"
    "Swap rows and columns: the adjugate" "\x02" "Transposing the cofactor matrix gives the adjugate." "\0"
    "Divide every entry by the determinant %\002The inverse = adjugate / determinant. A determinant of 0 means no inverse.\0"
    "Swap rows %" "\x02" "Swapping two rows only reorders the equations, so the solution is unchanged." "\0"
    "Scale a row: %" "\x02" "Divide the whole row by its pivot so the pivot becomes 1." "\0"
    "Clear the column: %" "\x02" "Add a multiple of the pivot row to cancel the other entries in the pivot's column." "\0"
    "Already in reduced form" "\x02" "Every pivot is 1 and the only non-zero entry in its column." "\0"
    "=Rows become columns" "\x02" "Row 1 is column 1 of the result, row 2 is column 2, and so on." "\0"
    "Multiply matching entries" "\x02" "Dot product: a1 b1 + a2 b2 + ... pairs up the entries in order." "\0"
    "Work out each product" "\x02" "Do the multiplications first, then add them." "\0"
    "Cross product, one part at a time" "\x02" "(a2b3-a3b2, a3b1-a1b3, a1b2-a2b1): each part is a 2x2 determinant." "\0"
    "Length: root of the sum of squares" "\x02" "|v| = sqrt(v1" "\x01" "+v2" "\x01" "+...): Pythagoras for any number of entries." "\0"
    /* stepscalc */
    "Sum rule: take it a term at a time\002The derivative of a sum is the sum of the derivatives: (f + g)' = f' + g'.\0"
    "=Constant rule on %\002The derivative of a number is 0, because it never changes.\0"
    "=Derivative of %\002The derivative of the variable by itself is 1.\0"
    "=Derivative of %\002A number times X has a slope equal to that number: (cX)' = c.\0"
    "=Power rule on %\002Bring the power down in front, then lower the power by 1: (X^n)' = n X^(n-1).\0"
    "=Derivative of %\002A root is the power 1/2, so the power rule gives (sqrt X)' = 1/(2 sqrt X).\0"
    "=Derivative of %\002Known result: the derivative of sin is cos.\0"
    "=Derivative of %\002Known result: the derivative of cos is -sin.\0"
    "=Derivative of %\002Known result: the derivative of tan is 1 + tan\001, which is sec\001.\0"
    "=Derivative of %\002Known result: e^X is its own derivative.\0"
    "=Derivative of %\002Known result: the derivative of ln(X) is 1/X.\0"
    "=Chain rule on %\002Derivative of the outside (inside kept as it is) times the derivative of the inside.\0"
    "=Differentiate %\002Use the rules together, one piece at a time.\0"
    "=Keep the constant %1, multiply by %2\002A constant factor stays in front: (c f)' = c f'.\0"
    "Product rule: first = %1, second = %2\002(first x second)' = first' x second + first x second'. A ' means the derivative.\0"
    "Quotient rule: top = %1, bottom = %2\002(bottom x top' - top x bottom') / bottom\001. A ' means the derivative.\0"
    "=Put in: first' x second + first x second'\002Plug the pieces into the product rule and work them out.\0"
    "=Put in: (bottom x top' - top x bottom') / bottom\001\002Plug the pieces into the quotient rule and work them out.\0"
    "=Simplify\002Multiply out and combine like terms.\0"
    "=Add the derivatives\002Sum rule: the answer is the sum of the term derivatives.\0"
    "Derivative number %1 of %2\002Differentiate the previous result again.\0"
    "=Put in %\002Replace the variable by that number in the derivative.\0"
    "Chain rule on %1: the inside is %2\002Differentiate the outside, keep the inside, then multiply by the inside's derivative.\0"
    "=Outside derivative, inside kept\002Treat the inside as one lump and differentiate the outer function.\0"
    "=Derivative of the inside %\002The chain rule multiplies by the derivative of what is inside.\0"
    "=Multiply %1 by %2\002Chain rule: (outside derivative) x (inside derivative).\0"
    "Sum rule: integrate each term\002The integral of a sum is the sum of the integrals.\0"
    "=Integral of the number %\002The integral of a number c is c times the variable.\0"
    "=Power rule on %1: new power %2, divide by %2\002Power rule: add 1 to the power, then divide by the new power.\0"
    "=Integral of %\002A root is the power 1/2: add 1 to get 3/2, then divide by 3/2.\0"
    "=Integral of %\002Known result: the derivative of -cos is sin, so the integral of sin is -cos.\0"
    "=Integral of %\002Known result: the derivative of sin is cos, so the integral of cos is sin.\0"
    "=Integral of %\002Known result: e^X is its own integral.\0"
    "=Integral of %1, divided by %2\002Undo the chain rule: divide by the slope of the inside.\0"
    "=Keep the constant %1, multiply by %2\002A constant factor stays in front of the integral.\0"
    "=Integrate %\002Find the function whose derivative is this.\0"
    "By parts: u = %1, dv = %2\002Integral of u dv = uv - integral of v du. Choose u that gets simpler when differentiated.\0"
    "=Then du = %1 and v = %2\002Differentiate u to get du; integrate dv to get v.\0"
    "=Integrate v du = %\002The new integral is simpler than the one we started with.\0"
    "=uv minus the integral of v du\002Put it together: u times v, minus the integral just found.\0"
    "=Add the integrals\002Sum rule: add the integrals found for each term.\0"
    "=Add the constant of integration\002A constant vanishes when you differentiate, so any constant could be there: add + C.\0"
    "F(%): put in the top limit\002F is the integral found above. Put the top number in for the variable.\0"
    "F(%): put in the bottom limit\002Now the bottom number.\0"
    "Subtract: F(%2) - F(%1)\002Fundamental theorem of calculus: the integral from a to b is F(b) minus F(a).\0"
    "Let u = %1, whose slope is %2\002Call the inside u. Then dx becomes du divided by the slope, so only u is left.\0"
    "=Now the integral in u\002The inside became u and dx became du over the slope.\0"
    "=Put the inside back: u = %\002Replace u by what it stood for.\0"
    "Split into partial fractions\002Break the fraction into simple pieces you can integrate one at a time.\0"
    "=Integrate each fraction\002A piece 1/(X-a) integrates to ln|X-a|; the others follow the power rule.\0"
    "Plug in the point %\002Nothing divides by 0 there, so the limit is just the value at that point.\0"
    "=Try plugging in %\002Top and bottom both become 0. 0/0 tells us nothing yet, so we need another method.\0"
    "=Try plugging in %\002The bottom becomes 0 but the top does not, so the value grows without bound near here.\0"
    "=Cancel the common factor %\002Top and bottom share a factor that is 0 here. A limit ignores that point, so cancel it.\0"
    "=Cancel the common factor\002Top and bottom share a factor that is 0 here. A limit ignores that point, so cancel it.\0"
    "Near %1 the top acts like %2 and the bottom like %3\002Close to the point only the lowest power matters. Keep just that for top and bottom.\0"
    "=Top is smaller, so the fraction goes to 0\002The top shrinks to 0 faster than the bottom does, so the ratio goes to 0.\0"
    "=Same power: divide the coefficients\002Both shrink at the same rate, so the limit is the ratio of their leading coefficients.\0"
    "Bottom is smaller: the value blows up\002The bottom reaches 0 faster than the top, so the fraction grows without bound.\0"
    "=Divide the leading terms\002The dominant term is the leading term of the top over the leading term of the bottom.\0"
    "Keep the highest powers: %1 over %2\002For huge values the highest power on top and on the bottom outgrows all the lower ones.\0"
    "=Bottom has the higher power: goes to 0\002The bottom grows faster than the top, so the fraction shrinks to 0.\0"
    "=Same powers: divide the coefficients\002The highest powers cancel, leaving the ratio of their coefficients.\0"
    "Top has the higher power: it blows up\002The top grows faster than the bottom, so the fraction grows without bound.\0"
    "=Slope function: the derivative\002The slope of the curve at a point is the derivative there.\0"
    "Slope at %: put the point into the derivative\002This number m is the slope of the line.\0"
    "Height at %: put the point into f\002This is the height where the line touches the curve.\0"
    "=Normal slope: -1 over the tangent slope\002The normal is perpendicular to the tangent, so its slope is the negative reciprocal of m.\0"
    "=Line through that point with that slope\002Point-slope form: y = f(a) + m(X - a).\0"
    "=Simplify\002Multiply out and collect like terms.\0"
    "=Put in X + h\002First look a little to the right of X.\0"
    "=Put in X - h\002Then look the same distance to the left of X.\0"
    "=Subtract, then divide by 2h\002The slope is estimated as (f(X+h) - f(X-h)) / (2h).\0"
    "Order %1: the derivative at %2 is %3, over %1!\002Each term: that derivative at the point, over k!, times (X - a)^k. k! = 1x2x...xk.\0"
    "=Add the terms\002The Taylor polynomial is the sum of these terms.\0"
    "=Expand around the point\002Each term is the derivative at the point over k!, times (X - a)^k, up to the order asked.\0"
    "=Series of the top\002Expand the top as a power series around the point.\0"
    "=Series of the bottom\002Expand the bottom as a power series as well.\0"
    "=Divide the series\002Long division of the two series, lowest power first, gives the answer.\0"
    "=Move everything to one side\002Write the equation as an expression equal to 0, so it can be differentiated.\0"
    "=Differentiate by X, the other letter held still\002This is the partial derivative: treat the other letter as a constant.\0"
    "=Differentiate by the other letter, X held still\002The same again for the other letter.\0"
    "=The slope is minus the X part over the other part\002Differentiating F = 0: (X part) + (other part) x slope = 0. Solve for the slope.\0"
    "=Differentiate the slope again\002Y depends on X, so also add the Y part times the slope (chain rule).\0"
    "=Write out the terms\002Put each whole number from the bottom to the top into the expression.\0"
    "=Work out each term\002Evaluate every term on its own.\0"
    "=Add them up\002The sum is all the terms added together.\0"
    "=Write out the factors\002Put each whole number from the bottom to the top into the expression.\0"
    "=Work out each factor\002Evaluate every factor on its own.\0"
    "=Multiply them\002The product is all the factors multiplied together.\0"
    "=Use a closed formula for the sum\002The terms follow a pattern whose total from the bottom to the top has an exact formula.\0"
    "=Slope function: the derivative\002A smooth curve is flat where it has a high or low point inside the interval.\0"
    "Flat at %: the value there\002Candidate: the derivative is 0 here. Put it into f.\0"
    "Endpoint %: the value there\002The highest or lowest point can also be at an end of the interval.\0"
    "The lowest value is at\002Compare all the values: the smallest one wins.\0"
    "The highest value is at\002Compare all the values: the biggest one wins.\0"
    "=Slope function: the derivative\002Arc length needs the slope of the curve.\0"
    "=Tiny piece of curve: root of 1 + slope\001\002By Pythagoras a tiny piece has length root(1 + slope\001) dX.\0"
    "=The root simplifies\0021 + slope\001 is a perfect square here, so the root disappears.\0"
    "=Integrate from %1 to %2\002Add up all the tiny pieces: the arc length is this integral.\0"
    "Get Y' by itself\002Move everything else to the right, so the equation reads Y' = something.\0"
    "Y' has no Y: integrate both sides\002Integrating undoes a derivative. The + C is the constant every integral can have.\0"
    "The simple part: drop the right side\002With the right side 0 the equation is easy to solve. Its solutions are C times this.\0"
    "One solution that fits the full equation\002Any one solution of the full equation will do. Add it to the simple part.\0"
    "Add the two parts\002The simple part plus the one solution you found gives every solution.\0"
    "Put in the starting value %\002Put the number in for X. Only the constants stay unknown, giving an equation for them.\0"
    "Solve for the constant\002Get the letter alone. With two constants, C and D, each starting value gives an equation: solve both together.\0"
    "Separate the Y and X parts, integrate\002Move all Y with dY to the left, all X with dX to the right, then integrate each side.\0"
    "Solve for Y\002Get Y by itself if that is possible; if not, the answer stays as an equation.\0"
    "Characteristic equation\002Try Y = e^(RX): Y'' becomes R\001, Y' becomes R, Y becomes 1. Solve for R.\0"
    "A root of that equation\002Each root R gives a solution e^(RX).\0"
    "Repeated root\002One root counted twice, so the second solution needs an extra X: X e^(RX).\0"
    "Complex roots a + bi and a - bi\002No real roots. They come as a pair, giving e^(aX) times cos(bX) and sin(bX).\0"
    "Difference of squares\002a\1-b\1 = (a-b)(a+b). Here a = %1 and b = %2.\0"
    "Perfect square\002a\1+2ab+b\1 = (a+b)\1. Here a = %1 and b = %2.\0"
    "Add and subtract %1\002Adding and taking away one term changes nothing. It turns the sum into a square minus a square.\0"
    "Write %1 as a power\002A root is a power of 1/2, and dividing by X^n takes n off the power.\0"
    "Same answer, written the calculator's way\002A power of sin(X) can be rewritten with sin(3X), since sin(3X) = 3sin(X) - 4sin(X)^3. Both forms are equal.\0"
    "Pascal's triangle, row %1\002Here a = %2 and b = %3. Row %1 gives the number in front of each term of (a+b)^%1.\0"
    "Write the terms\002Each term is its number times a to a falling power times b to a rising power.\0"
    /* stepsfx */
    "Let u = %\002The unknown only appears inside this power. Write the power as u, and the equation is an ordinary one in u. Solve for u first.\0"
    "Put back u = %\002We found u. It stands for the power, so the power equals this number.\0"
    "u = % has no answer\002A power with a positive base is always positive, so it can never be 0 or negative. Drop it.\0"
    "Take ln of both sides\002ln undoes e to a power: ln(e^A) = A. The same on both sides keeps it true.\0"
    "Take ln of both sides\002ln brings the power down in front: ln(B^A) = A ln(B). The same on both sides keeps it true.\0"
    "Simplify the logs\002%1 is %2 to the power %3, so ln(%1) is %3 times ln(%2), and the ln(%2) cancels.\0"
    "Combine the logs\002Put the logs on one side and the numbers on the other. n ln(A) = ln(A^n), and ln(A) + ln(B) = ln(AB).\0"
    "Raise % to both sides\002Raising the base undoes the log: e^(ln A) = A, and 10^(log A) = A. The same on both sides keeps it true.\0"
    "Equal logs, equal insides\002ln A = ln B only when A = B, so the logs can be dropped.\0"
    "X = % is not allowed\002A log needs a positive number inside. This X puts 0 or a negative number inside a log. Drop it.\0"
    "Take ln of both sides\002Both sides are positive, so ln keeps them equal. ln brings each power down in front: ln(B^A) = A ln(B). Then multiply out.\0"
    "Take the root of both sides\002X to a power equals this number. Take that root of both sides. An even power also has the negative root.\0"
    "Find the angle with that sine\002Use the unit circle (or sin^-1). The first angle with this sine is in the first or fourth quarter. It is only one of the solutions.\0"
    "Find the angle with that cosine\002Use the unit circle (or cos^-1). The first angle with this cosine is between 0 and 180 degrees (0 and pi). It is only one of the solutions.\0"
    "Find the angle with that tangent\002Use the unit circle (or tan^-1). The first angle with this tangent is between -90 and 90 degrees (-pi/2 and pi/2).\0"
    "Both angles in one turn\002Sine is the height on the unit circle. Two angles in one turn share it: the angle, and half a turn minus the angle (pi minus it, or 180 minus it).\0"
    "Both angles in one turn\002Cosine is the sideways place on the unit circle. Two angles in one turn share it: the angle, and a full turn minus it (2 pi - it, or 360 - it).\0"
    "Add the period\002One more turn is the same place: add the period times N. N is any whole number: ..., -1, 0, 1, ... The period: 2 pi (360 degrees), pi (180) for tan.\0"
    "Solve for the unknown\002Undo the inside: take away the number added to X, then divide by the number in front of X. Do it to every part, the N part too.\0"
    "No angle has this value\002Sine and cosine only give numbers from -1 to 1. Nothing outside that is a sine or a cosine, so this value gives no solution.\0"
    "Keep the answers from %1 to %2\002N is any whole number, so try values of N and keep only the answers between the two bounds. Both ends count.\0"
    "Let u = %\002The unknown only appears inside this sine, cosine or tangent. Call it u, and the equation is an ordinary one in u. Solve for u first.\0"
    "Put back u = %\002We found u. It stands for the sine, cosine or tangent, so that equals this number.\0"
    "All the answers\002Put the answers from each value together.\0"
    /* calcgap */
    "=Derivative of %\002Known result: the derivative of the inverse sine, sin^-1(X), is 1/sqrt(1 - X^2).\0"
    "=Derivative of %\002Known result: the derivative of the inverse cosine, cos^-1(X), is -1/sqrt(1 - X^2).\0"
    "=Derivative of %\002Known result: the derivative of the inverse tangent, tan^-1(X), is 1/(1 + X^2).\0"
    "=Derivative of %\002A fixed power, even a fraction: (u^c)' = c x u^(c-1) x u'. Bring c down, lower it by 1.\0"
    "=Derivative of %\002A fixed base to a power: (b^u)' = b^u x ln(b) x u'.\0"
    "=Derivative of %\002Base and power both change: write b^u as e^(u ln b), so (b^u)' = b^u x (u' ln b + u b'/b).\0"
    "=An arctan piece\002A fraction 1/(u^2 + c) integrates to tan^-1(u/sqrt(c))/sqrt(c). The part of the top that is the slope of the bottom gives a ln.\0"
    "=An arcsin: 1 over a root\002The integral of 1/sqrt(c - u^2) is sin^-1(u/sqrt(c)). Shift and scale the bottom into that shape.\0"
    /* gaplim */
    "=Multiply top and bottom by the conjugate %\002(a - b)(a + b) = a^2 - b^2, so the root goes away.\0"
    "Side %1: the abs becomes %2\002abs(u) is u when u is positive, -u when negative.\0"
    "%1 goes to %2\002This piece settles to a number, so put it in.\0"
    "Strongest parts: %1\002e^ beats powers, powers beat ln. Only the strongest parts count.\0"
    "=Bottom is stronger: goes to 0\002The bottom outgrows the top, so the fraction goes to 0.\0"
    "=Same strength: divide coefficients\002Same growth: the limit is the ratio of the coefficients.\0"
    "Top is stronger: it blows up\002The top outgrows the bottom, so the fraction grows without bound.\0"
    "=The wave is squeezed to 0\002Sin and cos stay between -1 and 1 and the rest goes to 0, so the product does too.\0"
    "No single limit\002Sin or cos keeps swinging between -1 and 1.\0"
    "=The exponent is c ln(b)\002b^c is e^(c ln b). Find the limit of the exponent first.\0"
    "=The exponent is c(b - 1)\002As b goes to 1, ln(b) acts like b - 1. Find the limit of c(b - 1).\0"
    "=Raise e to that limit\002b^c goes to e to the limit of its exponent.\0"
    "=The limit\002Nothing is left to cancel, so this is the value.\0"
    "Geometric series: first term %1, ratio %2\002Each term is the last one times the ratio, so there is a formula.\0"
    "=Use the geometric formula\002First term times (ratio^terms - 1), over (ratio - 1).\0"
    "=Ratio in (-1, 1): first term over (1 - ratio)\002The powers of the ratio shrink to 0.\0"
    "=A p-series: 1/n^p\002p = 2 sums to pi^2/6, p = 4 to pi^4/90. Subtract the terms before the start.\0"
    "Split into simple fractions: %1\002Most of them cancel in the sum.\0"
    "=The sum telescopes\002All but the first few terms cancel in pairs. What is left is the answer.\0"
    /* gaps-de */
    "Write it as Y' + P Y = Q\002Linear: Y and Y' only to the first power; P and Q have only X.\0"
    "Integrating factor: e^(integral of P)\002Times this, the left side becomes one derivative.\0"
    "Multiply every term by it\002The same on both sides keeps it true. The left is (U x Y)', the product rule backwards.\0"
    "Integrate both sides\002Integrating undoes the derivative. + C is the constant every integral can have.\0"
    "Take e to both sides\002e^(ln u) is u. e^C and the sign of |u| fold into one new constant, still C.\0"
    "Undo the function on Y\002Use the inverse (tan undoes tan^-1, ln undoes e^) on both sides.\0"
    /* end stepscalc */;

void steps_tok_(const uint8_t *t, char *out);

#define STX      4               /* the list's left edge, and its width */
#define STW      312
#define WT       48              /* the window between the bars */
#define WB       220
#define LA       12              /* cell rows above and below the baseline: large font, small */
#define LD       4
#define SA       12
#define SD       2
#define C_MATH   0x001F          /* the colours */
#define C_MOVE   0xD800
#define C_WHY    0x52AA
#define C_GREEN  0x0400
#define C_GBOX   0xDFFB
#define C_LINE   0xAD75

struct bx { int w, a, d; };      /* a box: width, rows above the baseline, rows below */

static int s_cw(uint8_t c, uint8_t sz)
{
    return sz ? (c == ' ' ? 5 : *(const volatile uint8_t *)(0xA3CA9u + 25u * c)) : 12;   /* the OS's small space is 2 px: widen it */
}

static int s_tw(const char *s, uint8_t sz)
{
    int w = 0;
    for (; *s; s++)
        w += s_cw((uint8_t)*s, sz);
    return w;
}

/* s at x, its cell above the baseline by; sz 1 is the small font */
static void s_tx(int x, int by, const char *s, uint8_t sz, uint16_t col, uint8_t dr)
{
    if (!dr)
        return;
    char w[64];
    unsigned k;
    FONTFL = sz ? FONTFL & ~4 : FONTFL | 4;
    DRAWFG = col;
    while (*s) {                        /* the small font word by word, a space being 5 px */
        for (k = 0; *s && (*s != ' ' || !sz) && k < 63; k++)
            w[k] = *s++;
        w[k] = 0;
        PENCOL = x;
        PENROW = by - (sz ? SA : LA);
        os_str(VPUTS, w);
        x += s_tw(w, sz);
        for (; *s == ' ' && sz; s++)
            x += 5;
    }
}

/* the length of the token at i: two bytes for the prefixes, as steps_tok_ reads them */
static unsigned s_tl(const uint8_t *t, unsigned i, unsigned n)
{
    uint8_t c = t[i];
    return (c == 0xBB || c == 0xEF || c == 0xAA || c == 0x7E || (c >= 0x5C && c <= 0x63 && c != 0x5F)) && i + 1 < n ? 2 : 1;
}

/* the token at i opens a bracket: ( or a function's name and (, logBASE( included, or a matrix's [ */
static int s_ob(const uint8_t *t, unsigned i, unsigned n)
{
    uint8_t c = t[i];
    return c == 0x10 || c == 0x06 || (c >= 0xB1 && c <= 0xCD && c != 0xBB) || (c == 0xEF && i + 1 < n && t[i + 1] == 0x34);
}

/* the ) or ] closing the bracket opened at i, or n */
static unsigned s_match(const uint8_t *t, unsigned i, unsigned n)
{
    unsigned d = 0;
    for (; i < n; i += s_tl(t, i, n))
        if (s_ob(t, i, n))
            d++;
        else if ((t[i] == 0x11 || t[i] == 0x07) && !--d)
            return i;
    return n;
}

/* the index past the token at i, or past its whole bracket */
static unsigned s_skip(const uint8_t *t, unsigned i, unsigned n)
{
    unsigned m;
    if (i >= n)
        return n;
    if (!s_ob(t, i, n))
        return i + s_tl(t, i, n);
    m = s_match(t, i, n);
    return m < n ? m + 1 : n;
}

/* the exponent after the ^ at i: its end */
static unsigned s_ext(const uint8_t *t, unsigned i, unsigned n)
{
    unsigned j = i + 1;
    if (j < n && (t[j] == 0xB0 || t[j] == 0x71))
        j++;
    if (j >= n)
        return n;
    if (t[j] < 0x30 || t[j] > 0x3A)
        return s_skip(t, j, n);
    while (j < n && t[j] >= 0x30 && t[j] <= 0x3A)
        j++;
    return j;
}

/* a numerator or exponent without the brackets round all of it */
static void s_strip(const uint8_t **t, unsigned *n)
{
    if (*n >= 2 && **t == 0x10 && s_match(*t, 0, *n) == *n - 1) {
        (*t)++;
        *n -= 2;
    }
}

static int s_isop(uint8_t c)    /* a sign, a relation, a comma, or, and, or the red marker */
{
    return !c || c == 0x70 || c == 0x71 || (c >= 0x6A && c <= 0x6F) || c == 0x2B || c == 0x3C || c == 0x40;
}

static void s_mx(const uint8_t *t, unsigned n, int x, int by, uint8_t sz, uint16_t col, uint8_t dr, struct bx *b);

static void s_grow(struct bx *b, const struct bx *c)
{
    if (c->a > b->a)
        b->a = c->a;
    if (c->d > b->d)
        b->d = c->d;
}

/* a tall round bracket drawn from lines: x, its width 5, as high as the box (the middle bulges outwards) */
static void s_brk(int x, int by, const struct bx *c, uint8_t right, uint16_t col, uint8_t dr)
{
    int y0 = by - c->a + 2, h = c->a + c->d - 4;
    if (!dr || h < 8)
        return;
    fill(x + (right ? 3 : 1), y0 + 3, 1, h - 6, col);
    fill(x + 2, y0 + 1, 1, 2, col);
    fill(x + 2, y0 + h - 3, 1, 2, col);
    fill(x + (right ? 1 : 3), y0, 1, 1, col);
    fill(x + (right ? 1 : 3), y0 + h - 1, 1, 1, col);
}

/* a matrix [[a,b][c,d]] as a grid between tall brackets, each cell centred in its column on the maths axis; t is
   what is between the outer brackets. More than 6 rows or columns: too wide to fit, so it is written flat. */
static void s_mat(const uint8_t *t, unsigned n, int x, int by, uint8_t sz, uint16_t col, uint8_t dr, struct bx *b)
{
    int cw[6] = { 0 }, ra[6] = { 0 }, rd[6] = { 0 }, nr = 0, nc = 0, gx = sz ? 8 : 12, gy = sz ? 3 : 5, tk = sz ? 1 : 2;
    int top = 0, h = 0, w = 0, cx, ry;
    unsigned i, k, e, z, c, q, r, pass;
    struct bx cb;
    b->w = 0;
    b->a = sz ? SA : LA;
    b->d = sz ? SD : LD;
    for (pass = 0; pass <= dr; pass++) {
        cx = x + 8;
        ry = top;
        for (r = 0, i = 0; i < n; r++, i = s_skip(t, i, n)) {
            z = t[i] == 0x06 ? s_match(t, i, n) : i + 1;
            if (r > 5) {
                b->w = 1000;
                return;
            }
            for (c = 0, k = t[i] == 0x06 ? i + 1 : i; ; c++, k = e + 1) {
                for (e = k; e < z && t[e] != 0x2B; e = s_skip(t, e, z))
                    ;
                if (c > 5) {
                    b->w = 1000;
                    return;
                }
                s_mx(t + k, e - k, 0, 0, sz, col, 0, &cb);
                if (!pass) {
                    if (cb.w > cw[c]) cw[c] = cb.w;
                    if (cb.a > ra[r]) ra[r] = cb.a;
                    if (cb.d > rd[r]) rd[r] = cb.d;
                    if (c >= (unsigned)nc) nc = c + 1;
                } else {
                    for (q = 0, cx = x + 8; q < c; q++)         /* left edge of column c */
                        cx += cw[q] + gx;
                    s_mx(t + k, e - k, cx + (cw[c] - cb.w) / 2, ry + ra[r], sz, col, 1, &cb);
                }
                if (e >= z)
                    break;
            }
            if (!pass) nr = r + 1;
            else ry += ra[r] + rd[r] + gy;
        }
        if (!pass) {
            for (r = 0; r < (unsigned)nr; r++)
                h += ra[r] + rd[r] + (r ? gy : 0);
            for (c = 0; c < (unsigned)nc; c++)
                w += cw[c] + (c ? gx : 0);
            top = by - (sz ? 3 : 5) - h / 2;
            b->w = w + 16;
            b->a = by - top;
            b->d = top + h - by;
        }
    }
    if (dr)
        for (q = 0; q < 2; q++) {                       /* the brackets: a bar and a short bar at each end */
            cx = q ? x + w + 9 : x + 2;
            fill(q ? cx + 5 - tk : cx, top - 1, tk, h + 2, col);
            fill(cx, top - 1, 5, 1, col);
            fill(cx, top + h, 5, 1, col);
        }
}

/* one term's factors left to right: runs of text, brackets, roots and functions, powers raised */
static void s_items(const uint8_t *t, unsigned n, int x, int by, uint8_t sz, uint16_t col, uint8_t dr, struct bx *b)
{
    static const uint8_t pw[] = { 0x32, 0x33, 0xB0, 0x31 };
    unsigned i = 0, j, m, tl;
    int x0 = x, lw, r = sz ? 4 : 7;
    char s[32];
    struct bx c, e;
    uint8_t o;
    const uint8_t *et;
    unsigned en;
    b->w = 0;
    b->a = sz ? SA : LA;
    b->d = sz ? SD : LD;
    while (i < n) {
        o = t[i];
        c.a = b->a;
        c.d = b->d;
        if (o == 0x06 && i + 1 < n && t[i + 1] == 0x06) {         /* a matrix */
            m = s_match(t, i, n);
            s_mat(t + i + 1, (m < n ? m : n) - i - 1, x, by, sz, col, dr, &e);
            c.w = e.w;
            c.a = e.a;
            c.d = e.d;
            i = m < n ? m + 1 : n;
        } else if (o == 0x82) {                                 /* a times sign: a dot */
            if (dr)
                fill(x + 2, by - (sz ? 4 : 5), 2, 2, col);
            c.w = sz ? 6 : 8;
            i++;
        } else if (s_ob(t, i, n)) {
            tl = s_tl(t, i, n);
            m = s_match(t, i, n);
            s[0] = 0;
            if (o == 0x10) { s[0] = '('; s[1] = 0; }
            else if (o == 0xBC) { s[0] = 0x10; s[1] = 0; }
            else if (o != 0xB2)
                steps_tok_(t + i, s);
            lw = o == 0xB2 ? 5 : s_tw(s, sz);
            s_mx(t + i + tl, m - i - tl, x + lw, by, sz, col, dr, &e);
            if (e.a < c.a) e.a = c.a;
            if (e.d < c.d) e.d = c.d;
            if (o == 0xB2) {
                if (dr)
                    fill(x + 1, by - e.a + 2, 1, e.a + e.d - 3, col);
            } else if (o == 0x10 && e.a + e.d > c.a + c.d)
                s_brk(x, by, &e, 0, col, dr);
            else
                s_tx(x, by, s, sz, col, dr);
            c.w = lw + e.w;
            if (o == 0xBC && dr)
                fill(x + lw - 2, by - e.a + 1, e.w + (m < n ? 4 : 2), 1, col);
            if (m < n) {
                if (o == 0xB2) {
                    if (dr)
                        fill(x + c.w + 2, by - e.a + 2, 1, e.a + e.d - 3, col);
                    c.w += 5;
                } else if (o == 0xBC)
                    c.w += 2;
                else {
                    if (e.a + e.d > c.a + c.d)
                        s_brk(x + c.w, by, &e, 1, col, dr);
                    else
                        s_tx(x + c.w, by, o == 0x06 ? "]" : ")", sz, col, dr);
                    c.w += s_cw(')', sz);
                }
            }
            c.a = e.a;
            c.d = e.d;
            i = m < n ? m + 1 : n;
        } else {                                                /* a run of plain tokens */
            for (m = 0; i < n && m < 20; ) {
                o = t[i];
                if (m && (s_ob(t, i, n) || o == 0xF0 || o == 0x0D || o == 0x0F || o == 0x0C || o == 0x82))
                    break;
                if (o == 0xB0) {
                    s[m++] = '-';
                    i++;
                } else {
                    steps_tok_(t + i, s + m);
                    while (s[m])
                        m++;
                    i += s_tl(t, i, n);
                }
            }
            s[m] = 0;
            s_tx(x, by, s, sz, col, dr);
            c.w = s_tw(s, sz);
        }
        x += c.w;
        s_grow(b, &c);
        while (i < n && (t[i] == 0xF0 || t[i] == 0x0D || t[i] == 0x0F || t[i] == 0x0C)) {   /* a power, raised */
            if (t[i] == 0xF0) {
                j = s_ext(t, i, n);
                et = t + i + 1;
                en = j - i - 1;
                i = j;
            } else {
                et = pw + (t[i] == 0x0D ? 0 : t[i] == 0x0F ? 1 : 2);
                en = t[i] == 0x0C ? 2 : 1;
                i++;
            }
            s_strip(&et, &en);
            s_mx(et, en, x + 1, by - r, 1, col, dr, &e);
            x += e.w + 1;
            e.a += r;
            e.d = 0;
            s_grow(b, &e);
        }
    }
    b->w = x - x0;
}

/* a term: a fraction, N/D with the brackets round each dropped, then the rest; else its factors */
static void s_term(const uint8_t *t, unsigned n, int x, int by, uint8_t sz, uint16_t col, uint8_t dr, struct bx *b)
{
    unsigned q = 0, p = 0, s, e;
    int w, yb, yn, yd, x0 = x;
    struct bx nb, db, rb;
    const uint8_t *nt, *dt;
    unsigned nn, dn;
    while (q < n && t[q] != 0x83) {
        if (t[q] >= 0x07 && t[q] <= 0x09 || t[q] == 0x06 && s_match(t, q, n) >= n)   /* a lone [ ] { }: the numerator starts after it */
            p = q + 1;
        q = s_skip(t, q, n);
    }
    if (q <= p || q + 1 >= n) {
        s_items(t, n, x, by, sz, col, dr, b);
        return;
    }
    s = p + (t[p] == 0xB0 && q > p + 1);
    b->w = 0;
    b->a = sz ? SA : LA;
    b->d = sz ? SD : LD;
    if (s) {
        s_items(t, s, x, by, sz, col, dr, &rb);
        x += rb.w;
    }
    e = q + 1;
    if (t[e] >= 0x30 && t[e] <= 0x3A)
        while (e < n && t[e] >= 0x30 && t[e] <= 0x3A)
            e++;
    else
        e = s_skip(t, e, n);
    while (e < n && (t[e] == 0xF0 || t[e] == 0x0D || t[e] == 0x0F))
        e = t[e] == 0xF0 ? s_ext(t, e, n) : e + 1;
    nt = t + s;
    nn = q - s;
    s_strip(&nt, &nn);
    dt = t + q + 1;
    dn = e - q - 1;
    s_strip(&dt, &dn);
    s_mx(nt, nn, 0, 0, sz, col, 0, &nb);
    s_mx(dt, dn, 0, 0, sz, col, 0, &db);
    w = (nb.w > db.w ? nb.w : db.w) + 6;
    yb = by - (sz ? 3 : 5);
    yn = yb - nb.d + 1;                 /* ink: large rows by-12..by+1, small by-10..by-1; keep 1 clear row at the bar */
    yd = yb + db.a + 2 - 2 * sz;
    if (dr) {
        s_mx(nt, nn, x + (w - nb.w) / 2, yn, sz, col, 1, &rb);
        s_mx(dt, dn, x + (w - db.w) / 2, yd, sz, col, 1, &rb);
        fill(x + 1, yb, w - 2, 1, col);
    }
    x += w;
    b->a = by - (yn - nb.a);
    b->d = yd + db.d - by;
    if (b->a < (sz ? SA : LA)) b->a = sz ? SA : LA;
    if (b->d < (sz ? SD : LD)) b->d = sz ? SD : LD;
    if (e < n) {
        s_term(t + e, n - e, x, by, sz, col, dr, &rb);
        x += rb.w;
        s_grow(b, &rb);
    }
    b->w = x - x0;
}

/* an expression: terms between signs, relations, commas, or and and; a 0 byte switches to red and back */
static void s_mx(const uint8_t *t, unsigned n, int x, int by, uint8_t sz, uint16_t col, uint8_t dr, struct bx *b)
{
    unsigned i = 0, j;
    int x0 = x, gl, gr;
    uint16_t bc = col;
    uint8_t u = 1, o;
    char s[12];
    struct bx c;
    b->a = sz ? SA : LA;
    b->d = sz ? SD : LD;
    while (i < n) {
        for (j = i; j < n && !s_isop(t[j]); j = s_skip(t, j, n))
            ;
        if (j > i) {
            s_term(t + i, j - i, x, by, sz, col, dr, &c);
            x += c.w;
            s_grow(b, &c);
            u = 0;
        }
        if (j >= n)
            break;
        o = t[j];
        if (!o)
            col = col == bc ? C_MOVE : bc;
        else {
            s[0] = 0;
            if (o == 0x3C || o == 0x40) {
                s[0] = 'o'; s[1] = 'r'; s[2] = 0;
                if (o == 0x40) { s[0] = 'a'; s[1] = 'n'; s[2] = 'd'; s[3] = 0; }
                gl = gr = 8;
            } else {
                steps_tok_(t + j, s);
                gl = o == 0x2B ? 0 : o == 0x70 || o == 0x71 ? (u ? 0 : 4) : (u ? 0 : 8);
                gr = o == 0x2B ? 6 : o == 0x70 || o == 0x71 ? (u ? 0 : 4) : 8;
            }
            s_tx(x + gl, by, s, sz, col, dr);
            x += gl + s_tw(s, sz) + gr;
            u = 1;
        }
        i = j + 1;
    }
    b->w = x - x0;
}

/* the text of n tokens, at most lim characters (and a NUL) */
static unsigned detok(const uint8_t *t, unsigned n, char *o, unsigned lim)
{
    char b[12];
    unsigned i = 0, m = 0, k;
    uint8_t q;
    while (i < n) {
        q = t[i];
        if (!q) {
            i++;
            continue;
        }
        if (q == 0xF0 && i + 1 < n && (t[i + 1] == 0x32 || t[i + 1] == 0x33) &&
            (i + 2 >= n || t[i + 2] < 0x30 || t[i + 2] > 0x3A)) {      /* ^2, ^3 as the answer writes them */
            q = t[i + 1] == 0x32 ? 0x0D : 0x0F;
            steps_tok_(&q, b);
            i += 2;
        } else if (q == 0xB0) {
            b[0] = '-';
            b[1] = 0;
            i++;
        } else {
            steps_tok_(t + i, b);
            i += s_tl(t, i, n);
        }
        for (k = 0; b[k] && m < lim; k++)
            o[m++] = b[k];
    }
    o[m] = 0;
    return m;
}

/* n: the rows that fit, once full. sl/sy: row and top of the step being drawn, ml/my: of its current block (a
   title, a why, a maths row): a page never ends inside either, see s_rowy. */
struct sv { int y, by, fy, sy, my; unsigned ln, sc, n, sl, ml; uint8_t full, lv, tr, keep, bk, tall; };

/* the next row, h high: its top, or -1 when it is scrolled off or there is no room */
static int s_rowy(struct sv *v, int h)
{
    int y = v->y;
    if (v->ln++ < v->sc)
        return -1;
    if (v->full || y + h > WB) {
        if (!v->full) {                 /* the page ends here: before the step, else before the block, else mid-block */
            unsigned m = v->sl > v->sc && !v->keep ? v->sl : v->ml > v->sc ? v->ml : v->ln - 1;
            v->n = m - v->sc;
            v->bk = m == v->sl && v->sl > v->sc;         /* the page ends before a step */
            v->tall = v->sl && v->sl <= v->sc;           /* it ran out inside a step that began on or above the page's top */
            if (m < v->ln - 1) {        /* wipe what of it was drawn; the next page starts at m */
                y = m == v->sl ? v->sy : v->my;
                fill(0, y, 320, WB - y, WHITE);
                v->fy = -1;
            }
        }
        v->full = 1;
        return -1;
    }
    v->y += h;
    return y;
}

/* the first line of s that fits w pixels: its length; it ends at a space, else after a sign (brk) or anywhere */
static unsigned s_line(const char *s, int w, uint8_t brk)
{
    unsigned i, last = 0;
    int a = 0;
    for (i = 0; s[i]; i++) {
        a += s_cw((uint8_t)s[i], 1);
        if (a > w && i)
            return last ? last : i;
        if (s[i] == ' ' || (brk && (s[i] == '+' || s[i] == '-' || s[i] == '=' || s[i] == ',')))
            last = i + 1;
    }
    return i;
}

/* s in the small font as rows of 14 from x, w wide, in col on bg, the first row pad higher (its top
   in v->fy, -1 when not drawn); 1 when out of room */
static int s_words(struct sv *v, const char *s, int x, int w, uint16_t col, uint8_t brk, uint16_t bg, int pad)
{
    unsigned k, j;
    char l[64];
    int y;
    v->fy = -1;
    v->ml = v->ln;
    v->my = v->y;
    while (*s) {
        k = s_line(s, w, brk);
        if (k > 63)
            k = 63;
        y = s_rowy(v, 14 + pad);
        if (v->full)
            return 1;
        if (y >= 0) {
            for (j = 0; j < k; j++)
                l[j] = s[j];
            l[k] = 0;
            DRAWBG = bg;
            s_tx(x, y + pad + SA, l, 1, col, 1);
            if (pad)
                v->fy = y;
        }
        pad = 0;
        s += k;
        while (*s == ' ')
            s++;
    }
    return 0;
}

/* a top-level + - / of t (a leading sign too when sign): the maths needs brackets to be a factor */
static int s_hasop(const uint8_t *t, unsigned n, uint8_t slash)
{
    unsigned i;
    for (i = 0; i < n; i = s_skip(t, i, n))
        if ((slash && t[i] == 0x83) || ((t[i] == 0x70 || t[i] == 0x71 || t[i] == 0xB0) && (i || slash)))
            return 1;
    return 0;
}

/* the first relation outside brackets, or n */
static unsigned s_rel(const uint8_t *t, unsigned n)
{
    unsigned i;
    for (i = 0; i < n && !(t[i] >= 0x6A && t[i] <= 0x6F); i = s_skip(t, i, n))
        ;
    return i;
}

/* the entry e: its name and ( are e[0..*a) (0 when it has none), its first argument e[*a..*z) */
static void s_arg(const uint8_t *e, unsigned n, unsigned *a, unsigned *z)
{
    unsigned i, k;
    char b[12];
    *a = 0;
    for (i = 0; i < n && !*a; i += s_tl(e, i, n)) {
        steps_tok_(e + i, b);
        for (k = 0; b[k]; k++)
            if (b[k] == '(')
                *a = i + s_tl(e, i, n);
    }
    for (i = *a; i < n && e[i] != 0x2B && e[i] != 0x11; i = s_skip(e, i, n))
        ;
    *z = i;
}

/* the equation e with the move of rule r (0 SUB 1 ADD 2 DIV 3 MUL) by p done to both sides, into o:
   the move between 0 bytes (drawn red); its length, 0 when it does not apply */
static unsigned s_move(uint8_t *o, unsigned cap, const uint8_t *e, unsigned en, const uint8_t *p, unsigned pn, uint8_t r)
{
    unsigned k = s_rel(e, en), m = 0, sn, j;
    const uint8_t *s;
    uint8_t side, pp = s_hasop(p, pn, r >= 2), q;
    if (k == 0 || k + 1 >= en || s_rel(e + k + 1, en - k - 1) < en - k - 1)
        return 0;
#define MPUT(c) do { if (m >= cap) return 0; o[m++] = (c); } while (0)
#define MPUTS(a, l) for (j = 0; j < (l); j++) MPUT((a)[j])
    for (side = 0; side < 2; side++) {
        s = side ? e + k + 1 : e;
        sn = side ? en - k - 1 : k;
        if (r < 2) {
            MPUTS(s, sn);
            MPUT(0);
            MPUT(r ? 0x70 : 0x71);
            if (pp) MPUT(0x10);
            MPUTS(p, pn);
            if (pp) MPUT(0x11);
            MPUT(0);
        } else if (r == 2) {
            MPUT(0x10);
            MPUTS(s, sn);
            MPUT(0x11);
            MPUT(0x83);
            MPUT(0x10);
            MPUT(0);
            MPUTS(p, pn);
            MPUT(0);
            MPUT(0x11);
        } else {
            MPUT(0);
            if (pp) MPUT(0x10);
            MPUTS(p, pn);
            if (pp) MPUT(0x11);
            MPUT(0);
            q = s_hasop(s, sn, 1);
            if (q) MPUT(0x10);
            MPUTS(s, sn);
            if (q) MPUT(0x11);
        }
        if (!side)
            MPUT(e[k]);
    }
    return m;
}
#undef MPUT
#undef MPUTS

/* the maths t as a row: 2D in the large font, else the small, else wrapped text (when lin); 1 when out of room.
   pre, if any, is a name in blue before it (the command of the entry) */
/* v->tr: the header of a too long entry is being laid out: flat text, then ... */
#define S_DOTS(s, m) do { if (v->tr) { (s)[m] = (s)[(m) + 1] = (s)[(m) + 2] = '.'; (s)[(m) + 3] = 0; } } while (0)
static int s_math(struct sv *v, const uint8_t *t, unsigned n, int x, int w, uint16_t col, uint16_t bg, uint8_t lin, const char *pre)
{
    struct bx b;
    char s[150];
    int y, pw = 0;
    uint8_t lv;
    for (lv = 0, y = 0; y < (int)n && pre; y++)         /* the typed command with a matrix in it stays flat text, blue name + maths, big when it fits */
        lv = t[y] == 0x06 ? 3 : lv;
    if (v->tr && pre)
        lv = 3;
    if (lv == 3) {
        for (pw = 0; pre[pw]; pw++)
            s[pw] = pre[pw];
        y = detok(t, n, s + pw, 145 - pw);
        S_DOTS(s + pw, y);
        for (lv = 0; lv < 2 && s_tw(s, lv) > w; lv++)
            ;
        v->lv = lv;
        if (lv < 2) {
            y = s_rowy(v, (lv ? SA + SD : LA + LD) + 5);
            if (y >= 0) {
                DRAWBG = bg;
                v->by = y + (lv ? SA : LA) + 2;
                s_tx(x, v->by, pre, lv, C_MATH, 1);
                s_tx(x + s_tw(pre, lv), v->by, s + pw, lv, col, 1);
            }
            return v->full;
        }
        lv = 2;
    }
    for (; lv < 2; lv++) {
        s_mx(t, n, 0, 0, lv, col, 0, &b);
        pw = pre ? s_tw(pre, lv) : 0;
        if (b.w + pw <= w)
            break;
    }
    v->lv = lv;
    if (lv < 2) {
        y = s_rowy(v, b.a + b.d + 5);                   /* 2 above, so a root's bar clears the line over it */
        if (y >= 0) {
            DRAWBG = bg;
            v->by = y + b.a + 2;
            if (pre)
                s_tx(x, v->by, pre, lv, C_MATH, 1);
            s_mx(t, n, x + pw, v->by, lv, col, 1, &b);
        }
        return v->full;
    }
    if (!lin)
        return 0;
    for (y = 0; pre && pre[y]; y++)
        s[y] = pre[y];
    pw = detok(t, n, s + y, 145 - y);
    S_DOTS(s + y, pw);
    v->by = v->y + SA;
    return s_words(v, s, x, w, col, 1, bg, 0);
}

/* the two lines of a move and its result with their relations in one column, at x; 0 when they do not fit so */
static int s_pair(struct sv *v, const uint8_t *m, unsigned mn, const uint8_t *e, unsigned en, int x)
{
    struct bx b1, b2, c;
    int l1, l2, xe, y;
    s_mx(m, mn, 0, 0, 0, C_MATH, 0, &b1);
    s_mx(e, en, 0, 0, 0, C_MATH, 0, &b2);
    s_mx(m, s_rel(m, mn), 0, 0, 0, C_MATH, 0, &c);
    l1 = c.w;
    s_mx(e, s_rel(e, en), 0, 0, 0, C_MATH, 0, &c);
    l2 = c.w;
    xe = x + (l1 > l2 ? l1 : l2);
    if (xe - l1 + b1.w > STX + STW || xe - l2 + b2.w > STX + STW)
        return 0;
    y = s_rowy(v, b1.a + b1.d + 3);
    if (y >= 0) {
        DRAWBG = WHITE;
        s_mx(m, mn, xe - l1, y + b1.a, 0, C_MATH, 1, &c);
    }
    if (v->full)
        return 1;
    y = s_rowy(v, b2.a + b2.d + 3);
    if (y >= 0) {
        DRAWBG = WHITE;
        s_mx(e, en, xe - l2, y + b2.a, 0, C_MATH, 1, &c);
    }
    return 1;
}

/* the template t with % (the parameter p, pl tokens) and %1..%9 (its comma-separated pieces) and \1 (the
   square glyph) put in, up to a \2 or the end, into o (lim characters); where it stopped */
static const char *s_sub(const char *t, const uint8_t *p, unsigned pl, char *o, unsigned lim)
{
    unsigned m = 0, i, a, z, k;
    for (; *t && *t != 2; t++) {
        if (*t == 1)
            m += detok((const uint8_t *)"\x0D", 1, o + m, m < lim ? lim - m : 0);
        else if (*t == '%') {
            a = 0;
            z = pl;
            if (t[1] >= '1' && t[1] <= '9') {
                for (k = t[1] - '1', i = 0; k && i < pl; i = s_skip(p, i, pl))
                    k -= p[i] == 0x2B;
                a = i;
                for (z = a; z < pl && p[z] != 0x2B; z = s_skip(p, z, pl))
                    ;
                t++;
            }
            m += detok(p + a, z - a, o + m, m < lim ? lim - m : 0);
        } else if (m < lim)
            o[m++] = *t == '[' ? 0xC1 : *t;     /* TI's charset has theta where ASCII has [ */
    }
    o[m] = 0;
    return t;
}

/* the answer: a green box with "Answer:" and the maths (or the text of none), as one row */
static int s_ans(struct sv *v, const uint8_t *t, unsigned n)
{
    struct bx b;
    char s[150], l[64];
    unsigned k, j;
    int y, h, ly, lv, by;
    s[0] = 0;
    if (!n) {
        for (k = 0; k < 17; k++)
            s[k] = "No real solution"[k];
        h = 14;
        lv = 2;
    } else {
        for (lv = 0; lv < 2; lv++) {
            s_mx(t, n, 0, 0, lv, C_GREEN, 0, &b);
            if (b.w <= 242)
                break;
        }
        h = b.a + b.d;
        if (lv == 2) {
            detok(t, n, s, 149);
            for (k = 0, h = 0; s[k]; k += s_line(s + k, 242, 1))
                h += 14;
        }
    }
    y = s_rowy(v, h + 21);
    if (y < 0)
        return v->full;
    fill(STX, y + 2, STW, 1, C_LINE);
    fill(STX, y + 5, STW, 1, C_LINE);
    fill(STX, y + 9, STW, h + 8, C_GBOX);
    DRAWBG = C_GBOX;
    by = y + 13 + (lv < 2 ? b.a : SA);
    s_tx(STX + 6, by, "Answer:", 1, C_GREEN, 1);
    if (lv < 2)
        s_mx(t, n, 70, by, lv, C_GREEN, 1, &b);
    else
        for (k = 0, ly = y + 13; s[k]; k += j, ly += 14) {
            j = s_line(s + k, 242, 1);
            for (h = 0; h < (int)j && h < 63; h++)
                l[h] = s[k + h];
            l[h] = 0;
            s_tx(70, ly + SA, l, 1, C_GREEN, 1);
        }
    return 0;
}

/* The rows from row sc, drawn; 1 when more are below them, and how many rows fit in *fit (flag 0x8000: the page ends
   before a step, 0x4000: it ran out inside the page's first step, so that step is taller than a page). keep: do not
   leave a step that does not fit for the next page, cut it at a block: a step taller than a page starts here. */
static unsigned stwalk(volatile uint8_t *st, unsigned sc, unsigned *fit, unsigned keep)
{
    struct sv v;
    char b[150], w[150], nm[24];
    uint8_t mv[230];
    const char *t;
    const uint8_t *ent = (const uint8_t *)st + 4, *pe, *par, *ex;
    unsigned p = 70, i, rr, pl, el, a0, a1, pn, mn, k, cut = 0, nh = 0;
    uint32_t hs[16], h;                      /* FNV of each why shown so far: a repeat is not drawn again */
    uint8_t eq;
    v.y = WT + 2; v.ln = 0; v.sc = sc; v.n = 0; v.full = 0; v.fy = -1; v.sl = v.ml = v.tr = v.bk = v.tall = 0; v.sy = v.my = 0; v.keep = keep;
    fill(0, WT, 320, WB - WT, WHITE);
    DRAWBG = WHITE;
    k = st[3];
    if (!k && st[4]) {                      /* a long entry: its first tokens that whole fit, then ... */
        for (; k + s_tl(ent, k, 61) <= 60; k += s_tl(ent, k, 61))
            ;
        v.tr = 1;
    }
    s_arg(ent, k, &a0, &a1);
    detok(ent, a0, nm, 23);
    s_math(&v, ent + a0, k - a0, STX, STW, BLACK, WHITE, 1, nm);
    v.tr = 0;       /* the command's name is blue, as on the home screen */
    pe = ent + a0;
    pn = a1 - a0;
    for (i = 0; i < st[1] && !v.full; i++) {
        if (p + 3 > 0x3B0 || p + 3 + (st[p + 1] & 127) + st[p + 2 + (st[p + 1] & 127)] > 0x3B0) {
            cut = 1;                        /* never read past the log */
            break;
        }
        v.sl = v.ln;
        v.sy = v.y;
        rr = st[p] | (st[p + 1] >> 7) << 8; pl = st[p + 1] & 127;   /* rule bit 8 is in plen's top bit */
        par = (const uint8_t *)st + p + 2;
        el = st[p + 2 + pl];
        ex = par + pl + 1;
        for (t = R; rr; rr--)
            t = next(t);
        rr = st[p] | (st[p + 1] >> 7) << 8;
        eq = *t == '=';
        t = s_sub(t + eq, par, pl, b, 149);
        if (*t == 2)
            s_sub(t + 1, par, pl, w, 149);
        else
            w[0] = 0;
        k = i + 1 > 9;
        nm[0] = k ? '0' + (i + 1) / 10 : '0' + (i + 1);
        nm[1] = k ? '0' + (i + 1) % 10 : 0;
        nm[2] = 0;
        s_words(&v, b, STX + 26, STW - 26, BLACK, 0, WHITE, 6);
        if (v.fy >= 0) {
            fill(STX, v.fy + 2, STW, 1, C_LINE);
            fill(STX, v.fy + 6, 22, 14, C_MATH);
            DRAWBG = C_MATH;
            s_tx(STX + 11 - s_tw(nm, 1) / 2, v.fy + 6 + SA, nm, 1, WHITE, 1);
        }
        if (v.full)
            break;
        for (k = 0, h = 2166136261u; w[k]; k++)
            h = (h ^ (uint8_t)w[k]) * 16777619u;
        for (k = 0; w[0] && k < nh; k++)
            if (hs[k] == h)
                w[0] = 0;                   /* the same why as an earlier step: title and maths only */
        if (w[0] && nh < 16)
            hs[nh++] = h;
        if (w[0])
            s_words(&v, w, STX + 26, STW - 26, C_WHY, 0, WHITE, 0);
        if (v.full)
            break;
        v.ml = v.ln;
        v.my = v.y;
        mn = rr < 4 && !eq ? s_move(mv, 230, pe, pn, par, pl, rr) : 0;
        if (el && (!mn || !s_pair(&v, mv, mn, ex, el, STX + 26))) {
            if (eq && el < 229) {
                mv[0] = 0x6A;
                for (k = 0; k < el; k++)
                    mv[k + 1] = ex[k];
                s_math(&v, mv, el + 1, STX + 26, STW - 26, C_MATH, WHITE, 1, 0);
            } else
                s_math(&v, ex, el, STX + 26, STW - 26, C_MATH, WHITE, 1, 0);
        }
        if (el) {
            pe = ex;
            pn = el;
        }
        p += 3 + pl + el;
    }
    v.sl = 0;
    if (!v.full && !cut && !st[1] && !(st[2] & 0x80))
        s_words(&v, "No worked steps for this command yet. The answer is below.", STX, STW, C_WHY, 0, WHITE, 4);
    if (!v.full && (cut || st[2] & 1))
        s_words(&v, "(more steps not shown)", STX, STW, C_WHY, 0, WHITE, 4);
    v.ml = v.ln;
    v.my = v.y;
    if (!v.full && !cut)
        s_ans(&v, (const uint8_t *)st + p + 1, st[p]);
    *fit = v.n | v.bk << 15 | v.tall << 14;
    return v.full;
}

#define C_YEL    0xFFE0
/* the black bar at the bottom: the keys, and which way there is more (arrows are the large font's 0xEF up and 0xF0 down) */
static void s_foot(unsigned sc, unsigned more)
{
    fill(0, 220, 320, 20, BLACK);
    DRAWBG = BLACK;
    s_tx(4, 236, "\xEF\xF0", 0, WHITE, 1);
    s_tx(32, 236, "scroll   CLEAR back", 1, WHITE, 1);
    if (more)
        s_tx(236, 236, "more", 1, C_YEL, 1);
    if (sc)
        s_tx(278, 236, "\xEF", 0, C_YEL, 1);
    if (more)
        s_tx(296, 236, "\xF0", 0, C_YEL, 1);
}

/* MathPrint draws a wide answer cut off at x=320 with no mark. The history's dashed line (2 px on, 4 off, two rows)
   is the answer's bottom: the rows of ink above it in the last 24 columns are the answer's, and get a gap and "..." */
static unsigned s_sep(unsigned y)
{
    volatile uint16_t *r = VRAM + y * 320;
    unsigned k;
    for (k = 0; k < 50; k++)
        if (r[6 * k + 2] == WHITE || r[6 * k + 3] == WHITE || r[6 * k] != WHITE || r[6 * k + 4] != WHITE)
            return 0;
    return 1;
}

static void s_cut(void)
{
    unsigned y = 239, a0 = 0, a1 = 0, x, gap = 0;
    while (y > 30 && !s_sep(y))
        y--;
    while (y > 30 && s_sep(y))
        y--;
    for (; y > 30 && !s_sep(y); y--) {
        for (x = 296; x < 320 && VRAM[y * 320 + x] == WHITE; x++)
            ;
        if (x < 320) {
            if (!a1)
                a1 = y;
            a0 = y;
            gap = 0;
        } else if (a1 && ++gap > 4)
            break;
    }
    if (!a1)
        return;
    fill(306, a0, 14, a1 - a0 + 1, WHITE);
    for (x = 0; x < 3; x++)
        fill(309 + 4 * x, (a0 + a1) / 2, 2, 2, BLACK);
}

void steps_show(void)
{
    volatile uint8_t *st = STLOG;
    uint16_t fg = DRAWFG, bg = DRAWBG;
    uint8_t font = FONTFL, tf = TEXTFL, k;
    volatile uint8_t *lg = (volatile uint8_t *)(SAVE + 12 + 80 * 210);   /* the log's copy, under the literals */
    uint16_t *lit = (uint16_t *)(lg + 0x3B0);
    volatile uint16_t *p = VRAM + 30 * 320;
    unsigned sc = 0, more, i, fit, hn = 0;
    uint16_t hs[32];                        /* the rows each page began at, for UP */
    if (st[0] != 0xA5)
        return;
    st[0] = 0;
    for (i = 0; i < 0x3B0; i++)             /* save_px's literals would run over the log */
        lg[i] = st[i];
    st = lg;
    save_px(p, 0, 80, 210, lit);
    FONTFL = font | 4;
    TEXTFL = tf & ~8;
    fill(0, 30, 320, 210, WHITE);
    fill(0, 30, 320, 18, BLACK);
    DRAWBG = BLACK;
    if (st[2] & 0x80)
        s_tx(124, 30 + 14, "ANSWER", 0, WHITE, 1);
    else
        s_tx(130, 30 + 14, "STEPS", 0, WHITE, 1);
    for (;;) {
        more = stwalk(st, sc, &fit, 0);
        if (fit & 0x8000) {                     /* the page ends before a step: if that step is taller than a page it starts here */
            stwalk(st, sc + (fit & 0x3FFF), &i, 0);
            more = stwalk(st, sc, &fit, i >> 14 & 1);
        }
        fit &= 0x3FFF;
        s_foot(sc, more);
        while (!(k = os_getcsc()))
            ;
        if (k == SK_DOWN && more) {             /* a page down: the first row not shown, at a step or block start */
            if (hn < 32)
                hs[hn++] = sc;
            sc += fit ? fit : 1;
        } else if (k == SK_UP && sc)
            sc = hn ? hs[--hn] : sc - 1;
        else if (k == SK_CLEAR || k == SK_ENTER || k == SK_2ND || k == 0x30 || (st[2] & 0x80 && k != SK_UP && k != SK_DOWN))
            break;                              /* the answer view closes on any other key (consumed), steps( only on these */
    }
    restore_px(p, 0, 80, 210, lit);
    sync_px(p, PSHADOW1, PSHADOW2, 210);
    DRAWFG = fg;
    DRAWBG = bg;
    FONTFL = font;
    TEXTFL = tf;
    if (st[2] & 0x80 && !(*(volatile uint8_t *)0xD000C4 & 0x20)) {
        /* Classic wrapped the answer over two rows (the cursor on the first), but the OS thinks a record is one row: the
           next entry would draw over the second, and UP's redraw (the record clipped to a row, 0xCE last) over the first.
           So the home screen shows it as that redraw does: the first row, clipped, and the second row clear. */
        volatile uint8_t *r = (volatile uint8_t *)0xD00595;
        char row[27];
        uint8_t y = *r - 1;                 /* the cursor is on the second row */
        if (y && y < 9) {
            for (k = 0; k < 26; k++)
                row[k] = ((volatile char *)0xD006C0)[26 * y + k];
            row[25] = (char)0xCE;
            row[26] = 0;
            *r = y;
            CURCOL = 0;
            os_str(PUTS, row);              /* 26 chars wrap the cursor to the next row */
            *r = y + 1;
            CURCOL = 0;
            os_str(ERASEEOL, "");
        }
    } else if (st[2] & 0x80)
        s_cut();
}

/* + in 2nd MATH, from the raw key hook (khook in app/main.asm: the OS has no
   menu hook event for it, and its own Catalog Help would explain the item's key
   code 0x21, a comma or whatever that token is, not the command). On a SymCE
   tab's item: the help screen, and 1 (the key is eaten) or the key the OS is to
   take in its place. 0: not ours, the OS's + goes on. */
unsigned menu_help(void)
{
    const char *s, *e;
    if (MENU[0] != 9 || CURAPP != 0x40 || MENU[1] < 3)
        return 0;
    s = item(menu(tops[MENU[1] - 3]), MENU[2]);
    if (!*s || sub(s) || !(e = hfind(*next(s) ? next(s) : s)))
        return 1;               /* a submenu's name, say: nothing to show */
    return help(e);
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
    6, 6, 4, 16, 12, 15, 13, 0x54, 0x33, 0x74, 0, 0, 0,
    0xFE, 0x0A, 0xFE, 0x0B, 0xFE, 0x0C, 0xFE, 0x0D, 0xFE, 0x0E, 0xFE, 0x0F,
    0xFE, 0x10, 0xFE, 0x11, 0xFE, 0x12, 0xFE, 0x13,
    0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8,
    0, 9, 0, 10, 0, 11, 0, 12, 0, 13, 0, 14, 0, 15, 0, 16,
    0, K_SYMCE, 1, K_SYMCE, 2, K_SYMCE, 3, K_SYMCE, 4, K_SYMCE,
    5, K_SYMCE, 6, K_SYMCE, 7, K_SYMCE, 8, K_SYMCE, 9, K_SYMCE, 0x0A, K_SYMCE, 0x0B, K_SYMCE,
    0x10, K_SYMCE, 0x11, K_SYMCE, 0x12, K_SYMCE, 0x13, K_SYMCE, 0x14, K_SYMCE,
    0x15, K_SYMCE, 0x16, K_SYMCE, 0x17, K_SYMCE, 0x18, K_SYMCE, 0x19, K_SYMCE,
    0x1A, K_SYMCE, 0x1B, K_SYMCE, 0x1C, K_SYMCE, 0x1D, K_SYMCE, 0x1E, K_SYMCE,
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

/* The error screens: what a SymCE error says below TI's title. hook.c throws
   error 0x2B with the engine's record in appErr1: "TITLE",0,"DETAIL",0,0, where
   the detail is a command's name (and, for a SYNTAX, its argument list) or a
   word like PARALLEL. The OS asks the localize hook (event 0xD3, lhook.c) for
   the description lines; this builds them at SAVE: the sentence for the title
   (and the detail, when a sentence is made for it), then the command's syntax
   and example from H. Entries: title, detail ("" = any), sentence. */
static const char S[] =
    "SYNTAX\0" "\0" "Check the arguments:\0"
    "ARGUMENT\0" "\0" "An argument is wrong:\0"
    "DOMAIN\0" "\0" "Not defined for these\nvalues:\0"
    "DATA TYPE\0" "\0" "An argument is not a\npolynomial in V:\0"
    "DIVIDE BY 0\0" "\0" "The expression divides by\nzero at this value.\0"
    "UNDEFINED\0" "\0" "The value is undefined at\nthis point.\0"
    "+INFINITY\0" "\0" "The limit is +INFINITY:\nit grows without bound.\0"
    "-INFINITY\0" "\0" "The limit is -INFINITY:\nit falls without bound.\0"
    "AT INFINITY\0" "\0" "Only approached at\ninfinity.\0"
    "AT +INFINITY\0" "\0" "Only approached at\n+infinity.\0"
    "AT -INFINITY\0" "\0" "Only approached at\n-infinity.\0"
    "MODE\0" "USE RADIAN\0" "This needs RADIAN mode.\nPress MODE and pick\nRADIAN.\0"
    "TOO WIDE\0" "TRY MATHPRINT\0" "The answer is too wide.\nTry MathPrint (MODE).\0"
    "TOO WIDE\0" "\0" "The answer is too wide\nfor the screen.\0"
    "NO SOLUTION\0" "PARALLEL\0" "The lines are parallel:\nthey never meet.\0"
    "NO SOLUTION\0" "COLLINEAR\0" "The points are on one\nline.\0"
    "NO SOLUTION\0" "\0" "No real solution.\nFor complex ones use\nCSOLVE(.\0"
    "ALWAYS TRUE\0" "SAME LINE\0" "The lines are the same:\nevery point is common.\0"
    "ALWAYS TRUE\0" "\0" "True for every value.\0"
    "SINGULAR MAT\0" "\0" "The matrix has no inverse:\nits determinant is 0.\0"
    "INVALID DIM\0" "\0" "Needs a square matrix,\nup to 4x4 (vectors up\nto 3 long).\0"
    "DIM MISMATCH\0" "\0" "The two vectors need the\nsame length.\0"
    "SYMCE LIMIT\0" "ANS\0" "SymCE can't do this one\nexactly. Ans can't be a\nlist or equation here.\0"
    "SYMCE LIMIT\0" "\0" "SymCE can't do this one\nexactly.\0"
    "\0";

static uint8_t same(const char *a, const char *b)
{
    while (*a == *b) {
        if (!*a)
            return 1;
        a++, b++;
    }
    return 0;
}

/* The entry of H for the command r starts with: SOLVE, SOLVE(A=B,X) and
   SOLVE( are all SOLVE(; DERIVAT is not DERIV( . */
static const char *hpre(const char *r)
{
    const char *h = H;
    for (; *h; h = next(next(next(next(h))))) {
        const char *a = h, *b = r;
        while (*a && *a != '(' && *a == *b)
            a++, b++;
        if ((!*a || (*a == '(' && !a[1])) && (!*b || *b == '('))
            return h;
    }
    return 0;
}

static char *eline(char *o, const char *s)       /* s's lines, NUL for each \n, one NUL after */
{
    for (; *s; s++)
        *o++ = *s == '\n' ? 0 : *s;
    *o++ = 0;
    return o;
}

/* 1: the lines are at SAVE, two NULs after the last; 0: no text for this one. */
uint8_t menu_err(void)
{
    const char *t = (const char *)0xD025A9, *r = next(t), *s = S, *e;
    char *o = (char *)SAVE;
    for (; *s; s = next(next(next(s))))
        if (same(s, t) && (!*next(s) || same(next(s), r)))
            break;
    if (!*s)
        return 0;
    o = eline(o, next(next(s)));
    if (!*next(s) && (e = hpre(r))) {
        o = eline(o, next(e));
        o = eline(o, next(next(next(e))));
    }
    *o = 0;
    return 1;
}

/* The entry points, first in the image (src/engine.ld): the homescreen hook
   jumps to +0, the menu hook to +4, the raw key hook to +8, the localize hook's error text to +12, steps +16, the paren pass +20, err_save +24, err_put +28. */
__asm__(
"	.section	.text._symce_menu,\"ax\",@progbits\n"
"	.globl	_symce_menu\n"
"_symce_menu:\n"
"	jp	_menu_key\n"
"	jp	_menu_tab\n"
"	jp	_menu_help\n"
"	jp	_menu_err\n"
"	jp	_steps_show\n"
"	jp	_paren_pass\n"
"	jp	_err_save\n"
"	jp	_err_put\n"
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
"	.section	.text._steps_tok_,\"ax\",@progbits\n"
"	.globl	_steps_tok_\n"
"_steps_tok_:\n"                 /* (t, out): the text of the token at t (GetTokString) into out */
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 t, +9 out */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 6)\n"
"	call	0x005E43B\n"
"	call	0x00A2C17\n"          /* HL = length, then the characters */
"	ld	bc, 0\n"
"	ld	c, (hl)\n"
"	inc	hl\n"
"	ld	de, (ix + 9)\n"
"	ld	a, c\n"
"	or	a, a\n"
"	jr	z, .Lstk0\n"
"	ldir\n"
".Lstk0:\n"
"	xor	a, a\n"
"	ld	(de), a\n"
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
"	ld	hl, (ix + 21)\n"        /* literal header at lit: +0 count, +1 16 colours, +33 next index byte (3); indices from +36 */
"	ld	bc, 36\n"
"	add	hl, bc\n"
"	ex	de, hl\n"
"	ld	hl, (ix + 21)\n"
"	ld	(hl), 0\n"
"	ld	bc, 33\n"
"	add	hl, bc\n"
"	ld	(hl), de\n"
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
".Lslit:\n"                      /* 3: the colour's index in the table at lit, one byte, if one more fits */
"	push	bc\n"
"	push	de\n"
"	ld	hl, (ix + 21)\n"
"	ld	b, (hl)\n"
"	inc	hl\n"
"	ld	c, 0\n"
"	ld	a, b\n"
"	or	a, a\n"
"	jr	z, .Lsadd\n"
".Lsfind:\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	cp	a, e\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	jr	nz, .Lsfn\n"
"	cp	a, d\n"
"	jr	z, .Lsgot\n"
".Lsfn:\n"
"	inc	c\n"
"	djnz	.Lsfind\n"
".Lsadd:\n"                     /* c = count, hl = the table's end */
"	ld	a, c\n"
"	cp	a, 16\n"
"	jr	nc, .Lsfull\n"
"	ld	(hl), e\n"
"	inc	hl\n"
"	ld	(hl), d\n"
"	ld	hl, (ix + 21)\n"
"	inc	(hl)\n"
"	jr	.Lsgot\n"
".Lsfull:\n"
"	ld	c, 0xFF\n"               /* a 17th colour: restore_px gives black */
".Lsgot:\n"                     /* c = the index */
"	ld	hl, (ix + 21)\n"
"	ld	de, 33\n"
"	add	hl, de\n"
"	push	hl\n"
"	ld	de, (hl)\n"
"	ld	hl, 0x0D13D5E\n"
"	or	a, a\n"
"	sbc	hl, de\n"
"	pop	hl\n"
"	jr	c, .Lsnolit\n"           /* no room: 3 with no literal (restore_px: black) */
"	ex	de, hl\n"
"	ld	(hl), c\n"
"	inc	hl\n"
"	ex	de, hl\n"
"	ld	(hl), de\n"
".Lsnolit:\n"
"	pop	de\n"
"	pop	bc\n"
"	scf\n"
"	rr	c\n"
"	scf\n"
"	rr	c\n"
"	jp	.Lsnext\n"
"	.section	.text._restore_px,\"ax\",@progbits\n"
"	.globl	_restore_px\n"
"_restore_px:\n"                 /* (p, skip, w4, h, lit) */
"	push	ix\n"
"	push	iy\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"
"	ld	iy, (ix + 9)\n"
"	ld	hl, (ix + 21)\n"        /* the next index byte := lit + 36 */
"	ld	bc, 36\n"
"	add	hl, bc\n"
"	ex	de, hl\n"
"	ld	hl, (ix + 21)\n"
"	ld	bc, 33\n"
"	add	hl, bc\n"
"	ld	(hl), de\n"
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
"	push	bc\n"                    /* 3: the next index byte, if save_px had room (its pointer <= 0xD13D5E) */
"	ld	hl, (ix + 21)\n"
"	ld	bc, 33\n"
"	add	hl, bc\n"
"	push	hl\n"
"	ld	de, (hl)\n"
"	ld	hl, 0x0D13D5E\n"
"	or	a, a\n"
"	sbc	hl, de\n"
"	pop	hl\n"
"	jr	c, .Lrnolit\n"
"	ex	de, hl\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	ex	de, hl\n"
"	ld	(hl), de\n"
"	ld	hl, (ix + 21)\n"
"	cp	a, (hl)\n"
"	jr	nc, .Lrnolit\n"           /* 0xFF: a colour past the table's 16 */
"	inc	hl\n"
"	add	a, a\n"
"	ld	c, a\n"
"	ld	b, 0\n"
"	add	hl, bc\n"
"	ld	e, (hl)\n"
"	inc	hl\n"
"	ld	d, (hl)\n"
"	pop	bc\n"
"	jr	.Lrput\n"
".Lrnolit:\n"
"	ld	de, 0\n"
"	pop	bc\n"
".Lrput:\n"
"	ld	(iy + 0), e\n"
"	ld	(iy + 1), d\n"
"	lea	iy, iy + 2\n"
"	djnz	.Lrpx\n"
"	lea	iy, iy - 8\n"
"	pop	bc\n"
"	pop	de\n"
"	pop	hl\n"
"	jp	.Lrnext\n"
"	.section	.text._sync_px,\"ax\",@progbits\n"
"	.globl	_sync_px\n"
"_sync_px:\n"                    /* (p, s1, s2, rows): the shadows' bits = pixels not white */
"	push	iy\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"
"	ld	hl, (ix + 9)\n"
"	ld	de, (ix + 12)\n"
"	ld	iy, (ix + 15)\n"
"	ld	c, (ix + 18)\n"
"	push	de\n"
"	pop	ix\n"
".Lyrow:\n"
"	ld	b, 40\n"
".Lybyte:\n"
"	ld	d, 0\n"
"	.rept	8\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	and	a, (hl)\n"
"	inc	hl\n"
"	inc	a\n"                    /* 0 for white (FFFF) */
"	add	a, 0xFF\n"             /* carry unless 0 */
"	rl	d\n"
"	.endr\n"
"	ld	(ix + 0), d\n"
"	ld	(iy + 0), d\n"
"	inc	ix\n"
"	inc	iy\n"
"	djnz	.Lybyte\n"
"	dec	c\n"
"	jr	nz, .Lyrow\n"
"	pop	ix\n"
"	pop	iy\n"
"	ret\n"
);

/* The OS's Quit (and 2:Goto) from an error screen redraws home from its 1-bit pixel shadows in black, and
   the font hook is not asked for a glyph, so the history lost its rainbow parentheses and blue letters
   until a line was highlighted (*measured*, SymCE's errors and the OS's own alike). hook.c calls err_save
   (+24, no arguments, keeps ix and iy) before it throws: every pixel under the status bar (200 rows from
   y = 30) that is neither white nor black goes to the list at 0xD0F004 as (VRAM address, colour), 5 bytes,
   up to 0xD13700 (3600 pixels; past that the rest stay black), the end at 0xD0F000, errFl (0xD0EE1E) = 0xA5.
   fhook.c's cursor-glyph pass calls err_put (+28) once the home screen is back: it puts each colour back
   where the pixel is black now, and clears errFl (a RAM that was never set up has no 0xA5 there, and a list end out of range is ignored).
   ponytail: a 2D layout that moved after Quit gets a few wrong pixels, only on black ones. */
__asm__(
"	.section	.text._err_save,\"ax\",@progbits\n"
"	.globl	_err_save\n"
"_err_save:\n"
"	ld	hl, 0x0D44B00\n"           /* VRAM row 30 */
"	ld	de, 0x0D0F004\n"
"	ld	bc, 64000\n"
".Lesv:\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	and	a, (hl)\n"
"	inc	a\n"
"	jr	z, .Lesw\n"                 /* white */
"	dec	hl\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	or	a, (hl)\n"
"	jr	z, .Lesw\n"                 /* black */
"	dec	hl\n"
"	ex	de, hl\n"
"	ld	(hl), de\n"                 /* the address of its low byte */
"	inc	hl\n"
"	inc	hl\n"
"	inc	hl\n"
"	ex	de, hl\n"
"	ld	a, (hl)\n"
"	ld	(de), a\n"
"	inc	hl\n"
"	inc	de\n"
"	ld	a, (hl)\n"
"	ld	(de), a\n"
"	inc	de\n"
"	push	hl\n"
"	ld	hl, 0x0D13700\n"
"	or	a, a\n"
"	sbc	hl, de\n"
"	pop	hl\n"
"	jr	c, .Lesd\n"                 /* full */
".Lesw:\n"
"	inc	hl\n"
"	dec	bc\n"
"	ld	a, b\n"
"	or	a, c\n"
"	jr	nz, .Lesv\n"
".Lesd:\n"
"	ld	hl, 0x0D0F000\n"
"	ld	(hl), de\n"
"	ld	hl, 0x0D0F004\n"
"	or	a, a\n"
"	sbc	hl, de\n"
"	ret	z\n"
"	ld	a, 0xA5\n"
"	ld	(0x0D0EE1E), a\n"            /* errFl */
"	ret\n"
"	.section	.text._err_put,\"ax\",@progbits\n"
"	.globl	_err_put\n"
"_err_put:\n"
"	xor	a, a\n"
"	ld	(0x0D0EE1E), a\n"
"	ld	hl, 0x0D13706\n"          /* a list end past what err_save writes: not ours */
"	ld	de, (0x0D0F000)\n"
"	or	a, a\n"
"	sbc	hl, de\n"
"	ret	c\n"
"	ld	hl, 0x0D0F004\n"
".Lepu:\n"
"	ld	de, (0x0D0F000)\n"
"	or	a, a\n"
"	sbc	hl, de\n"
"	ret	nc\n"
"	add	hl, de\n"
"	ld	de, (hl)\n"
"	inc	hl\n"
"	inc	hl\n"
"	inc	hl\n"
"	ld	a, (de)\n"
"	inc	de\n"
"	ld	c, a\n"
"	ld	a, (de)\n"
"	or	a, c\n"
"	dec	de\n"
"	jr	nz, .Lepn\n"                /* not black any more */
"	ld	a, (hl)\n"
"	ld	(de), a\n"
"	inc	hl\n"
"	inc	de\n"
"	ld	a, (hl)\n"
"	ld	(de), a\n"
"	inc	hl\n"
"	jr	.Lepu\n"
".Lepn:\n"
"	inc	hl\n"
"	inc	hl\n"
"	jr	.Lepu\n"
);

/* MathPrint's parentheses are vector segments the OS draws in black, never through the font hook, so
   fhook.c calls this (the menu blob's entry +20) when it is asked for a cursor glyph, which is the last
   thing a redraw does. HL = the palette (6 RGB565 colours). Scans every 6th VRAM row under the status bar for
   the ROM large glyphs 0x28 / 0x29 in black (exact 14-row shapes, white either side of every run, so a
   letter or a tall paren is not one), and recolours each by depth, left to right inside a band (the
   same top row). A ) with nothing open stays black. State at 0xD13BB0: +0 depth, +1 band's row, +4
   palette, +7 row being scanned, +10 rows left, +11 colour. Keeps ix, iy; clobbers the rest.
   ponytail: tall (stretched) parens, small-font parens and an exponent's parens are other shapes and stay black. */
__asm__(
"	.section	.text._paren_pass,\"ax\",@progbits\n"
"	.globl	_paren_pass\n"
"_paren_pass:\n"
"	push	ix\n"
"	ld	(0x0D13BB4), hl\n"
"	xor	a, a\n"
"	ld	(0x0D13BB0), a\n"
"	ld	hl, 0\n"
"	ld	(0x0D13BB1), hl\n"
"	ld	hl, 0x0D40000 + 640 * 30\n"
"	ld	(0x0D13BB7), hl\n"
"	ld	a, 35\n"
"	ld	(0x0D13BBA), a\n"
".Lprow:\n"
"	ld	hl, (0x0D13BB7)\n"
"	ld	bc, 640\n"
".Lpscan:\n"
"	xor	a, a\n"
"	cpir\n"
"	jp	po, .Lpnext\n"
"	bit	0, l\n"                  /* hl = the zero byte + 1: odd means the low byte of a pixel */
"	jr	z, .Lpscan\n"
"	ld	a, (hl)\n"
"	or	a, a\n"
"	jr	nz, .Lpscan\n"           /* low byte 0, high byte 0: a black pixel, hl = its high byte */
"	push	bc\n"
"	push	hl\n"
"	dec	hl\n"
"	push	hl\n"
"	call	.Lprun3\n"              /* the stem of a paren: a run of 3 black pixels on its own... */
"	pop	hl\n"
"	jr	nz, .Lpdone\n"
"	ld	c, 0\n"
".Lpup:\n"                      /* ...that goes up 0..5 more rows (the stem is rows 4..9 of the shape, and the scanned rows are 6 apart: one hit per paren) */
"	ld	de, -640\n"
"	add	hl, de\n"
"	push	hl\n"
"	call	.Lprun3\n"
"	pop	hl\n"
"	jr	nz, .Lpupd\n"
"	inc	c\n"
"	ld	a, c\n"
"	cp	a, 6\n"
"	jr	c, .Lpup\n"
"	jr	.Lpdone\n"
".Lpupd:\n"
"	ld	de, 640 - 4 * 640\n"
"	add	hl, de\n"               /* the first row of the shape, at the stem's x */
"	push	hl\n"
"	ld	de, 6\n"
"	add	hl, de\n"
"	ex	de, hl\n"               /* ( starts 3 pixels right of its stem */
"	ld	ix, .Lptopen\n"
"	call	.Lpmatch\n"
"	pop	hl\n"
"	jr	z, .Lpopen\n"
"	ld	de, -6\n"
"	add	hl, de\n"
"	ex	de, hl\n"               /* ) starts 3 pixels left of its stem */
"	ld	ix, .Lptclose\n"
"	call	.Lpmatch\n"
"	jr	z, .Lpclose\n"
".Lpdone:\n"
"	pop	hl\n"
"	pop	bc\n"
"	jr	.Lpscan\n"
".Lpband:\n"                     /* a new top row: depth 0 again */
"	ld	hl, (0x0D13BB7)\n"
"	ld	bc, (0x0D13BB1)\n"
"	or	a, a\n"
"	sbc	hl, bc\n"
"	ret	z\n"
"	ld	hl, (0x0D13BB7)\n"
"	ld	(0x0D13BB1), hl\n"
"	xor	a, a\n"
"	ld	(0x0D13BB0), a\n"
"	ret\n"
".Lpopen:\n"
"	call	.Lpband\n"
"	call	.Lpcol\n"
"	cp	a, 240\n"
"	jr	nc, .Lpdone\n"
"	ld	hl, 0x0D13BB0\n"
"	inc	(hl)\n"
"	call	.Lppaint\n"
"	jr	.Lpdone\n"
".Lpclose:\n"
"	call	.Lpband\n"
"	ld	a, (0x0D13BB0)\n"
"	or	a, a\n"
"	jr	z, .Lpdone\n"
"	dec	a\n"
"	ld	(0x0D13BB0), a\n"
"	call	.Lpcol\n"
"	call	.Lppaint\n"
"	jr	.Lpdone\n"
".Lpcol:\n"                      /* colour for depth (0x0D13BB0) mod 6 into 0x0D13BBB; a = the depth; keeps de */
"	push	de\n"
"	ld	a, (0x0D13BB0)\n"
"	ld	c, a\n"
".Lpmod:\n"
"	cp	a, 6\n"
"	jr	c, .Lpmd\n"
"	sub	a, 6\n"
"	jr	.Lpmod\n"
".Lpmd:\n"
"	add	a, a\n"
"	ld	de, 0\n"
"	ld	e, a\n"
"	ld	hl, (0x0D13BB4)\n"
"	add	hl, de\n"
"	ld	a, (hl)\n"
"	ld	(0x0D13BBB), a\n"
"	inc	hl\n"
"	ld	a, (hl)\n"
"	ld	(0x0D13BBC), a\n"
"	pop	de\n"
"	ld	a, c\n"
"	ret\n"
".Lprun3:\n"                    /* hl = a pixel: Z if it starts a run of exactly 3 black pixels */
"	dec	hl\n"
"	dec	hl\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	or	a, (hl)\n"
"	jr	z, .Lpr3f\n"
"	inc	hl\n"
"	ld	b, 3\n"
".Lpr3l:\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	or	a, (hl)\n"
"	jr	nz, .Lpr3f\n"
"	inc	hl\n"
"	djnz	.Lpr3l\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	or	a, (hl)\n"
"	jr	z, .Lpr3f\n"
"	xor	a, a\n"
"	ret\n"
".Lpr3f:\n"
"	or	a, 1\n"
"	ret\n"
".Lpmatch:\n"                    /* ix = a shape, de = its first pixel: Z if all 14 rows are there */
"	push	ix\n"
"	push	de\n"
"	ld	b, 14\n"
"	ex	de, hl\n"
".Lpmr:\n"
"	push	hl\n"
"	ld	de, -6\n"
"	add	hl, de\n"
"	ld	a, (ix + 0)\n"
"	add	a, a\n"
"	ld	de, 0\n"
"	ld	e, a\n"
"	add	hl, de\n"                /* hl = the run's first pixel */
"	dec	hl\n"
"	dec	hl\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	or	a, (hl)\n"
"	jr	z, .Lpfail\n"            /* black to its left */
"	inc	hl\n"
"	ld	c, (ix + 1)\n"
".Lpmx:\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	or	a, (hl)\n"
"	jr	nz, .Lpfail\n"           /* not black */
"	inc	hl\n"
"	dec	c\n"
"	jr	nz, .Lpmx\n"
"	ld	a, (hl)\n"
"	inc	hl\n"
"	or	a, (hl)\n"
"	jr	z, .Lpfail\n"            /* black to its right */
"	pop	hl\n"
"	ld	de, 640\n"
"	add	hl, de\n"
"	lea	ix, ix + 2\n"
"	djnz	.Lpmr\n"
"	pop	de\n"
"	pop	ix\n"
"	xor	a, a\n"
"	ret\n"
".Lpfail:\n"
"	pop	hl\n"
"	pop	de\n"
"	pop	ix\n"
"	or	a, 1\n"
"	ret\n"
".Lppaint:\n"                    /* ix = the shape, de = its first pixel */
"	ex	de, hl\n"
"	ld	b, 14\n"
".Lppr:\n"
"	push	hl\n"
"	ld	de, -6\n"
"	add	hl, de\n"
"	ld	a, (ix + 0)\n"
"	add	a, a\n"
"	ld	de, 0\n"
"	ld	e, a\n"
"	add	hl, de\n"
"	ld	c, (ix + 1)\n"
".Lppx:\n"
"	ld	a, (0x0D13BBB)\n"
"	ld	(hl), a\n"
"	inc	hl\n"
"	ld	a, (0x0D13BBC)\n"
"	ld	(hl), a\n"
"	inc	hl\n"
"	dec	c\n"
"	jr	nz, .Lppx\n"
"	pop	hl\n"
"	ld	de, 640\n"
"	add	hl, de\n"
"	lea	ix, ix + 2\n"
"	djnz	.Lppr\n"
"	ret\n"
".Lpnext:\n"
"	ld	hl, (0x0D13BB7)\n"
"	ld	de, 6 * 640\n"
"	add	hl, de\n"
"	ld	(0x0D13BB7), hl\n"
"	ld	hl, 0x0D13BBA\n"
"	dec	(hl)\n"
"	jp	nz, .Lprow\n"
"	pop	ix\n"
"	ret\n"
".Lptopen:\n"
"	.byte	3, 3, 2, 2, 1, 3, 1, 2, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 1, 2, 1, 3, 2, 2, 3, 3\n"
".Lptclose:\n"
"	.byte	3, 3, 5, 2, 5, 3, 6, 2, 6, 3, 6, 3, 6, 3, 6, 3, 6, 3, 6, 3, 6, 2, 5, 3, 5, 2, 3, 3\n"
);
