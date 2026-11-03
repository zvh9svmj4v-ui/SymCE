/* TABLE (2nd GRAPH) on SymCE's grapher: the state (struct tbl, a field of struct gw). The code is graph_table.c,
   included by graph.c. */
struct tbl {
    uint8_t on;                  /* the table is up */
    uint8_t nc;                  /* columns after X: the selected, defined Y1..Y0 */
    uint8_t fn[10];              /* each column's function, 0..9 */
    uint8_t c0;                  /* the first Y column shown (of 4) */
    uint8_t c, r, hdr;           /* the selected column (0 = X, k = fn[k-1]), row 0..10, or its header */
    uint16_t ok;                 /* bit k: column k's cells are computed for the rows shown */
    uint8_t xs[9];               /* X in the shown rows as the OS had it when the table was drawn: X is put back on leaving */
};
