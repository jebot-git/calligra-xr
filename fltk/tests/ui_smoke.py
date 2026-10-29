#!/usr/bin/env python3
"""Real keyboard editing/save/menu checks on private Xvfb and D-Bus.
Run with uv run --no-project --with python-xlib python fltk/tests/ui_smoke.py.
"""
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
        rich_stage=fixtures/'rich.odp';rich_words=fixtures/'rich.odt'
        subprocess.run([str(args.test_bin_dir/'calligra-native-rich-text-test'),'--fixture',str(rich_stage),str(rich_words)],env=env,check=True)
        for app,doc in [('stage',rich_stage),('words',rich_words)]:
            report=art/('rich-'+app+'.json');report.unlink(missing_ok=True)
            capture=art/('rich-'+app+'.ppm')
            with zipfile.ZipFile(doc) as z:style_bytes=z.read('styles.xml')
            with (art/('rich-'+app+'.log')).open('w') as log:
                command=[str(args.bin_dir.resolve()/('calligra-'+app+'-fltk')),str(doc),'--report',str(report),'--capture',str(capture)]
                if app=='words':command.append('--page-layout')
                process=subprocess.Popen(command,env=env,stdout=log,stderr=log)
                def data():return json.loads(report.read_text())
                wait(lambda:report.exists() and capture.exists())
                if app=='stage':
                    assert data()['scene']['shaped_lines']==18 and data()['scene']['overflow_frames']==1 and not data()['scene']['missing_glyphs'] and not data()['scene']['render_error']
                subprocess.run([sys.executable,str(ROOT/'fltk/tests/rich_text_pixels.py'),str(capture),str(report)],check=True)
                host=wait(lambda:window(app));host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
                if app=='stage':
                    click(host,350,565);key('Home',('Control_L',));key('End');text(' edited')
                    wait(lambda:'Rich native text edited' in data()['text'])
                    key('s',('Control_L',));wait(lambda:not data()['modified'])
                    with zipfile.ZipFile(doc) as z:
                        assert z.read('styles.xml')==style_bytes
                        xml=ET.fromstring(z.read('content.xml'))
                        assert any(e.attrib.get('{urn:oasis:names:tc:opendocument:xmlns:text:1.0}style-name')=='Red' for e in xml.iter())
                key('q',('Control_L',));assert process.wait(timeout=10)==0
                print('PASS:',app,'native rich-text colors, paper/frame layout and retained styles',flush=True)
        paths=fixtures/'paths.odp';report=art/'paths.json';report.unlink(missing_ok=True)
        subprocess.run([str(args.test_bin_dir/'calligra-native-path-test'),'--fixture',str(paths)],env=env,check=True)
        original=paths.read_bytes()
        with (art/'paths.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-stage-fltk'),str(paths),'--report',str(report),'--capture',str(art/'paths.ppm')],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists() and (art/'paths.ppm').exists())
            scene=data()['scene'];assert scene['rendered_shapes']==9 and not scene['render_error'] and not scene['unsupported']
            subprocess.run([sys.executable,str(ROOT/'fltk/tests/path_pixels.py'),str(art/'paths.ppm'),str(report)],check=True)
            host=wait(lambda:window('stage'));host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
            initial_points=scene['geometry_points'];initial_view=scene['viewport']
            key('F5');wait(lambda:data()['scene']['geometry_points']>initial_points)
            fullscreen_points=data()['scene']['geometry_points']
            assert not data()['scene']['render_error']
            key('Escape');wait(lambda:data()['scene']['viewport']==initial_view)
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            assert paths.read_bytes()==original
            print('PASS: Stage curve fills/holes, rounded and partial ellipses, adaptive fullscreen geometry',initial_points,'to',fullscreen_points,'points, and unchanged source',flush=True)
        enhanced=fixtures/'enhanced.odp';report=art/'enhanced.json';report.unlink(missing_ok=True)
        subprocess.run([str(args.test_bin_dir/'calligra-native-enhanced-test'),'--fixture',str(enhanced)],env=env,check=True)
        ns='{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}'
        def geometries(content):
            def signature(e):return (e.tag,sorted(e.attrib.items()),[(signature(c)) for c in e])
            return [signature(e) for e in ET.fromstring(content).iter(ns+'enhanced-geometry')]
        with zipfile.ZipFile(enhanced) as z:original_geometry=geometries(z.read('content.xml'));original_styles=z.read('styles.xml')
        with (art/'enhanced.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-stage-fltk'),str(enhanced),'--report',str(report),'--capture',str(art/'enhanced.ppm')],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists() and (art/'enhanced.ppm').exists())
            s=data()['scene'];assert s['shapes']==6 and s['editable_frames']==3 and not s['unsupported'] and not s['render_error'] and not s['missing_glyphs']
            subprocess.run([sys.executable,str(ROOT/'fltk/tests/enhanced_pixels.py'),str(art/'enhanced.ppm'),str(report)],check=True)
            host=wait(lambda:window('stage'))
            def select(px,py):
                s=data()['scene'];x,y,w,h=s['viewport'];scale=min((w-32)/s['width'],(h-32)/s['height'])
                click(host,int(x+(w-s['width']*scale)/2+px*scale),int(y+(h-s['height']*scale)/2+py*scale))
            select(335,97);wait(lambda:data()['scene']['selected_frame']==2);assert data()['text']=='Mirrored text'
            select(60,97);wait(lambda:data()['scene']['selected_frame']==1);assert data()['text']=='Editable arrow'
            click(host,350,565);key('a',('Control_L',));text('Arrow edited');wait(lambda:data()['text']=='Arrow edited')
            key('z',('Control_L',));wait(lambda:data()['text']=='Editable arrow');key('z',('Control_L','Shift_L'));wait(lambda:data()['text']=='Arrow edited')
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            with zipfile.ZipFile(enhanced) as z:assert geometries(z.read('content.xml'))==original_geometry and z.read('styles.xml')==original_styles
            # A different frame must share the document journal and saved point.
            select(335,97);wait(lambda:data()['scene']['selected_frame']==2)
            click(host,350,565);key('a',('Control_L',));text('Mirror changed')
            wait(lambda:data()['text']=='Mirror changed' and data()['modified'])
            select(60,97);wait(lambda:data()['scene']['selected_frame']==1)
            key('z',('Control_L',));wait(lambda:data()['scene']['selected_frame']==2 and data()['text']=='Mirrored text' and not data()['modified'])
            key('z',('Control_L',));wait(lambda:data()['scene']['selected_frame']==1 and data()['text']=='Editable arrow' and data()['modified'])
            key('z',('Control_L','Shift_L'));wait(lambda:data()['text']=='Arrow edited' and not data()['modified'])
            key('y',('Control_L',));wait(lambda:data()['scene']['selected_frame']==2 and data()['text']=='Mirror changed')
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            key('m',('Control_L',));wait(lambda:data()['scene']['slides']==2 and data()['scene']['page']==2)
            # Focus the text editor to exercise its explicit native undo binding.
            click(host,350,565);key('z',('Control_L',));wait(lambda:data()['scene']['slides']==1 and data()['scene']['selected_frame']==2 and not data()['modified'])
            key('y',('Control_L',));wait(lambda:data()['scene']['slides']==2 and data()['scene']['page']==2)
            address=data()['desktop_menu']
            menu_call=['gdbus','call','--session','--dest',address['service'],'--object-path',address['path']]
            layout=subprocess.check_output([*menu_call,'--method','com.canonical.dbusmenu.GetLayout','0','2','[]'],text=True)
            delete=re.search(r"\((\d+), \{[^{}]*'label': <'_?Delete slide'>",layout)
            assert delete,layout
            subprocess.run([*menu_call,'--method','com.canonical.dbusmenu.Event',delete[1],'clicked','<0>','0'],check=True,stdout=subprocess.DEVNULL)
            wait(lambda:data()['scene']['slides']==1)
            key('z',('Control_L',));wait(lambda:data()['scene']['slides']==2 and data()['scene']['page']==2)
            key('y',('Control_L',));wait(lambda:data()['scene']['slides']==1)
            # Undo deletion and insertion, then verify saved text and clean state.
            key('z',('Control_L',));wait(lambda:data()['scene']['slides']==2)
            key('z',('Control_L',));wait(lambda:data()['scene']['slides']==1 and not data()['modified'])
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            with zipfile.ZipFile(enhanced) as z:
                xml=ET.fromstring(z.read('content.xml'))
                assert geometries(z.read('content.xml'))==original_geometry and z.read('styles.xml')==original_styles
                text_ns='{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'
                def saved_text(e):
                    if e.tag==text_ns+'s':return ' '*int(e.get(text_ns+'c','1'))
                    if e.tag==text_ns+'tab':return '\t'
                    if e.tag==text_ns+'line-break':return '\n'
                    return (e.text or '')+''.join(saved_text(c)+(c.tail or '') for c in e)
                assert 'Mirror changed' in saved_text(xml) and 'Arrow edited' in saved_text(xml)
            assert not data()['scene']['render_error'];key('q',('Control_L',));assert process.wait(timeout=10)==0
            print('PASS: enhanced shapes, cross-frame/slide native undo, editor/menu bindings, save checkpoints and retained geometry/equations',flush=True)
        handles=fixtures/'handles.odp';report=art/'handles.json';report.unlink(missing_ok=True)
        subprocess.run([str(args.test_bin_dir/'calligra-native-handle-test'),'--fixture',str(handles)],env=env,check=True)
        with zipfile.ZipFile(handles) as z:
            before_handle_content=z.read('content.xml');before_handle_styles=z.read('styles.xml')
            before_opaque=z.read('unknown/retained.bin')
        with (art/'handles.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-stage-fltk'),str(handles),'--report',str(report),'--capture',str(art/'handles.ppm')],env=env,stdout=log,stderr=log)
            def data():return json.loads(report.read_text())
            wait(lambda:report.exists());host=wait(lambda:window('stage'))
            def adjustment(shape):return next(h for h in data()['scene']['adjustments'] if h['shape']==shape and h['handle']==0)
            def screen(px,py):
                s=data()['scene'];x,y,w,h=s['viewport'];scale=min((w-32)/s['width'],(h-32)/s['height'])
                return int(x+(w-s['width']*scale)/2+px*scale),int(y+(h-s['height']*scale)/2+py*scale)
            def click_scene(px,py):click(host,*screen(px,py))
            def drag(shape,target,cancel=False):
                h=adjustment(shape);start=screen(h['x'],h['y']);end=screen(*target)
                origin=xd.screen().root.translate_coords(host,0,0);host.set_input_focus(X.RevertToParent,X.CurrentTime)
                xtest.fake_input(xd,X.MotionNotify,x=origin.x+start[0],y=origin.y+start[1]);xd.sync();time.sleep(.08)
                xtest.fake_input(xd,X.ButtonPress,1);xd.sync();time.sleep(.08)
                for step in range(1,4):
                    xtest.fake_input(xd,X.MotionNotify,x=origin.x+round(start[0]+(end[0]-start[0])*step/3),y=origin.y+round(start[1]+(end[1]-start[1])*step/3));xd.sync();time.sleep(.09)
                if cancel:key('Escape')
                xtest.fake_input(xd,X.ButtonRelease,1);xd.sync();time.sleep(.15)
            assert len(data()['scene']['adjustments'])==3 and data()['scene']['selected_shape']==0
            drag(0,(195,10));wait(lambda:abs(adjustment(0)['x']-164)<1 and abs(adjustment(0)['y']-34.5)<1 and data()['modified'])
            key('z',('Control_L',));wait(lambda:abs(adjustment(0)['x']-128)<1 and not data()['modified'])
            key('y',('Control_L',));wait(lambda:abs(adjustment(0)['x']-164)<1)
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            key('z',('Control_L',));wait(lambda:abs(adjustment(0)['x']-128)<1 and data()['scene']['can_redo'])
            drag(0,(110,80),cancel=True)
            wait(lambda:abs(adjustment(0)['x']-128)<1 and data()['scene']['can_redo'])
            key('y',('Control_L',));wait(lambda:abs(adjustment(0)['x']-164)<1 and not data()['modified'])
            click_scene(295,95);wait(lambda:data()['scene']['selected_shape']==1 and data()['scene']['selected_frame']==-1)
            assert data()['text']==''
            drag(1,(300,125));wait(lambda:abs(adjustment(1)['x']-300)<2 and abs(adjustment(1)['y']-120)<2)
            key('z',('Control_L',));wait(lambda:abs(adjustment(1)['x']-270)<1 and data()['scene']['selected_shape']==1)
            key('y',('Control_L',));wait(lambda:abs(adjustment(1)['y']-120)<2)
            h=adjustment(2);click_scene(h['x'],h['y']);wait(lambda:data()['scene']['selected_shape']==2)
            import math
            target=(h['x']+10*math.cos(.3),h['y']+10*math.sin(.3))
            drag(2,target);wait(lambda:abs(adjustment(2)['x']-target[0])<2 and abs(adjustment(2)['y']-target[1])<2)
            click_scene(50,75);wait(lambda:data()['scene']['selected_frame']==0)
            click(host,350,565);key('a',('Control_L',));text('Geometry plus text');wait(lambda:data()['text']=='Geometry plus text')
            key('s',('Control_L',));wait(lambda:not data()['modified'])
            key('z',('Control_L',));wait(lambda:data()['text']=='Adjustable text')
            key('y',('Control_L',));wait(lambda:data()['text']=='Geometry plus text' and not data()['modified'])
            with zipfile.ZipFile(handles) as z:
                original=list(ET.fromstring(before_handle_content).iter(ns+'enhanced-geometry'))
                updated=list(ET.fromstring(z.read('content.xml')).iter(ns+'enhanced-geometry'))
                mods=[list(map(float,g.get(ns+'modifiers').split())) for g in updated]
                assert mods[0]==[80,5] and abs(mods[1][0]-90)<3 and abs(mods[1][1]-40)<1e-8 and abs(mods[2][0]-50)<2,mods
                for a,b in zip(original,updated):
                    a.attrib.pop(ns+'modifiers');b.attrib.pop(ns+'modifiers')
                    assert ET.tostring(a)==ET.tostring(b)
                assert z.read('styles.xml')==before_handle_styles and z.read('unknown/retained.bin')==before_opaque
            assert not data()['scene']['render_error']
            key('F5');time.sleep(.25);key('Escape');key('q',('Control_L',));assert process.wait(timeout=10)==0
            print('PASS: native XY/polar/grouped handle dragging, clamping, nontext selection, cancellation/redo, text history and retained ODP',flush=True)
        invalid_handles=fixtures/'invalid-handles.odp';report=art/'invalid-handles.json';report.unlink(missing_ok=True)
        subprocess.run([str(args.test_bin_dir/'calligra-native-handle-test'),'--invalid-fixture',str(invalid_handles)],env=env,check=True)
        original_invalid=invalid_handles.read_bytes()
        with (art/'invalid-handles.log').open('w') as log:
            process=subprocess.Popen([str(args.bin_dir.resolve()/'calligra-stage-fltk'),str(invalid_handles),'--report',str(report)],env=env,stdout=log,stderr=log)
            wait(lambda:report.exists());host=wait(lambda:window('stage'))
            drag(0,(195,48))
            wait(lambda:abs(adjustment(0)['x']-128)<1 and not data()['modified'] and not data()['scene']['can_undo'])
            assert not data()['scene']['render_error'] and invalid_handles.read_bytes()==original_invalid
            key('q',('Control_L',));assert process.wait(timeout=10)==0
            print('PASS: invalid live handle geometry rolls back the whole drag and keeps the source unchanged',flush=True)
        list_words=fixtures/'lists.odt';list_stage=fixtures/'lists.odp'
        subprocess.run([str(args.test_bin_dir/'calligra-native-list-test'),'--fixture',str(list_words),str(list_stage)],env=env,check=True)
        for app,doc in [('words',list_words),('stage',list_stage)]:
            report=art/f'lists-{app}.json';report.unlink(missing_ok=True);capture=art/f'lists-{app}.ppm'
            with zipfile.ZipFile(doc) as z:before_list=z.read('content.xml');before_styles=z.read('styles.xml');before_payload=z.read('extra/retained.bin')
            with (art/f'lists-{app}.log').open('w') as log:
                command=[str(args.bin_dir.resolve()/f'calligra-{app}-fltk'),str(doc),'--report',str(report),'--capture',str(capture)]
                if app=='words':command.append('--page-layout')
                process=subprocess.Popen(command,env=env,stdout=log,stderr=log)
                def data():return json.loads(report.read_text())
                wait(lambda:report.exists() and capture.exists());host=wait(lambda:window(app))
                from PIL import Image
                picture=Image.open(capture).convert('RGB')
                def red(pixel):r,g,b=pixel;return r>130 and r>g+50 and g<120 and b<160
                if app=='words':
                    layout=data()['page_layout'];labels=layout['list_labels']
                    assert len(labels)==15 and layout['pages']>=2 and not layout['missing_glyphs'] and not layout['render_error']
                    x,y,w,h=layout['viewport'];zoom=max(.05,min(1.5,(w-48)/max(l['page_width'] for l in labels)));checked=0
                    for label in labels:
                        if label['page']!=0 or label['text'] not in ('•','(1)','(7)'):continue
                        left=x+(w-label['page_width']*zoom)/2;top=y+12
                        bounds=(max(0,int(left+label['x']*zoom)-2),max(0,int(top+(label['baseline']-1.4*label['size'])*zoom)),min(picture.width,int(left+(label['x']+label['width'])*zoom)+3),min(picture.height,int(top+(label['baseline']+.4*label['size'])*zoom)+2))
                        count=sum(red(pixel) for pixel in picture.crop(bounds).get_flattened_data());assert count>4,(label,count,bounds);checked+=1
                    assert checked>=2
                    click(host,610,25);wait(lambda:not data()['page_layout']['visible']);click(host,350,100)
                else:
                    scene=data()['scene'];assert scene['list_labels']==6 and not scene['missing_glyphs'] and not scene['render_error'] and not scene['overflow_frames']
                    x,y,w,h=scene['viewport'];assert sum(red(pixel) for pixel in picture.crop((x,y,x+w,y+h)).get_flattened_data())>30
                    click(host,350,565)
                key('Home',('Control_L',));text('Edited ');wait(lambda:data()['text'].startswith('Edited '))
                key('s',('Control_L',));wait(lambda:not data()['modified'])
                if app=='words':
                    click(host,610,25);wait(lambda:data()['page_layout']['visible'] and len(data()['page_layout']['list_labels'])==15)
                else:
                    key('z',('Control_L',));wait(lambda:not data()['text'].startswith('Edited ') and data()['scene']['list_labels']==6)
                    key('y',('Control_L',));wait(lambda:data()['text'].startswith('Edited ') and not data()['modified'])
                whitespace={'{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'+name for name in ('s','tab','line-break')}
                def structure(e):return e.tag,tuple(sorted(e.attrib.items())),tuple(structure(c) for c in e if c.tag not in whitespace)
                with zipfile.ZipFile(doc) as z:
                    assert structure(ET.fromstring(z.read('content.xml')))==structure(ET.fromstring(before_list))
                    assert b'Edited' in z.read('content.xml') and z.read('styles.xml')==before_styles and z.read('extra/retained.bin')==before_payload
                key('q',('Control_L',));assert process.wait(timeout=10)==0
                print('PASS:',app,'native list glyphs, numbering/reflow, source offsets and XML-preserving edit/save',flush=True)
        subprocess.run([sys.executable,str(ROOT/'fltk/tests/image_lists_producer.py'),str(fixtures)],env=env,check=True)
        for app,extension in [('words','odt'),('stage','odp')]:
            doc=fixtures/f'image-lists.{extension}';report=art/f'image-lists-{app}.json';capture=art/f'image-lists-{app}.ppm'
            report.unlink(missing_ok=True);capture.unlink(missing_ok=True)
            with zipfile.ZipFile(doc) as z:original={n:z.read(n) for n in z.namelist()}
            with (art/f'image-lists-{app}.log').open('w') as log:
                command=[str(args.bin_dir.resolve()/f'calligra-{app}-fltk'),str(doc),'--report',str(report),'--capture',str(capture)]
                if app=='words':command.append('--page-layout')
                process=subprocess.Popen(command,env=env,stdout=log,stderr=log)
                def data():return json.loads(report.read_text())
                def state():return data()['page_layout' if app=='words' else 'scene']
                wait(lambda:report.exists() and capture.exists());host=wait(lambda:window(app))
                st=state();assert not st['render_error'],st
                assert len(st['image_labels'])==9 if app=='words' else st['image_labels']==9,st
                assert st['image_uploads']==6 and st['image_decodes']==7 and len(st['image_errors'])==2,st
                picture=Image.open(capture).convert('RGB')
                if app=='words':
                    x,y,w,h=st['viewport'];labels=st['image_labels'];zoom=max(.05,min(1.5,(w-48)/max(l['page_width'] for l in labels)))
                    def sample(label,fx,fy):
                        left=x+(w-label['page_width']*zoom)/2;top=y+12
                        return picture.getpixel((int(left+(label['x']+label['width']*fx)*zoom),int(top+(label['y']+label['height']*fy)*zoom)))
                    png=labels[0];assert all(abs(a-b)<4 for a,b in zip(sample(png,.2,.5),(237,137,147))),sample(png,.2,.5)
                    assert sample(png,.8,.5)==(255,255,255),sample(png,.8,.5)
                    gif=labels[3];assert sample(gif,.8,.5)==(255,255,255),sample(gif,.8,.5)
                    assert len([l for l in labels if l['available']])==8
                else:
                    x,y,w,h=st['viewport'];ink=list(picture.crop((x,y,x+w,y+h)).get_flattened_data())
                    for color in [(237,137,147),(20,180,40),(32,48,221),(240,190,20),(180,30,210),(20,170,210)]:
                        assert sum(all(abs(a-b)<5 for a,b in zip(p,color)) for p in ink)>25,(color,st)
                    # Fullscreen changes the GL context and SVG raster size; redraws
                    # in either context must reuse the uploaded textures.
                    click(host,600,25);wait(lambda:state()['image_uploads']>=12)
                    key('Escape');wait(lambda:state()['image_uploads']>=18)
                    host=wait(lambda:window(app))
                stable=(state()['image_decodes'],state()['image_uploads'])
                for _ in range(3):host.clear_area(exposures=True);xd.sync();time.sleep(.12)
                assert (state()['image_decodes'],state()['image_uploads'])==stable
                if app=='words':click(host,610,25);wait(lambda:not state()['visible']);click(host,350,100)
                else:click(host,350,565)
                key('Home',('Control_L',));text('Edited ');wait(lambda:data()['text'].startswith('Edited '))
                key('s',('Control_L',));wait(lambda:not data()['modified'])
                with zipfile.ZipFile(doc) as z:
                    assert set(z.namelist())==set(original)
                    for name,content in original.items():
                        if name!='content.xml':assert z.read(name)==content,name
                    assert structure(ET.fromstring(z.read('content.xml')))==structure(ET.fromstring(original['content.xml']))
                if app=='words':
                    click(host,610,25);wait(lambda:state()['visible'] and len(state()['image_labels'])==9)
                    assert (state()['image_decodes'],state()['image_uploads'])==stable,state()
                else:
                    for action,slides in [('m',2),('z',1),('y',2),('z',1)]:
                        key(action,('Control_L',));wait(lambda:data()['scene']['slides']==slides)
                    wait(lambda:state()['image_labels']==9)
                    assert (state()['image_decodes'],state()['image_uploads'])==stable,state()
                key('q',('Control_L',));assert process.wait(timeout=10)==0
                # Reopen saved documents with a fresh process to exercise retained
                # encoded data, inline payloads and percent-encoded package paths.
                reopen=art/f'image-lists-{app}-reopen.json'
                command=[str(args.bin_dir.resolve()/f'calligra-{app}-fltk'),str(doc),'--report',str(reopen),'--quit-after-ms','2200']
                if app=='words':command.append('--page-layout')
                subprocess.run(command,env=env,stdout=log,stderr=log,check=True)
                restored=json.loads(reopen.read_text())['page_layout' if app=='words' else 'scene']
                assert restored['image_uploads']==6 and restored['image_decodes']==7,restored
                print('PASS:',app,'PNG/JPEG/SVG/GIF/BMP and inline image lists, alpha pixels, shared GPU cache, placeholders and edit/save/reopen',flush=True)
        subprocess.run([sys.executable,str(ROOT/'fltk/tests/tabs_ui.py'),'--bin-dir',str(args.bin_dir.resolve()),'--test-bin-dir',str(args.test_bin_dir.resolve()),'--artifacts',str(art/'tabs')],env=env,check=True)
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
