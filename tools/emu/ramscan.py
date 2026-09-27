#!/usr/bin/env python3
"""Which fixed RAM does the OS leave alone while the home screen is in use?

The hook body currently lives in an archived appvar, so its address moves
whenever the OS garbage collects the archive -- and the armed pointer then aims
at whatever took that address. A FIXED RAM address cannot move. This finds one:
hash each window at boot, drive the home screen, hash it again. Unchanged means
the OS did not write there.

No program needs to run, which is why this works with nothing installed.

(An older note here said nothing can execute on this ROM because arTIfiCE does
not support OS 5.8.4. That was false and it blocked every end-to-end test:
arTIfiCE v2.1 launches on 5.8.4, and this ROM already has the AsmHook2 app in
flash, which arms the parser hook so prgmSYMCE runs straight from the home
screen. See e2e.py. Do NOT test through arTIfiCE though -- leaving it zeroes
the hook block and the body at 0xD0F000.)
"""
import json, os, re, subprocess, sys
HERE=os.path.dirname(os.path.abspath(__file__))
ROM=os.path.expanduser("~/CEdev/ti84pce.rom")
TESTER=os.path.expanduser("~/CEdev/bin/cemu-autotester")

LO, HI, WIN = 0xD02600, 0xD1A000, 2048

def scan(keys, lo=LO, hi=HI, win=WIN, boot=2500, per_key=200):
    wins=[(a, min(win, hi-a)) for a in range(lo, hi, win)]
    seq=["delay|%d"%boot]
    hashes={}
    n=0
    for a,sz in wins:                      # baseline, before touching anything
        n+=1; seq.append("hash|%d"%n)
        hashes[str(n)]={"description":"A%06X"%a,"start":hex(a),"size":str(sz),
                        "expected_CRCs":["00000000"]}
    for k in keys.split():
        seq += ["delay|1500"] if k=="." else ["key|"+k,"delay|%d"%per_key]
    seq += ["delay|1500"]
    for a,sz in wins:                      # and again, after
        n+=1; seq.append("hash|%d"%n)
        hashes[str(n)]={"description":"B%06X"%a,"start":hex(a),"size":str(sz),
                        "expected_CRCs":["00000000"]}
    cfg={"transfer_files":["DEMO.8xp"],"target":{"name":"DEMO","isASM":True},
         "sequence":seq,"hashes":hashes}
    p=os.path.join(HERE,"_scan.json"); json.dump(cfg,open(p,"w"))
    r=subprocess.run([TESTER,p],capture_output=True,text=True,
                     env=dict(os.environ,AUTOTESTER_ROM=ROM),cwd=HERE)
    if "unknown key" in r.stderr: sys.exit("BAD KEY:\n"+r.stderr)
    got={}
    for _,name,c in re.findall(r'Hash #(\d+) \("([^"]*)"\).*?got ([0-9A-F]{1,8})\)', r.stdout):
        got[name]=c
    return wins, got

if __name__=="__main__":
    keys = sys.argv[1] if len(sys.argv)>1 else "clear 2 xton + 2 xton enter . 3 xton enter ."
    wins, got = scan(keys)
    safe=[]
    for a,sz in wins:
        x,y = got.get("A%06X"%a), got.get("B%06X"%a)
        if x is None or y is None: print("%06X  MISSING"%a); continue
        if x==y: safe.append((a,sz))
    print("keys: %s" % keys)
    print("%d/%d windows untouched" % (len(safe), len(wins)))
    # report contiguous runs, which is what a 1588-byte body needs
    runs=[]
    for a,sz in safe:
        if runs and runs[-1][1]==a: runs[-1][1]=a+sz
        else: runs.append([a,a+sz])
    for lo,hi in runs:
        print("  untouched %06X..%06X  (%d bytes)" % (lo,hi,hi-lo))
