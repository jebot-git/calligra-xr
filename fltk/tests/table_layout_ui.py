#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Stage table paint/clipping/source editing on private Xvfb and D-Bus."""
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
        from table_layout_producer import produce
        from PIL import Image
        fixtures=produce(base/'fixtures');doc=fixtures/'tables.odp';report=art/'tables-stage.json';capture=art/'tables-stage.ppm'
        with zipfile.ZipFile(doc) as z:original={name:z.read(name) for name in z.namelist()}
        with (art/'tables-stage.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-stage-fltk'),str(doc),'--report',str(report),'--capture',str(capture)],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists() and capture.exists());host=wait(lambda:window('stage'))
            scene=data()['scene'];assert not scene['render_error'] and not scene['missing_glyphs'] and not scene['overflow_frames'],scene
            assert len(scene['table_cells'])==16 and scene['table_paints']>40,scene
            assert not any('Native table cell layout' in v for v in scene.get('unsupported',[])),scene
            image=Image.open(capture).convert('RGB');image.save(art/'tables-stage.jpg')
            x,y,w,h=scene['viewport'];zoom=min((w-32)/scene['width'],(h-32)/scene['height']);left=x+(w-scene['width']*zoom)/2;top=y+(h-scene['height']*zoom)/2
            def bounds(cell,margin=3):
                points=cell['corners'];xs=[p[0] for p in points];ys=[p[1] for p in points]
                return (int(left+(min(xs)+margin)*zoom),int(top+(min(ys)+margin)*zoom),int(left+(max(xs)-margin)*zoom),int(top+(max(ys)-margin)*zoom))
            def color_count(rect,color,tolerance=2):
                return sum(all(abs(a-b)<=tolerance for a,b in zip(pixel,color)) for pixel in image.crop(rect).get_flattened_data())
            cells=scene['table_cells'];header=next(c for c in cells if c['table']==0 and c['row']==0 and c['column']==0)
            assert header['columns']==2 and color_count(bounds(header),(200,216,255))>1000
            assert color_count(bounds(header),(32,64,170))>20
            merged=next(c for c in cells if c['table']==0 and c['row']==1 and c['column']==0)
            assert merged['rows']==2 and color_count(bounds(merged),(255,224,192))>1000
            clipped=next(c for c in cells if c['table']==0 and c['row']==2 and c['column']==1)
            points=clipped['corners'];bottom=max(p[1] for p in points)
            below=(int(left+(min(p[0] for p in points)+5)*zoom),int(top+(bottom+2)*zoom),int(left+(max(p[0] for p in points)-5)*zoom),int(top+(bottom+7)*zoom))
            assert color_count(below,(204,32,64))==0,below
            for cell in (c for c in cells if c['table']==1):
                points=cell['corners'];xmin=min(p[0] for p in points);xmax=max(p[0] for p in points);ymin=min(p[1] for p in points)
                strip=(int(left+(xmin+4)*zoom),int(top+ymin*zoom),int(left+(xmax-4)*zoom),int(top+(ymin+3)*zoom)+1)
                pixels=list(image.crop(strip).get_flattened_data())
                colored=sum(p[1]<180 and p[2]>p[1]+25 for p in pixels)
                if cell['column']<8:assert colored>5,(cell,strip,colored)
                else:assert colored==0,(cell,strip,colored)
            image_cell=next(c for c in cells if c['table']==0 and c['row']==2 and c['column']==2)
            assert scene['image_labels']==1 and not scene['image_errors'],scene
            assert color_count(bounds(image_cell),(220,20,20))>100
            assert color_count(bounds(image_cell),(20,220,20))==0
            neighbor=next(c for c in cells if c['table']==0 and c['row']==1 and c['column']==2)
            middle=next(c for c in cells if c['table']==0 and c['row']==1 and c['column']==1)
            assert scene['tabs'] and color_count(bounds(middle),(16,170,32))>20 and color_count(bounds(neighbor),(16,170,32))==0
            # Edit the source text, retain the grid/styles and check native undo.
            click(host,350,565);key('Home',('Control_L',));text('Edited ');wait(lambda:data()['text'].startswith('Edited '))
            key('z',('Control_L',));wait(lambda:not data()['text'].startswith('Edited '));key('y',('Control_L',));wait(lambda:data()['text'].startswith('Edited '))
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            assert len(data()['scene']['table_cells'])==16
            space='{urn:oasis:names:tc:opendocument:xmlns:text:1.0}s'
            def signature(e):return e.tag,tuple(sorted(e.attrib.items())),tuple(signature(c) for c in e if c.tag!=space)
            with zipfile.ZipFile(doc) as saved:
                (art/'tables-original.xml').write_bytes(original['content.xml']);(art/'tables-saved.xml').write_bytes(saved.read('content.xml'))
                assert signature(ET.fromstring(saved.read('content.xml')))==signature(ET.fromstring(original['content.xml']))
                for name,value in original.items():
                    if name!='content.xml':assert saved.read(name)==value,name
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            reopen=art/'tables-stage-reopen.json'
            subprocess.run([str(args.bin_dir.resolve()/'calligra-stage-fltk'),str(doc),'--report',str(reopen),'--quit-after-ms','2200'],env=env,stdout=log,stderr=log,check=True)
            restored=json.loads(reopen.read_text());assert restored['text'].startswith('Edited ') and len(restored['scene']['table_cells'])==16 and not restored['scene']['render_error']
            print('PASS: Stage native table cell/background/border pixels, merged cells, fixed-row clipping, source-preserving edit/undo/save/reopen',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
