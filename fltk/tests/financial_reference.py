#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Retained Calligra cases plus independent numpy-financial/mpmath references."""
import argparse, ast, collections, json, math, os, random, re, subprocess
from pathlib import Path
import mpmath as mp
import numpy as np
import numpy_financial as npf

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--test-bin',type=Path,default=ROOT/'build-native/fltk/calligra-native-financial-test')
p.add_argument('--output',type=Path)
a=p.parse_args();cases=[]
def add(group,formula,want,rtol=3e-11,atol=1e-12):
    if isinstance(want,(float,int)) and not math.isfinite(float(want)):return
    cases.append((group,formula,want,rtol,atol))
def lit(v):return format(float(v),'.17g')
def array(values):return '{'+','.join(lit(v) for v in values)+'}'
def constant(node):
    if isinstance(node,ast.Constant) and type(node.value) in (int,float):return node.value
    if isinstance(node,ast.UnaryOp) and isinstance(node.op,(ast.USub,ast.UAdd)):
        v=constant(node.operand);return -v if isinstance(node.op,ast.USub) else v
    if isinstance(node,ast.BinOp):
        x,y=constant(node.left),constant(node.right)
        if isinstance(node.op,ast.Add):return x+y
        if isinstance(node.op,ast.Sub):return x-y
        if isinstance(node.op,ast.Mult):return x*y
        if isinstance(node.op,ast.Div):return x/y
    raise AssertionError(ast.dump(node))

native=(ROOT/'fltk/src/native_financial.cpp').read_text().split('Text financial_name')[0]
names=set(re.findall(r'\{"([A-Z_]+)",',native));assert len(names)==41
names-= {'XIRR','XNPV','ACCRINTM','DISC','INTRATE','PRICEDISC','RECEIVED','YIELDDISC'} # Covered by the dated cash-flow and calendar corpora.
upstream=(ROOT/'sheets/functions/financial.cpp').read_text()
registrations=list(re.finditer(r'new Function\("([A-Z_]+)"',upstream))
aliases={}
for index,match in enumerate(registrations):
    end=registrations[index+1].start() if index+1<len(registrations) else upstream.find('FinancialModule::~')
    alias=re.search(r'setAlternateName\("([^"]+)"',upstream[match.end():end])
    if alias and match[1] in names:aliases[alias[1]]=match[1]
source=(ROOT/'sheets/tests/TestFinancialFunctions.cpp').read_text()
source=re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',lambda m:m[0] if m[0].startswith('"') else '',source)
for line in source.splitlines():
    m=re.search(r'CHECK_EVAL(_SHORT|_EQUAL)?\(("(?:\\.|[^"\\])*")\s*,\s*(.*?)\);',line)
    if not m:continue
    formula=ast.literal_eval(m[2]);calls=re.findall(r'([A-Z_][A-Z_0-9.]*)\s*\(',formula)
    if not calls or any(aliases.get(name,name) not in names for name in calls):continue
    want=m[3].strip()
    while want.startswith('Value(') and want.endswith(')'):want=want[6:-1]
    if want.startswith('Value::error'):
        key=re.fullmatch(r'Value::error([A-Z0-9]+)\(\)',want)[1]
        want={'VALUE':'#VALUE!','DIV0':'#DIV/0!','NUM':'#NUM!'}[key]
    else:want=constant(ast.parse(want,mode='eval').body)
    # Preserve the original macro's absolute tolerance for rounded constants.
    add('retained',formula,want,rtol=1e-12,atol=2.3e-5 if m[1]=='_SHORT' else 3e-10)

rng=random.Random(20261029)
for _ in range(120):
    rate=rng.uniform(-.02,.12);periods=rng.randint(2,60)
    pv=rng.uniform(100,100000);fv=rng.uniform(0,10000);due=rng.randrange(2)
    pay=float(npf.pmt(rate,periods,pv,fv,due))
    args=','.join(map(lit,[rate,periods,pv,fv,due]))
    add('numpy-financial',f'PMT({args})',pay)
    add('numpy-financial',f'PV({lit(rate)},{periods},{lit(pay)},{lit(fv)},{due})',float(npf.pv(rate,periods,pay,fv,due)))
    add('numpy-financial',f'FV({lit(rate)},{periods},{lit(pay)},{lit(pv)},{due})',float(npf.fv(rate,periods,pay,pv,due)),atol=2e-9)
    add('numpy-financial',f'NPER({lit(rate)},{lit(pay)},{lit(pv)},{lit(fv)},{due})',float(npf.nper(rate,pay,pv,fv,due)))
    add('numpy-financial',f'RATE({periods},{lit(pay)},{lit(pv)},{lit(fv)},{due},{lit(rate+.005)})',rate,atol=1e-11)
    at=rng.randint(1,periods)
    add('numpy-financial',f'IPMT({lit(rate)},{at},{periods},{lit(pv)},{lit(fv)},{due})',float(npf.ipmt(rate,at,periods,pv,fv,due)))
    add('numpy-financial',f'PPMT({lit(rate)},{at},{periods},{lit(pv)},{lit(fv)},{due})',float(npf.ppmt(rate,at,periods,pv,fv,due)))
    if rate>0:
        start=rng.randint(1,periods);end=rng.randint(start,periods)
        slots=np.arange(start,end+1)
        add('numpy-financial',f'CUMIPMT({lit(rate)},{periods},{lit(pv)},{start},{end},{due})',float(np.sum(npf.ipmt(rate,slots,periods,pv,0,due))),atol=2e-8)
        add('numpy-financial',f'CUMPRINC({lit(rate)},{periods},{lit(pv)},{start},{end},{due})',float(np.sum(npf.ppmt(rate,slots,periods,pv,0,due))),atol=2e-8)
    cash=[-rng.uniform(100,10000)]+[rng.uniform(0,2000) for _ in range(rng.randint(2,20))]
    add('numpy-financial',f'IRR({array(cash)})',float(npf.irr(cash)),atol=2e-11)
    finance,reinvest=rng.uniform(0,.2),rng.uniform(0,.2)
    add('numpy-financial',f'MIRR({array(cash)},{lit(finance)},{lit(reinvest)})',float(npf.mirr(cash,finance,reinvest)))
    add('numpy-financial',f'NPV({lit(finance)},{array(cash)})',float(npf.npv(finance,cash)/(1+finance)))

mp.mp.dps=100
for rate in [0.,1e-20,-1e-20,1e-12,-1e-12,1e-8,.001,.05,-.05]:
    for periods in [1,12,360,360.5]:
        for due in [0,1]:
            r=mp.mpf(rate);n=mp.mpf(periods);pv=mp.mpf(100000);fv=mp.mpf(100)
            g=(1+r)**n;annuity=n if not r else mp.expm1(n*mp.log1p(r))/r
            pay=-(pv*g+fv)/((1+r*due)*annuity)
            add('mpmath',f'PMT({lit(rate)},{periods},100000,100,{due})',float(pay),rtol=2e-13)
            add('mpmath',f'PV({lit(rate)},{periods},-100,100,{due})',float((-fv+100*(1+r*due)*annuity)/g),rtol=2e-13)
            add('mpmath',f'FV({lit(rate)},{periods},-100,100000,{due})',float(-pv*g+100*(1+r*due)*annuity),rtol=2e-13)
            # NPER is checked using a payment expressed as an exact binary64
            # literal, so the reference solves for that same rounded payment.
            pay=mp.mpf(float(pay))
            want=-(pv+fv)/pay if not r else mp.log(-(r*fv-pay*(1+r*due))/(r*pv+pay*(1+r*due)))/mp.log1p(r)
            add('mpmath',f'NPER({lit(rate)},{lit(pay)},100000,100,{due})',float(want),rtol=2e-12)
for rate in [1e-300,1e-20,1e-12,.03,.15]:
    r=mp.mpf(rate)
    add('mpmath',f'EFFECT({lit(rate)},12)',float(mp.expm1(12*mp.log1p(r/12))),rtol=2e-13,atol=1e-310)
    add('mpmath',f'NOMINAL({lit(rate)},12)',float(12*mp.expm1(mp.log1p(r)/12)),rtol=2e-13,atol=1e-310)
    for due in [0,1]:
        n=60;pv=mp.mpf(100000);annuity=mp.expm1(n*mp.log1p(r))/r
        pay=-pv*(1+r)**n/((1+r*due)*annuity)
        amounts=[]
        for period in range(1,n+1):
            if due and period==1:amounts.append(mp.mpf(0));continue
            exponent=period-(2 if due else 1)
            an=mp.expm1(exponent*mp.log1p(r))/r
            value=-pv*(1+r)**exponent-pay*(1+r*due)*an-(pay if due else 0)
            amounts.append(value*r)
        add('mpmath',f'CUMIPMT({lit(rate)},60,100000,1,60,{due})',float(sum(amounts)),rtol=3e-12,atol=1e-305)

for formula,want in [
    ('FV_ANNUITY(1000,.05,5)',5525.63125),('PV_ANNUITY(1000,.05,5)',4329.47667063082),
    ('FV_ANNUITY(10,0,5)',50),('PV_ANNUITY(10,0,5)',50),
    ('IRR({-100,230,-132},.1)',.1),('IRR({-100,230,-132},.3)',.2),
    ('RRI(.5,100,200)',3),('NPV(1,{1,2;3,4})',1.625),
    ('EFFECTIVE(.12,12)',.12682503013196972)
]:add('boundaries',formula,want)
for name in sorted(names):
    token=re.compile(r'\b'+name+r'(?=\s*\()')
    _,formula,want,rtol,atol=next(c for c in cases if token.search(c[1]))
    for prefix in ('_xlfn.','COM.MICROSOFT.'):
        add('excel-namespaces',token.sub(prefix+name,formula,count=1),want,rtol,atol)
covered={name for name in names if any(re.search(r'\b'+name+r'\s*\(',c[1]) for c in cases)}
assert covered==names,sorted(names-covered)
run=subprocess.run([str(a.test_bin.resolve()),'--eval'],input='\n'.join(c[1] for c in cases)+'\n',capture_output=True,text=True,
                   env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},timeout=120)
assert run.returncode==0,(run.returncode,run.stderr[-2000:])
results=[json.loads(line) for line in run.stdout.splitlines()];assert len(results)==len(cases)
failures=[];counts=collections.Counter()
for (group,formula,want,rtol,atol),got in zip(cases,results):
    counts[group]+=1
    ok=got['display']==want if isinstance(want,str) else got['kind']==1 and math.isclose(got['value'],want,rel_tol=rtol,abs_tol=atol)
    if not ok or got['incomplete']:failures.append({'group':group,'formula':formula,'expected':want,'actual':got})
report={'cases':len(cases),'groups':dict(counts),'registered_names':len(names),'failures':failures}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'cases':len(cases),'groups':dict(counts),'failed':len(failures)},indent=2))
for failure in failures[:25]:print(failure)
raise SystemExit(bool(failures))
