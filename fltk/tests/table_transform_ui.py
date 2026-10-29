#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native table clipping under an affine Stage frame transform."""
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
        from table_layout_producer import produce
        fixtures=produce(base/'fixtures');doc=fixtures/'tables-rotated.odp';report=art/'rotated.json';capture=art/'rotated.ppm'
        with (art/'rotated.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-stage-fltk'),str(doc),'--report',str(report),'--capture',str(capture),'--quit-after-ms','3500'],env=env,stdout=log,stderr=log)
            wait(lambda:report.exists() and capture.exists())
            data=json.loads(report.read_text());scene=data['scene'];assert not scene['render_error'] and not scene['missing_glyphs'] and len(scene['table_cells'])==16,scene
            picture=Image.open(capture).convert('RGB');picture.save(art/'rotated.jpg')
            x,y,w,h=scene['viewport'];zoom=min((w-32)/scene['width'],(h-32)/scene['height']);left=x+(w-scene['width']*zoom)/2;top=y+(h-scene['height']*zoom)/2
            def crop(cell,u1=0,v1=0,u2=1,v2=1):
                corners=cell['corners'];p=[(left+a*zoom,top+b*zoom) for a,b in corners]
                dx=(p[1][0]-p[0][0],p[1][1]-p[0][1]);dy=(p[3][0]-p[0][0],p[3][1]-p[0][1]);origin=(p[0][0]+u1*dx[0]+v1*dy[0],p[0][1]+u1*dx[1]+v1*dy[1])
                width=max(1,round(math.hypot(*dx)*(u2-u1)));height=max(1,round(math.hypot(*dy)*(v2-v1)))
                matrix=(dx[0]*(u2-u1)/width,dy[0]*(v2-v1)/height,origin[0],dx[1]*(u2-u1)/width,dy[1]*(v2-v1)/height,origin[1])
                return picture.transform((width,height),Image.Transform.AFFINE,matrix,Image.Resampling.NEAREST)
            def count(image,color,tolerance=3):return sum(all(abs(a-b)<=tolerance for a,b in zip(pixel,color)) for pixel in image.get_flattened_data())
            cells=scene['table_cells'];header=next(c for c in cells if c['table']==0 and c['row']==0 and c['column']==0)
            assert abs(header['corners'][1][1]-header['corners'][0][1])>10,header
            head=crop(header);head.save(art/'header-rectified.png');assert count(head,(200,216,255))>1000 and count(head,(32,64,170))>20
            clipped=next(c for c in cells if c['table']==0 and c['row']==2 and c['column']==1)
            height=math.dist(clipped['corners'][0],clipped['corners'][3]);below=crop(clipped,.05,1+2/height,.95,1+7/height)
            assert count(below,(204,32,64))==0
            image_cell=next(c for c in cells if c['table']==0 and c['row']==2 and c['column']==2)
            assert scene['image_labels']==1 and not scene['image_errors']
            clipped_image=crop(image_cell)
            assert count(clipped_image,(220,20,20))>100 and count(clipped_image,(20,220,20))==0
            neighbor=next(c for c in cells if c['table']==0 and c['row']==1 and c['column']==2)
            middle=next(c for c in cells if c['table']==0 and c['row']==1 and c['column']==1)
            assert scene['tabs'] and count(crop(middle),(16,170,32))>20 and count(crop(neighbor),(16,170,32))==0
            assert process.wait(timeout=10)==0
            print('PASS: affine table backgrounds/borders/glyphs and fixed-row clipping in rotated source coordinates',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
