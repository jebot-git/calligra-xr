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
        subprocess.run([str(args.test_bin_dir/'calligra-fltk-test'),'--fixtures',str(fixtures)],env=env,check=True,stdout=subprocess.DEVNULL)
        xlsx=fixtures/'import.xlsx'
        subprocess.run([str(args.test_bin_dir/'calligra-fltk-xlsx-test'),'--fixture',str(xlsx)],env=env,check=True)
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
        xlsx=fixtures/'arrays.xlsx';converted=fixtures/'arrays.ods';report=art/'xlsx-arrays.json'
        subprocess.run([str(args.test_bin_dir/'calligra-fltk-xlsx-test'),'--array-fixture',str(xlsx)],env=env,check=True,stdout=subprocess.DEVNULL)
        original=xlsx.read_bytes();report.unlink(missing_ok=True)
        with (art/'xlsx-arrays.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-sheets-fltk'),str(xlsx),'--report',str(report),'--capture',str(art/'xlsx-arrays.ppm')],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists());host=wait(lambda:window('sheets'))
            wait(lambda:(art/'xlsx-arrays.ppm').exists())
            click(host,410,74);key('a',('Control_L',));text('10');key('Return')
            for _ in range(3):key('Right')
            wait(lambda:data()['cell']['column']==4 and data()['cell']['value']=='64')
            key('s',('Control_L',));dialog=wait(chooser)
            dialog.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();time.sleep(.2);key('Return')
            wait(lambda:converted.exists() and data()['file']==str(converted))
            with zipfile.ZipFile(converted) as archive:
                content=ET.fromstring(archive.read('content.xml'))
                table_ns='{urn:oasis:names:tc:opendocument:xmlns:table:1.0}'
                office_ns='{urn:oasis:names:tc:opendocument:xmlns:office:1.0}'
                table=next(content.iter(table_ns+'table'));cells={};r=1
                for row in table.findall(table_ns+'table-row'):
                    c=1
                    for cell in row:
                        cells[(r,c)]=cell
                        c+=int(cell.get(table_ns+'number-columns-repeated','1'))
                    r+=int(row.get(table_ns+'number-rows-repeated','1'))
                for at,value in {(1,4):'64',(1,5):'76',(2,4):'43',(2,5):'50',(1,7):'233',(9,4):'32',(11,4):'10',(11,5):'3',(12,4):'2',(12,5):'4'}.items():
                    assert cells[at].get(office_ns+'value')==value,(at,cells[at].attrib)
                assert cells[(1,4)].get(table_ns+'number-matrix-columns-spanned')=='2'
                assert cells[(1,4)].get(table_ns+'number-matrix-rows-spanned')=='2'
                assert cells[(1,8)].get(office_ns+'string-value')=='Český'
                assert cells[(1,9)].get(office_ns+'string-value')=='#N/A'
                assert cells[(2,8)].get(office_ns+'boolean-value')=='true'
                assert cells[(5,1)].get(office_ns+'value')=='901' # Unsupported formula retains its source cache.
            assert xlsx.read_bytes()==original
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            print('PASS: XLSX matrix import, keyboard edit, dependent recalculation and typed ODS result caches',flush=True)
        subprocess.run([str(args.test_bin_dir/'calligra-fltk-office-test'),'--fixtures',str(fixtures)],env=env,check=True)
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
        flow=fixtures/'flow.odt';report=art/'text-layout.json';report.unlink(missing_ok=True)
        subprocess.run([str(args.test_bin_dir/'calligra-native-text-layout-test'),'--fixture',str(flow)],env=env,check=True,stdout=subprocess.DEVNULL)
        with zipfile.ZipFile(flow) as archive:original_styles=archive.read('styles.xml')
        with (art/'text-layout.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-words-fltk'),str(flow),'--page-layout','--report',str(report),'--capture',str(art/'text-layout.ppm')],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists() and data()['page_layout']['visible'] and data()['page_layout']['pages']==2)
            host=wait(lambda:window('words'));host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
            wait(lambda:(art/'text-layout.ppm').exists())
            assert not data()['page_layout']['render_error']
            assert data()['page_layout']['shaped_lines']>0 and not data()['page_layout']['missing_glyphs']
            host.configure(width=900,height=700);xd.sync();time.sleep(.2)
            key('End');wait(lambda:data()['page_layout']['page']==2)
            key('Home');wait(lambda:data()['page_layout']['page']==1)
            time.sleep(.2);cached=data()['page_layout']['rasterizations'];assert cached>0
            key('Home');time.sleep(.2);assert data()['page_layout']['rasterizations']==cached
            host.configure(width=1100,height=780);xd.sync();time.sleep(.2)
            click(host,610,26);wait(lambda:not data()['page_layout']['visible'])
            click(host,350,100);key('a',('Control_L',));text('Changed title');key('Return');text('Body text');key('Return');text('Next page')
            wait(lambda:data()['text']=='Changed title\nBody text\nNext page')
            click(host,610,26);wait(lambda:data()['page_layout']['visible'] and data()['page_layout']['pages']==2)
            assert not data()['page_layout']['render_error']
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            with zipfile.ZipFile(flow) as archive:
                assert archive.read('styles.xml')==original_styles
                content=ET.fromstring(archive.read('content.xml'))
                text_ns='{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'
                paragraphs=[e for e in content.iter() if e.tag in (text_ns+'p',text_ns+'h')]
                def odf_text(e):
                    result=e.text or ''
                    for item in e:
                        if item.tag==text_ns+'s':result+=' '*int(item.get(text_ns+'c','1'))
                        elif item.tag==text_ns+'tab':result+='\t'
                        elif item.tag==text_ns+'line-break':result+='\n'
                        else:result+=odf_text(item)
                        result+=item.tail or ''
                    return result
                assert [odf_text(e) for e in paragraphs]==['Changed title','Body text','Next page']
                assert paragraphs[2].get(text_ns+'style-name')=='NewPage'
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            print('PASS: Words native page layout, page navigation, text editing, reflow and style-preserving ODT save',flush=True)
        shaping=fixtures/'shaping.odt';report=art/'shaping.json';report.unlink(missing_ok=True)
        subprocess.run([str(args.test_bin_dir/'calligra-native-text-shaper-test'),'--fixture',str(shaping)],env=env,check=True)
        original=shaping.read_bytes()
        with (art/'shaping.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-words-fltk'),str(shaping),'--page-layout','--report',str(report),'--capture',str(art/'shaping.ppm')],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists() and data()['page_layout']['shaped_lines']==7)
            host=wait(lambda:window('words'));host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
            wait(lambda:(art/'shaping.ppm').exists())
            layout=data()['page_layout']
            assert layout['pages']==1 and layout['glyphs']>100 and not layout['missing_glyphs'] and not layout['render_error']
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            assert shaping.read_bytes()==original
            print('PASS: Words styled multilingual glyph rendering, joined scripts, bidi metadata and unchanged source',flush=True)
        scene=fixtures/'scene.odp';report=art/'scene.json';report.unlink(missing_ok=True)
        subprocess.run([str(args.test_bin_dir/'calligra-native-scene-test'),'--fixture',str(scene)],env=env,check=True)
        with zipfile.ZipFile(scene) as z:scene_styles=z.read('styles.xml')
        with (art/'scene.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-stage-fltk'),str(scene),'--report',str(report),'--capture',str(art/'scene.ppm')],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists() and data()['scene']['rendered_shapes']==9)
            assert not data()['scene']['render_error'] and data()['scene']['master_shapes']==2
            wait(lambda:(art/'scene.ppm').exists())
            subprocess.run([sys.executable,str(ROOT/'fltk/tests/scene_pixels.py'),str(art/'scene.ppm'),str(report)],check=True)
            host=wait(lambda:window('stage'))
            # Default canvas fits 400x240 points inside (194,62,894,418).
            click(host,432,429);wait(lambda:data()['scene']['selected_frame']==2)
            assert data()['text']=='Plain editor'
            click(host,770,241);wait(lambda:data()['scene']['selected_frame']==0)
            assert data()['text']=='Grouped editable'
            click(host,350,565);key('a',('Control_L',));text('Nested frame edit')
            wait(lambda:data()['text']=='Nested frame edit');key('s',('Control_L',));wait(lambda:not data()['modified'])
            with zipfile.ZipFile(scene) as z:
                assert z.read('styles.xml')==scene_styles
                ns='{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'
                def read_text(e):
                    if e.tag==ns+'s':return ' '*int(e.get(ns+'c','1'))
                    if e.tag==ns+'tab':return '\t'
                    if e.tag==ns+'line-break':return '\n'
                    return (e.text or '')+''.join(read_text(c)+(c.tail or '') for c in e)
                paragraphs=[read_text(e) for e in ET.fromstring(z.read('content.xml')).iter(ns+'p')]
                assert 'Nested frame edit' in paragraphs and 'Plain editor' in paragraphs,paragraphs
            assert not data()['scene']['render_error']
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            print('PASS: Stage master/vector rendering, transformed frame selection, grouped edit and style-preserving save',flush=True)
        for app,extension in [('words','odt'),('sheets','ods'),('stage','odp')]:
            doc=fixtures/f'{app}.{extension}';report=art/f'{app}.json';report.unlink(missing_ok=True)
            with (art/f'{app}.log').open('w') as log:
                process=subprocess.Popen([str(args.bin_dir.resolve()/f'calligra-{app}-fltk'),str(doc),'--report',str(report),'--capture',str(art/f'{app}.ppm')],env=env,stdout=log,stderr=log)
                def data():return json.loads(report.read_text())
                wait(lambda:report.exists());host=wait(lambda:window(app));assert not data()['qt_widgets']
                address=data()['desktop_menu'];layout=subprocess.check_output(['gdbus','call','--session','--dest',address['service'],'--object-path',address['path'],'--method','com.canonical.dbusmenu.GetLayout','0','2','[]'],text=True);assert 'Save' in layout and 'Undo' in layout
                if app=='words':assert 'Page layout' in layout
                if app=='sheets':
                    click(host,410,74);key('a',('Control_L',));text('40');key('Return');key('Right')
                    wait(lambda:data()['cell']['value']=='70');key('z',('Control_L',));wait(lambda:data()['cell']['value']=='52');key('z',('Control_L','Shift_L'));wait(lambda:data()['cell']['value']=='70')
                    key('c',('Control_L',));key('Right');key('v',('Control_L',));wait(lambda:data()['cell']['value']=='70')
                    click(host,410,74);key('a',('Control_L',));text('=SUMPRODUCT(A1:A2,{2;3})');key('Return')
                    wait(lambda:data()['cell']['value']=='170')
                    key('z',('Control_L',));wait(lambda:data()['cell']['value']=='70')
                    key('z',('Control_L','Shift_L'));wait(lambda:data()['cell']['value']=='170')
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
                    else:
                        table='{urn:oasis:names:tc:opendocument:xmlns:table:1.0}'
                        office='{urn:oasis:names:tc:opendocument:xmlns:office:1.0}'
                        assert any(e.get(table+'formula')=='of:=SUMPRODUCT([.A1:.A2];{2|3})' and e.get(office+'value')=='170' for e in xml.iter())
                wait(lambda:(art/f'{app}.ppm').exists())
                if app=='stage':
                    key('m',('Control_L',));wait(lambda:data()['modified']);key('s',('Control_L',));wait(lambda:not data()['modified'])
                    key('F5');time.sleep(.4);key('Right');key('Escape')
                key('q',('Control_L',));assert process.wait(timeout=10)==0
                print('PASS:',app,'native editing, save, universal menu and shortcuts',flush=True)
    finally:
        if process and process.poll() is None:process.kill();process.wait()
        xd.close();xvfb.terminate();xvfb.wait(timeout=5)
