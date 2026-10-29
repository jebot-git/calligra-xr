#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent engineering references (uv --with scipy --with mpmath --with pint)."""
import argparse, ast, cmath, collections, json, math, os, random, re, subprocess
from pathlib import Path
import mpmath as mp
from scipy import special
import pint
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--test-bin',type=Path,default=ROOT/'build-native/fltk/calligra-native-engineering-test')
p.add_argument('--output',type=Path)
a=p.parse_args()
cases=[]
def add(group,formula,want,rtol=3e-11,atol=1e-14):cases.append((group,formula,want,rtol,atol))
def literal(x):return format(float(x),'.17g')
def zlit(z):return f'COMPLEX({literal(z.real)},{literal(z.imag)},"j")'
# Read literal upstream cases; remove comments without changing quoted C++ strings.
s=(ROOT/'sheets/tests/TestEngineeringFunctions.cpp').read_text()
s=re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',lambda m:m[0] if m[0].startswith('"') else '',s)
for line in s.splitlines():
 m=re.search(r'CHECK_EVAL\(("(?:\\.|[^"\\])*")\s*,\s*(.*?)\);',line)
 if not m:continue
 formula=ast.literal_eval(m[1]);expected=m[2]
 if expected.startswith('Value('):expected=expected[6:-1]
 expected={'true':'True','false':'False'}.get(expected,expected)
 try:want=ast.literal_eval(expected)
 except (ValueError,SyntaxError):raise AssertionError(('Unparsed upstream case',line))
 # Upstream tests explicitly round through float to 10 digits. Compare raw constants instead.
 add('upstream',formula,want,rtol=2e-9,atol=1e-10)
for name,fn in [('BESSELI',special.iv),('BESSELJ',special.jv),('BESSELK',special.kv),('BESSELY',special.yv)]:
 for n in [0,1,2,3,10,50,100,250]:
  for x in [1e-5,.1,1.,3.5,20.,100.,500.,1000.]:
   with __import__('numpy').errstate(all='ignore'):v=float(fn(n,x))
   if math.isfinite(v):add('bessel',f'{name}({x},{n}+.9)',v,rtol=2e-9,atol=1e-290)
 for n in [0,1,3,10]:
  if name in ('BESSELI','BESSELJ'):add('bessel',f'{name}(-3.5,{n})',float(fn(n,-3.5)))
rng=random.Random(20261029)
functions={'IMCOS':cmath.cos,'IMCOSH':cmath.cosh,'IMSIN':cmath.sin,'IMSINH':cmath.sinh,'IMTAN':cmath.tan,'IMTANH':cmath.tanh,'IMEXP':cmath.exp,'IMSQRT':cmath.sqrt,'IMLN':cmath.log,'IMLOG2':lambda z:cmath.log(z)/math.log(2),'IMLOG10':cmath.log10,'IMCONJUGATE':lambda z:z.conjugate(),'IMCOT':lambda z:1/cmath.tan(z),'IMCSC':lambda z:1/cmath.sin(z),'IMCSCH':lambda z:1/cmath.sinh(z),'IMSEC':lambda z:1/cmath.cos(z),'IMSECH':lambda z:1/cmath.cosh(z),'IMREAL':lambda z:z.real,'IMAGINARY':lambda z:z.imag,'IMABS':abs,'IMARGUMENT':cmath.phase}
zs=[complex(rng.uniform(-10,10),rng.uniform(-10,10)) for _ in range(30)]+[complex(-4,0),complex(-4,-1e-12),complex(4,0),complex(1e-200,1e-200)]
for name,fn in functions.items():
 for z in zs:add('complex',f'{name}({zlit(z)})',fn(z),atol=1e-210 if name=='IMABS' and abs(z)<1e-100 else 1e-13)
for _ in range(50):
 z,w=complex(rng.uniform(-3,3),rng.uniform(-3,3)),complex(rng.uniform(-2,2),rng.uniform(-2,2))
 for name,expected in [('IMSUB',z-w),('IMDIV',z/w),('IMPOWER',z**w),('IMSUM',z+w),('IMPRODUCT',z*w)]:add('complex',f'{name}({zlit(z)},{zlit(w)})',expected)
mp.mp.dps=100
for x in [-20,-8,-1,-.1,0,.1,1,8,20]:
 add('erf',f'ERF({x})',float(mp.erf(x)))
 add('erf',f'ERFC({x})',float(mp.erfc(x)),atol=1e-290)
for lo,hi in [(8,9),(20,21),(1,1.000000001),(-9,-8),(-1,2),(2,1)]:
 expected=float(mp.erfc(lo)-mp.erfc(hi))
 add('erf',f'ERF({lo},{hi})',expected,rtol=3e-7,atol=1e-290)
 add('erf',f'ERFC({lo},{hi})',-expected,rtol=3e-7,atol=1e-290)
ureg=pint.UnitRegistry()
# Independently map spreadsheet names to Pint's physical unit definitions.
units={
 'g':('g',{'lbm':'pound','ozm':'ounce','grain':'grain','pweight':'pennyweight','stone':'stone','ton':'short_ton','hweight':'long_hundredweight','shweight':'hundredweight','uk_ton':'long_ton','sg':'slug'}),
 'm':('m',{'in':'inch','ft':'foot','yd':'yard','mi':'mile','Nmi':'nautical_mile','ang':'angstrom','ly':'light_year','parsec':'parsec','Pica':'point','pica':'pica','survey_mi':'survey_mile'}),
 'Pa':('Pa',{'atm':'atm','psi':'psi','mmHg':'mmHg','Torr':'torr'}),
 'N':('N',{'dyn':'dyne','lbf':'pound_force','pond':'gram_force'}),
 'J':('J',{'c':'thermochemical_calorie','cal':'international_calorie','e':'erg','eV':'eV','Wh':'watt_hour','flb':'foot*pound_force','BTU':'Btu_it','HPh':'horsepower*hour'}),
 'W':('W',{'HP':'horsepower','PS':'metric_horsepower'}),
 'T':('T',{'ga':'gauss'}),
 'l':('liter',{'ft3':'foot**3','in3':'inch**3','m3':'m**3','yd3':'yard**3','mi3':'mile**3','Nmi3':'nautical_mile**3','ang3':'angstrom**3','pica3':'pica**3','Pica3':'point**3','gal':'gallon','uk_gal':'imperial_gallon','pt':'pint','uk_pt':'imperial_pint','qt':'quart','uk_qt':'imperial_quart','oz':'fluid_ounce','tbs':'tablespoon','tsp':'teaspoon','cup':'cup','barrel':'oil_barrel','bushel':'bushel'}),
 'm2':('m**2',{'ft2':'foot**2','in2':'inch**2','yd2':'yard**2','mi2':'mile**2','Nmi2':'nautical_mile**2','ang2':'angstrom**2','Pica2':'point**2','pica2':'pica**2','ha':'hectare','acre':'acre'}),
 'm/s':('m/s',{'mph':'mile/hour','kn':'knot','m/h':'m/hour'}),
 's':('s',{'mn':'minute','hr':'hour','d':'day','yr':'year'}),
 'bit':('bit',{'byte':'byte'})}
for base,(si,entries) in units.items():
 for name,ref in entries.items():
  try:want=ureg.Quantity(1,ref).to(si).magnitude
  except pint.DimensionalityError:
   if name=='ga':want=1e-4
   else:raise
  add('units',f'CONVERT(1,"{name}","{base}")',float(want),rtol=3e-10,atol=1e-290)
  add('units',f'CONVERT(1,"{base}","{name}")',1/float(want),rtol=3e-10,atol=1e-290)
for formula,want in [('CONVERT(1,"MTON","ft3")',40.),('CONVERT(1,"GRT","ft3")',100.),('CONVERT(1,"km2","m2")',1e6),('CONVERT(1,"cm^3","m3")',1e-6),('CONVERT(0,"C","F")',32.),('CONVERT(32,"F","C")',0.),('CONVERT(491.67,"Rank","C")',0.),('CONVERT(80,"Reau","F")',212.),('CONVERT(1,"ly2","m2")',(299792458.*86400*365.25)**2),('CONVERT(1,"ly3","m3")',(299792458.*86400*365.25)**3)]:add('units',formula,want,atol=1e-12)
for radix in [2,3,8,10,16,27,36]:
 for n in [0,1,radix-1,radix,65535,2**53-1]:
  add('base',f'DECIMAL(BASE({n},{radix}),{radix})',n,rtol=0,atol=0)
for from_,base_,bits in [('BIN',2,10),('OCT',8,30),('HEX',16,40)]:
 for n in [-(2**(bits-1)),-1,0,1,2**(bits-1)-1]:
  add('base',f'{from_}2DEC(DEC2{from_}({n}))',n,rtol=0,atol=0)
for formula,want in [('BESSELJ(1,-1)','#NUM!'),('BESSELK(0,1)','#NUM!'),('BESSELY(-1,1)','#NUM!'),('IMLN(0)','#NUM!'),('IMPOWER(0,0)','#NUM!'),('IMPOWER(0,2)','#NUM!'),('IMDIV(1,0)','#DIV/0!'),('CONVERT(1,"min","ft")','#N/A'),('CONVERT(1,"C","m")','#N/A')]:add('domains',formula,want)
# Derive legacy aliases from upstream registrations, not the new implementation.
upstream=(ROOT/'sheets/functions/engineering.cpp').read_text()
registered=list(re.finditer(r'new Function\("([A-Z0-9]+)"',upstream))
alias_count=0
for at,match in enumerate(registered):
 end=registered[at+1].start() if at+1<len(registered) else upstream.find('EngineeringModule::~')
 if end<0:end=len(upstream)
 alias=re.search(r'setAlternateName\("([^"]+)"',upstream[match.end():end])
 if not alias:continue
 name=match[1];token=re.compile(r'\b'+name+r'(?=\s*\()')
 source=next((c for c in cases if token.search(c[1])),None)
 assert source,('No canonical reference for legacy alias',name)
 _,formula,want,rtol,atol=source
 add('aliases',token.sub(alias[1],formula,count=1),want,rtol,atol);alias_count+=1
assert alias_count==39,alias_count
native=(ROOT/'fltk/src/native_engineering.cpp').read_text().split('Text engineering_name')[0]
names=set(re.findall(r'\{"([A-Z0-9]+)",',native))
covered={name for name in names if any(re.search(r'\b'+name+r'\s*\(',c[1]) for c in cases)}
assert len(names)==50 and covered==names,sorted(names-covered)
command=[str(a.test_bin.resolve()),'--eval']
result=subprocess.run(command,input='\n'.join(c[1] for c in cases)+'\n',text=True,capture_output=True,env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},timeout=90)
if result.returncode:raise AssertionError((result.returncode,result.stdout[-2000:],result.stderr[-2000:]))
results=[json.loads(line) for line in result.stdout.splitlines()];assert len(results)==len(cases),(len(results),len(cases))
failures=[];counts=collections.Counter()
for (group,formula,want,rtol,atol),got in zip(cases,results):
 counts[group]+=1
 if isinstance(want,bool):ok=got.get('boolean') is want
 elif isinstance(want,str):ok=got['display']==want
 elif isinstance(want,complex):ok=got['kind']=='complex' and abs(complex(got['real'],got['imag'])-want)<=atol+rtol*abs(want)
 else:ok=got['kind']=='number' and math.isclose(got['real'],want,rel_tol=rtol,abs_tol=atol)
 if not ok or got['incomplete']:failures.append({'group':group,'formula':formula,'expected':str(want),'actual':got})
report={'cases':len(cases),'groups':dict(counts),'failures':failures}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'cases':len(cases),'groups':dict(counts),'failed':len(failures)},indent=2))
for failure in failures[:30]:print(failure)
raise SystemExit(bool(failures))
