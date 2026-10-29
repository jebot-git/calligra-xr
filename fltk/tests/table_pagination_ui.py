#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Words table pagination, repeated-header pixels and cell selection."""
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
        from table_pagination_producer import produce
        from PIL import Image
        doc=produce(base/'fixtures');report=art/'table-pages.json';capture=art/'table-pages.ppm'
        with zipfile.ZipFile(doc) as z:original={n:z.read(n) for n in z.namelist()}
        with (art/'table-pages.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-words-fltk'),str(doc),'--page-layout','--report',str(report),'--capture',str(capture)],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            def state():return data()['page_layout']
            wait(lambda:report.exists() and capture.exists());host=wait(lambda:window('words'))
            st=state();assert st['pages']==3 and not st['render_error'] and not st['missing_glyphs'] and not st['unsupported'],st
            cells=st['table_cells'];assert len(cells)==42 and len([c for c in cells if c['header']])==6,cells
            assert len([c for c in cells if c['continued']])==4,cells
            for cell in cells:
                assert cell['y']>=15 and cell['y']+cell['height']<=245+1e-6,cell
            image=Image.open(capture).convert('RGB');image.save(art/'table-pages.jpg')
            x,y,w,h=st['viewport'];zoom=max(.05,min(1.5,(w-48)/300));left=x+(w-300*zoom)/2;top=y+12
            def rect(c):return tuple(int(v) for v in (left+(c['x']+4)*zoom,top+(c['y']+4)*zoom,left+(c['x']+c['width']-4)*zoom,top+(c['y']+c['height']-4)*zoom))
            for col in (0,1):
                header=next(c for c in cells if c['page']==0 and c['row']==0 and c['column']==col)
                pixels=list(image.crop(rect(header)).get_flattened_data())
                assert sum(all(abs(a-b)<=2 for a,b in zip(p,(200,216,255))) for p in pixels)>300
                assert sum(p[2]<160 and p[0]<100 for p in pixels)>10
            # Double-clicking a right-hand cell must resolve its source paragraph,
            # rather than the left-hand cell that shares the same line height.
            target=next(c for c in cells if c['page']==0 and c['row']==1 and c['column']==1)
            px=left+(target['x']+10)*zoom;py=top+(target['y']+10)*zoom
            click(host,int(px),int(py));click(host,int(px),int(py))
            wait(lambda:not state()['visible'])
            assert data()['editing']['cursor'] == len(data()['text'].split('R01')[0]) + 1
            text('Marked ')
            wait(lambda:'L01\nRMarked 01' in data()['text'])
            key('z',('Control_L',));wait(lambda:'Marked ' not in data()['text'])
            key('y',('Control_L',));wait(lambda:'L01\nRMarked 01' in data()['text'])
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            with zipfile.ZipFile(doc) as saved:
                xml=ET.fromstring(saved.read('content.xml'));ns={'t':'urn:oasis:names:tc:opendocument:xmlns:table:1.0','p':'urn:oasis:names:tc:opendocument:xmlns:text:1.0'}
                rows=xml.findall('.//t:table-row',ns);assert len(rows)==19
                rowcells=rows[1].findall('t:table-cell',ns)
                assert ''.join(rowcells[0].itertext())=='L01'
                assert ''.join(rowcells[1].itertext()).startswith('RMarked')
                for name,value in original.items():
                    if name!='content.xml':assert saved.read(name)==value,name
            click(host,610,25);wait(lambda:state()['visible'] and state()['pages']==3)
            # Every rendered page remains reachable through native page controls.
            click(host,350,100);key('End');wait(lambda:state()['page']==3)
            key('Home');wait(lambda:state()['page']==1)
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            reopen=art/'table-pages-reopen.json'
            subprocess.run([str(args.bin_dir.resolve()/'calligra-words-fltk'),str(doc),'--page-layout','--report',str(reopen),'--quit-after-ms','1800'],env=env,stdout=log,stderr=log,check=True)
            final=json.loads(reopen.read_text());assert final['page_layout']['pages']==3 and 'L01\nRMarked 01' in final['text'],final
            print('PASS: Words native table pages, repeated headers, GPU cell pixels, right-cell selection and edit/undo/save/reopen',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
