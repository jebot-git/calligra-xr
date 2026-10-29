#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native split/merged cell glyph pixels, page continuations and source editing."""
import argparse
import json
import os
import re
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import zipfile
import xml.etree.ElementTree as ET
from Xlib import X, XK, display
from Xlib.error import XError
from Xlib.ext import xtest
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bin-dir',type=Path,default=ROOT/'build-native/fltk')
parser.add_argument('--artifacts',type=Path,default=ROOT/'build-native/ui-artifacts')
parser.add_argument('--test-bin-dir',type=Path,default=ROOT/'build-native/fltk')
args=parser.parse_args()
if not os.environ.get('CALLIGRA_UI_BUS'):
    os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'CALLIGRA_UI_BUS':'1'})
signal.pthread_sigmask(signal.SIG_SETMASK,[])
art=args.artifacts.resolve();art.mkdir(parents=True,exist_ok=True)
def wait(test,timeout=20):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            v=test()
            if v:return v
        except (OSError,ValueError,KeyError,XError):pass
        time.sleep(.06)
    raise AssertionError('Timed out; inspect '+str(art))
with tempfile.TemporaryDirectory(prefix='calligra-ui-') as temp:
    base=Path(temp);env={**os.environ,'FLTK_BACKEND':'x11','NO_AT_BRIDGE':'1'}
    for name in ('CONFIG','DATA','CACHE','STATE','RUNTIME'):
        p=base/name.lower();p.mkdir(mode=0o700);env[f'XDG_{name}_'+('DIR' if name=='RUNTIME' else 'HOME')]=str(p)
    for key in ('WAYLAND_DISPLAY','WAYLAND_SOCKET'):env.pop(key,None)
    rd,wr=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1400x1000x24','-nolisten','tcp'],pass_fds=(wr,),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);os.close(wr)
    with os.fdopen(rd) as stream:env['DISPLAY']=':'+stream.readline().strip()
    xd=display.Display(env['DISPLAY']);process=None
    def key(name,modifiers=()):
        mods=[xd.keysym_to_keycode(XK.string_to_keysym(m)) for m in modifiers]
        for code in mods:xtest.fake_input(xd,X.KeyPress,code);xd.sync();time.sleep(.02)
        code=xd.keysym_to_keycode(XK.string_to_keysym(name));assert code,name
        xtest.fake_input(xd,X.KeyPress,code);xd.sync();time.sleep(.02);xtest.fake_input(xd,X.KeyRelease,code)
        for code in reversed(mods):xtest.fake_input(xd,X.KeyRelease,code)
        xd.sync();time.sleep(.05)
    def text(value):
        for c in value:
            choices=xd.keysym_to_keycodes(ord(c));code,index=next((code,index) for code,index in choices if index<2)
            shift=xd.keysym_to_keycode(XK.string_to_keysym('Shift_L'))
            if index:xtest.fake_input(xd,X.KeyPress,shift)
            xtest.fake_input(xd,X.KeyPress,code);xtest.fake_input(xd,X.KeyRelease,code)
            if index:xtest.fake_input(xd,X.KeyRelease,shift)
            xd.sync();time.sleep(.025)
    def window(app):
        for w in xd.screen().root.query_tree().children:
            if 'Calligra '+app.title() in str(w.get_wm_name()):return w
    def click(w,x,y):
        w.configure(stack_mode=X.Above);w.set_input_focus(X.RevertToParent,X.CurrentTime)
        point=xd.screen().root.translate_coords(w,0,0);xd.sync();time.sleep(.07)
        xtest.fake_input(xd,X.MotionNotify,x=point.x+x,y=point.y+y);xd.sync();time.sleep(.04)
        xtest.fake_input(xd,X.ButtonPress,1);xtest.fake_input(xd,X.ButtonRelease,1);xd.sync();time.sleep(.06)
    try:
        from cell_flow_producer import produce,verify
        from PIL import Image
        doc,expected=produce(base/'fixtures');verify(doc,expected,args.test_bin_dir,art)
        report=art/'cell-flow.json';capture=art/'cell-flow.ppm'
        with zipfile.ZipFile(doc) as z:original={n:z.read(n) for n in z.namelist()}
        with (art/'cell-flow.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-words-fltk'),str(doc),'--page-layout','--report',str(report),'--capture',str(capture)],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            def state():return data()['page_layout']
            wait(lambda:report.exists() and capture.exists());host=wait(lambda:window('words'))
            st=state();assert st['pages']>=3 and not st['unsupported'] and not st['render_error'] and not st['missing_glyphs'],st
            cells=st['table_cells'];outer=next(c['table'] for c in cells if c['rows']==2)
            pages=st['pages'];widths={p:next(c['page_width'] for c in cells if c['page']==p) for p in range(pages)}
            heights={p:next(c['page_height'] for c in cells if c['page']==p) for p in range(pages)}
            assert widths[0]==300 and all(widths[p]==380 for p in range(1,pages)),widths
            assert len([c for c in cells if c['header'] and c['table']==outer])==pages*2
            assert any(c['continued'] and c['rows']==2 for c in cells if c['page']>0)
            x,y,w,h=st['viewport'];zoom=max(.05,min(1.5,(w-48)/max(widths.values())))
            extent=24+sum(heights[p]*zoom+24 for p in range(pages));maximum=max(0,extent-h)
            scroll=0;seen={p:0 for p in range(pages)}
            click(host,x+int(w/2),y+10)
            while True:
                xd.sync();time.sleep(.2);g=host.get_geometry();snap=host.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
                picture=Image.frombytes('RGB',(g.width,g.height),snap.data,'raw','BGRX')
                picture.save(art/f'cell-flow-scroll-{int(scroll)}.png')
                regions=[];top=y+12-scroll
                for p in range(pages):
                    left=x+(w-widths[p]*zoom)/2
                    for c in cells:
                        if c['page']==p and c['table']==outer and not c['header']:
                            regions.append((p,c['column'],left+c['x']*zoom,top+c['y']*zoom,left+(c['x']+c['width'])*zoom,top+(c['y']+c['height'])*zoom))
                    top+=heights[p]*zoom+24
                for py in range(max(0,y),min(picture.height,y+h)):
                    for px in range(max(0,x),min(picture.width,x+w)):
                        r,g,b=picture.getpixel((px,py));red=r>100 and r>g+60 and r>b+60;green=g>r+35 and g>b+25
                        if not red and not green:continue
                        matches=[v for v in regions if v[1]==(0 if red else 1) and v[2]-1<=px<v[4]+1 and v[3]-1<=py<v[5]+1]
                        assert matches,('glyph outside source cell',scroll,px,py,(r,g,b),regions)
                        if red:seen[matches[0][0]]+=1
                if scroll>=maximum-1e-6:break
                key('Page_Down');scroll=min(maximum,scroll+h*.85)
            assert all(v>30 for v in seen.values()),seen
            key('Home');wait(lambda:state()['page']==1)
            target=next(c for c in cells if c['page']==0 and c['table']==outer and c['rows']==2)
            left=x+(w-widths[0]*zoom)/2;px=left+(target['x']+10)*zoom;py=y+12+(target['y']+10)*zoom
            click(host,int(px),int(py));click(host,int(px),int(py));wait(lambda:not state()['visible'])
            text('Edited ');wait(lambda:'Edited LEFT_00' in data()['text'])
            key('z',('Control_L',));wait(lambda:'Edited LEFT_00' not in data()['text'])
            key('y',('Control_L',));wait(lambda:'Edited LEFT_00' in data()['text'])
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            whitespace='{urn:oasis:names:tc:opendocument:xmlns:text:1.0}s'
            def signature(e):return e.tag,tuple(sorted(e.attrib.items())),tuple(signature(c) for c in e if c.tag!=whitespace)
            with zipfile.ZipFile(doc) as saved:
                assert signature(ET.fromstring(saved.read('content.xml')))==signature(ET.fromstring(original['content.xml']))
                for name,value in original.items():
                    if name!='content.xml':assert saved.read(name)==value,name
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            expected[3]='Edited '+expected[3]
            saved=art/'saved';saved.mkdir();verify(doc,expected,args.test_bin_dir,saved)
            reopen=art/'cell-flow-reopen.json'
            subprocess.run([str(args.bin_dir.resolve()/'calligra-words-fltk'),str(doc),'--page-layout','--report',str(reopen),'--quit-after-ms','1800'],env=env,stdout=log,stderr=log,check=True)
            restored=json.loads(reopen.read_text());assert 'Edited LEFT_00' in restored['text'] and not restored['page_layout']['render_error'],restored
            print('PASS: split/merged/nested cell glyph pixels on all page widths, whole source coverage, source selection and XML-preserving edit/undo/save/reopen',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
