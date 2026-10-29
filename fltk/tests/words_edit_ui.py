#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Words structural editing against odfpy documents on private X11 or Wayland.

uv run --no-project --with python-xlib --with Pillow --with odfpy python \
    fltk/tests/words_edit_ui.py --artifacts /absolute/path [--wayland]
"""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

from odf.opendocument import OpenDocumentText
from odf.style import (Map, MasterPage, PageLayout, PageLayoutProperties,
                       ParagraphProperties, Style, TextProperties, ListLevelProperties)
from odf.table import Table, TableCell, TableColumn, TableRow
from odf.text import (A, LineBreak, List, ListHeader, ListItem, ListLevelStyleNumber,
                      ListStyle, P, S, Section, Span, Tab)
from PIL import Image
from Xlib import X, XK, display
from Xlib.error import XError
from Xlib.ext import xtest

ROOT = Path(__file__).resolve().parents[2]
TEXT = '{urn:oasis:names:tc:opendocument:xmlns:text:1.0}'
STYLE = '{urn:oasis:names:tc:opendocument:xmlns:style:1.0}'
FO = '{urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0}'
TABLE = '{urn:oasis:names:tc:opendocument:xmlns:table:1.0}'
XML = '{http://www.w3.org/XML/1998/namespace}'
XLINK = '{http://www.w3.org/1999/xlink}'


def produce(path, conditional=False):
    doc = OpenDocumentText()
    layout = PageLayout(name='Page')
    layout.addElement(PageLayoutProperties(pagewidth='18cm', pageheight='24cm', margin='1cm'))
    doc.automaticstyles.addElement(layout)
    doc.masterstyles.addElement(MasterPage(name='Standard', pagelayoutname=layout))
    body = Style(name='Body', family='paragraph')
    body.addElement(TextProperties(fontfamily='DejaVu Sans', fontsize='12pt'))
    doc.styles.addElement(body)
    first = Style(name='First', family='paragraph', parentstylename=body, masterpagename='Standard')
    first.addElement(ParagraphProperties(breakbefore='page', marginleft='5pt'))
    if conditional:
        first.addElement(Map(condition='table()', applystylename=body))
    doc.styles.addElement(first)
    bold = Style(name='Bold', family='text')
    bold.addElement(TextProperties(fontweight='bold'))
    doc.styles.addElement(bold)
    p = P(stylename=first, text='Start ')
    p.setAttrNS(XML[1:-1], 'id', 'first')
    span = Span(stylename=bold, text='Český 日本語')
    p.addElement(span)
    p.addText(' tail')
    doc.text.addElement(p)
    section = Section(name='Section')
    section.addElement(P(stylename=body, text='Section alpha'))
    section.addElement(P(stylename=body, text='Section beta'))
    doc.text.addElement(section)
    table = Table(name='Cells')
    table.addElement(TableColumn(numbercolumnsrepeated=2))
    row = TableRow()
    table.addElement(row)
    cell = TableCell(valuetype='string')
    p = P(stylename=body, text='Cell ')
    link = A(href='https://example.invalid/document', text='link')
    p.addElement(link)
    p.addElement(S(c=3))
    p.addElement(Tab())
    p.addText('end')
    p.addElement(LineBreak())
    p.addText('soft')
    cell.addElement(p)
    row.addElement(cell)
    cell = TableCell(valuetype='string')
    cell.addElement(P(stylename=body, text='Other cell'))
    row.addElement(cell)
    doc.text.addElement(table)
    numbers = ListStyle(name='Numbers')
    number = ListLevelStyleNumber(level=1, numformat='1', numsuffix='.')
    number.addElement(ListLevelProperties(spacebefore='0pt', minlabelwidth='24pt', minlabeldistance='6pt'))
    numbers.addElement(number)
    number = ListLevelStyleNumber(level=2, numformat='a', numsuffix=')', displaylevels=2)
    number.addElement(ListLevelProperties(spacebefore='24pt', minlabelwidth='36pt', minlabeldistance='6pt'))
    numbers.addElement(number)
    doc.styles.addElement(numbers)
    roman = ListStyle(name='Roman')
    number = ListLevelStyleNumber(level=1, numformat='I', numsuffix='.')
    number.addElement(ListLevelProperties(spacebefore='0pt', minlabelwidth='24pt', minlabeldistance='6pt'))
    roman.addElement(number)
    doc.styles.addElement(roman)
    listing = List(stylename=numbers)
    listing.setAttrNS(XML[1:-1], 'id', 'numbered-list')
    header = ListHeader()
    header.addElement(P(stylename=body, text='List header'))
    listing.addElement(header)
    item = ListItem(startvalue=4)
    item.setAttrNS(XML[1:-1], 'id', 'numbered-item')
    p = P(stylename=body, text='List ')
    span = Span(stylename=bold, text='item')
    p.addElement(span)
    item.addElement(p)
    item.addElement(P(stylename=body, text='List continuation'))
    nested = List()
    child = ListItem()
    child.addElement(P(stylename=body, text='Nested item'))
    nested.addElement(child)
    item.addElement(nested)
    listing.addElement(item)
    item = ListItem(styleoverride='Roman')
    item.addElement(P(stylename=body, text='Next item'))
    listing.addElement(item)
    doc.text.addElement(listing)
    joining = List(stylename=numbers)
    joining.setAttrNS(XML[1:-1], 'id', 'joining-list')
    item = ListItem(styleoverride='Roman')
    item.addElement(P(stylename=body, text='Join one'))
    joining.addElement(item)
    item = ListItem()
    item.addElement(P(stylename=body, text='Join two'))
    item.addElement(P(stylename=body, text='Join continuation'))
    nested = List()
    child = ListItem()
    child.addElement(P(stylename=body, text='Join nested'))
    nested.addElement(child)
    item.addElement(nested)
    joining.addElement(item)
    doc.text.addElement(joining)
    doc.save(str(path))
    with zipfile.ZipFile(path, 'a') as archive:
        archive.writestr('Extras/opaque.bin', b'\0keep\xff')


def plain(node):
    if node.tag == TEXT + 's':
        return ' ' * int(node.get(TEXT + 'c', '1'))
    if node.tag == TEXT + 'tab':
        return '\t'
    if node.tag == TEXT + 'line-break':
        return '\n'
    return (node.text or '') + ''.join(plain(c) + (c.tail or '') for c in node)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bin-dir', type=Path, default=ROOT / 'build-native/fltk')
parser.add_argument('--artifacts', type=Path, required=True)
parser.add_argument('--wayland', action='store_true')
parser.add_argument('--ime', action='store_true', help='Exercise Fcitx/Anthy on the private Wayland connection')
parser.add_argument('--page-only', action='store_true', help='Run the focused native page fixture without the full editing regression')
parser.add_argument('--text-view', action='store_true', help='Run the full editing regression in the explicit plain text view')
args = parser.parse_args()
if args.ime and not args.wayland:
    parser.error('--ime requires --wayland')
if not os.environ.get('WORDS_EDIT_BUS'):
    os.execvpe('dbus-run-session', ['dbus-run-session', '--', sys.executable, *sys.argv],
               {**os.environ, 'WORDS_EDIT_BUS': '1'})
signal.pthread_sigmask(signal.SIG_SETMASK, [])
art = args.artifacts.resolve()
art.mkdir(parents=True, exist_ok=True)
processes, logs = [], []
xd = None


def start(command, env, name, **kwargs):
    log = (art / (name + '.log')).open('w')
    logs.append(log)
    process = subprocess.Popen([str(x) for x in command], env=env,
                               stdout=log, stderr=subprocess.STDOUT, **kwargs)
    processes.append(process)
    return process


def wait(fn, timeout=25):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            result = fn()
            if result:
                return result
        except (OSError, ValueError, KeyError, XError):
            pass
        time.sleep(.04)
    raise AssertionError('Timeout; inspect ' + str(art))


try:
    with tempfile.TemporaryDirectory(prefix='words-edit-') as temp:
        base = Path(temp)
        env = {**os.environ, 'NO_AT_BRIDGE': '1', 'LIBGL_ALWAYS_SOFTWARE': '1'}
        for name in ('CONFIG', 'DATA', 'CACHE', 'STATE', 'RUNTIME'):
            path = base / name.lower()
            path.mkdir(mode=0o700)
            env['XDG_' + name + ('_DIR' if name == 'RUNTIME' else '_HOME')] = str(path)
        for name in ('WAYLAND_DISPLAY', 'WAYLAND_SOCKET'):
            env.pop(name, None)
        rd, wr = os.pipe()
        start(['Xvfb', '-displayfd', wr, '-screen', '0', '1400x1000x24', '-nolisten', 'tcp'],
              env, 'xvfb', pass_fds=(wr,))
        os.close(wr)
        with os.fdopen(rd) as stream:
            env['DISPLAY'] = ':' + stream.readline().strip()
        xd = display.Display(env['DISPLAY'])
        client = {**env, 'FLTK_BACKEND': 'x11'}
        if args.wayland:
            (base / 'home').mkdir()
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                port = sock.getsockname()[1]
            env.update(HOME=str(base / 'home'), DOME_PREVIEW_PORT=str(port), DOME_RENDERER='gles',
                       DOME_START_IME='1' if args.ime else '0', DOME_LOGIN_SESSION='0', DOME_START_XR='0',
                       DOME_START_POLKIT='0', DOME_START_KDECONNECT='0', DOME_START_TRAY='0',
                       RUST_LOG='warn')
            start(['/usr/local/bin/dome-compositor', '--backend', 'nested'], env, 'compositor')

            def scene():
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/state', timeout=1) as response:
                    return json.load(response)['scene']

            wait(lambda: scene()['source'] == 'wayland')
            socket_name = wait(lambda: re.search(r'Dome Wayland socket: (\S+)',
                                                 (art / 'compositor.log').read_text()))[1]
            client = {**env, 'WAYLAND_DISPLAY': socket_name, 'FLTK_BACKEND': 'wayland'}
            client.pop('DISPLAY', None)

        def input_method():
            return subprocess.run(['/usr/bin/fcitx5-remote', '--check', '-n'], env=client,
                                  capture_output=True, text=True).stdout.strip()

        if args.ime:
            wait(lambda: 'true' in subprocess.run(['gdbus', 'call', '--session', '--dest',
                'org.freedesktop.DBus', '--object-path', '/org/freedesktop/DBus', '--method',
                'org.freedesktop.DBus.NameHasOwner', 'org.fcitx.Fcitx5'], env=client,
                capture_output=True, text=True).stdout)

        def key(name, modifiers=(), count=1):
            mods = [xd.keysym_to_keycode(XK.string_to_keysym(m)) for m in modifiers]
            code = xd.keysym_to_keycode(XK.string_to_keysym(name))
            assert code, name
            for value in mods:
                xtest.fake_input(xd, X.KeyPress, value)
                if args.ime: xd.sync(); time.sleep(.04)
            for _ in range(count):
                xtest.fake_input(xd, X.KeyPress, code)
                if args.ime: xd.sync(); time.sleep(.04)
                xtest.fake_input(xd, X.KeyRelease, code)
                xd.sync()
                time.sleep(.04 if args.ime else .003)
            for value in reversed(mods):
                xtest.fake_input(xd, X.KeyRelease, value)
                if args.ime: xd.sync(); time.sleep(.04)
            xd.sync()
            time.sleep(.15 if args.ime else .09)

        source = art / 'editing.odt'
        produce(source)
        report = art / 'report.json'
        report.unlink(missing_ok=True)
        process = start([args.bin_dir.resolve() / 'calligra-words-fltk', source,
                         '--report', report, *(['--text-view'] if args.text_view else [])], client, 'words')

        def data():
            return json.loads(report.read_text())

        wait(lambda: report.exists())
        if args.wayland:
            window = wait(lambda: next((w for w in scene()['windows']
                                       if 'calligra-words' in w.get('app_id', '').lower() and w.get('client_pid') == process.pid), None))
            host = max((w for w in xd.screen().root.query_tree().children
                        if w.get_attributes().map_state == X.IsViewable),
                       key=lambda w: w.get_geometry().width * w.get_geometry().height)
            host.set_input_focus(X.RevertToParent, X.CurrentTime)
            request = urllib.request.Request(f'http://127.0.0.1:{port}/api/action',
                data=json.dumps({'op': 'focus', 'id': window['id']}).encode(),
                headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=2) as response:
                response.read()
            wait(lambda: scene()['focused'] == window['id'])
            time.sleep(.3)
        else:
            host = wait(lambda: next((w for w in xd.screen().root.query_tree().children
                                     if 'Calligra Words' in str(w.get_wm_name())), None))
            host.set_input_focus(X.RevertToParent, X.CurrentTime)
        xd.sync()

        def click(x, y, count=1):
            point = xd.screen().root.translate_coords(host, 0, 0)
            if args.wayland:
                box = next(w for w in scene()['windows'] if w['id'] == window['id'])['desktop']
                x += int(box['x'])
                y += int(box['y'])
            xtest.fake_input(xd, X.MotionNotify, x=point.x + x, y=point.y + y)
            xd.sync()
            time.sleep(.15)
            for index in range(count):
                xtest.fake_input(xd, X.ButtonPress, 1)
                xd.sync()
                time.sleep(.05)
                xtest.fake_input(xd, X.ButtonRelease, 1)
                xd.sync()
                if index + 1 < count:
                    time.sleep(.06)
            time.sleep(.2)

        assert data()['page_layout']['visible'] == (not args.text_view)
        if args.text_view:
            click(350, 100)
        if args.ime:
            wait(lambda: input_method() in ('keyboard-us', 'anthy'))
            subprocess.run(['/usr/bin/fcitx5-remote', '-s', 'keyboard-us'], env=client, check=True)
            wait(lambda: input_method() == 'keyboard-us')

        def drag(x1, y1, x2, y2, hold=0, cancel=False):
            point = xd.screen().root.translate_coords(host, 0, 0)
            bx = by = 0
            if args.wayland:
                box = next(w for w in scene()['windows'] if w['id'] == window['id'])['desktop']
                bx, by = int(box['x']), int(box['y'])
            xtest.fake_input(xd, X.MotionNotify, x=point.x+bx+x1, y=point.y+by+y1)
            xd.sync(); time.sleep(.1)
            xtest.fake_input(xd, X.ButtonPress, 1); xd.sync(); time.sleep(.08)
            for step in range(1,7):
                xtest.fake_input(xd, X.MotionNotify,
                    x=point.x+bx+round(x1+(x2-x1)*step/6),
                    y=point.y+by+round(y1+(y2-y1)*step/6))
                xd.sync(); time.sleep(.04)
            held = None
            if hold:
                time.sleep(.15)
                before = data()
                time.sleep(hold)
                after = data()
                held = (before['page_layout']['scroll'], after['page_layout']['scroll'],
                        before['editing']['cursor'], after['editing']['cursor'])
                if cancel:
                    key('Escape')
                    wait(lambda: data()['editing']['anchor'] == data()['editing']['cursor'])
                    stopped = data()['page_layout']['scroll']
                    time.sleep(.25)
                    assert data()['page_layout']['scroll'] == stopped
            xtest.fake_input(xd, X.ButtonRelease, 1); xd.sync(); time.sleep(.2)
            return held

        def capture_window(name):
            geometry = host.get_geometry()
            snap = host.get_image(0, 0, geometry.width, geometry.height, X.ZPixmap, 0xffffffff)
            picture = Image.frombytes('RGB', (geometry.width, geometry.height), snap.data, 'raw', 'BGRX')
            picture.save(art / (name + '.png'))
            return picture

        def primary_view():
            menu_action('Text view' if args.text_view else 'Page layout')
            wait(lambda: data()['page_layout']['visible'] == (not args.text_view))

        def view_labels(name=None):
            menu_action('Page layout')
            wait(lambda: data()['page_layout']['visible'] and data()['page_layout']['glyphs'] > 0)
            labels = [label['text'] for label in data()['page_layout']['list_labels']]
            if name:
                time.sleep(.4)
                capture_window(name)
            primary_view()
            return labels

        def menu_action(label):
            address = data()['desktop_menu']
            command = ['gdbus', 'call', '--session', '--dest', address['service'], '--object-path', address['path']]
            layout = subprocess.check_output([*command, '--method', 'com.canonical.dbusmenu.GetLayout', '0', '3', '[]'], text=True)
            caption = ''.join('_?' + re.escape(c) for c in label)
            action = re.search(r"\((\d+), \{[^{}]*'label': <'" + caption + r"'>", layout)
            assert action, layout
            subprocess.run([*command, '--method', 'com.canonical.dbusmenu.Event', action[1], 'clicked', '<0>', '0'], check=True, stdout=subprocess.DEVNULL)

        def go(position):
            key('Home', ('Control_L',))
            if position:
                key('Right', count=position)
            wait(lambda: data()['editing']['cursor'] == position)

        def select_all():
            key('a', ('Control_L',))
            wait(lambda: data()['editing']['anchor'] == 0 and data()['editing']['cursor'] == len(data()['text']))

        def check_text(expected, cursor=None):
            wait(lambda: data()['text'] == expected and
                 (cursor is None or data()['editing']['cursor'] == cursor))

        with zipfile.ZipFile(source) as archive:
            original_styles = archive.read('styles.xml')

        def save(name):
            before = source.stat().st_ino
            key('s', ('Control_L',))
            # A false modified flag may belong to the prior report snapshot.
            # Native saves atomically replace the file; acknowledge that write
            # before comparing exact XML bytes or copying the saved package.
            wait(lambda: source.stat().st_ino != before and not data()['modified'])
            raw = source.read_bytes()
            (art / (name + '.odt')).write_bytes(raw)
            with zipfile.ZipFile(source) as archive:
                assert archive.read('styles.xml') == original_styles
                assert archive.read('Extras/opaque.bin') == b'\0keep\xff'
                xml = archive.read('content.xml')
            root = ET.fromstring(xml)
            ids = [e.get(XML + 'id') for e in root.iter() if e.get(XML + 'id')]
            assert len(ids) == len(set(ids)), ids
            return root, xml

        def type_text(value):
            for char in value:
                name = {'/': 'slash', ' ': 'space', '.': 'period', '#': 'numbersign', '-': 'minus', "'": 'apostrophe', '"': 'quotedbl', ',': 'comma'}.get(char, char)
                key(name, ('Shift_L',) if char.isupper() or char in '#\"' else ())

        layout_report = {}
        if args.page_only:
            key('q', ('Control_L',))
            assert process.wait(timeout=15) == 0
        else:
            original = data()['text']
            assert original == 'Start Český 日本語 tail\nSection alpha\nSection beta\nCell link   \tend\nsoft\nOther cell\nList header\nList item\nList continuation\nNested item\nNext item\nJoin one\nJoin two\nJoin continuation\nJoin nested'
            _, original_xml = save('original')

            def type_text(value):
                for char in value:
                    name = {'/': 'slash', ' ': 'space', '.': 'period', '#': 'numbersign', '-': 'minus', "'": 'apostrophe', '"': 'quotedbl', ',': 'comma'}.get(char, char)
                    key(name, ('Shift_L',) if char.isupper() or char in '#\"' else ())

            def field(x, y, value):
                click(x, y)
                key('a', ('Control_L',))
                key('BackSpace')
                type_text(value)

            def panel_origin(width, height):
                w, h = data()['window_size']
                return max(0, (w-width)//2), max(0, (h-height)//2)

            def text_properties(root):
                return {e.get(STYLE+'name'): e.find(STYLE+'text-properties')
                        for e in root.iter(STYLE+'style')}

            # Invalid fields stay in the panel and cannot consume redo or saved
            # state. Correcting a quoted fallback list must render and round-trip.
            go(2); key('x'); check_text(original[:2]+'x'+original[2:], 3)
            key('z', ('Control_L',)); check_text(original, 2)
            assert data()['editing']['can_redo'] and not data()['modified']
            go(6); key('Right', ('Shift_L',), count=5)
            key('d', ('Control_L',)); wait(lambda: data()['format_dialog'])
            x, y = panel_origin(560, 350)
            revision = data()['editing']['revision']
            for name in ("Hack'", '"Hack', ", '', "):
                field(x+250, y+65, name)
                click(x+480, y+315); time.sleep(.25)
                assert data()['format_dialog'] and not data()['modified']
                assert data()['editing']['can_redo'] and data()['editing']['revision'] == revision
            capture_window('invalid-family-dialog')
            field(x+250, y+65, "'Unavailable, Font', \"Hack\"")
            field(x+190, y+105, '0')
            click(x+480, y+315); time.sleep(.25)
            assert data()['format_dialog'] and not data()['modified']
            field(x+190, y+105, '12')
            field(x+220, y+225, '#gggggg')
            click(x+480, y+315); time.sleep(.25)
            assert data()['format_dialog'] and not data()['modified']
            field(x+220, y+225, '#102030')
            click(x+480, y+315)
            wait(lambda: not data()['format_dialog'] and data()['modified'])
            root, _ = save('format-quoted-family')
            assert any(p is not None and p.get(FO+'font-family') == "'Unavailable, Font', \"Hack\""
                       for p in text_properties(root).values())
            view_labels('quoted-family-layout')
            key('z', ('Control_L',)); assert save('format-quoted-family-undone')[1] == original_xml
            # Cancellation after a validation error retains the prior typing patch.
            go(2); key('b', ('Control_L',)); wait(lambda: data()['editing']['typing_format'])
            key('d', ('Control_L',)); wait(lambda: data()['format_dialog'])
            field(x+250, y+65, "Hack'")
            click(x+480, y+315); time.sleep(.25)
            assert data()['format_dialog'] and not data()['modified']
            key('Escape'); wait(lambda: not data()['format_dialog'])
            assert data()['editing']['typing_format'] and not data()['modified']
            select_all(); wait(lambda: not data()['editing']['typing_format'])
            go(2); key('i', ('Control_L',)); wait(lambda: data()['editing']['typing_format'])
            menu_action('Select all')
            wait(lambda: data()['editing']['cursor'] == len(original) and not data()['editing']['typing_format'])
            # Paragraph numeric errors have the same non-destructive correction path.
            go(original.index('Section alpha'))
            menu_action('Paragraph…'); wait(lambda: data()['format_dialog'])
            x, y = panel_origin(700, 470)
            field(x+575, y+155, '0')
            click(x+620, y+435); time.sleep(.25)
            assert data()['format_dialog'] and not data()['modified']
            capture_window('invalid-paragraph-dialog')
            key('Escape'); wait(lambda: not data()['format_dialog'])
            assert save('format-invalid-cancelled')[1] == original_xml

            # Formatting is a native XML command with the same range and checkpoint
            # semantics as text editing, including mixed/inherited source styles.
            go(6)
            key('Right', ('Shift_L',), count=5)
            key('b', ('Control_L',))  # Existing selected word is bold: turn it off.
            wait(lambda: data()['modified'])
            check_text(original, 11)
            root, normal_xml = save('format-normal')
            assert any(p is not None and p.get(FO+'font-weight') == '400'
                       for p in text_properties(root).values())
            key('z', ('Control_L',))
            check_text(original, 11)
            assert data()['editing']['anchor'] == 6
            assert save('format-normal-undone')[1] == original_xml
            key('y', ('Control_L',))
            assert save('format-normal-redone')[1] == normal_xml
            key('z', ('Control_L',))
            assert save('format-normal-restored')[1] == original_xml
            key('i', ('Control_L',))
            wait(lambda: data()['modified'])
            root, italic_xml = save('format-italic')
            assert any(p is not None and p.get(FO+'font-style') == 'italic'
                       for p in text_properties(root).values())
            key('z', ('Control_L',))
            assert save('format-italic-undone')[1] == original_xml

            key('d', ('Control_L',))
            wait(lambda: data()['format_dialog'])
            x, y = panel_origin(560, 350)
            field(x+250, y+65, 'Hack')
            field(x+190, y+105, '22')
            click(x+220, y+145); key('Up'); key('Return')  # Bold: Yes -> No
            click(x+220, y+185); key('Down'); key('Return')  # Italic: No -> Yes
            field(x+220, y+225, '#b02040')
            capture_window('character-dialog')
            click(x+480, y+315)
            wait(lambda: not data()['format_dialog'] and data()['modified'])
            root, font_xml = save('format-font')
            assert any(p is not None and p.get(FO+'font-size') == '22pt' and
                       p.get(FO+'font-family') == 'Hack' and p.get(FO+'color') == '#b02040' and
                       p.get(FO+'font-weight') == '400' and p.get(FO+'font-style') == 'italic'
                       for p in text_properties(root).values())
            view_labels('character-formatted-layout')
            # Cancel and unchanged Apply must not consume an undo entry or redo.
            menu_action('Font…'); wait(lambda: data()['format_dialog'])
            field(x+190, y+105, '30')
            key('Escape'); wait(lambda: not data()['format_dialog'])
            assert not data()['modified'] and save('format-font-cancelled')[1] == font_xml
            key('d', ('Control_L',)); wait(lambda: data()['format_dialog'])
            click(x+480, y+315); wait(lambda: not data()['format_dialog'])
            assert not data()['modified'] and save('format-font-unchanged')[1] == font_xml
            key('z', ('Control_L',))
            assert save('format-font-undone')[1] == original_xml
            key('y', ('Control_L',)); assert save('format-font-redone')[1] == font_xml
            key('z', ('Control_L',)); assert save('format-font-restored')[1] == original_xml

            # Alignment shortcuts and a paragraph dialog retain the current range.
            go(original.index('Section alpha')+2)
            key('e', ('Control_L',))
            wait(lambda: data()['modified'])
            root, centered_xml = save('format-center')
            assert any(e.get(FO+'text-align') == 'center' for e in root.iter(STYLE+'paragraph-properties'))
            key('z', ('Control_L',)); assert save('format-center-undone')[1] == original_xml
            menu_action('Paragraph…'); wait(lambda: data()['format_dialog'])
            x, y = panel_origin(700, 470)
            click(x+220, y+65); key('Up', count=3); key('Return')  # Start -> Center
            click(x+220, y+290); key('Down'); key('Return')  # Keep together: Yes
            click(x+600, y+290); key('Down'); key('Return')  # Keep with next: Yes
            field(x+185, y+110, '24')
            field(x+575, y+110, '12')
            field(x+185, y+155, '-6')
            field(x+575, y+155, '1.5')
            field(x+185, y+200, '9')
            field(x+575, y+200, '11')
            capture_window('paragraph-dialog')
            click(x+620, y+435)
            wait(lambda: not data()['format_dialog'] and data()['modified'])
            root, para_xml = save('format-paragraph')
            assert any(e.get(FO+'margin-left') == '24pt' and e.get(FO+'margin-right') == '12pt' and
                       e.get(FO+'text-indent') == '-6pt' and e.get(FO+'line-height') == '150%' and
                       e.get(FO+'text-align') == 'center' and e.get(FO+'keep-together') == 'always' and
                       e.get(FO+'keep-with-next') == 'always'
                       for e in root.iter(STYLE+'paragraph-properties'))
            view_labels('paragraph-formatted-layout')
            key('z', ('Control_L',)); assert save('format-paragraph-undone')[1] == original_xml
            key('y', ('Control_L',)); assert save('format-paragraph-redone')[1] == para_xml
            key('z', ('Control_L',)); assert save('format-paragraph-restored')[1] == original_xml

            # With a caret, format only future typing. Navigation ends that format.
            go(2)
            key('b', ('Control_L',))
            wait(lambda: data()['editing']['typing_format'])
            assert not data()['modified']
            key('x'); check_text(original[:2]+'x'+original[2:], 3)
            root, typed_xml = save('format-typed')
            assert any(p is not None and p.get(FO+'font-weight') == '700'
                       for p in text_properties(root).values())
            key('z', ('Control_L',)); check_text(original, 2)
            assert save('format-typed-undone')[1] == original_xml
            key('y', ('Control_L',)); check_text(original[:2]+'x'+original[2:], 3)
            assert save('format-typed-redone')[1] == typed_xml
            key('z', ('Control_L',)); check_text(original, 2)
            assert save('format-typed-restored')[1] == original_xml
            key('i', ('Control_L',)); wait(lambda: data()['editing']['typing_format'])
            key('Right'); wait(lambda: not data()['editing']['typing_format'])
            assert not data()['modified']
            # FLTK removes the selection and inserts text in separate callbacks.
            # They must form one command, including on a failed replacement.
            go(6)
            key('Right', ('Shift_L',), count=8)
            selected = {k: data()['editing'][k] for k in ('anchor', 'cursor')}
            key('x')
            check_text(original[:6] + 'x' + original[14:], 7)
            save('selection-replaced')
            key('z', ('Control_L',))
            check_text(original, selected['cursor'])
            assert data()['editing']['anchor'] == selected['anchor']
            assert not data()['editing']['can_undo'] and data()['editing']['can_redo']
            key('y', ('Control_L',))
            check_text(original[:6] + 'x' + original[14:], 7)
            assert not data()['modified']
            key('z', ('Control_L',))
            check_text(original)
            assert save('selection-replacement-undone')[1] == original_xml
            select_all()
            before = data()['editing']
            key('x')
            time.sleep(.25)
            check_text(original, before['cursor'])
            assert data()['editing']['anchor'] == before['anchor']
            assert not data()['modified'] and not data()['editing']['can_undo']
            assert data()['editing']['can_redo']
            assert save('compound-rejected-first')[1] == original_xml
            # An unavailable clipboard cannot consume a selection. Once populated,
            # every paste entry point must reject cross-structure replacement intact.
            key('v', ('Control_L',))
            time.sleep(.3)
            check_text(original, before['cursor'])
            assert data()['editing']['anchor'] == before['anchor']
            go(0)
            key('Right', ('Shift_L',), count=5)
            key('c', ('Control_L',))
            def paste(method):
                if method == 'control':
                    key('v', ('Control_L',))
                elif method == 'shift-insert':
                    key('Insert', ('Shift_L',))
                elif method == 'menu':
                    menu_action('Paste')
                else:
                    # The standard FLTK context menu has Cut, Copy, Paste rows.
                    # Keyboard navigation avoids assumptions about popup geometry.
                    point = xd.screen().root.translate_coords(host, 0, 0)
                    x, y = 95, 77
                    if args.wayland:
                        box = next(w for w in scene()['windows'] if w['id'] == window['id'])['desktop']
                        x += int(box['x'])
                        y += int(box['y'])
                    xtest.fake_input(xd, X.MotionNotify, x=point.x + x, y=point.y + y)
                    xtest.fake_input(xd, X.ButtonPress, 3)
                    xtest.fake_input(xd, X.ButtonRelease, 3)
                    xd.sync()
                    time.sleep(.25)
                    key('Down', count=3)
                    key('Return')
            for method in ('control', 'shift-insert', 'menu', 'context'):
                select_all()
                before = data()['editing']
                paste(method)
                time.sleep(.3)
                check_text(original, before['cursor'])
                assert data()['editing']['anchor'] == before['anchor']
                assert not data()['modified'] and not data()['editing']['can_undo']
                assert data()['editing']['can_redo']
                assert save('compound-paste-rejected-' + method)[1] == original_xml
            for method in ('control', 'shift-insert', 'menu', 'context'):
                go(6)
                key('Right', ('Shift_L',), count=8)
                paste(method)
                check_text(original[:6] + 'Start' + original[14:], 11)
                save('selection-pasted-' + method)
                key('z', ('Control_L',))
                check_text(original, 14)
                assert data()['editing']['anchor'] == 6 and not data()['editing']['can_undo']
                key('y', ('Control_L',))
                check_text(original[:6] + 'Start' + original[14:], 11)
                assert not data()['modified']
                key('z', ('Control_L',))
                check_text(original)
                assert save('selection-paste-undone-' + method)[1] == original_xml
            split = original.index('日本語') + 1
            go(split)
            key('Return')
            split_text = original[:split] + '\n' + original[split:]
            check_text(split_text, split + 1)
            root, split_xml = save('body-split')
            paragraphs = root.findall('.//' + TEXT + 'p')
            assert paragraphs[0].get(XML + 'id') == 'first'
            continuation = paragraphs[1].get(TEXT + 'style-name')
            style = next(e for e in root.iter(STYLE + 'style') if e.get(STYLE + 'name') == continuation)
            assert not style.get(STYLE + 'master-page-name')
            props = style.find(STYLE + 'paragraph-properties')
            assert props.get(FO + 'break-before') == 'auto' and props.get(FO + 'margin-left') == '5pt'
            key('z', ('Control_L',))
            check_text(original, split)
            assert data()['modified']
            key('y', ('Control_L',))
            check_text(split_text, split + 1)
            assert not data()['modified']
            assert save('body-redone')[1] == split_xml
            key('z', ('Control_L',))
            check_text(original, split)
            assert save('body-undone')[1] == original_xml

            split = original.index('Cell link') + 7
            go(split)
            key('Return')
            check_text(original[:split] + '\n' + original[split:], split + 1)
            root, _ = save('cell-split')
            cells = root.findall('.//' + TABLE + 'table-cell')
            assert len(cells[0].findall(TEXT + 'p')) == 2
            links = root.findall('.//' + TEXT + 'a')
            assert len(links) == 2 and all(e.get(XLINK + 'href') == 'https://example.invalid/document' for e in links)
            key('BackSpace')
            check_text(original, split)
            root, _ = save('cell-joined')
            assert len(root.findall('.//' + TABLE + 'table-cell')[0].findall(TEXT + 'p')) == 1

            soft = original.index('end') + 1
            go(soft)
            key('Return', ('Shift_L',))
            soft_text = original[:soft] + '\n' + original[soft:]
            check_text(soft_text, soft + 1)
            root, _ = save('soft-break')
            assert len(root.findall('.//' + TEXT + 'line-break')) == 2
            assert len(root.findall('.//' + TABLE + 'table-cell')[0].findall(TEXT + 'p')) == 1
            key('z', ('Control_L',))
            check_text(original, soft)
            key('y', ('Control_L',))
            check_text(soft_text, soft + 1)
            assert not data()['modified']
            key('z', ('Control_L',))
            check_text(original)
            save('soft-undone')

            split = original.index('Section alpha') + 7
            go(split)
            key('Return')
            check_text(original[:split] + '\n' + original[split:])
            root, _ = save('section-split')
            assert len(root.find('.//' + TEXT + 'section').findall(TEXT + 'p')) == 3
            key('BackSpace')
            check_text(original)
            _, checkpoint_xml = save('section-joined')

            boundary = original.index('Other cell')
            go(boundary)
            key('BackSpace')
            check_text(original, boundary)
            assert not data()['modified']
            key('x')
            check_text(original[:boundary] + 'x' + original[boundary:], boundary + 1)
            key('z', ('Control_L',))
            check_text(original, boundary)
            assert not data()['modified']
            # An apparently adjacent newline can cross a nested-list boundary.
            nested = original.index('Nested item')
            go(nested)
            key('BackSpace')
            check_text(original, nested)
            assert not data()['modified']
            listing = original.index('List item') + 4
            go(listing)
            key('Return')
            listed = original[:listing] + '\n' + original[listing:]
            check_text(listed, listing + 1)
            root, listed_xml = save('list-split')
            main_list = root.find('.//' + TEXT + 'list')
            items = main_list.findall(TEXT + 'list-item')
            assert len(items) == 3 and len(root.findall('.//' + TEXT + 'list-item')) == 7
            assert items[0].get(XML + 'id') == 'numbered-item' and items[0].get(TEXT + 'start-value') == '4'
            assert items[1].get(XML + 'id') is None and items[1].get(TEXT + 'start-value') is None
            assert len(items[1].findall(TEXT + 'p')) == 2 and items[1].find(TEXT + 'list') is not None
            menu_action('Page layout')
            wait(lambda: data()['page_layout']['visible'] and data()['page_layout']['glyphs'] > 0)
            assert [label['text'] for label in data()['page_layout']['list_labels']] == ['4.', '5.', '5.a)', 'VI.', 'I.', '2.', '2.a)']
            time.sleep(.4)
            capture_window('list-split-layout')
            primary_view()
            go(listing + 1)
            key('BackSpace')
            check_text(original, listing)
            save('list-joined')
            key('z', ('Control_L',))
            check_text(listed)
            assert save('list-join-undone')[1] == listed_xml
            key('z', ('Control_L',))
            check_text(original)
            assert save('list-split-undone')[1] == checkpoint_xml
            heading = original.index('List header') + 4
            go(heading)
            key('Return')
            check_text(original[:heading] + '\n' + original[heading:], heading + 1)
            root, _ = save('list-header-split')
            main_list = root.find('.//' + TEXT + 'list')
            assert plain(main_list.find(TEXT + 'list-header').find(TEXT + 'p')) == 'List'
            assert plain(main_list.findall(TEXT + 'list-item')[0].find(TEXT + 'p')) == ' header'
            key('z', ('Control_L',))
            check_text(original)
            assert save('list-header-undone')[1] == checkpoint_xml
            joining = original.index('Join two')
            go(joining)
            key('BackSpace')
            joined = original[:joining - 1] + original[joining:]
            check_text(joined, joining - 1)
            root, _ = save('override-items-joined')
            joined_list = next(e for e in root.iter(TEXT + 'list') if e.get(XML + 'id') == 'joining-list')
            items = joined_list.findall(TEXT + 'list-item')
            assert len(items) == 1 and items[0].get(TEXT + 'style-override') == 'Roman'
            assert items[0].find(TEXT + 'list').get(TEXT + 'style-name') == 'Numbers'
            assert len(items[0].findall(TEXT + 'p')) == 2
            menu_action('Page layout')
            wait(lambda: data()['page_layout']['visible'] and data()['page_layout']['glyphs'] > 0)
            assert [label['text'] for label in data()['page_layout']['list_labels']][-2:] == ['I.', 'I.a)']
            time.sleep(.4)
            capture_window('override-join-layout')
            primary_view()
            key('z', ('Control_L',))
            check_text(original, joining)
            assert save('override-join-undone')[1] == checkpoint_xml
            # Level changes alter XML and numbering while leaving editor text intact.
            next_item = original.index('Next item')
            go(next_item)
            revision = data()['editing']['revision']
            key('Tab', ('Shift_L',))
            assert data()['editing']['revision'] == revision and not data()['modified']
            key('Tab')
            wait(lambda: data()['editing']['revision'] > revision and data()['modified'])
            check_text(original, next_item)
            root, indented_xml = save('level-indented')
            main_list = root.find('.//' + TEXT + 'list')
            assert len(main_list.findall(TEXT + 'list-item')) == 1
            assert len(main_list.findall(TEXT + 'list-item')[0].find(TEXT + 'list').findall(TEXT + 'list-item')) == 2
            assert view_labels('level-indented-layout')[:3] == ['4.', '4.a)', 'II.']
            labels = data()['page_layout']['list_labels']
            assert labels[2]['x'] > labels[0]['x'] + 10
            go(next_item)
            key('Tab', ('Shift_L',))
            wait(lambda: data()['modified'])
            check_text(original, next_item)
            save('level-outdented')
            assert view_labels('level-outdented-layout')[:3] == ['4.', '4.a)', 'V.']
            labels = data()['page_layout']['list_labels']
            assert abs(labels[2]['x'] - labels[0]['x']) < .1
            key('z', ('Control_L',))
            check_text(original)
            assert save('outdent-undone')[1] == indented_xml
            key('z', ('Control_L',))
            assert save('indent-undone')[1] == checkpoint_xml
            key('y', ('Control_L',))
            assert save('indent-redone')[1] == indented_xml
            key('z', ('Control_L',))
            assert save('indent-redo-undone')[1] == checkpoint_xml
            # Universal menu formatting also works with a caret inside an item.
            go(next_item + 2)
            revision = data()['editing']['revision']
            menu_action('Indent')
            wait(lambda: data()['editing']['revision'] > revision)
            check_text(original, next_item + 2)
            assert save('menu-indented')[1] == indented_xml
            menu_action('Outdent')
            wait(lambda: data()['modified'])
            assert view_labels()[:3] == ['4.', '4.a)', 'V.']
            key('z', ('Control_L',))
            key('z', ('Control_L',))
            assert save('menu-levels-undone')[1] == checkpoint_xml
            go(next_item)
            selection_end = next_item + len('Next item') + 1
            key('Right', ('Shift_L',), count=selection_end - next_item)
            wait(lambda: data()['editing']['anchor'] == next_item and data()['editing']['cursor'] == selection_end)
            key('Tab')
            wait(lambda: data()['modified'])
            check_text(original, selection_end)
            assert data()['editing']['anchor'] == next_item
            assert save('selected-item-indented')[1] == indented_xml
            key('z', ('Control_L',))
            check_text(original, selection_end)
            assert data()['editing']['anchor'] == next_item
            assert save('selected-indent-undone')[1] == checkpoint_xml
            # Outdenting the first nested item preserves the following items in a
            # continued nested tail, including a different item style override.
            go(next_item)
            key('Tab')
            wait(lambda: data()['modified'])
            nested_item = original.index('Nested item')
            go(nested_item)
            key('Tab', ('Shift_L',))
            check_text(original, nested_item)
            root, _ = save('outdent-nested-prefix')
            main_list = root.find('.//' + TEXT + 'list')
            items = main_list.findall(TEXT + 'list-item')
            assert len(items) == 2 and plain(items[1].find(TEXT + 'p')) == 'Nested item'
            assert items[1].find(TEXT + 'list').get(TEXT + 'continue-list')
            assert view_labels('nested-tail-layout')[:3] == ['4.', '5.', 'I.']
            key('z', ('Control_L',))
            key('z', ('Control_L',))
            assert save('nested-outdent-undone')[1] == checkpoint_xml
            # Enter twice leaves an empty item instead of creating more numbered
            # blanks. The second command changes structure, with identical text.
            for prefix, end in [('root', next_item + len('Next item')),
                                ('nested', nested_item + len('Nested item'))]:
                go(end)
                key('Return')
                with_blank = original[:end] + '\n' + original[end:]
                check_text(with_blank, end + 1)
                root, created_xml = save(prefix + '-empty-item-created')
                revision = data()['editing']['revision']
                key('Return')
                wait(lambda: data()['editing']['revision'] > revision and data()['modified'])
                check_text(with_blank, end + 1)
                root, exited_xml = save(prefix + '-empty-item-exited')
                blanks = [p for p in root.iter(TEXT + 'p') if not plain(p)]
                assert len(blanks) == 1
                assert all(blanks[0] not in set(listing.iter()) for listing in root.iter(TEXT + 'list'))
                assert view_labels(prefix + '-empty-exit-layout')[:3] == ['4.', '4.a)', 'V.']
                key('z', ('Control_L',))
                check_text(with_blank)
                assert save(prefix + '-empty-exit-undone')[1] == created_xml
                key('y', ('Control_L',))
                assert save(prefix + '-empty-exit-redone')[1] == exited_xml
                key('z', ('Control_L',))
                key('z', ('Control_L',))
                check_text(original)
                assert save(prefix + '-empty-creation-undone')[1] == checkpoint_xml
            # Ordinary tabs still insert text, including after an inline soft break.
            go(1)
            key('Tab')
            check_text(original[:1] + '\t' + original[1:], 2)
            key('z', ('Control_L',))
            check_text(original)
            assert save('ordinary-tab-undone')[1] == checkpoint_xml
            soft = next_item + 3
            go(soft)
            key('Return', ('Shift_L',))
            key('Tab')
            check_text(original[:soft] + '\n\t' + original[soft:], soft + 2)
            root, _ = save('tab-after-soft-break')
            assert len(root.find('.//' + TEXT + 'list').findall(TEXT + 'list-item')) == 2
            key('z', ('Control_L',))
            key('z', ('Control_L',))
            check_text(original)
            assert save('soft-tab-undone')[1] == checkpoint_xml
            go(0)
            key('Right', ('Shift_L',), count=3)
            wait(lambda: data()['editing']['anchor'] == 0 and data()['editing']['cursor'] == 3)
            key('BackSpace')
            check_text(original[3:], 0)
            key('z', ('Control_L',))
            check_text(original, 3)
            assert data()['editing']['anchor'] == 0 and not data()['modified']
            key('y', ('Control_L',))
            check_text(original[3:], 0)
            key('z', ('Control_L',))
            check_text(original)
            assert save('final')[1] == checkpoint_xml
            go(original.index('Next item'))

            menu_action('Page layout')
            wait(lambda: data()['page_layout']['visible'] and data()['page_layout']['glyphs'] > 0)
            layout_report = data()['page_layout']
            assert not layout_report['render_error'] and layout_report['table_cells']
            assert layout_report['pages'] == 1, layout_report
            assert [label['text'] for label in layout_report['list_labels']] == ['4.', '4.a)', 'V.', 'I.', '2.', '2.a)']
            time.sleep(.4)
            picture = capture_window('layout')
            assert sum(min(pixel) > 235 for pixel in picture.get_flattened_data()) > 10000
            maps = Path(f'/proc/{process.pid}/maps').read_text()
            assert 'libQt' not in maps
            assert '/usr/lib64/libfltk' in maps or '/lib64/libfltk' in maps
            key('Tab'); wait(lambda: data()['modified'])
            assert data()['page_layout']['visible'] and data()['text'] == original
            capture_window('page-list-indented')
            key('Tab', ('Shift_L',))
            assert data()['page_layout']['visible'] and data()['text'] == original
            key('z', ('Control_L',)); key('z', ('Control_L',))
            wait(lambda: not data()['modified'])
            assert [label['text'] for label in data()['page_layout']['list_labels']] == [label['text'] for label in layout_report['list_labels']]
            key('q', ('Control_L',))
            assert process.wait(timeout=15) == 0
            # Reopen the independently inspected saved package through the real UI.
            reopened = art / 'reopened.json'
            run = start([args.bin_dir.resolve() / 'calligra-words-fltk', source,
                         '--report', reopened, '--page-layout', '--quit-after-ms', '1800'], client, 'reopened')
            assert run.wait(timeout=15) == 0
            restored = json.loads(reopened.read_text())
            assert restored['text'] == original and not restored['modified']
            assert not restored['editing']['can_undo'] and restored['page_layout']['pages'] == 1
            # A conditional first-page style rejects paragraph continuation. Here
            # selection deletion succeeds first, so the later failed Enter callback
            # must restore that deletion as well as the pre-existing redo branch.
            source = art / 'conditional.odt'
            produce(source, conditional=True)
            report = art / 'conditional-report.json'
            process = start([args.bin_dir.resolve() / 'calligra-words-fltk', source,
                             '--report', report, *(['--text-view'] if args.text_view else [])], client, 'conditional')
            wait(lambda: report.exists())
            if args.wayland:
                host.set_input_focus(X.RevertToParent, X.CurrentTime)
                xd.sync()
                wait(lambda: xd.get_input_focus().focus == host)
                window = wait(lambda: next((w for w in scene()['windows']
                                           if 'calligra-words' in w.get('app_id', '').lower() and w.get('client_pid') == process.pid), None))
                request = urllib.request.Request(f'http://127.0.0.1:{port}/api/action',
                    data=json.dumps({'op': 'focus', 'id': window['id']}).encode(),
                    headers={'Content-Type': 'application/json'})
                with urllib.request.urlopen(request, timeout=2) as response:
                    response.read()
                wait(lambda: scene()['focused'] == window['id'])
            else:
                host = wait(lambda: next((w for w in xd.screen().root.query_tree().children
                                         if 'Calligra Words' in str(w.get_wm_name())), None))
                host.set_input_focus(X.RevertToParent, X.CurrentTime)
            xd.sync()
            click(350, 100)
            with zipfile.ZipFile(source) as archive:
                original_styles = archive.read('styles.xml')
            _, conditional_xml = save('conditional-original')
            go(2)
            key('x')
            check_text(original[:2] + 'x' + original[2:], 3)
            key('z', ('Control_L',))
            check_text(original, 2)
            assert data()['editing']['can_redo'] and not data()['modified']
            go(6)
            key('Right', ('Shift_L',), count=8)
            key('Return')
            time.sleep(.25)
            check_text(original, 14)
            assert data()['editing']['anchor'] == 6 and not data()['modified']
            assert not data()['editing']['can_undo'] and data()['editing']['can_redo']
            assert save('compound-rejected-late')[1] == conditional_xml
            key('y', ('Control_L',))
            check_text(original[:2] + 'x' + original[2:], 3)
            key('z', ('Control_L',))
            check_text(original, 2)
            assert not data()['modified']
            key('q', ('Control_L',))
            assert process.wait(timeout=15) == 0
        # Native startup keys and page clicks preserve scalar source positions
        # through the UTF-8 buffer and native XML editing transactions.
        source = art / 'page-hit.odt'
        doc = OpenDocumentText()
        page = PageLayout(name='HitPage')
        page.addElement(PageLayoutProperties(pagewidth='400pt', pageheight='400pt', margin='20pt'))
        doc.automaticstyles.addElement(page)
        doc.masterstyles.addElement(MasterPage(name='Standard', pagelayoutname=page))
        body = Style(name='HitBody', family='paragraph')
        body.addElement(TextProperties(fontfamily='Hack', fontsize='20pt'))
        doc.styles.addElement(body)
        first_line = 'Český á office'
        page_lines = [first_line, 'אבג 123', *['scroll line ' + str(i) for i in range(50)]]
        rtl_body = Style(name='HitRtl', family='paragraph')
        rtl_body.addElement(ParagraphProperties(writingmode='rl-tb', textalign='start'))
        rtl_body.addElement(TextProperties(fontfamily='Noto Sans Hebrew', fontsize='20pt'))
        doc.styles.addElement(rtl_body)
        indented = Style(name='HitIndented', family='paragraph', parentstylename=body)
        indented.addElement(ParagraphProperties(marginleft='120pt'))
        doc.styles.addElement(indented)
        for i, line in enumerate(page_lines):
            doc.text.addElement(P(stylename=rtl_body if i == 1 else indented if i == 3 else body, text=line))
        page_text = '\n'.join(page_lines)
        doc.save(str(source))
        with zipfile.ZipFile(source, 'a') as archive:
            archive.writestr('Extras/opaque.bin', b'\0keep\xff')
            original_styles = archive.read('styles.xml')
        report = art / 'page-hit-report.json'
        selection_capture = art / 'page-alpha.ppm'
        process = start([args.bin_dir.resolve() / 'calligra-words-fltk', source,
                         '--report', report, '--capture', selection_capture,
                         '--capture-delay-ms', '5000'], client, 'page-hit')
        wait(lambda: report.exists() and data()['page_layout']['glyphs'] > 0)
        if args.wayland:
            host.set_input_focus(X.RevertToParent, X.CurrentTime)
            xd.sync()
            wait(lambda: xd.get_input_focus().focus == host)
            window = wait(lambda: next((w for w in scene()['windows']
                                       if 'calligra-words' in w.get('app_id', '').lower() and w.get('client_pid') == process.pid), None))
            request = urllib.request.Request(f'http://127.0.0.1:{port}/api/action',
                data=json.dumps({'op': 'focus', 'id': window['id']}).encode(),
                headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=2) as response:
                response.read()
            wait(lambda: scene()['focused'] == window['id'])
        else:
            host = wait(lambda: next((w for w in xd.screen().root.query_tree().children
                                     if 'Calligra Words' in str(w.get_wm_name())), None))
            host.set_input_focus(X.RevertToParent, X.CurrentTime)
        xd.sync()
        time.sleep(.4)  # Wait for the compositor's initial size configure/report.
        x, y, w, h = data()['page_layout']['viewport']
        zoom = max(.05, min(1.5, (w - 48) / 400))
        left = x + (w - 400 * zoom) / 2
        # Hack is monospaced, with about 12pt advance at 20pt. At offset 3,
        # the leading Č has already made the UTF-8 byte offset differ.
        px, py = round(left + (20 + 3 * 12.04) * zoom), round(y + 12 + 32 * zoom)
        # Native pages and the caret are ready before any document click.
        assert data()['page_layout']['visible'] and data()['page_layout']['editing']
        check_text(page_text, 0)
        key('space'); check_text(' '+page_text, 1)
        key('z', ('Control_L',)); check_text(page_text, 0)
        assert not data()['modified']
        key('Right', count=3); check_text(page_text, 3)
        key('Left'); check_text(page_text, 2)
        key('Right'); check_text(page_text, 3)
        click(px, py)
        wait(lambda: data()['page_layout']['editing'])
        assert data()['page_layout']['visible']
        check_text(page_text, 3)
        assert data()['editing']['anchor'] == 3 and not data()['modified']
        key('a', ('Control_L',))
        wait(lambda: data()['editing']['anchor'] == 0 and data()['editing']['cursor'] == len(page_text))
        alpha_file = Path(str(selection_capture)+'.json')
        wait(lambda: alpha_file.exists())
        compositing = json.loads(alpha_file.read_text())
        assert compositing['page_selection'] == [0, len(page_text)], compositing
        assert compositing['page_min_alpha'] == 255, compositing
        Image.open(selection_capture).save(art / 'page-alpha.png')
        key('Home', ('Control_L',)); click(px, py)
        check_text(page_text, 3)
        key('x')
        check_text(page_text[:3] + 'x' + page_text[3:], 4)
        key('z', ('Control_L',))
        check_text(page_text, 3)
        assert not data()['modified']
        key('y', ('Control_L',))
        check_text(page_text[:3] + 'x' + page_text[3:], 4)
        save('page-hit-saved')
        expected = page_text[:3] + 'x' + page_text[3:]
        first_length = len(expected.split('\n')[0])
        # Visual RTL movement stays on the native line and source positions.
        key('Home', ('Control_L',)); key('Down', ('Control_L',))
        check_text(expected, first_length+1)
        key('Up', ('Control_L',)); check_text(expected, 0)
        click(round(left + 380*zoom), round(y+12+65*zoom))
        check_text(expected, first_length+1)
        key('Left'); check_text(expected, first_length+2)
        key('Right'); check_text(expected, first_length+1)
        # Vertical movement must visit the adjacent indented line, then
        # restore the original horizontal target on the following plain line.
        key('Down', ('Control_L',))
        line_starts = [0]
        for line in expected.split('\n'):
            line_starts.append(line_starts[-1]+len(line)+1)
        check_text(expected, line_starts[2])
        key('Down'); check_text(expected, line_starts[3])
        key('Down'); check_text(expected, line_starts[4])
        key('Up'); check_text(expected, line_starts[3])
        key('Up'); check_text(expected, line_starts[2])
        # Ctrl+End reveals the caret on a distant page. Repagination while
        # typing/undoing there must preserve the viewport rather than jump up.
        key('End', ('Control_L',)); check_text(expected, len(expected))
        menu_action('Text view'); wait(lambda: not data()['page_layout']['visible'])
        check_text(expected, len(expected))
        menu_action('Page layout'); wait(lambda: data()['page_layout']['visible'])
        check_text(expected, len(expected))
        menu_action('Page layout'); assert data()['page_layout']['visible']
        wait(lambda: data()['page_layout']['caret_page'] > 1)
        old_scroll = data()['page_layout']['scroll']
        assert old_scroll > 0
        key('x'); check_text(expected+'x', len(expected)+1)
        assert abs(data()['page_layout']['scroll']-old_scroll) < 100
        key('z', ('Control_L',)); check_text(expected, len(expected))
        assert abs(data()['page_layout']['scroll']-old_scroll) < 100
        assert not data()['modified']
        key('Home', ('Control_L',)); check_text(expected, 0)
        # Move over the complete combining sequence, then erase it as one
        # transaction; Undo restores both scalars and the caret.
        key('Right', ('Control_L',)); check_text(expected, 7)
        key('Right'); check_text(expected, 9)
        key('BackSpace'); check_text(expected[:7]+expected[9:], 7)
        key('z', ('Control_L',)); check_text(expected, 9)
        assert not data()['modified']
        # Real pointer dragging must produce the same scalar range as keys.
        key('Home', ('Control_L',))
        drag(round(left+(20+2*12.04)*zoom), py, round(left+(20+5*12.04)*zoom), py)
        wait(lambda: data()['editing']['anchor'] == 2 and data()['editing']['cursor'] == 5)
        capture_window('page-drag-selection')
        # Hold at the lower edge after the last motion. Repeated timer
        # scrolling must extend selection even while the pointer is stationary.
        key('Home', ('Control_L',))
        held = drag(px, py, px, y+h-3, hold=.8)
        assert held[1] > held[0]+100 and held[3] > held[2], held
        assert data()['editing']['anchor'] == 3 and not data()['modified']
        capture_window('page-stationary-drag')
        released_scroll = data()['page_layout']['scroll']
        time.sleep(.25)
        assert data()['page_layout']['scroll'] == released_scroll
        key('Escape'); key('Home', ('Control_L',))
        # Page movement uses the viewport distance before the horizontal
        # target; upward edge scrolling also stops immediately on Escape.
        key('Home', ('Control_L',))
        view = data()['page_layout']
        virtual_y = view['caret_rect'][1]+view['scroll']
        key('Page_Down')
        wait(lambda: data()['editing']['cursor'] > first_length)
        view = data()['page_layout']
        assert view['caret_rect'][1]+view['scroll'] > virtual_y+h*.5
        key('Page_Up')
        wait(lambda: data()['page_layout']['caret_page'] == 1)
        key('End', ('Control_L',)); check_text(expected, len(expected))
        caret = data()['page_layout']['caret_rect']
        held = drag(round(caret[0]), round(caret[1]+caret[3]/2), px, y+3,
                    hold=.8, cancel=True)
        assert held[1] < held[0]-100 and held[3] < held[2], held
        assert not data()['modified'] and process.poll() is None
        key('Home', ('Control_L',))
        # Native range painting and formatting retain the same selection.
        key('Home', ('Control_L',)); key('Right', count=2)
        before_selection = capture_window('page-selection-before')
        key('Right', ('Shift_L',), count=3)
        wait(lambda: data()['editing']['anchor'] == 2 and data()['editing']['cursor'] == 5)
        time.sleep(.2)
        painted_selection = capture_window('page-selection')
        assert sum(a != b for a,b in zip(before_selection.get_flattened_data(), painted_selection.get_flattened_data())) > 100
        key('b', ('Control_L',)); wait(lambda: data()['modified'])
        assert data()['page_layout']['visible'] and data()['editing']['anchor'] == 2 and data()['editing']['cursor'] == 5
        key('z', ('Control_L',)); check_text(expected, 5)
        wait(lambda: data()['editing']['anchor'] == 2 and not data()['modified'])
        key('c', ('Control_L',)); key('End', ('Control_L',))
        menu_action('Paste')
        check_text(expected+expected[2:5], len(expected)+3)
        key('z', ('Control_L',)); check_text(expected, len(expected))
        assert not data()['modified']
        # A selected whole document is replaced and restored atomically.
        key('a', ('Control_L',)); key('x')
        check_text('x', 1)
        key('z', ('Control_L',)); check_text(expected, len(expected))
        assert data()['editing']['anchor'] == 0 and not data()['modified']
        key('Home', ('Control_L',)); key('End')
        check_text(expected, first_length)
        key('Return')
        check_text(expected[:first_length]+'\n'+expected[first_length:], first_length+1)
        key('BackSpace'); check_text(expected, first_length)
        key('z', ('Control_L',)); check_text(expected[:first_length]+'\n'+expected[first_length:], first_length+1)
        key('z', ('Control_L',)); check_text(expected, first_length)
        assert not data()['modified']
        key('Return', ('Shift_L',))
        check_text(expected[:first_length]+'\n'+expected[first_length:], first_length+1)
        root, _ = save('page-soft-break')
        assert len(list(root.iter(TEXT+'line-break'))) == 1
        key('z', ('Control_L',)); check_text(expected, first_length)
        save('page-soft-break-undone')
        # Single, double and triple clicks share native source selection.
        key('Home', ('Control_L',))
        wait(lambda: data()['page_layout']['scroll'] == 0)
        click(px, py, count=2)
        wait(lambda: data()['editing']['anchor'] == 0 and data()['editing']['cursor'] == 6)
        click(px, py, count=3)
        wait(lambda: data()['editing']['anchor'] == 0 and data()['editing']['cursor'] == first_length+1)
        key('Home', ('Control_L',))
        key('Escape'); check_text(expected, 0)
        assert process.poll() is None and data()['page_layout']['visible']
        save('page-edited-final')
        maps = Path(f'/proc/{process.pid}/maps').read_text()
        assert 'libQt' not in maps
        assert '/usr/lib64/libfltk' in maps or '/lib64/libfltk' in maps
        if args.ime:
            with zipfile.ZipFile(source) as archive:
                before_preedit_xml = archive.read('content.xml')
            history = (data()['editing']['can_undo'], data()['editing']['can_redo'])
            subprocess.run(['/usr/bin/fcitx5-remote', '-s', 'anthy'], env=client, check=True)
            wait(lambda: input_method() == 'anthy')
            def compose_japanese():
                assert xd.get_input_focus().focus == host and scene()['focused'] == window['id']
                wait(lambda: input_method() == 'anthy')
                for character, prefix in zip('nihongo', ['n','に','にh','にほ','にほn','にほんg','にほんご']):
                    key(character)
                    wait(lambda: data()['page_layout']['preedit'] and data()['text'] == prefix+expected)
            compose_japanese()
            capture_window('page-preedit')
            # Cancel at the input method. The draft never enters the journal.
            key('Escape'); key('Escape')
            assert process.poll() is None
            wait(lambda: not data()['page_layout']['preedit'])
            check_text(expected, 0)
            assert not data()['modified']
            assert (data()['editing']['can_undo'], data()['editing']['can_redo']) == history
            assert save('page-preedit-cancelled')[1] == before_preedit_xml
            compose_japanese()
            key('space'); key('space')
            for attempt in range(6):
                if data()['text'].startswith('日本語'): break
                key('space')
            wait(lambda: data()['text'].startswith('日本語'))
            popup = wait(lambda: next(iter(scene().get('popups', [])), None))
            caret = data()['page_layout']['caret_rect']
            assert abs(popup['anchor']['x']-caret[0]) < 120, (popup,caret)
            assert abs(popup['anchor']['y']-(caret[1]+caret[3])) < 120, (popup,caret)
            capture_window('page-ime-candidates')
            key('Return')
            wait(lambda: not data()['page_layout']['preedit'])
            check_text('日本語'+expected, 3)
            key('z', ('Control_L',)); check_text(expected, 0)
            assert not data()['modified']
            key('y', ('Control_L',)); check_text('日本語'+expected, 3)
            save('page-ime-committed')
            subprocess.run(['/usr/bin/fcitx5-remote', '-s', 'keyboard-us'], env=client, check=True)
            wait(lambda: input_method() == 'keyboard-us')
        last_saved_text = data()['text']
        key('n', ('Control_L',))
        wait(lambda: data()['text'] == '' and data()['page_layout']['visible'])
        check_text('', 0)
        assert data()['editing']['anchor'] == 0 and not data()['modified']
        assert data()['page_layout']['scroll'] == 0 and data()['page_layout']['caret_page'] == 1
        key('space'); check_text(' ', 1)
        key('Return'); check_text(' \n', 2)
        key('z', ('Control_L',)); check_text(' ', 1)
        key('z', ('Control_L',)); check_text('', 0)
        assert not data()['modified']
        menu_action('Text view'); wait(lambda: not data()['page_layout']['visible'])
        key('n', ('Control_L',)); check_text('', 0)
        wait(lambda: data()['page_layout']['pages'] == 0)
        assert data()['page_layout']['caret_rect'] is None and not data()['page_layout']['editing']
        key('space'); check_text(' ', 1)
        menu_action('Page layout'); wait(lambda: data()['page_layout']['visible'])
        check_text(' ', 1)
        assert data()['page_layout']['caret_rect'] is not None
        key('z', ('Control_L',)); check_text('', 0)
        assert not data()['modified']
        # Open through the real FLTK chooser while native pages are active.
        # Reloading also clears a distant old caret, scroll and document history.
        for attempt in range(2):
            key('o', ('Control_L',))
            if args.wayland:
                wait(lambda: any('Open document' in w.get('title', '') for w in scene()['windows']))
            else:
                dialog = wait(lambda: next((w for w in xd.screen().root.query_tree().children
                                           if 'Open document' in str(w.get_wm_name())), None))
                dialog.set_input_focus(X.RevertToParent, X.CurrentTime); xd.sync()
            time.sleep(.2)
            if attempt == 0:
                key('a', ('Control_L',)); type_text(str(source))
            key('Return')
            wait(lambda: data()['file'] == str(source) and data()['text'] == last_saved_text
                 and data()['editing']['cursor'] == 0 and data()['page_layout']['scroll'] == 0)
            assert data()['page_layout']['visible'] and data()['page_layout']['caret_page'] == 1
            assert not data()['editing']['can_undo'] and not data()['editing']['can_redo']
            if attempt == 0:
                key('End', ('Control_L',)); check_text(last_saved_text, len(last_saved_text))
                assert data()['page_layout']['scroll'] > 0
        key('q', ('Control_L',))
        assert process.wait(timeout=15) == 0
        # The signature marker is an external read-only fixture, not a claim
        # that this test constructs or validates a cryptographic signature.
        readonly_text = last_saved_text
        source_before = source.read_bytes()
        source = art / 'page-readonly.odt'
        source.write_bytes(source_before)
        with zipfile.ZipFile(source, 'a') as archive:
            archive.writestr('META-INF/documentsignatures.xml', b'<signatures/>')
        signed_before = source.read_bytes()
        report = art / 'page-readonly-report.json'
        process = start([args.bin_dir.resolve() / 'calligra-words-fltk', source,
                         '--report', report, '--page-layout'], client, 'page-readonly')
        wait(lambda: report.exists() and data()['page_layout']['glyphs'] > 0)
        if args.wayland:
            host.set_input_focus(X.RevertToParent, X.CurrentTime)
            xd.sync()
            wait(lambda: xd.get_input_focus().focus == host)
            window = wait(lambda: next((w for w in scene()['windows']
                                       if 'calligra-words' in w.get('app_id', '').lower() and w.get('client_pid') == process.pid), None))
            request = urllib.request.Request(f'http://127.0.0.1:{port}/api/action',
                data=json.dumps({'op': 'focus', 'id': window['id']}).encode(),
                headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=2) as response:
                response.read()
            wait(lambda: scene()['focused'] == window['id'])
        else:
            host = wait(lambda: next((w for w in xd.screen().root.query_tree().children
                                     if 'Calligra Words' in str(w.get_wm_name())), None))
            host.set_input_focus(X.RevertToParent, X.CurrentTime)
        xd.sync(); time.sleep(.4)
        x, y, w, h = data()['page_layout']['viewport']
        zoom = max(.05, min(1.5, (w - 48) / 400))
        left = x + (w - 400 * zoom) / 2
        click(round(left+56*zoom), round(y+12+32*zoom))
        if args.ime:
            subprocess.run(['/usr/bin/fcitx5-remote', '-s', 'keyboard-us'], env=client, check=True)
            wait(lambda: input_method() == 'keyboard-us')
        key('a', ('Control_L',))
        for name, modifiers in [('x',()), ('BackSpace',()), ('Delete',()),
                                ('Return',()), ('Tab',()), ('v',('Control_L',)),
                                ('x',('Control_L',)), ('b',('Control_L',))]:
            key(name, modifiers)
        time.sleep(.2)
        assert data()['text'] == readonly_text and not data()['modified']
        assert not data()['editing']['can_undo'] and not data()['editing']['can_redo']
        assert source.read_bytes() == signed_before
        capture_window('page-readonly')
        key('q', ('Control_L',)); assert process.wait(timeout=15) == 0
        results = {'native_wayland': args.wayland, 'external_producer': 'odfpy',
                   'native_character_paragraph_formatting': True,
                   'font_validation_and_inline_correction': True,
                   'select_all_clears_typing_format': True,
                   'native_page_click_scalar_position': True,
                   'direct_native_page_editing': True,
                   'native_page_readonly': True,
                   'native_page_mouse_drag': True,
                   'native_page_default_view_and_keyboard_start': True,
                   'native_page_new_empty_document': True,
                   'native_page_open_chooser_and_caret_reset': True,
                   'native_page_explicit_text_view_roundtrip': True,
                   'native_page_stationary_drag_scroll': True,
                   'native_page_selection_opaque_compositing': True,
                   'native_page_adjacent_vertical_navigation': True,
                   'native_page_viewport_navigation': True,
                   'native_page_drag_scroll_escape': True,
                   'native_page_list_editing': not args.page_only,
                   'native_page_escape_and_readonly_global_shortcuts': True,
                   'native_page_ime_preedit_commit_cancel': args.ime,
                   'native_page_bidi_grapheme_selection_clipboard_formatting': True,
                   'native_page_repagination_scroll_and_structural_undo': True,
                   'format_dialog_cancel_noop_undo': True,
                   'caret_typing_format': True,
                   'compound_replacement_single_undo': True,
                   'compound_failure_rollback_and_redo': True,
                   'paste_keyboard_menu_context_atomic': True,
                   'body_cell_section_split_join': True, 'soft_breaks': True,
                   'styled_xml_undo_redo': True, 'saved_state': True,
                   'selection_restore': True, 'cross_cell_rejection': True,
                   'list_item_split_join': True,
                   'list_numbering_reflow': True, 'nested_list_retention': True,
                   'list_header_split': True,
                   'inherited_sublist_style_after_join': True,
                   'list_indent_outdent_and_menu': True, 'empty_item_exit': True,
                   'ordinary_and_soft_break_tabs': True,
                   'archive_retention': True, 'save_reopen': True,
                   'page_layout': layout_report, 'qt_loaded': False}
        results['full_editor_regressions'] = not args.page_only
        results['full_native_editor_regressions'] = not args.page_only and not args.text_view
        results['full_text_view_regressions'] = not args.page_only and args.text_view
        if args.page_only:
            keep = ('native_wayland', 'external_producer', 'native_page', 'direct_native_page', 'full_editor_regressions', 'full_native_editor_regressions', 'full_text_view_regressions', 'qt_loaded')
            results = {name:value for name,value in results.items() if any(name.startswith(k) for k in keep)}
        (art / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print('PASS native page editing, bidi/grapheme navigation, selection/formatting, structure/scroll, clipboard and read-only protection' + ('; Fcitx preedit, commit/cancel and candidate anchoring' if args.ime else '') if args.page_only else 'PASS Words real keyboard editing, XML/style preservation, undo/redo, saved state, selections, atomic rejection and native page editing', flush=True)
except BaseException:
    if xd and 'host' in globals():
        try:
            geometry = host.get_geometry()
            snap = host.get_image(0, 0, geometry.width, geometry.height, X.ZPixmap, 0xffffffff)
            Image.frombytes('RGB', (geometry.width, geometry.height), snap.data, 'raw', 'BGRX').save(art / 'failure.png')
            if args.wayland:
                (art / 'failure-scene.json').write_text(json.dumps(scene(), indent=2))
        except (OSError, ValueError, XError):
            pass
    raise
finally:
    if xd:
        xd.close()
    for process in reversed(processes):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    for log in logs:
        log.close()
