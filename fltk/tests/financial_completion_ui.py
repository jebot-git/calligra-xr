#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Financial-completion keyboard editing, recalculation, cache persistence and reopen."""
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
        import math
        from PIL import Image
        from financial_completion_producer import produce,verify,expected
        sources=produce(base/'fixtures')
        for source in sources:verify(source,args.test_bin_dir,art/'producer')
        doc=sources[0];report=art/'financial.json';capture=art/'financial.ppm'
        with zipfile.ZipFile(doc) as z:original={name:z.read(name) for name in z.namelist()}
        with (art/'financial.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-sheets-fltk'),str(doc),'--report',str(report),'--capture',str(capture)],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists());host=wait(lambda:window('sheets'))
            def first():
                click(host,110,140)
                for _ in range(data()['cell']['column']-1):key('Left')
                wait(lambda:data()['cell']['column']==1 and data()['cell']['row']==1)
            def cells(cash):
                first()
                for column,value in enumerate(expected(cash),1):
                    def matches():
                        cell=data()['cell']
                        if cell['column']!=column or cell['row']!=1:return False
                        if isinstance(value,str):return cell['value']==value
                        try:
                            actual=float(cell['value']);painted=float(cell.get('painted_value','nan'))
                            return math.isclose(actual,value,rel_tol=3e-10,abs_tol=1e-10) and math.isclose(painted,actual,rel_tol=1e-3,abs_tol=1e-15)
                        except ValueError:return False
                    wait(matches)
                    if column<len(expected(cash)):key('Right')
            cells(1000)
            first();click(host,410,74);key('a',('Control_L',));text('2000');key('Return')
            cells(2000)
            key('z',('Control_L',));cells(1000)
            key('y',('Control_L',));cells(2000)
            first();time.sleep(.15);g=host.get_geometry();snap=host.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
            Image.frombytes('RGB',(g.width,g.height),snap.data,'raw','BGRX').save(art/'financial-edited.png')
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            verify(doc,args.test_bin_dir,art/'saved',money=2000)
            with zipfile.ZipFile(doc) as saved:
                for name,value in original.items():
                    if name!='content.xml':assert saved.read(name)==value,name
            wait(lambda:capture.exists())
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            report.unlink()
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-sheets-fltk'),str(doc),'--report',str(report)],env=env,stdout=log,stderr=log)
            wait(lambda:report.exists());host=wait(lambda:window('sheets'));cells(2000)
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            print('PASS financial-completion keyboard editing, native recalculation, undo/redo, native save/cache retention and reopen',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
