#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real FLTK print dialogs and CUPS jobs for Words, Sheets and Stage."""
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
parser.add_argument('--read-only',action='store_true');args=parser.parse_args()
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
        from PIL import Image
        from pypdf import PdfReader
        from print_producer import produce
        from cups_runtime import PrivateCups
        sources=produce(base/'fixtures')
        if args.read_only:
            for source in sources.values():
                with zipfile.ZipFile(source) as z:parts={name:z.read(name) for name in z.namelist()}
                parts['META-INF/documentsignatures.xml']=b'<document-signatures xmlns="urn:oasis:names:tc:opendocument:xmlns:digitalsignature:1.0"/>'
                with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as z:
                    for name,value in parts.items():z.writestr(name,value)

        with PrivateCups(art/'cups') as cups:
            env['CUPS_SERVER']=cups.server
            count=0
            for app,source in sources.items():
                with (art/(app+'.log')).open('w') as log:
                    report=art/(app+'-report.json')
                    process=subprocess.Popen([str(args.bin_dir.resolve()/('calligra-'+app+'-fltk')),str(source),'--report',str(report)],env=env,stdout=log,stderr=log)
                    def data():return json.loads(report.read_text())
                    host=wait(lambda:window(app))
                    host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
                    key('p',('Control_L',))
                    wait(lambda:report.exists() and data()['print_dialog'])
                    def origin():
                        width,height=data()['window_size']
                        return max(0,(width-510)//2),max(0,(height-(320 if app=='sheets' else 270))//2)
                    if app=='words':
                        key('Escape');wait(lambda:not data()['print_dialog'])
                        assert not list((art/'cups/output').glob('*.pdf'))
                        host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();key('p',('Control_L',));wait(lambda:data()['print_dialog'])
                        left,top=origin()
                        click(host,left+170,top+80);key('a',('Control_L',));text('2')
                        click(host,left+205,top+125);text('2')
                    time.sleep(.2)
                    geometry=host.get_geometry();snap=host.get_image(0,0,geometry.width,geometry.height,X.ZPixmap,0xffffffff)
                    Image.frombytes('RGB',(geometry.width,geometry.height),snap.data,'raw','BGRX').save(art/(app+'-print-dialog.png'))
                    left,top=origin();click(host,left+435,top+(285 if app=='sheets' else 235))
                    wait(lambda:not data()['print_dialog'])
                    count+=1
                    files=cups.completed(count)
                    assert len(PdfReader(files[-1]).pages)=={'words':2,'sheets':5,'stage':2}[app]
                    attributes=subprocess.check_output(['ipptool','-tv','ipp://'+cups.server+'/jobs/'+str(count),'/usr/share/cups/ipptool/get-job-attributes.test'],env=cups.env,text=True)
                    (art/(app+'-job-attributes.txt')).write_text(attributes)
                    assert 'job-state (enum) = completed' in attributes,attributes
                    if app=='words':
                        assert 'copies (integer) = 2' in attributes and 'page-ranges (rangeOfInteger) = 2' in attributes,attributes
                    host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();key('q',('Control_L',))
                    assert process.wait(timeout=10)==0
                    process=None
                    print('PASS',app,'Ctrl+P, native FLTK printer dialog and completed CUPS job',flush=True)
            print('PASS all three real GUI print workflows; system queues unchanged',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
