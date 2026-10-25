#!/usr/bin/env python3
"""Real keyboard editing/save/menu checks on private Xvfb and D-Bus.
Run with uv run --no-project --with python-xlib python fltk/tests/ui_smoke.py.
"""
import argparse
import json
import os
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
parser.add_argument('--bin-dir',type=Path,default=ROOT/'build-fltk/fltk')
parser.add_argument('--artifacts',type=Path,default=ROOT/'build-fltk/ui-artifacts')
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
        subprocess.run([str(ROOT/'build-fltk/fltk/calligra-fltk-test'),'--fixtures',str(fixtures)],env=env,check=True,stdout=subprocess.DEVNULL)
        xlsx=fixtures/'import.xlsx'
        subprocess.run([str(ROOT/'build-fltk/fltk/calligra-fltk-xlsx-test'),'--fixture',str(xlsx)],env=env,check=True)
        original=xlsx.read_bytes();converted=fixtures/'import.ods';report=art/'xlsx.json'
        report.unlink(missing_ok=True)
        with (art/'xlsx.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-sheets-fltk'),str(xlsx),'--report',str(report)],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists());host=wait(lambda:window('sheets'))
            click(host,410,74);key('a',('Control_L',));text('20');key('Return');key('Right')
            wait(lambda:data()['cell']['value']=='50')
            key('s',('Control_L',))
            def chooser():
                return next((w for w in xd.screen().root.query_tree().children if str(w.get_wm_name()or'').startswith('Save imported document')),None)
            dialog=wait(chooser)
            dialog.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();time.sleep(.2)
            key('Return')
            wait(lambda:converted.exists() and data()['file']==str(converted))
            assert xlsx.read_bytes()==original
            with zipfile.ZipFile(converted) as archive:
                assert archive.read('mimetype')==b'application/vnd.oasis.opendocument.spreadsheet'
                content=ET.fromstring(archive.read('content.xml'))
                ns='{urn:oasis:names:tc:opendocument:xmlns:office:1.0}'
                assert any(e.get(ns+'value')=='20' for e in content.iter())
            key('s',('Control_L',));time.sleep(.2);assert not chooser()
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            print('PASS: XLSX import, edit, Ctrl+S conversion to ODS and unchanged source',flush=True)
        subprocess.run([str(ROOT/'build-fltk/fltk/calligra-fltk-office-test'),'--fixtures',str(fixtures)],env=env,check=True)
        for app,source_ext,dest_ext in [('words','docx','odt'),('stage','pptx','odp')]:
            source=fixtures/f'import.{source_ext}';before=source.read_bytes()
            converted=fixtures/f'import.{dest_ext}';report=art/f'{source_ext}.json'
            report.unlink(missing_ok=True)
            with (art/f'{source_ext}.log').open('w') as log:
                process=subprocess.Popen([str(args.bin_dir.resolve()/f'calligra-{app}-fltk'),str(source),'--report',str(report)],env=env,stdout=log,stderr=log)
                def data():return json.loads(report.read_text())
                wait(lambda:report.exists());host=wait(lambda:window(app))
                click(host,350,100 if app=='words' else 565);key('a',('Control_L',));text('Imported '+app)
                wait(lambda:data()['text']=='Imported '+app)
                key('s',('Control_L',));dialog=wait(chooser)
                dialog.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();time.sleep(.2);key('Return')
                wait(lambda:converted.exists() and data()['file']==str(converted))
                with zipfile.ZipFile(converted) as archive:
                    assert archive.read('mimetype')==('application/vnd.oasis.opendocument.'+{'words':'text','stage':'presentation'}[app]).encode()
                    assert ('Imported' in archive.read('content.xml').decode())
                assert source.read_bytes()==before
                key('s',('Control_L',));time.sleep(.2);assert not chooser()
                key('q',('Control_L',));assert process.wait(timeout=10)==0
                print('PASS:',source_ext.upper(),'import, editing, Ctrl+S conversion and unchanged source',flush=True)
        for app,extension in [('words','odt'),('sheets','ods'),('stage','odp')]:
            doc=fixtures/f'{app}.{extension}';report=art/f'{app}.json';report.unlink(missing_ok=True)
            with (art/f'{app}.log').open('w') as log:
                process=subprocess.Popen([str(args.bin_dir.resolve()/f'calligra-{app}-fltk'),str(doc),'--report',str(report),'--capture',str(art/f'{app}.ppm')],env=env,stdout=log,stderr=log)
                def data():return json.loads(report.read_text())
                wait(lambda:report.exists());host=wait(lambda:window(app));assert not data()['qt_widgets']
                address=data()['desktop_menu'];layout=subprocess.check_output(['gdbus','call','--session','--dest',address['service'],'--object-path',address['path'],'--method','com.canonical.dbusmenu.GetLayout','0','2','[]'],text=True);assert 'Save' in layout and 'Undo' in layout
                if app=='sheets':
                    click(host,410,74);key('a',('Control_L',));text('40');key('Return');key('Right')
                    wait(lambda:data()['cell']['value']=='70');key('z',('Control_L',));wait(lambda:data()['cell']['value']=='52');key('z',('Control_L','Shift_L'));wait(lambda:data()['cell']['value']=='70')
                    key('c',('Control_L',));key('Right');key('v',('Control_L',));wait(lambda:data()['cell']['value']=='70')
                else:
                    click(host,350,100 if app=='words' else 565);key('a',('Control_L',));text('DomeD '+app)
                    wait(lambda:data()['text']=='DomeD '+app)
                key('s',('Control_L',));wait(lambda:not data()['modified'])
                # Inspect the real saved archive, independent of the UI report.
                with zipfile.ZipFile(doc) as z:
                    xml=ET.fromstring(z.read('content.xml'))
                    assert z.read('mimetype').decode().endswith({'words':'text','sheets':'spreadsheet','stage':'presentation'}[app])
                    if app!='sheets':
                        ns='{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'
                        def odf_text(e):
                            if e.tag==ns+'s':return ' '*int(e.get(ns+'c','1'))
                            if e.tag==ns+'tab':return '\t'
                            if e.tag==ns+'line-break':return '\n'
                            return (e.text or '')+''.join(odf_text(c)+(c.tail or '') for c in e)
                        assert 'DomeD '+app in odf_text(xml)
                wait(lambda:(art/f'{app}.ppm').exists())
                if app=='stage':
                    key('m',('Control_L',));wait(lambda:data()['modified']);key('s',('Control_L',));wait(lambda:not data()['modified'])
                    key('F5');time.sleep(.4);key('Right');key('Escape')
                key('q',('Control_L',));assert process.wait(timeout=10)==0
                print('PASS:',app,'native editing, save, universal menu and shortcuts',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
