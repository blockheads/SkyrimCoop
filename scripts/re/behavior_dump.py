#!/usr/bin/env python3
"""Read Skyrim's behaviour graphs (.hkx in the Animations BSA): state machines, transitions with their
event names and conditions, and which nodes write a graph variable.

    scripts/re/behavior_dump.py extract OUT_DIR             third-person character behaviours from the BSA
    scripts/re/behavior_dump.py machines FILE.hkx [NAMES]   state machines (comma-separated names) and transitions
    scripts/re/behavior_dump.py writers OUT_DIR VARIABLE    nodes in every behaviour that write VARIABLE

Needs `lz4` (pip). Layouts are Havok 2010.2 64-bit packfiles (hk_2010.2.0-r1), checked against
CommonLibSSE-NG and live graphs; docs/re/systems/actor-ai-and-copies.md has what they explained.
"""
import glob, os, re, struct, sys

import lz4.frame

GAME = os.environ.get("SKYRIM_GAME_DIR", "/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition")
def read_bsa(path, want):
    f=open(path,'rb'); d=f.read()
    magic,ver,foff,aflags,fcount,filecount,tfoldn,tfilen,fflags,_=struct.unpack_from('<4sIIIIIIIHH',d,0)
    assert magic==b'BSA\0' and ver==105, (magic,ver)
    pos=foff; folders=[]
    for i in range(fcount):
        h,cnt,_,off=struct.unpack_from('<QIIQ',d,pos); pos+=24; folders.append(cnt)
    names_pos=None
    recs=[]
    for cnt in folders:
        ln=d[pos]; dname=d[pos+1:pos+ln].rstrip(b'\0').decode(); pos+=1+ln
        for j in range(cnt):
            h,size,off=struct.unpack_from('<QII',d,pos); pos+=16
            recs.append((dname,size,off))
    # file names
    names=d[pos:pos+tfilen].split(b'\0')
    out=[]
    for (dname,size,off),fn in zip(recs,names):
        full=dname+'\\'+fn.decode()
        if not want(full): continue
        comp = bool(aflags & 4) ^ bool(size & 0x40000000)
        size &= 0x3FFFFFFF
        p=off
        if aflags & 0x100:
            ln=d[p]; p+=1+ln; size-=1+ln
        if comp:
            orig=struct.unpack_from('<I',d,p)[0]; data=lz4.frame.decompress(d[p+4:p+size])
        else:
            data=d[p:p+size]
        out.append((full,data))
    return out

class Pack:
    def __init__(self, path):
        d=open(path,'rb').read(); self.d=d
        magic0,magic1,userTag,fileVersion,layout=struct.unpack_from('<IIii4s',d,0)
        nsec,contentsSec,contentsSecOff,contentsClsSec,contentsClsOff=struct.unpack_from('<iiiii',d,0x14)
        ver=d[0x28:0x38].split(b'\0')[0]
        flags,maxpred,secoff=struct.unpack_from('<ihh',d,0x38)
        self.ptrsize=layout[0]; self.ver=ver
        hdr=0x40 + (secoff if fileVersion>=11 and secoff>0 else 0)
        self.secs={}
        for i in range(nsec):
            o=hdr+i*(0x30 if fileVersion<11 else 0x40)
            name=d[o:o+20].split(b'\0')[0].decode()
            start,lf,gf,vf,ex,im,end=struct.unpack_from('<7I',d,o+20)
            self.secs[name]=dict(start=start,lf=lf,gf=gf,vf=vf,ex=ex,im=im,end=end)
        cn=self.secs['__classnames__']; ds=self.secs['__data__']
        self.base=ds['start']
        self.local={}
        p=ds['start']+ds['lf']
        while p < ds['start']+ds['gf']:
            src,dst=struct.unpack_from('<II',d,p); p+=8
            if src==0xFFFFFFFF: continue
            self.local[src]=dst
        self.glob={}
        p=ds['start']+ds['gf']
        while p < ds['start']+ds['vf']:
            src,sec,dst=struct.unpack_from('<III',d,p); p+=12
            if src==0xFFFFFFFF: continue
            self.glob[src]=dst
        self.objs={}  # offset -> classname
        p=ds['start']+ds['vf']
        while p + 12 <= ds["start"]+ds["ex"]:
            src,sec,nameoff=struct.unpack_from('<III',d,p); p+=12
            if src==0xFFFFFFFF: continue
            nm=d[cn['start']+nameoff:].split(b'\0')[0].decode()
            self.objs[src]=nm
    def ptr(self, off):  # data-relative offset of pointer field -> target offset
        if off in self.local: return self.local[off]
        if off in self.glob: return self.glob[off]
        return None
    def i32(self,off): return struct.unpack_from('<i',self.d,self.base+off)[0]
    def u16(self,off): return struct.unpack_from('<H',self.d,self.base+off)[0]
    def i8(self,off): return struct.unpack_from('<b',self.d,self.base+off)[0]
    def f32(self,off): return struct.unpack_from('<f',self.d,self.base+off)[0]
    def cstr(self,off): return self.d[self.base+off:].split(b'\0')[0].decode('latin1')
    def strptr(self,off):
        t=self.ptr(off); return self.cstr(t) if t is not None else None
    def arr(self,off):  # hkArray: ptr(8), size(4), cap(4) -> (target, size)
        return self.ptr(off), self.i32(off+8)
    def of_class(self, cls): return [o for o,c in self.objs.items() if c==cls]



def strings(P, offset):
    sd = P.of_class('hkbBehaviorGraphStringData')[0]
    t, n = P.arr(sd + offset)
    return [P.strptr(t + 8 * i) for i in range(n)]


def machines(path, names=None):
    P = Pack(path)
    events = strings(P, 0x10)
    event = lambda i: events[i] if 0 <= i < len(events) else str(i)
    def condition(off):
        c = P.ptr(off)
        return f"{P.objs.get(c, '?')}:{P.strptr(c + 0x10)!r}" if c is not None else ''
    for sm in P.of_class('hkbStateMachine'):
        name = P.strptr(sm + 0x38)
        if names and name not in names:
            continue
        t, n = P.arr(sm + 0x90)
        states = {P.i32(P.ptr(t + 8 * i) + 0x68): (P.strptr(P.ptr(t + 8 * i) + 0x60), P.ptr(t + 8 * i)) for i in range(n)}
        def transitions(array, label):
            if array is None:
                return
            tt, tn = P.arr(array + 0x10)
            for k in range(tn):  # TransitionInfo, 0x48: condition 0x28, eventId 0x30, toStateId 0x34, flags 0x42
                ti = tt + 0x48 * k
                print(f"   {label}: event {event(P.i32(ti + 0x30))!r} -> {states.get(P.i32(ti + 0x34), ('?',))[0]} flags {P.u16(ti + 0x42):#x} {condition(ti + 0x28)}")
        sync = P.i32(sm + 0x7C)
        variables = strings(P, 0x30)
        print(f"== {name} start={states.get(P.i32(sm + 0x68), ('?',))[0]}" + (f" syncVariable={variables[sync]}" if 0 <= sync < len(variables) else ''))
        transitions(P.ptr(sm + 0xA0), "wildcard")
        for state_name, info in states.values():
            transitions(P.ptr(info + 0x50), state_name)


def writers(directory, variable):
    for path in sorted(glob.glob(os.path.join(directory, '*.hkx'))):
        try:
            P = Pack(path)
            variables = strings(P, 0x30)
        except Exception:
            continue
        if variable not in variables:
            continue
        index, name = variables.index(variable), os.path.basename(path)
        owner = {}
        for o in P.objs:
            bs = P.ptr(o + 0x10) if P.objs[o] != 'hkbVariableBindingSet' else None
            if bs is not None and P.objs.get(bs) == 'hkbVariableBindingSet':
                owner.setdefault(bs, []).append(o)
        for bs in P.of_class('hkbVariableBindingSet'):  # Binding, 0x28: memberPath 0x0, variableIndex 0x1C, bindingType 0x21
            t, n = P.arr(bs + 0x10)
            for i in range(n):
                b = t + 0x28 * i
                member = P.strptr(b) or ''
                if P.i32(b + 0x1C) == index and P.d[P.base + b + 0x21] == 0:
                    for o in owner.get(bs, []):
                        if (P.objs[o], member[:9]) in (('BSIsActiveModifier', 'bIsActive'), ('hkbStateMachine', 'isActive')):
                            print(f"{name}: {P.objs[o]} {P.strptr(o + 0x38)}.{member}")
        for sm in P.of_class('hkbStateMachine'):
            if P.i32(sm + 0x7C) == index:
                print(f"{name}: syncVariable of {P.strptr(sm + 0x38)}")
        for o in P.of_class('hkbEvaluateExpressionModifier'):
            t, n = P.arr(P.ptr(o + 0x50) + 0x10)  # hkbExpressionData, 0x18: expression 0x0
            for i in range(n):
                expression = P.strptr(t + 0x18 * i) or ''
                if re.match(rf'\s*{re.escape(variable)}\s*=', expression):
                    print(f"{name}: expression {P.strptr(o + 0x38)} {expression!r}")


if __name__ == '__main__':
    command = sys.argv[1] if len(sys.argv) > 1 else ''
    if command == 'extract':
        os.makedirs(sys.argv[2], exist_ok=True)
        for name, data in read_bsa(os.path.join(GAME, 'Data/Skyrim - Animations.bsa'),
                                   lambda n: n.lower().startswith('meshes\\actors\\character\\behaviors\\') and n.lower().endswith('.hkx')):
            open(os.path.join(sys.argv[2], name.split('\\')[-1]), 'wb').write(data)
    elif command == 'machines':
        machines(sys.argv[2], set(sys.argv[3].split(',')) if len(sys.argv) > 3 else None)
    elif command == 'writers':
        writers(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
