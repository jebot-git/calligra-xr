#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent high-precision cash-flow valuation, yield and duration references."""
import argparse,ast,collections,datetime as dt,json,math,os,random,re,subprocess
from pathlib import Path
import mpmath as mp
from coupon_reference import schedule,day_count
from calendar_reference import fraction
ROOT=Path(__file__).resolve().parents[2]
mp.mp.dps=90

def bond_values(start,end,coupon,yield_,redemption,frequency,basis=0):
    start=dt.date.fromisoformat(start) if isinstance(start,str) else start
    end=dt.date.fromisoformat(end) if isinstance(end,str) else end
    prev,next_,count=schedule(start,end,frequency)
    period=(next_-prev).days if basis==1 else mp.mpf(365 if basis==3 else 360)/frequency
    offset=mp.mpf(day_count(start,next_,basis))/period
    pay=100*mp.mpf(coupon)/frequency;base=1+mp.mpf(yield_)/frequency
    # Explicit individual cash flows, independent of the binary polynomial sum.
    flows=[(mp.mpf(k)+offset,pay+(mp.mpf(redemption) if k==count-1 else 0)) for k in range(count)]
    weights=[amount/base**time for time,amount in flows]
    dirty=mp.fsum(weights)
    calendar_fraction=fraction(start,end,basis)
    calendar_time=mp.mpf(calendar_fraction.numerator)/calendar_fraction.denominator
    duration_offset=calendar_time*frequency-count+1
    duration_flows=[(mp.mpf(k)+duration_offset,pay+(mp.mpf(redemption) if k==count-1 else 0)) for k in range(count)]
    duration_weights=[amount/base**time for time,amount in duration_flows]
    duration=mp.fsum(time*weight for (time,_),weight in zip(duration_flows,duration_weights))/mp.fsum(duration_weights)/frequency
    accrued=pay*day_count(prev,start,basis)/period
    return dirty-accrued,duration,duration/base

def lit(v):return format(float(v),'.17g')
def formula(name,start,end,coupon,yield_,frequency,basis=0,redemption=100):
    core=f'"{start}","{end}",{lit(coupon)},{lit(yield_)}'
    return f'{name}({core},'+(lit(redemption)+',' if name in {'PRICE','YIELD'} else '')+f'{frequency},{basis})'

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--test-bin',type=Path,default=ROOT/'build-native/fltk/calligra-native-financial-test');p.add_argument('--output',type=Path);a=p.parse_args()
    cases=[];compatibility_differences=[]
    def add(group,f,want,rtol=5e-12,atol=1e-12):
        if isinstance(want,(float,int,mp.mpf)):
            want=float(want)
            if not math.isfinite(want):raise ValueError((f,want))
        cases.append((group,f,want,rtol,atol))
    source=(ROOT/'sheets/tests/TestFinancialFunctions.cpp').read_text()
    source=re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',lambda m:m[0] if m[0].startswith('"') else '',source)
    for m in re.finditer(r'CHECK_EVAL(_SHORT|_EQUAL)?\(("(?:\\.|[^"\\])*")\s*,\s*Value\(([^;]*?)\)\s*\);',source):
        f=ast.literal_eval(m[2]);name=re.match(r'\s*([\w.]+)\s*\(',f)[1].upper().split('GET')[-1]
        if name in {'DURATION_ADD','MDURATION'}:add('retained',f,ast.literal_eval(m[3]),atol=3e-10)
    assert len(cases)==3,len(cases)
    rng=random.Random(20261029)
    for _ in range(180):
        year=rng.choice([1900,2000,2024,2100]);start=dt.date(year,rng.randint(1,12),rng.randint(1,28))
        end=dt.date(year+rng.randint(1,30),rng.randint(1,12),rng.choice([1,15,28]))
        if end<=start:end=end.replace(year=end.year+1)
        coupon=rng.choice([0.,1e-20,.03,.125]);yield_=rng.choice([0.,1e-20,.001,.07,1.2])
        frequency=rng.choice([1,2,4]);basis=rng.randrange(5);redemption=rng.uniform(20,200)
        price,duration,modified=bond_values(start,end,coupon,yield_,redemption,frequency,basis)
        add('mpmath-cashflows',formula('PRICE',start,end,coupon,yield_,frequency,basis,redemption),price,atol=1e-10)
        _,duration,modified=bond_values(start,end,coupon,yield_,100,frequency,basis)
        add('mpmath-cashflows',formula('DURATION',start,end,coupon,yield_,frequency,basis),duration)
        add('mpmath-cashflows',formula('MDURATION',start,end,coupon,yield_,frequency,basis),modified)
        _,legacy,_=bond_values(start,end,coupon,yield_,100,frequency,basis)
        add('retained-calendar-clock',formula('DURATION_ADD',start,end,coupon,yield_,frequency,basis),legacy)
        if price>0:
            # The price is rounded to the same binary64 Number passed to the
            # native solver. Rate tolerance allows that rounding at zero yield.
            add('inverse-yield',formula('YIELD',start,end,coupon,float(price),frequency,basis,redemption),yield_,rtol=2e-10,atol=3e-11)
    for yield_ in [-.01,-.2,-.8,-.9999,-.99999999999999]:
        start,end=dt.date(2024,1,1),dt.date(2025,1,1)
        price,_,_=bond_values(start,end,.1,yield_,100,1,0)
        add('negative-yield',formula('YIELD',start,end,.1,price,1),yield_,rtol=2e-12,atol=1e-13)
    # Boundary rates/spans use direct high-precision cash-flow sums, including
    # zero-coupon underflow in the discounted value but a finite duration.
    for years in [1,10,100]:
        for rate in [0.,1e-20,1.,1e100,1e300]:
            start,end=dt.date(2000,1,1),dt.date(2000+years,1,1)
            for coupon in [0.,.1]:
                price,duration,modified=bond_values(start,end,coupon,rate,100,2,1)
                add('extreme-rates',formula('PRICE',start,end,coupon,rate,2,1),price,atol=1e-300)
                add('extreme-rates',formula('DURATION',start,end,coupon,rate,2,1),duration)
                add('extreme-rates',formula('MDURATION',start,end,coupon,rate,2,1),modified,atol=1e-310)
    for rate in [1e10,1e100,1e300]:
        price=mp.mpf(100)/(1+mp.mpf(rate))
        add('extreme-inversion',formula('YIELD','2024-01-01','2025-01-01',0,price,1),rate,rtol=3e-12)
    add('representability','YIELD("2024-01-01","2025-01-01",0,1e-300,1e300,1)','#NUM!')
    add('wide-span','PRICE("0001-01-01","9999-12-31",.1,0,100,12,0)',
        mp.mpf(.1)*100/12*(mp.mpf(119988)-mp.mpf(1)/30)+100,atol=1e-9)
    for frequency in [3,6,12]:
        price,_,_=bond_values('2024-02-29','2028-08-31',.07,.1,100,frequency,1)
        add('retained-frequencies',formula('PRICE','2024-02-29','2028-08-31',.07,.1,frequency,1),price)
    for f,want in [
        ('PRICE("2008-02-15","2017-11-15",.0575,.065,100,2,0)',94.6343616213225),
        ('PRICE("2024-08-30","2024-08-31",.1,.1,100,2,0)',100),
        ('YIELD("2024-08-30","2024-08-31",.1,100,100,2,0)','#NUM!'),
        ('YIELD(0,365,.1,0,100,2)','#VALUE!'),
        ('DURATION(0,365,.1,.1)','#VALUE!'),
        ('PRICE(0,365,.1,-.1,100,2)','#VALUE!'),
    ]:add('documented-and-degenerate',f,want,atol=1e-10)
    # Retain published Excel values as explicit compatibility observations.
    # The native port follows the source YEARFRAC clock, rather than silently
    # calling coupon-period times an identical implementation.
    observations=[('DURATION','2018-07-01','2048-01-01',10.9191453,7),
                  ('MDURATION','2008-01-01','2016-01-01',5.736,3)]
    for name,start,end,published,decimals in observations:
        _,duration,modified=bond_values(start,end,.08,.09,100,2,1)
        native=duration if name=='DURATION' else modified
        f=formula(name,start,end,.08,.09,2,1)
        assert abs(float(native)-published)>.5*10**(-decimals),(name,native,published)
        compatibility_differences.append({'formula':f,'published_excel_value':published,
                                         'published_decimals':decimals,'retained_calendar_clock_value':float(native),
                                         'source':'https://support.microsoft.com/en-us/excel/functions/'+name.lower()+'-function'})
        add('published-calendar-clock-difference',f,native)
    for name in ['PRICE','YIELD','DURATION','MDURATION','DURATION_ADD']:
        sample=next(c for c in cases if c[1].startswith(name+'(') and isinstance(c[2],float))
        for prefix in ['_xlfn.','COM.MICROSOFT.']+([] if name=='DURATION_ADD' else ['COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GET']):
            add('namespaces',sample[1].replace(name,prefix+name,1),sample[2],sample[3],sample[4])
    run=subprocess.run([str(a.test_bin.resolve()),'--eval'],input='\n'.join(c[1] for c in cases)+'\n',text=True,capture_output=True,env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},timeout=120)
    assert run.returncode==0,(run.returncode,run.stderr[-2000:])
    values=[json.loads(line) for line in run.stdout.splitlines()];assert len(values)==len(cases)
    counts=collections.Counter();failures=[]
    for (group,f,want,rtol,atol),got in zip(cases,values):
        counts[group]+=1
        valid=got['display']==want if isinstance(want,str) else got['kind']==1 and math.isclose(got['value'],want,rel_tol=rtol,abs_tol=atol)
        if not valid or got['incomplete']:failures.append({'formula':f,'expected':want,'actual':got})
    report={'cases':len(cases),'groups':dict(counts),'retained_deviations':[],'compatibility_differences':compatibility_differences,'failures':failures}
    if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'cases':len(cases),'groups':dict(counts),'failures':len(failures)},indent=2))
    for failure in failures[:15]:print(failure)
    raise SystemExit(bool(failures))
