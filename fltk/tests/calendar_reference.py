#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""OpenFormula calendar/securities references and visible retained-test deviations."""
import argparse,ast,calendar,collections,datetime as dt,json,math,os,random,re,subprocess
from fractions import Fraction as F
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
NAMES={'YEARFRAC','DAYS360','ACCRINTM','DISC','INTRATE','PRICEDISC','RECEIVED','YIELDDISC'}
EPOCH=dt.date(1899,12,30)

def date(v):
    if isinstance(v,dt.date):return v
    if isinstance(v,str):return dt.date.fromisoformat(v[:10])
    return EPOCH+dt.timedelta(days=math.trunc(v))
def days(a,b,basis=0):
    a,b=sorted((date(a),date(b)))
    if a==b:return 0
    if basis not in (0,4):return (b-a).days
    d1=min(a.day,30);d2=b.day
    if basis==4:d2=min(d2,30)
    else:
        if d1==30:d2=min(d2,30)
        feb1=a.month==2 and a.day==calendar.monthrange(a.year,2)[1]
        feb2=b.month==2 and b.day==calendar.monthrange(b.year,2)[1]
        if feb1 and feb2:d2=30
        if feb1:d1=30
    return 360*(b.year-a.year)+30*(b.month-a.month)+d2-d1
def fraction(a,b,basis=0):
    basis=math.trunc(basis)
    if basis not in range(5):return '#VALUE!'
    a,b=sorted((date(a),date(b)))
    denominator=365 if basis==3 else 360
    if basis==1:
        long=(b.year,b.month,b.day)>(a.year+1,a.month,a.day)
        if long:
            denominator=F(sum(366 if calendar.isleap(y) else 365 for y in range(a.year,b.year+1)),b.year-a.year+1)
        elif a.year!=b.year and calendar.isleap(a.year):denominator=366
        else:
            leap=any(calendar.isleap(y) and a<=dt.date(y,2,29)<=b for y in {a.year,b.year})
            denominator=366 if leap else 365
    return F(days(a,b,basis),1)/denominator
def days360(a,b,method=False):
    a,b=date(a),date(b)
    d1=min(a.day,30);d2=b.day
    if method:d2=min(d2,30)
    else:
        if a.month==2 and a.day==calendar.monthrange(a.year,2)[1]:d1=30
        if d1==30:d2=min(d2,30)
    return 360*(b.year-a.year)+30*(b.month-a.month)+d2-d1
def security(name,a,b,x,y=1000,basis=0):
    a,b=date(a),date(b);basis=math.trunc(basis)
    invalid='#NUM!' if name=='DISC' else '#VALUE!'
    if a>b or (a==b and name!='ACCRINTM') or basis not in range(5) or x<=0 or y<=0:return invalid
    f=fraction(a,b,basis)
    if name=='ACCRINTM':return x*y*f
    if name=='PRICEDISC':return y*(1-x*f)
    if name=='RECEIVED':return x/(1-y*f) if y*f!=1 else '#VALUE!'
    return (y-x)/(y if name=='DISC' else x)/f if f else '#DIV/0!'

def reference(formula):
    text=re.sub(r'COM\.SUN\.STAR\.SHEET\.ADDIN\.ANALYSIS\.GET','',formula,flags=re.I).replace(';',',')
    text=re.sub(r'"(?:\\.|[^"\\])*"|(?<![\w.])0+([0-9]+)(?![\w.])',lambda m:m[0] if m[0].startswith('"') else str(int(m[1])),text)
    text=re.sub(r'([0-9]+(?:\.[0-9]+)?)%',r'(\1/100)',text)
    tree=ast.parse(text,mode='eval')
    def visit(n):
        if isinstance(n,ast.Constant):return n.value
        if isinstance(n,ast.UnaryOp) and isinstance(n.op,ast.USub):return -visit(n.operand)
        if isinstance(n,ast.BinOp) and isinstance(n.op,ast.Div):return visit(n.left)/visit(n.right)
        if isinstance(n,ast.Call) and isinstance(n.func,ast.Name):
            name=n.func.id.upper();args=[visit(a) for a in n.args]
            if name=='DATE':return dt.date(*map(int,args)).isoformat()
            if name=='YEARFRAC':return fraction(*args)
            if name=='DAYS360':return days360(*args)
            if name in NAMES:return security(name,*args)
        if isinstance(n,ast.Name) and n.id.upper() in ('TRUE','FALSE'):return n.id.upper()=='TRUE'
        raise ValueError(ast.dump(n))
    return visit(tree.body)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--test-bin',type=Path,default=ROOT/'build-native/fltk/calligra-native-financial-test');p.add_argument('--output',type=Path);a=p.parse_args()
    cases=[];deviations=[]
    def add(group,formula,want,atol=1e-11):cases.append((group,formula,float(want) if not isinstance(want,str) else want,atol))
    for file in ['TestDatetimeFunctions.cpp','TestFinancialFunctions.cpp']:
        text=(ROOT/'sheets/tests'/file).read_text()
        # Retain multiline macro invocations. Strip comments without touching strings.
        text=re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',lambda m:m[0] if m[0].startswith('"') else '',text)
        pattern=r'CHECK_EVAL(_SHORT|_EQUAL)?\(("(?:\\.|[^"\\])*")\s*,\s*(Value\([^;]*?\)|Value::error[A-Z0-9]+\(\))\s*\);'
        for m in re.finditer(pattern,text):
            formula=ast.literal_eval(m[2]);name=re.match(r'\s*([\w.]+)\s*\(',formula)[1].upper().split('GET')[-1]
            if name not in NAMES:continue
            old=m[3]
            if '::error' in old:old={'VALUE':'#VALUE!','NUM':'#NUM!','DIV0':'#DIV/0!'}[re.search(r'error(\w+)\(',old)[1]]
            else:old=ast.literal_eval(old[6:-1].replace('false','False').replace('true','True'))
            want=reference(formula);tolerance=2.3e-5 if m[1]=='_SHORT' else 3e-10
            same=old==want if isinstance(old,str) or isinstance(want,str) else math.isclose(old,float(want),rel_tol=1e-12,abs_tol=tolerance)
            if not same:deviations.append({'formula':formula,'retained_expected':old,'openformula_expected':float(want) if not isinstance(want,str) else want})
            add('retained-normative',formula,want)
    assert len(cases)==88, len(cases)
    changed=collections.Counter(re.match(r'\s*([A-Z]+)',d['formula'])[1] for d in deviations)
    assert changed=={'YEARFRAC':6,'ACCRINTM':1,'DISC':1,'INTRATE':1}, changed
    special=[dt.date(y,m,d) for y in [1899,1900,1999,2000,2004,2023,2024,2025,2100] for m in [1,2,3,10,12] for d in sorted({1,28,calendar.monthrange(y,m)[1]})]
    rng=random.Random(20261029)
    pairs=[(dt.date(1,1,1),dt.date(9999,12,31))]
    pairs += [(a,b) for a,b in [(dt.date(2004,10,1),dt.date(2005,1,11)),(dt.date(2024,2,29),dt.date(2025,2,28)),(dt.date(2023,2,28),dt.date(2023,3,31))]]
    pairs += [(rng.choice(special),rng.choice(special)) for _ in range(180)]
    for start,end in pairs:
        for basis in range(5):
            add('gregorian-bases',f'YEARFRAC("{start}","{end}",{basis})',fraction(start.isoformat(),end.isoformat(),basis))
        for method in (0,1):add('signed-days360',f'DAYS360("{start}","{end}",{method})',days360(start.isoformat(),end.isoformat(),method))
        start,end=sorted((start,end))
        if start==end:continue
        for basis in range(5):
            for name in sorted(NAMES-{'YEARFRAC','DAYS360'}):
                x,y=(.03,1000) if name in {'ACCRINTM','PRICEDISC'} else (1000,.03) if name=='RECEIVED' else (950,1000)
                formula=f'{name}("{start}","{end}",{x},{y},{basis})'
                add('securities',formula,security(name,start.isoformat(),end.isoformat(),x,y,basis),atol=1e-8)
    for name in sorted(NAMES):
        sample=next(c for c in cases if re.search(r'\b'+name+r'\(',c[1]))
        for prefix in ('_xlfn.','COM.MICROSOFT.'):
            add('namespaces',re.sub(r'\b'+name+r'(?=\()',prefix+name,sample[1],count=1),sample[2])
        if name!='DAYS360':add('analysis-alias',re.sub(r'\b'+name+r'(?=\()', 'COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GET'+name,sample[1],count=1),sample[2])
    run=subprocess.run([str(a.test_bin.resolve()),'--eval'],input='\n'.join(c[1] for c in cases)+'\n',text=True,capture_output=True,env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},timeout=120)
    assert run.returncode==0,(run.returncode,run.stderr[-2000:])
    values=[json.loads(line) for line in run.stdout.splitlines()];assert len(values)==len(cases)
    failures=[];counts=collections.Counter()
    for (group,formula,want,atol),got in zip(cases,values):
        counts[group]+=1
        valid=got['display']==want if isinstance(want,str) else got['kind']==1 and math.isclose(got['value'],want,rel_tol=3e-12,abs_tol=atol)
        if not valid or got['incomplete']:failures.append({'formula':formula,'expected':want,'actual':got})
    result={'cases':len(cases),'groups':dict(counts),'retained_deviations':deviations,'failures':failures}
    if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'cases':len(cases),'groups':dict(counts),'retained_deviations':len(deviations),'failures':len(failures)},indent=2))
    for f in failures[:12]:print(f)
    raise SystemExit(bool(failures))
