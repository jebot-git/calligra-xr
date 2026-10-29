#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native kept table/caption pixels and source-preserving editing."""
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
        from keep_producer import produce,verify
        from PIL import Image
        for case in produce(base/'fixtures'):
            kind=case['kind'];out=art/kind;out.mkdir()
            reference=verify(case,args.test_bin_dir,out)
            doc=case['path'];report=out/'words.json';capture=out/'words.ppm'
            with zipfile.ZipFile(doc) as z:original={n:z.read(n) for n in z.namelist()}
            with (out/'words.log').open('w') as log:
                process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-words-fltk'),str(doc),'--page-layout','--report',str(report),'--capture',str(capture)],env=env,stdout=log,stderr=log)
                def data():return json.loads(report.read_text())
                def state():return data()['page_layout']
                wait(lambda:report.exists() and capture.exists());host=wait(lambda:window('words'))
                st=state();assert not st['unsupported'] and not st['render_error'] and not st['missing_glyphs'],st
                assert st['pages']==len(reference['pages']),st
                x,y,w,h=st['viewport'];zoom=max(.05,min(1.5,(w-48)/max(p['width'] for p in reference['pages'])))
                extent=24+sum(p['height']*zoom+24 for p in reference['pages']);scroll=max(0,extent-h)
                click(host,x+int(w/2),y+10);key('End');wait(lambda:state()['page']==st['pages'])
                time.sleep(.2);xd.sync();g=host.get_geometry();snap=host.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
                picture=Image.frombytes('RGB',(g.width,g.height),snap.data,'raw','BGRX');picture.save(out/'kept-caption.png')
                last=len(reference['pages'])-1;page=reference['pages'][last]
                top=y+12-scroll+sum(p['height']*zoom+24 for p in reference['pages'][:last]);left=x+(w-page['width']*zoom)/2
                captions=[l for l in page['lines'] if l['source']==case['caption']];assert captions
                bounds=tuple(int(v) for v in (left+min(l['x'] for l in captions)*zoom-2,top+min(l['y'] for l in captions)*zoom-2,left+(page['width']-15)*zoom,top+max(l['y']+l['height'] for l in captions)*zoom+2))
                magenta=sum(r>90 and b>90 and r>g+40 and b>g+40 for r,g,b in picture.crop(bounds).get_flattened_data())
                assert magenta>25,(kind,magenta,bounds)
                cells=state()['table_cells'];bodycell=next(c for c in cells if c['table']==case['table'] and c['page']==last and not c['header'])
                bounds=tuple(int(v) for v in (left+(bodycell['x']+3)*zoom,top+(bodycell['y']+3)*zoom,left+(bodycell['x']+bodycell['width']-3)*zoom,top+(bodycell['y']+bodycell['height']-3)*zoom))
                blue=sum(all(abs(a-b)<=2 for a,b in zip(p,(200,216,255))) for p in picture.crop(bounds).get_flattened_data())
                assert blue>100,(kind,blue,bounds)
                # Select the actual kept table's source cell, including a child
                # table nested within the outer cell's continuing text flow.
                key('Home');wait(lambda:state()['page']==1)
                target=next(c for c in cells if c['table']==case['table'] and not c['header'] and c['column']==0)
                tpage=target['page'];left=x+(w-reference['pages'][tpage]['width']*zoom)/2
                top=y+12+sum(p['height']*zoom+24 for p in reference['pages'][:tpage])
                px=left+(target['x']+10)*zoom;py=top+(target['y']+10)*zoom
                assert y<=py<y+h,(kind,py,(y,h))
                click(host,int(px),int(py));click(host,int(px),int(py));wait(lambda:not state()['visible'])
                text('Edited ');wanted='Edited '+case['expected'][case['target']]
                wait(lambda:wanted in data()['text'])
                key('z',('Control_L',));wait(lambda:wanted not in data()['text'])
                key('y',('Control_L',));wait(lambda:wanted in data()['text'])
                key('s',('Control_L',));wait(lambda:not data()['modified'])
                whitespace='{urn:oasis:names:tc:opendocument:xmlns:text:1.0}s'
                def signature(e):return e.tag,tuple(sorted(e.attrib.items())),tuple(signature(c) for c in e if c.tag!=whitespace)
                with zipfile.ZipFile(doc) as saved:
                    assert signature(ET.fromstring(saved.read('content.xml')))==signature(ET.fromstring(original['content.xml']))
                    for name,value in original.items():
                        if name!='content.xml':assert saved.read(name)==value,name
                key('q',('Control_L',));assert process.wait(timeout=10)==0
                case['expected'][case['target']]=wanted;verify(case,args.test_bin_dir,out/'saved')
                reopen=out/'reopen.json'
                subprocess.run([str(args.bin_dir.resolve()/'calligra-words-fltk'),str(doc),'--page-layout','--report',str(reopen),'--quit-after-ms','1800'],env=env,stdout=log,stderr=log,check=True)
                restored=json.loads(reopen.read_text());assert wanted in restored['text'] and not restored['page_layout']['render_error'],restored
                print('PASS:',kind,'kept table/caption GPU pixels, nested source selection, edit/undo/save/reopen and retained keep styles',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
