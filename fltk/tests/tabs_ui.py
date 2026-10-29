#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native paragraph tab and leader pixels/editing on private Xvfb and D-Bus."""
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
        fixtures=base/'fixtures'
        subprocess.run([sys.executable,str(ROOT/'fltk/tests/tabs_producer.py'),str(fixtures),'--test-bin-dir',str(args.test_bin_dir.resolve())],env=env,check=True)
        from PIL import Image
        for app,extension in [('words','odt'),('stage','odp')]:
            doc=fixtures/f'tabs.{extension}';report=art/f'tabs-{app}.json';capture=art/f'tabs-{app}.ppm'
            report.unlink(missing_ok=True);capture.unlink(missing_ok=True)
            with zipfile.ZipFile(doc) as z:original={n:z.read(n) for n in z.namelist()}
            with (art/f'tabs-{app}.log').open('w') as log:
                command=[str(args.bin_dir.resolve()/f'calligra-{app}-fltk'),str(doc),'--report',str(report),'--capture',str(capture)]
                if app=='words':command.append('--page-layout')
                process=subprocess.Popen(command,env=env,stdout=log,stderr=log)
                def data():return json.loads(report.read_text())
                def state():return data()['page_layout' if app=='words' else 'scene']
                wait(lambda:report.exists() and capture.exists());host=wait(lambda:window(app))
                st=state();assert not st['render_error'] and not st['missing_glyphs'],st
                assert len(st['tabs'])==14 and len([t for t in st['tabs'] if t['glyphs']])==2,st
                if app=='stage':assert not st['overflow_frames'],st
                picture=Image.open(capture).convert('RGB')
                x,y,w,h=st['viewport']
                if app=='words':
                    zoom=max(.05,min(1.5,(w-48)/max(t['page_width'] for t in st['tabs'])))
                    left=x+(w-620*zoom)/2;top=y+12
                else:
                    zoom=min((w-32)/st['width'],(h-32)/st['height']);left=x+(w-st['width']*zoom)/2;top=y+(h-st['height']*zoom)/2
                for i,tab in enumerate(st['tabs'][:10]):
                    baseline=tab['baseline'] if app=='words' else tab['y1']
                    x1,x2=sorted((tab['x1'],tab['x2']))
                    textual=bool(tab['glyphs']);above=32 if textual else 4;below=5
                    bounds=(max(0,int(left+(x1+2)*zoom)),max(0,int(top+(baseline-above)*zoom)),min(picture.width,int(left+(x2-2)*zoom)),min(picture.height,int(top+(baseline+below)*zoom)+1))
                    assert bounds[0]<bounds[2] and bounds[1]<bounds[3],(i,bounds)
                    crop=list(picture.crop(bounds).get_flattened_data())
                    def red(p):r,g,b=p;return r>140 and r>g+65 and r>b+65
                    def purple(p):r,g,b=p;return b>r+30 and b>g+70 and r>g+35
                    count=sum((purple if textual else red)(p) for p in crop)
                    assert count>4,(app,i,tab,bounds,count)
                if app=='words':
                    raster=state()['rasterizations']
                    for _ in range(3):host.clear_area(exposures=True);xd.sync();time.sleep(.12)
                    assert state()['rasterizations']==raster
                    click(host,610,25);wait(lambda:not state()['visible']);click(host,350,100)
                else:click(host,350,565)
                key('Home',('Control_L',));text('Edited ');wait(lambda:data()['text'].startswith('Edited '))
                key('s',('Control_L',));wait(lambda:not data()['modified'])
                if app=='words':click(host,610,25);wait(lambda:state()['visible'] and len(state()['tabs'])==14)
                else:
                    key('z',('Control_L',));wait(lambda:not data()['text'].startswith('Edited '))
                    key('y',('Control_L',));wait(lambda:data()['text'].startswith('Edited ') and not data()['modified'])
                whitespace={'{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'+name for name in ('s','line-break')}
                def signature(e):return e.tag,tuple(sorted(e.attrib.items())),tuple(signature(c) for c in e if c.tag not in whitespace)
                with zipfile.ZipFile(doc) as saved:
                    assert signature(ET.fromstring(saved.read('content.xml')))==signature(ET.fromstring(original['content.xml']))
                    for name,value in original.items():
                        if name!='content.xml':assert saved.read(name)==value,name
                key('q',('Control_L',));assert process.wait(timeout=10)==0
                reopen=art/f'tabs-{app}-reopen.json'
                command=[str(args.bin_dir.resolve()/f'calligra-{app}-fltk'),str(doc),'--report',str(reopen),'--quit-after-ms','2200']
                if app=='words':command.append('--page-layout')
                subprocess.run(command,env=env,stdout=log,stderr=log,check=True)
                restored=json.loads(reopen.read_text())['page_layout' if app=='words' else 'scene']
                assert len(restored['tabs'])==14 and not restored['render_error'],restored
                print('PASS:',app,'tab anchors, all leader pixels, scoped styles, cached glyph redraw, source offsets and native edit/save/reopen',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
