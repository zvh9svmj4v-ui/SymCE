"""STO and Ans with SymCE answers (Classic): Ans after a numeric answer, ->Y1, ->Str1, ->A."""
import sys
import e2e
import screen

PD = e2e.spell("POLYDEGREE") + " xton ^ 3 ) "
CASES = (
    ("Ans*2 after POLYDEGREE", PD + "enter . . 2nd (-) * 2 enter . .", b"6", 0),
    ("FACTOR->Y1, Y= shows it", "alpha down 2 xton ^ 2 - 1 ) sto vars right 1 1 enter . . y= .", b"(X-1)(X+1)", 0),
    ("FACTOR->Str1, recalled", "alpha down 2 xton ^ 2 - 4 ) sto vars 7 1 enter . . vars 7 1 enter . .", b"(X-2)(X+2)", 0),
    ("numeric ->A, SOLVE sees it", PD + "sto alpha math enter . . alpha down 1 xton 2nd math 1 alpha math , xton ) enter . .", b"X=3", 0),
    ("Graph Ans (item 11) fills Y1 and opens the grapher", "alpha down 2 xton ^ 2 - 1 ) enter . . alpha down up enter w3000 y= w1500 .", b"(X-1)(X+1)", 0),
    ("Graph Ans again goes to Y2", "alpha down 2 xton ^ 2 - 1 ) enter . . alpha down up enter w3000 clear w2000 alpha down 2 xton ^ 2 - 4 ) enter . . alpha down up enter w3000 y= w1500 .", b"(X-2)(X+2)", 0),
    ("symbolic ->A is an error", "alpha down 2 xton ^ 2 - 1 ) sto alpha math enter . .", b"NOT A NUMBER", 0x2B),
)


def rows(keys):
    for settle in (1500, 1700, 1900, 2100, 2300, 2500, 2700):
        r = screen.read([("errNo", 0xD008DF, 1), ("msg", 0xD025A9, 26),
                         ("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS)],
                        keys=e2e.PROLOGUE + keys, files=e2e.FILES, launch=e2e.LAUNCH, lead=False,
                        settle=settle, per_key=300)
        if b"\xff" * 4 not in r["msg"] and r["screen"].count(b".") < 100:
            break
    b = r["screen"]
    return r, [b[i * screen.COLS:(i + 1) * screen.COLS].strip(b" ") for i in range(screen.ROWS)]


def main():
    only = sys.argv[1:] 
    ok = True
    for i, (name, keys, want, err) in enumerate(CASES):
        if only and str(i) not in only:
            continue
        r, rs = rows(keys)
        got = [x for x in rs if x]
        good = (want in r["msg"] if err else any(want in x for x in got)) and r["errNo"][0] == err
        ok &= good
        print("%-4s %s" % ("ok" if good else "FAIL", name), "" if good else (got[-4:], r["msg"][:16], r["errNo"].hex()))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
