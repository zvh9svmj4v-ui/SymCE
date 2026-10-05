/* 2nd DRAW on SymCE's grapher: the state (struct dst, a field of struct gw). The code is graph_draw.c,
   included by graph.c. */
enum { D_CLR = 1, D_LINE, D_HORZ, D_VERT, D_TANG, D_CIRC, D_TEXT, D_PEN, D_PTON, D_PTOFF, D_PTCHG };

struct dst {
    uint8_t dm, ds;              /* the tool (D_*) or 0; its stage: 0 waiting for the first point, 1 the first point is set */
    uint8_t cm, nib;             /* drawing: 0 = a preview (VRAM only, logged), 1 = into the layer too; the layer value */
    uint8_t on, ovf;             /* a preview is up; the undo log overflowed (the picture is redrawn instead) */
    int px, py;                  /* the previous point of the thick line */
    int x0, y0;                  /* the first point (Line), the centre (Circle) */
    unsigned n;                  /* undo entries */
};
