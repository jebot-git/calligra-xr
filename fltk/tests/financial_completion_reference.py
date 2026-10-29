#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Complete financial registration audit, retained corpus and independent references."""
import argparse,ast,calendar,collections,datetime as dt,json,math,os,random,re,subprocess
from decimal import Decimal as D,localcontext,ROUND_HALF_UP,ROUND_FLOOR
from fractions import Fraction as F
from pathlib import Path
from calendar_reference import date,fraction,days
ROOT=Path(__file__).resolve().parents[2]
NAMES={'ACCRINT','AMORDEGRC','AMORLINC','EURO','EUROCONVERT','ORG.OPENOFFICE.CONVERT',
       'ODDLPRICE','ODDLYIELD','PRICEMAT','TBILLEQ','TBILLPRICE','TBILLYIELD','YIELDMAT'}
RATES={key:D(value) for key,value in {'ATS':'13.7603','BEF':'40.3399','CYP':'.585274','DEM':'1.95583','EEK':'15.6466',
       'ESP':'166.386','EUR':'1','FIM':'5.94573','FRF':'6.55957','GRD':'340.75','IEP':'.787564','ITL':'1936.27',
       'LTL':'3.45280','LUX':'40.3399','LUF':'40.3399','LVL':'.702804','MTL':'.4293','NLG':'2.20371',
       'PTE':'200.482','SIT':'239.64','SKK':'30.126'}.items()}
def decimal(value):
    if isinstance(value,D):return value
    if isinstance(value,F):return D(value.numerator)/D(value.denominator)
    return D(str(value))
def round_decimal(value,digits=0):return value.quantize(D(1).scaleb(-digits),rounding=ROUND_HALF_UP)
def euro(value,source,target,full=False,precision=None,strict=False):
    if strict and (source!=source.upper() or target!=target.upper()):return '#VALUE!'
    source,target=source.upper(),target.upper()
    if source not in RATES or target not in RATES:return '#NUM!'
    if precision is not None:
        precision=math.floor(precision)
        if precision<3:return '#VALUE!'
    value=decimal(value)
    if source==target:return value
    with localcontext() as ctx:
        ctx.prec=100
        intermediate=value/RATES[source]
        if precision is not None and precision<80:intermediate=round_decimal(intermediate,precision)
        result=intermediate*RATES[target]
        return result if full or strict else round_decimal(result,0 if target in {'BEF','ESP','ITL','LUF','LUX'} else 2)
def accrue(issue,first,settlement,rate,par,frequency,basis=0):
    issue,settlement=date(issue),date(settlement);basis=math.trunc(basis);frequency=math.trunc(frequency)
    if frequency not in (1,2,3,4,6,12) or issue>=settlement or rate<=0 or par<0 or basis not in range(5):return '#VALUE!'
    if basis in (0,4):
        count=(settlement.year-issue.year)*360+(settlement.month-issue.month)*30+settlement.day-issue.day
        if basis==0 and issue.month==2 and settlement.month!=2 and issue.year==settlement.year:count-=1 if calendar.isleap(issue.year) else 2
    else:count=(settlement-issue).days
    denominator=366 if basis==1 and calendar.isleap(issue.year) else 365 if basis in (1,3) else 360
    return decimal(rate)*decimal(par)*D(count)/denominator
def maturity(name,settlement,end,issue,rate,quote,redemption=100,frequency=2,basis=0):
    settlement,end,issue=map(date,(settlement,end,issue));basis=math.trunc(basis);frequency=math.trunc(frequency)
    if settlement>=end or rate<0 or basis not in range(5):return '#VALUE!'
    total,accrued,left=[decimal(fraction(a,b,basis)) for a,b in [(issue,end),(issue,settlement),(settlement,end)]]
    rate,quote,redemption=map(decimal,(rate,quote,redemption))
    if name.startswith('ODDL'):
        if issue>=settlement or rate<=0 or redemption<=0 or frequency not in (1,2,4):return '#VALUE!'
        if name=='ODDLPRICE':return (redemption+100*rate*total)/(1+quote*left)-100*rate*accrued if quote>=0 else '#VALUE!'
        if quote<=0:return '#VALUE!'
        return ((redemption+100*rate*total)/(quote+100*rate*accrued)-1)/left if left else '#DIV/0!'
    if name=='PRICEMAT':return 100*((1+total*rate)/(1+left*quote)-accrued*rate) if quote>=0 else '#VALUE!'
    if quote<=0 or rate<=0:return '#VALUE!'
    return ((1+total*rate)/(quote/100+accrued*rate)-1)/left if left else '#DIV/0!'
def treasury(name,start,end,value):
    start,end=date(start),date(end);value=decimal(value)
    if value<=0:return '#VALUE!'
    if name=='TBILLEQ':
        end+=dt.timedelta(days=1)
        if start>=end:return '#VALUE!'
        count=days(start,end,0)
        if count>360:return '#VALUE!'
        denominator=360-value*count
        return 365*value/denominator if denominator else '#DIV/0!'
    if start>=end:return '#VALUE!'
    if name=='TBILLPRICE':
        if (end-start).days>365:return '#VALUE!'
        f=decimal(fraction(start,end+dt.timedelta(days=1),0))
        return '#VALUE!' if f==int(f) else 100*(1-value*f)
    count=days(start,end,0)+1
    return '#VALUE!' if count>360 else (100-value)/value*360/count

def amort(name,cost,purchase,end,salvage,period,rate,basis=0):
    period=math.trunc(period);basis=math.trunc(basis)
    if cost<0 or salvage<0 or period<0 or rate<0 or basis not in range(5):return '#VALUE!'
    cost,salvage,rate=map(decimal,(cost,salvage,rate));f=decimal(fraction(purchase,end,basis))
    if name=='AMORLINC':
        if cost==0 or rate==0:return D(0)
        annual=cost*rate;first=f*annual;full=int((cost-salvage-first)/annual)
        if period==0:return first
        if period<=full:return annual
        return max(D(0),cost-salvage-annual*full-first) if period==full+1 else D(0)
    if cost<=0 or rate<=0 or salvage>cost or date(purchase)>date(end):return '#VALUE!'
    life=1/float(rate);coefficient=1 if life<3 else 1.5 if life<5 else 2 if life<=6 else 2.5
    rate=D.from_float(float(rate)*coefficient)
    depreciation=round_decimal(f*rate*cost);cost-=depreciation;rest=cost-salvage
    for n in range(min(period,100001)):
        depreciation=round_decimal(D.from_float(float(rate)*float(cost)));new_rest=rest-depreciation
        if new_rest<0:return round_decimal(cost/2) if period-n<=1 else D(0)
        new_cost=cost-depreciation
        if new_cost==cost and new_rest==rest:return depreciation
        cost,rest=new_cost,new_rest
    return '#LIMIT!' if period>100000 else depreciation

def reference(formula):
    s=re.sub(r'COM\.SUN\.STAR\.SHEET\.ADDIN\.ANALYSIS\.GET','',formula,flags=re.I).replace('ORG.OPENOFFICE.CONVERT','OOO_CONVERT').replace(';',',')
    s=re.sub(r'"(?:\\.|[^"\\])*"|(?<![\w.])0+([0-9]+)(?![\w.])',lambda m:m[0] if m[0].startswith('"') else str(int(m[1])),s)
    s=re.sub(r'([0-9]+(?:\.[0-9]+)?)%',r'(\1/100)',s)
    def visit(n):
        if isinstance(n,ast.Constant):return n.value
        if isinstance(n,ast.UnaryOp) and isinstance(n.op,ast.USub):return -visit(n.operand)
        if isinstance(n,ast.BinOp):
            a,b=visit(n.left),visit(n.right)
            if isinstance(n.op,ast.Add):return a+b
            if isinstance(n.op,ast.Sub):return a-b
            if isinstance(n.op,ast.Mult):return a*b
            if isinstance(n.op,ast.Div):return a/b
        if isinstance(n,ast.Call) and isinstance(n.func,ast.Name):
            name=n.func.id.upper();args=[visit(a) for a in n.args]
            if name=='DATE':return dt.date(*map(int,args))
            if name in ('TRUE','FALSE'):return name=='TRUE'
            if name=='EURO':return RATES.get(args[0].upper(),'#NUM!')
            if name=='EUROCONVERT':return euro(*args)
            if name=='OOO_CONVERT':return euro(*args,full=True,strict=True)
            if name=='ACCRINT':return accrue(*args)
            if name.startswith('AMOR'):return amort(name,*args)
            if name.startswith('TBILL'):return treasury(name,*args)
            if name.startswith('ODDL'):return maturity(name,*args)
            if name in ('PRICEMAT','YIELDMAT'):
                return maturity(name,*args[:5],basis=args[5] if len(args)>5 else 0)
        raise ValueError(ast.dump(n))
    return visit(ast.parse(s,mode='eval').body)
def uncomment(source):return re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',lambda m:m[0] if m[0].startswith('"') else '',source)

def audit_registration():
    original=uncomment((ROOT/'sheets/functions/financial.cpp').read_text())
    registered=set(re.findall(r'new\s+Function\s*\(\s*"([^"]+)"',original))
    native=(ROOT/'fltk/src/native_financial.cpp').read_text().split('Text financial_name')[0]
    implemented=set(re.findall(r'\{"([A-Z_.]+)",',native))
    assert registered==implemented,(sorted(registered-implemented),sorted(implemented-registered))
    assert len(implemented)==64,len(implemented)
    entries=list(re.finditer(r'new\s+Function\s*\(\s*"([^"]+)"',original));original_arity={}
    for index,entry in enumerate(entries):
        block=original[entry.end():entries[index+1].start() if index+1<len(entries) else len(original)]
        match=re.search(r'setParamCount\(\s*(\d+)(?:\s*,\s*(-?\d+))?\s*\)',block);assert match,entry[1]
        low=int(match[1]);high=int(match[2]) if match[2] is not None else low
        original_arity[entry[1]]=[low,255 if high==-1 else high]
    native_arity={name:[int(low),int(high)] for name,low,high in re.findall(r'\{"([A-Z_.]+)",\s*\{(\d+),\s*(\d+)\}\}',native)}
    arity_differences={name:{'original':arity,'native':native_arity[name]} for name,arity in original_arity.items() if arity!=native_arity[name]}
    assert arity_differences=={'DURATION':{'original':[3,3],'native':[3,6]}},arity_differences
    return {'active_original_registrations':len(registered),'native_registrations':len(implemented),'missing':[],'spurious':[],
            'previously_missed_dotted_registration':'ORG.OPENOFFICE.CONVERT','arity_differences':arity_differences}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--test-bin',type=Path,default=ROOT/'build-native/fltk/calligra-native-financial-test');p.add_argument('--output',type=Path);a=p.parse_args()
    audit=audit_registration();cases=[];deviations=[]
    def add(group,formula,want,atol=1e-10,rtol=5e-12):
        if isinstance(want,(int,float,D,F)):want=float(want)
        assert isinstance(want,str) or math.isfinite(want),(formula,want)
        cases.append((group,formula,want,atol,rtol))
    source=uncomment((ROOT/'sheets/tests/TestFinancialFunctions.cpp').read_text())
    for m in re.finditer(r'CHECK_EVAL(_SHORT|_EQUAL)?\(("(?:\\.|[^"\\])*")\s*,\s*(.*?)\);',source,re.S):
        formula=ast.literal_eval(m[2]);call=re.match(r'\s*([\w.]+)\s*\(',formula)
        if not call or call[1].upper().split('GET')[-1] not in NAMES:continue
        raw=m[3].strip()
        while raw.startswith('Value(') and raw.endswith(')'):raw=raw[6:-1]
        if raw.startswith('Value::error'):want={'VALUE':'#VALUE!','NUM':'#NUM!','DIV0':'#DIV/0!'}[re.search(r'error(\w+)\(',raw)[1]]
        else:
            want=reference(raw)
        native_want=reference(formula)
        tolerance=2.3e-5 if m[1]=='_SHORT' else 3e-10
        same=want==native_want if isinstance(want,str) or isinstance(native_want,str) else math.isclose(float(want),float(native_want),abs_tol=tolerance,rel_tol=1e-12)
        if not same:deviations.append({'formula':formula,'retained_expected':want,'native_calendar_expected':str(native_want)})
        add('retained',formula,native_want,atol=1e-9)
    assert len(cases)==331, len(cases)
    assert not deviations, deviations
    rng=random.Random(20261029)
    for _ in range(100):
        issue=dt.date(rng.choice([1900,1999,2000,2024,2100]),rng.randint(1,12),rng.randint(1,28))
        start=issue+dt.timedelta(days=rng.randint(1,365));end=start+dt.timedelta(days=rng.randint(1,365*8))
        rate=rng.uniform(.001,.2);quote=rng.uniform(.001,.3);par=rng.uniform(10,10000);basis=rng.randrange(5);freq=rng.choice([1,2,4])
        args=f'"{start}","{end}","{issue}",{rate:.17g},{quote:.17g}'
        for name in ['PRICEMAT','YIELDMAT']:
            formula=name+'('+args+f',{basis})';add('maturity',formula,reference(formula))
        for name in ['ODDLPRICE','ODDLYIELD']:
            args=f'"{start}","{end}","{issue}",{rate:.17g},{quote if name=="ODDLPRICE" else 95:.17g},{par:.17g},{freq},{basis}'
            formula=name+'('+args+')';add('odd-last',formula,reference(formula))
        first=issue+dt.timedelta(days=182)
        formula=f'ACCRINT("{issue}","{first}","{start}",{rate:.17g},{par:.17g},{freq},{basis})';add('accrued',formula,reference(formula))
        for name in ['AMORLINC','AMORDEGRC']:
            cost=rng.randint(100,10000);salvage=rng.randrange(cost//3);period=rng.randrange(20);rate=rng.choice([.05,.1,.15,.25,.5])
            formula=f'{name}({cost},"{issue}","{first}",{salvage},{period},{rate},{basis})';add('amortization',formula,reference(formula),atol=1e-8)
        end=issue+dt.timedelta(days=rng.randint(1,365))
        for name,value in [('TBILLEQ',.05),('TBILLPRICE',.05),('TBILLYIELD',99.5)]:
            formula=f'{name}("{issue}","{end}",{value})';add('treasury',formula,reference(formula))
    for source in RATES:
        for target in RATES:
            for full in [False,True]:
                amount=rng.randint(-100000,100000)/100
                formula=f'EUROCONVERT({amount},"{source.lower()}","{target}",{str(full).upper()}())';add('fixed-euro',formula,euro(amount,source,target,full),atol=1e-8)
            amount=123.456
            formula=f'EUROCONVERT({amount},"{source}","{target}",TRUE(),3)';add('triangulation',formula,euro(amount,source,target,True,3),atol=1e-8)
    for name in sorted(NAMES):
        sample=next(c for c in cases if c[1].startswith(name+'(') and not isinstance(c[2],str))
        prefixes=['_xlfn.','COM.MICROSOFT.']
        if name not in {'EURO','EUROCONVERT','ORG.OPENOFFICE.CONVERT'}:prefixes.append('COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GET')
        for prefix in prefixes:
            add('namespaces',sample[1].replace(name,prefix+name,1),sample[2])
    for formula,want in [
        ('AMORDEGRC(2000,DATE(2004,2,1),DATE(2004,12,31),200,1,.15,1)',494),
        ('AMORDEGRC(1000,0,365,0,0,.2,3)',400),
        ('AMORDEGRC(1000,0,365,0,0,1/6,3)',333),
    ]:add('binary64-rounding-boundaries',formula,want)
    for formula,want in [
        ('AMORLINC(0,0,365,0,1,.1)',0),
        ('AMORLINC(100,0,365,0,1,0)',0),
        ('AMORDEGRC(100,0,365,0,1e300,.5,3)',0),
        ('EUROCONVERT(100,"ATS","EUR",TRUE(),1e100)',euro(100,'ATS','EUR',True)),
        ('EUROCONVERT(12.345,"EUR","EUR")',12.345),
        ('ORG.OPENOFFICE.CONVERT(100,"eur","DEM")','#VALUE!'),
    ]:add('bounded-edges',formula,want)
    run=subprocess.run([str(a.test_bin.resolve()),'--eval'],input='\n'.join(c[1] for c in cases)+'\n',text=True,capture_output=True,env={**os.environ,'LD_LIBRARY_PATH':'/usr/local/lib/domed/bitmap'},timeout=120)
    assert run.returncode==0,(run.returncode,run.stderr[-2000:])
    values=[json.loads(line) for line in run.stdout.splitlines()];assert len(values)==len(cases)
    failures=[];counts=collections.Counter()
    for (group,formula,want,atol,rtol),got in zip(cases,values):
        counts[group]+=1
        valid=got['display']==want if isinstance(want,str) else got['kind']==1 and math.isclose(got['value'],want,rel_tol=rtol,abs_tol=atol)
        if not valid or got['incomplete']:failures.append({'formula':formula,'expected':want,'actual':got})
    report={'cases':len(cases),'groups':dict(counts),'registration_audit':audit,'retained_deviations':deviations,'failures':failures}
    if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'cases':len(cases),'groups':dict(counts),'retained_deviations':len(deviations),'failures':len(failures)},indent=2))
    for item in failures[:15]:print(item)
    raise SystemExit(bool(failures))
