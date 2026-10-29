#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Retained coupon cases and independent enumerated maturity schedules."""
import argparse,ast,calendar,collections,datetime as dt,json,math,os,random,re,subprocess
from fractions import Fraction as F
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
NAMES={'COUPDAYBS','COUPDAYS','COUPDAYSNC','COUPNCD','COUPNUM','COUPPCD'}
EPOCH=dt.date(1899,12,30)
def date(value):
    if isinstance(value,dt.date):return value
    if isinstance(value,str):return dt.date.fromisoformat(value[:10])
    return EPOCH+dt.timedelta(days=math.trunc(value))
def month_end(d):return d.day==calendar.monthrange(d.year,d.month)[1]
def schedule(settlement,maturity,frequency,eom=True):
    # Deliberately enumerate from maturity, rather than using the native
    # quotient/adjustment algorithm. Always retain the original anchor day.
    result=[];current=maturity;period=0
    while current>settlement:
        result.append(current);period+=1
        total=maturity.year*12+maturity.month-1-period*(12//frequency)
        y,m=divmod(total,12);m+=1
        end=calendar.monthrange(y,m)[1]
        current=dt.date(y,m,end if eom and month_end(maturity) else min(maturity.day,end))
    return current,result[-1],len(result)
def day_count(start,end,basis):
    if basis in (1,2,3):return (end-start).days
    d1,d2=start.day,end.day
    if basis==0:
        if start.month==2 and month_end(start):d1=30
        if end.month==2 and month_end(end):d2=30
        if d2==31 and d1>=30:d2=30
        if d1==31:d1=30
    else:
        d1=min(d1,30)
        if basis==4:d2=min(d2,30)
    return (end.year-start.year)*360+(end.month-start.month)*30+d2-d1
def coupon(name,start,end,frequency,basis=0,eom=True):
    start,end=date(start),date(end);frequency=math.trunc(frequency);basis=math.trunc(basis)
    if start>=end or frequency not in (1,2,3,4,6,12) or basis not in range(6):return '#VALUE!'
    previous,next_,count=schedule(start,end,frequency,eom)
    if name=='COUPNUM':return count
    if name=='COUPNCD':return next_
    if name=='COUPPCD':return previous
    if name=='COUPDAYBS':return day_count(previous,start,basis)
    if name=='COUPDAYSNC':return day_count(start,next_,basis)
    return (next_-previous).days if basis==1 else F(365 if basis==3 else 360,frequency)
def reference(formula):
    s=re.sub(r'COM\.SUN\.STAR\.SHEET\.ADDIN\.ANALYSIS\.GET','',formula,flags=re.I).replace(';',',')
    s=re.sub(r'"(?:\\.|[^"\\])*"|(?<![\w.])0+([0-9]+)(?![\w.])',lambda m:m[0] if m[0].startswith('"') else str(int(m[1])),s)
    s=re.sub(r'(?<![<>=!])=(?!=)','==',s)
    def visit(n):
        if isinstance(n,ast.Constant):return n.value
        if isinstance(n,ast.UnaryOp) and isinstance(n.op,ast.USub):return -visit(n.operand)
        if isinstance(n,ast.Call) and isinstance(n.func,ast.Name):
            name=n.func.id.upper();args=[visit(a) for a in n.args]
            if name=='DATE':return dt.date(*map(int,args))
            if name in NAMES:return coupon(name,*args)
        if isinstance(n,ast.Compare) and len(n.ops)==1 and isinstance(n.ops[0],ast.Eq):return visit(n.left)==visit(n.comparators[0])
        raise ValueError(ast.dump(n))
    return visit(ast.parse(s,mode='eval').body)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--test-bin',type=Path,default=ROOT/'build-native/fltk/calligra-native-financial-test');p.add_argument('--output',type=Path);a=p.parse_args()
    cases=[]
    def add(group,formula,want):cases.append((group,formula,want))
    source=(ROOT/'sheets/tests/TestFinancialFunctions.cpp').read_text()
    source=re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',lambda m:m[0] if m[0].startswith('"') else '',source)
    for match in re.finditer(r'CHECK_EVAL(_SHORT|_EQUAL)?\(("(?:\\.|[^"\\])*")\s*,\s*(Value\([^;]*?\)|Value::error[A-Z0-9]+\(\))\s*\);',source):
        formula=ast.literal_eval(match[2]);name=re.match(r'\s*([\w.]+)\s*\(',formula)[1].upper().split('GET')[-1]
        if name not in NAMES:continue
        raw=match[3];want='#VALUE!' if raw=='Value::errorVALUE()' else ast.literal_eval(raw[6:-1].replace('true','True').replace('false','False'))
        assert reference(formula)==want,(formula,reference(formula),want)
        add('retained',formula,want)
    assert len(cases)==61, len(cases)
    rng=random.Random(20261030)
    pairs=[]
    for y in (1900,2000,2023,2024,2100):
        pairs.extend((dt.date(y,m,d),dt.date(y+2,2,calendar.monthrange(y+2,2)[1])) for m,d in [(1,15),(2,28),(3,1),(8,30),(8,31)])
        pairs.append((dt.date(y,2,calendar.monthrange(y,2)[1]),dt.date(y+2,8,31)))
    for _ in range(70):
        maturity=dt.date(rng.choice([1900,2000,2024,2025,2100]),rng.randrange(1,13),1)
        maturity=maturity.replace(day=rng.choice([1,15,28,calendar.monthrange(maturity.year,maturity.month)[1]]))
        pairs.append((maturity-dt.timedelta(days=rng.randint(1,365*20)),maturity))
    for start,end in pairs:
        for frequency in (1,2,3,4,6,12):
            for basis in range(6):
                for name in sorted(NAMES):
                    f=f'{name}("{start}","{end}",{frequency},{basis})'
                    add('enumerated-schedules',f,coupon(name,start,end,frequency,basis))
            for eom in (False,True):
                f=f'COUPNUM("{start}","{end}",{frequency},1,{"TRUE()" if eom else "FALSE()"})'
                add('explicit-eom',f,coupon('COUPNUM',start,end,frequency,1,eom))
    for formula,want in [
        ('COUPDAYBS("2001-01-25","2001-11-15",2,3)',71),
        ('COUPDAYS("2001-01-25","2001-11-15",2,3)',182.5),
        ('COUPDAYSNC("2001-01-25","2001-11-15",2,3)',110),
        ('COUPNCD("2001-01-25","2001-11-15",2,3)',dt.date(2001,5,15)),
        ('COUPNUM("2001-01-25","2001-11-15",2,3)',2),
        ('COUPPCD("2001-01-25","2001-11-15",2,3)',dt.date(2000,11,15)),
        ('COUPNCD("2024-02-29","2025-08-31",2,1)',dt.date(2024,8,31)),
        ('COUPPCD("2024-02-29","2025-08-31",2,1)',dt.date(2024,2,29)),
        ('COUPNUM("2024-08-31","2025-08-31",2)',2),
        ('COUPNUM("2024-08-30","2025-02-28",2,1,FALSE())',1),
        ('COUPNUM("2024-08-30","2025-02-28",2,1,TRUE())',2)
    ]:add('documented-boundaries',formula,want)
    for name in sorted(NAMES):
        sample=next(c for c in cases if c[1].startswith(name+'(') and not isinstance(c[2],bool))
        for prefix in ('_xlfn.','COM.MICROSOFT.','COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GET'):
            add('namespaces',sample[1].replace(name,prefix+name,1),sample[2])
    run=subprocess.run([str(a.test_bin.resolve()),'--eval'],input='\n'.join(c[1] for c in cases)+'\n',text=True,capture_output=True,env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},timeout=180)
    assert run.returncode==0,(run.returncode,run.stderr[-2000:])
    values=[json.loads(line) for line in run.stdout.splitlines()];assert len(values)==len(cases)
    failures=[];counts=collections.Counter()
    for (group,formula,want),got in zip(cases,values):
        counts[group]+=1
        if isinstance(want,dt.date):valid=got['kind']==1 and got['value']==(want-EPOCH).days and got['display']==want.isoformat()
        elif isinstance(want,bool):valid=got['kind']==2 and got['display']==('TRUE' if want else 'FALSE')
        elif isinstance(want,str):valid=got['display']==want
        else:valid=got['kind']==1 and math.isclose(got['value'],float(want),rel_tol=1e-12,abs_tol=1e-11)
        if not valid or got['incomplete']:failures.append({'formula':formula,'expected':str(want),'actual':got})
    result={'cases':len(cases),'groups':dict(counts),'retained_deviations':[],'failures':failures}
    if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'cases':len(cases),'groups':dict(counts),'failures':len(failures)},indent=2))
    for f in failures[:12]:print(f)
    raise SystemExit(bool(failures))
