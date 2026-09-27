#!/usr/bin/env python3
"""Does editTop move when the cursor enters a MathPrint box?

If it does, a hook can tell "inside a box" from "flat entry" WITHOUT any flag
guess: record editTop while the line is still virgin, compare at ENTER. The
baseline moves as history grows, which is why it has to be captured per entry
rather than hardcoded -- that is what killed every earlier attempt.
"""
import json, os, re, subprocess, sys, crcread
HERE=os.path.dirname(os.path.abspath(__file__))
ROM=os.path.expanduser("~/CEdev/ti84pce.rom")
TESTER=os.path.expanduser("~/CEdev/bin/cemu-autotester")

def read(keys, probes, boot=2500, per_key=220):
    seq=["delay|%d"%boot]
    for k in keys.split():
        seq += ["delay|1500"] if k=="." else ["key|"+k,"delay|%d"%per_key]
    seq += ["delay|1200"]
    hashes={}
    for i,(n,a,sz) in enumerate(probes,1):
        seq.append("hash|%d"%i)
        hashes[str(i)]={"description":n,"start":a,"size":str(sz),
                        "expected_CRCs":["00000000"]}
    cfg={"transfer_files":["DEMO.8xp"],"target":{"name":"DEMO","isASM":True},
         "sequence":seq,"hashes":hashes}
    p=os.path.join(HERE,"_et.json"); json.dump(cfg,open(p,"w"))
    r=subprocess.run([TESTER,p],capture_output=True,text=True,
                     env=dict(os.environ,AUTOTESTER_ROM=ROM),cwd=HERE)
    if "unknown key" in r.stderr: sys.exit("BAD KEY:\n"+r.stderr)
    out={}
    for _,name,c in re.findall(r'Hash #(\d+) \("([^"]*)"\).*?got ([0-9A-F]{1,8})\)', r.stdout):
        sz=dict((x[0],x[2]) for x in probes)[name]
        out[name]=int.from_bytes(crcread.invert(int(c,16),sz),"little")
    return out

P=[("top","0xD02437",3),("cur","0xD0243A",3),("tail","0xD0243D",3),
   ("btm","0xD02440",3),("cmd","0xD0008C",1),("tf","0xD00085",1)]

CASES = [
 ("virgin (nothing typed)", "clear"),
 ("flat 2X+2X",             "clear 2 xton + 2 xton"),
 ("cube root, X inside",    "clear math 4 xton"),
 ("x-th root, X inside",    "clear math 5 xton"),
 ("exponent X^2",           "clear xton ^ 2"),
 ("fraction n/d",           "clear alpha y= 1"),
 ("after one entry, virgin","clear 1 + 1 enter . clear"),
 ("after one entry, flat",  "clear 1 + 1 enter . 2 xton + 2 xton"),
 ("after one entry, root",  "clear 1 + 1 enter . math 4 xton"),
]
if __name__=="__main__":
    print("%-26s %-8s %-8s %-8s %-8s cmd tf" % ("state","top","cur","tail","btm"))
    for label,keys in CASES:
        r=read(keys,P)
        print("%-26s %06X   %06X   %06X   %06X   %02X  %02X" %
              (label,r["top"],r["cur"],r["tail"],r["btm"],r["cmd"],r["tf"]))
