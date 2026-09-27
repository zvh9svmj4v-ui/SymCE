#!/usr/bin/env python3
"""Run SYMCE under arTIfiCE and ask whether the hook survives leaving the shell.

The claim "arTIfiCE restores the whole hook block when it quits" was measured
once, in the same session that also measured a key name that did not exist. It
gates every end-to-end test, so re-measure it.
"""
import json, os, re, subprocess, sys, crcread
HERE=os.path.dirname(os.path.abspath(__file__))
ROM=os.path.expanduser("~/CEdev/ti84pce.rom")
TESTER=os.path.expanduser("~/CEdev/bin/cemu-autotester")

def probe(keys, probes, files=("arTIfiCE.8xp","SYMCE.8xp"),
          boot=1500, settle=1500, per_key=200):
    seq=["delay|%d"%boot,"action|launch","delay|4000"]
    for k in keys.split():
        if k==".":   seq += ["delay|2000"]
        elif k=="L": seq += ["action|launch","delay|4000"]   # re-launch prgmA
        else:        seq += ["key|"+k,"delay|%d"%per_key]
    seq += ["delay|%d"%settle]
    hashes={}
    for i,(n,a,sz) in enumerate(probes,1):
        seq.append("hash|%d"%i)
        hashes[str(i)]={"description":n,"start":a,"size":str(sz),
                        "expected_CRCs":["00000000"]}
    cfg={"transfer_files":list(files),"target":{"name":"A","isASM":False},
         "sequence":seq,"hashes":hashes}
    p=os.path.join(HERE,"_art.json"); json.dump(cfg,open(p,"w"))
    r=subprocess.run([TESTER,p],capture_output=True,text=True,
                     env=dict(os.environ,AUTOTESTER_ROM=ROM),cwd=HERE)
    if "unknown key" in r.stderr: sys.exit("BAD KEY:\n"+r.stderr)
    out={}
    for _,name,c in re.findall(r'Hash #(\d+) \("([^"]*)"\).*?got ([0-9A-F]{1,8})\)', r.stdout):
        sz=dict((x[0],x[2]) for x in probes)[name]
        out[name]=crcread.invert(int(c,16),sz)
    return out

P=[("hookptr","0xD025E1",3),("hookflag","0xD000B4",1),("cx","0xD007E0",1)]
if __name__=="__main__":
    for label,keys in [("in shell, after SYMCE", sys.argv[1] if len(sys.argv)>1 else "enter . enter . clear"),
                       ]:
        r=probe(keys,P)
        print("%-24s ptr=%s flag=%s cx=%s" % (label,
              r["hookptr"].hex(), r["hookflag"].hex(), r["cx"].hex()))
