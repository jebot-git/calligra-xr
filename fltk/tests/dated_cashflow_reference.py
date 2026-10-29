#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent dated cash-flow roots and values (SciPy, mpmath, civil dates)."""
import argparse,ast,collections,datetime as dt,json,math,os,random,re,subprocess
from pathlib import Path
import mpmath as mp
from scipy.optimize import brentq
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--test-bin',type=Path,default=ROOT/'build-native/fltk/calligra-native-financial-test')
p.add_argument('--output',type=Path);a=p.parse_args();cases=[]
def add(group,formula,want,atol=1e-10):cases.append((group,formula,want,atol))
def lit(n):return format(float(n),'.17g')
def array(values):return '{'+','.join(lit(v) for v in values)+'}'
source=(ROOT/'sheets/tests/TestFinancialFunctions.cpp').read_text()
source=re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',lambda m:m[0] if m[0].startswith('"') else '',source)
for line in source.splitlines():
    m=re.search(r'CHECK_EVAL(_SHORT)?\(("(?:\\.|[^"\\])*")\s*,\s*(.*?)\);',line)
    if not m:continue
    formula=ast.literal_eval(m[2])
    if not re.match(r'(?:COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GET)?X(?:IRR|NPV)\(',formula):continue
    want=m[3]
    while want.startswith('Value(') and want.endswith(')'):want=want[6:-1]
    if want.startswith('Value::error'):want={'VALUE':'#VALUE!','NUM':'#NUM!'}[re.fullmatch(r'Value::error([A-Z]+)\(\)',want)[1]]
    else:want=ast.literal_eval(want)
    add('retained',formula,want,2.3e-5 if m[1] else 3e-10)
rng=random.Random(20261030);mp.mp.dps=90;epoch=dt.date(1899,12,30)
for index in range(200):
    first=dt.date(2000+rng.randrange(20),rng.randint(1,12),rng.randint(1,28))
    days=sorted(rng.sample(range(1,2200),rng.randint(2,12)))
    rate=rng.uniform(-.6,.8);positive=[rng.uniform(100,10000) for _ in days]
    scale=10000/sum(v/(1+rate)**(d/365) for v,d in zip(positive,days))
    values=[-10000]+[float(v*scale) for v in positive];offsets=[0]+days
    if index%2:
        order=list(range(1,len(values)));rng.shuffle(order);order=[0]+order
        values=[values[i] for i in order];offsets=[offsets[i] for i in order]
    dates=[first+dt.timedelta(days=d) for d in offsets]
    serials=[(d-epoch).days for d in dates]
    def npv(r):return sum(v/(1+r)**(d/365) for v,d in zip(values,offsets))
    root=brentq(npv,-.999999,10,xtol=1e-13,rtol=1e-14)
    date_array=array(serials)
    add('scipy-roots',f'XIRR({array(values)},{date_array})',root)
    add('scipy-roots',f'XIRR({array(values)},{date_array},{lit(rate+.03)})',root)
    if index%10==0:
        expr='{'+','.join(f'DATE({d.year},{d.month},{d.day})' for d in dates)+'}'
        add('date-expressions',f'XIRR({array(values)},{expr})',root)
        expr='{'+','.join('"'+d.isoformat()+'"' for d in dates)+'}'
        add('iso-dates',f'XIRR({array(values)},{expr})',root)
    discount=rng.uniform(-.4,.8);base=1+mp.mpf(discount)
    want=float(sum(mp.mpf(v)/base**(mp.mpf(d)/365) for v,d in zip(values,offsets)))
    add('mpmath-values',f'XNPV({lit(discount)},{array(values)},{date_array})',want,2e-8)
for days in [1,365,366,365000,2958000]:
    want=float(mp.expm1(mp.log(2)*365/days))
    for guess in [.1,.5,-.3]:add('extreme-span',f'XIRR({{-1,2}},{{0,{days}}},{guess})',want)
for formula,want in [
    ('XIRR({-100,230,-132},{0,365,730},.3)',.2),
    ('XIRR({-100,230,-132},{0,365,730},.05)',.1),
    ('XIRR({0,-100,110},{0,2958000,2958365},.5)',.1),
    ('XNPV(.1,{-100,110},{0,365})',0),
    ('XNPV(.1,{1,-1},{2958465,2958465})',0),
    ('XNPV(1e308,{0,1},{0,2958465})',0),
    ('XNPV(-.99,{1,1},{0,2958465})','#NUM!'),
    ('XNPV(-1,{-100,110},{0,365})','#NUM!'),
    ('XIRR({1,2},{0,365})','#NUM!'),
    ('XIRR({100,-100},{0,0})','#NUM!'),
    ('XNPV(.1,{-100,110},{365,0})','#NUM!'),
    ('XNPV(.1,{-100,110},{0})','#NUM!'),
    ('XNPV(.1,{-100,110},{"bad",365})','#VALUE!')
]:add('boundaries',formula,want)
for name in ('XIRR','XNPV'):
    _,f,w,tol=next(c for c in cases if c[1].startswith(name+'('))
    for prefix in ('COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GET','_xlfn.','COM.MICROSOFT.'):
        add('namespaces',prefix+f,w,tol)
run=subprocess.run([str(a.test_bin.resolve()),'--eval'],input='\n'.join(c[1] for c in cases)+'\n',text=True,capture_output=True,
                   env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},timeout=90)
assert run.returncode==0,(run.returncode,run.stderr[-2000:])
got=[json.loads(line) for line in run.stdout.splitlines()];assert len(got)==len(cases)
failures=[];counts=collections.Counter()
for (group,formula,want,tol),value in zip(cases,got):
    counts[group]+=1
    ok=value['display']==want if isinstance(want,str) else value['kind']==1 and math.isclose(value['value'],want,rel_tol=3e-11,abs_tol=tol)
    if not ok or value['incomplete']:failures.append({'group':group,'formula':formula,'expected':want,'actual':value})
report={'cases':len(cases),'groups':dict(counts),'failures':failures}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'cases':len(cases),'groups':dict(counts),'failed':len(failures)},indent=2))
for failure in failures[:20]:print(failure)
raise SystemExit(bool(failures))
