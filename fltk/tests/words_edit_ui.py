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
                       ParagraphProperties, Style, TextProperties, ListLevelProperties,
                       TableProperties, TableCellProperties)
from odf.table import Table, TableCell, TableColumn, TableRow, TableHeaderRows, CoveredTableCell
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


def color_count(picture, color):
    pixels = picture.load()
    return sum(pixels[x,y] == color for y in range(picture.height)
               for x in range(picture.width))


def produce(path, conditional=False, repeated=False):
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
    if repeated:
        row.setAttribute('numberrowsrepeated',3)
        row.setAttrNS(XML[1:-1],'id','repeated-row')
    table.addElement(row)
    cell = TableCell(valuetype='string')
    if repeated: cell.setAttrNS(XML[1:-1],'id','repeated-cell')
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
parser.add_argument('--rudimentary-only', action='store_true', help='Verify basic native editing, save/reopen and history with the current system font')
parser.add_argument('--grid-repeat-only', action='store_true', help='Run repeated-row caret/undo UI checks')
parser.add_argument('--repeated-edit-only', action='store_true', help='Run local typing, deletion and clipboard checks in preexisting repeated cells')
parser.add_argument('--repeated-format-only', action='store_true', help='Run local character, paragraph, tab and named formatting in repeated cells')
parser.add_argument('--repeated-lists-only', action='store_true', help='Run local list indent/outdent and empty exit in repeated cells')
parser.add_argument('--merge-only', action='store_true', help='Run rectangular merge/split and history checks with a flat/nested fixture')
parser.add_argument('--nested-fixture', type=Path, help='Nested text fixture, or flat/nested source fixture for format/list/merge modes')
parser.add_argument('--grid-only', action='store_true', help='Run focused table row/column UI checks')
parser.add_argument('--tables-only', action='store_true', help='Run focused structural table UI checks')
parser.add_argument('--tabs-only', action='store_true', help='Run focused native tab stop UI checks')
parser.add_argument('--page-setup-only', action='store_true', help='Run focused native page setup UI checks')
parser.add_argument('--character-styles-only', action='store_true', help='Run focused named character style UI checks')
parser.add_argument('--styles-only', action='store_true', help='Run focused named paragraph style UI checks')
parser.add_argument('--format-only', action='store_true', help='Run focused character/paragraph format UI checks')
parser.add_argument('--text-view', action='store_true', help='Run the full editing regression in the explicit plain text view')
args = parser.parse_args()
if args.nested_fixture and not (args.repeated_edit_only or args.repeated_format_only or args.repeated_lists_only or args.merge_only):
    parser.error('--nested-fixture requires an edit/format/list/merge mode')
if (args.repeated_lists_only or args.merge_only) and not args.nested_fixture:
    parser.error('List/merge modes require a flat or nested fixture via --nested-fixture')
if args.rudimentary_only:
    args.page_only = True
if args.grid_repeat_only or args.repeated_edit_only or args.repeated_format_only or args.repeated_lists_only or args.merge_only:
    if args.grid_only or args.tables_only or args.tabs_only or args.page_setup_only or args.page_only or args.character_styles_only or args.styles_only or args.format_only or args.ime or args.text_view or sum((args.grid_repeat_only,args.repeated_edit_only,args.repeated_format_only,args.repeated_lists_only,args.merge_only))>1:
        parser.error('Repeated-cell modes require native page editing without other focused modes')
    args.grid_only=True

if args.grid_only and (args.tables_only or args.tabs_only or args.page_setup_only or args.page_only or args.character_styles_only or args.styles_only or args.format_only or args.ime):
    parser.error('--grid-only is incompatible with other focused modes or --ime')
if args.tables_only and (args.tabs_only or args.page_setup_only or args.page_only or args.character_styles_only or args.styles_only or args.format_only or args.ime):
    parser.error('--tables-only is incompatible with other focused modes or --ime')
if args.tabs_only and (args.page_only or args.page_setup_only or args.character_styles_only or args.styles_only or args.format_only or args.ime):
    parser.error('--tabs-only is incompatible with other focused modes or --ime')
if args.page_setup_only and (args.page_only or args.character_styles_only or args.styles_only or args.format_only or args.ime):
    parser.error('--page-setup-only is incompatible with other focused modes or --ime')
if args.character_styles_only and (args.page_only or args.styles_only or args.format_only or args.ime):
    parser.error('--character-styles-only is incompatible with other focused modes or --ime')
if args.styles_only and (args.page_only or args.format_only or args.ime):
    parser.error('--styles-only is incompatible with --page-only/--format-only/--ime')
if args.format_only and (args.page_only or args.ime):
    parser.error('--format-only is incompatible with --page-only/--ime')
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


class FormatComplete(Exception):
    pass


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
        if (args.repeated_format_only or args.repeated_lists_only or args.merge_only) and args.nested_fixture:
            source.write_bytes(args.nested_fixture.read_bytes())
        else:
            produce(source,repeated=args.grid_repeat_only or args.repeated_edit_only or args.repeated_format_only or args.repeated_lists_only or args.merge_only)
        report = art / 'report.json'
        report.unlink(missing_ok=True)
        process = start([args.bin_dir.resolve() / 'calligra-words-fltk', source,
                         '--report', report, *(['--text-view'] if args.text_view else [])], client, 'words')

        def data():
            return json.loads(report.read_text())

        def first_report():
            if process.poll() is not None:
                raise RuntimeError('Words exited before its first report: '+str(process.returncode)+
                                   '; inspect '+str(art/'words.log'))
            return report.exists()
        wait(first_report)
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

        def save(name, style_check=None):
            before = source.stat().st_ino
            key('s', ('Control_L',))
            # A false modified flag may belong to the prior report snapshot.
            # Native saves atomically replace the file; acknowledge that write
            # before comparing exact XML bytes or copying the saved package.
            wait(lambda: source.stat().st_ino != before and not data()['modified'])
            raw = source.read_bytes()
            (art / (name + '.odt')).write_bytes(raw)
            with zipfile.ZipFile(source) as archive:
                if style_check is None:
                    assert archive.read('styles.xml') == original_styles
                else:
                    style_check(archive.read('styles.xml'))
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
            if not (args.repeated_format_only or args.repeated_lists_only or args.merge_only):
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

            if not (args.styles_only or args.character_styles_only or args.page_setup_only or args.tabs_only or args.tables_only or args.grid_only):
                # Invalid fields stay in the panel and cannot consume redo or saved
                # state. Correcting a quoted fallback list must render and round-trip.
                go(2); key('x'); check_text(original[:2]+'x'+original[2:], 3)
                key('z', ('Control_L',)); check_text(original, 2)
                assert data()['editing']['can_redo'] and not data()['modified']
                go(6); key('Right', ('Shift_L',), count=5)
                key('d', ('Control_L',)); wait(lambda: data()['format_dialog'])
                x, y = panel_origin(560, 390)
                revision = data()['editing']['revision']
                for name in ("Hack'", '"Hack', ", '', "):
                    field(x+250, y+65, name)
                    click(x+480, y+355); time.sleep(.25)
                    assert data()['format_dialog'] and not data()['modified']
                    assert data()['editing']['can_redo'] and data()['editing']['revision'] == revision
                capture_window('invalid-family-dialog')
                field(x+250, y+65, "'Unavailable, Font', \"Hack\"")
                field(x+190, y+105, '0')
                click(x+480, y+355); time.sleep(.25)
                assert data()['format_dialog'] and not data()['modified']
                field(x+190, y+105, '12')
                field(x+220, y+225, '#gggggg')
                click(x+480, y+355); time.sleep(.25)
                assert data()['format_dialog'] and not data()['modified']
                field(x+220, y+225, '#102030')
                field(x+220, y+265, 'auto')
                click(x+480, y+355); time.sleep(.25)
                assert data()['format_dialog'] and not data()['modified']
                assert data()['editing']['can_redo']
                field(x+220, y+265, 'transparent')
                click(x+480, y+355)
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
                click(x+480, y+355); time.sleep(.25)
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

                for field_name, action in [('text-underline', lambda: key('u', ('Control_L',))),
                                           ('text-line-through', lambda: menu_action('Strikethrough'))]:
                    action(); wait(lambda: data()['modified'])
                    root, line_xml = save('format-'+field_name)
                    assert any(p is not None and p.get(STYLE+field_name+'-style') == 'solid' and
                               p.get(STYLE+field_name+'-type') == 'single'
                               for p in text_properties(root).values())
                    view_labels(field_name+'-layout')
                    action(); wait(lambda: data()['modified'])
                    root, _ = save('format-'+field_name+'-off')
                    assert any(p is not None and p.get(STYLE+field_name+'-style') == 'none' and
                               p.get(STYLE+field_name+'-type') is None
                               for p in text_properties(root).values())
                    key('z', ('Control_L',)); assert save('format-'+field_name+'-restored')[1] == line_xml
                    key('z', ('Control_L',)); assert save('format-'+field_name+'-undone')[1] == original_xml

                key('d', ('Control_L',))
                wait(lambda: data()['format_dialog'])
                x, y = panel_origin(560, 390)
                field(x+250, y+65, 'Hack')
                field(x+190, y+105, '22')
                click(x+220, y+145); key('Up'); key('Return')  # Bold: Yes -> No
                click(x+220, y+185); key('Down'); key('Return')  # Italic: No -> Yes
                click(x+460, y+145); key('Down'); key('Return')  # Underline: No -> Yes
                click(x+460, y+185); key('Down'); key('Return')  # Strikethrough: No -> Yes
                field(x+220, y+225, '#b02040')
                field(x+220, y+265, '#fff176')
                capture_window('character-dialog')
                click(x+480, y+355)
                wait(lambda: not data()['format_dialog'] and data()['modified'])
                root, font_xml = save('format-font')
                assert any(p is not None and p.get(FO+'font-size') == '22pt' and
                           p.get(FO+'font-family') == 'Hack' and p.get(FO+'color') == '#b02040' and
                           p.get(FO+'font-weight') == '400' and p.get(FO+'font-style') == 'italic' and
                           p.get(FO+'background-color') == '#fff176' and
                           p.get(STYLE+'text-underline-style') == 'solid' and
                           p.get(STYLE+'text-line-through-style') == 'solid'
                           for p in text_properties(root).values())
                view_labels('character-formatted-layout')
                menu_action('Page layout')
                wait(lambda: data()['page_layout']['visible'])
                key('Right'); time.sleep(.4)
                unselected = capture_window('highlight-unselected')
                yellow_before = color_count(unselected, (255,241,118))
                assert yellow_before > 100, yellow_before
                go(6); key('Right', ('Shift_L',), count=5); time.sleep(.4)
                selected = capture_window('highlight-selected')
                yellow_after = color_count(selected, (255,241,118))
                assert yellow_after < yellow_before, (yellow_before, yellow_after)
                layout_report['highlight'] = {'unselected_yellow_pixels': yellow_before,
                                              'selected_yellow_pixels': yellow_after,
                                              'selection_visible_over_highlight': True}
                primary_view()
                key('d', ('Control_L',)); wait(lambda: data()['format_dialog'])
                field(x+220, y+265, 'transparent')
                click(x+480, y+355); wait(lambda: not data()['format_dialog'])
                cleared, _ = save('format-highlight-cleared')
                assert any(p is not None and p.get(FO+'background-color') == 'transparent'
                           for p in text_properties(cleared).values())
                key('z', ('Control_L',))
                assert save('format-highlight-restored')[1] == font_xml
                # Cancel and unchanged Apply must not consume an undo entry or redo.
                menu_action('Font…'); wait(lambda: data()['format_dialog'])
                field(x+190, y+105, '30')
                key('Escape'); wait(lambda: not data()['format_dialog'])
                assert not data()['modified'] and save('format-font-cancelled')[1] == font_xml
                key('d', ('Control_L',)); wait(lambda: data()['format_dialog'])
                click(x+480, y+355); wait(lambda: not data()['format_dialog'])
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
                key('u', ('Control_L',)); menu_action('Strikethrough')
                wait(lambda: data()['editing']['typing_format'])
                assert not data()['modified']
                key('x'); check_text(original[:2]+'x'+original[2:], 3)
                root, typed_xml = save('format-typed')
                assert any(p is not None and p.get(FO+'font-weight') == '700' and
                           p.get(STYLE+'text-underline-style') == 'solid' and
                           p.get(STYLE+'text-line-through-style') == 'solid'
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
                if args.format_only:
                    key('q', ('Control_L',)); assert process.wait(timeout=15) == 0
                    (art / 'results.json').write_text(json.dumps({
                        'native_wayland': args.wayland, 'external_producer': 'odfpy',
                        'focused_character_paragraph_formatting': True,
                        'native_underline_strike_highlight': True,
                        'typing_format_cancel_undo_redo': True,
                        'source_archive_preservation': True,
                        'full_native_editor_regressions': False,
                        'page_layout': layout_report}, indent=2)+'\n')
                    print('PASS focused Words native character/paragraph formatting, source/history and typing', flush=True)
                    raise FormatComplete
            # Named definitions and paragraph assignment are one source/ZIP
            # transaction. Cancel must discard staged definitions and retain redo.
            O = '{urn:oasis:names:tc:opendocument:xmlns:office:1.0}'
            def parts(name):
                with zipfile.ZipFile(art/(name+'.odt')) as archive:
                    return tuple(archive.read(e) for e in ('content.xml','styles.xml','META-INF/manifest.xml'))
            original_parts = parts('original')
            def exact_styles(expected):
                def check(value):
                    assert value == expected
                return check
            def canonical(node):
                return (node.tag, sorted(node.attrib.items()),
                        node.text if node.text and node.text.strip() else '',
                        tuple(canonical(child) for child in node))
            original_style_tree = ET.fromstring(original_styles)
            def choice(x, y, index, current=0):
                click(x,y)
                delta=index-current
                key('Down' if delta>=0 else 'Up', count=abs(delta))
                key('Return'); time.sleep(.15)
            if not (args.character_styles_only or args.page_setup_only or args.tabs_only or args.tables_only or args.grid_only):
                style_id = []
                def definition_check(label, size):
                    def check(raw):
                        tree = ET.fromstring(raw)
                        old_group = original_style_tree.find(O+'styles')
                        group = tree.find(O+'styles')
                        assert [canonical(e) for e in list(group)[:len(old_group)]] == [canonical(e) for e in old_group]
                        assert len(group) == len(old_group)+1
                        named = [e for e in group if e.get(STYLE+'display-name') == label]
                        assert len(named) == 1
                        node = named[0]
                        identifier = node.get(STYLE+'name')
                        assert identifier.startswith('WordsNamed')
                        if style_id: assert style_id[0] == identifier
                        else: style_id.append(identifier)
                        assert node.get(STYLE+'parent-style-name') == 'Body'
                        assert node.get(STYLE+'default-outline-level') == '2'
                        props = node.find(STYLE+'text-properties')
                        assert props.get(FO+'font-family') == 'Hack' and props.get(FO+'font-size') == str(size)+'pt'
                        assert props.get(FO+'font-weight') == '700' and props.get(FO+'color') == '#165aa7'
                        assert props.get(STYLE+'text-underline-style') == 'solid'
                    return check
                def open_styles():
                    menu_action('Paragraph styles…'); wait(lambda: data()['format_dialog'])
                    time.sleep(.2)
                    return panel_origin(680,480)
                # A staged create can be corrected after validation and then cancelled.
                go(6); key('Right', ('Shift_L',), count=5)
                before = data()['editing'].copy()
                x,y = open_styles(); click(x+65,y+390); time.sleep(.2)
                x,y = panel_origin(650,460)
                field(x+300,y+65,'')
                click(x+575,y+425); time.sleep(.2)
                assert data()['format_dialog'] and not data()['modified']
                field(x+300,y+65,'Temporary')
                click(x+575,y+425); time.sleep(.3)
                assert not data()['modified']
                key('Escape'); wait(lambda: not data()['format_dialog'])
                assert data()['editing']['can_redo'] == before['can_redo']
                assert save('styles-staged-cancelled')[1] == original_xml
                assert parts('styles-staged-cancelled') == original_parts
                # New style inherits Body, has explicit heading role and a self next.
                x,y = open_styles(); click(x+65,y+390); time.sleep(.2)
                x,y = panel_origin(650,460)
                field(x+300,y+65,'Notice')
                choice(x+300,y+105,1,current=2)  # Body, rather than First's forced page break.
                choice(x+300,y+145,1)  # Same style.
                choice(x+300,y+185,3)  # Heading 2.
                click(x+250,y+235); time.sleep(.2)
                x,y = panel_origin(560,390)
                field(x+250,y+65,'Hack'); field(x+190,y+105,'20')
                choice(x+220,y+145,2,current=1); choice(x+460,y+145,2,current=1)
                field(x+220,y+225,'#165aa7')
                click(x+480,y+355); time.sleep(.3)
                x,y = panel_origin(650,460)
                click(x+445,y+235); time.sleep(.2)
                x,y = panel_origin(700,470)
                # Match the existing paragraph panel's control positions.
                choice(x+185,y+65,2,current=5)  # Center.
                field(x+185,y+110,'24'); field(x+575,y+110,'12')
                field(x+185,y+155,'-6')
                click(x+620,y+435); time.sleep(.3)
                x,y = panel_origin(650,460)
                capture_window('named-style-editor')
                click(x+575,y+425); time.sleep(.3)
                assert not data()['modified']
                x,y = panel_origin(680,480)
                capture_window('named-style-chooser')
                click(x+605,y+445)
                wait(lambda: not data()['format_dialog'] and data()['modified'])
                root, created_xml = save('style-created-applied',definition_check('Notice',20))
                created_parts = parts('style-created-applied')
                assert created_parts[1] != original_styles
                heading = root.find('.//'+TEXT+'h')
                assert heading is not None and heading.get(TEXT+'style-name') == style_id[0]
                assert heading.get(TEXT+'outline-level') == '2' and heading.get(XML+'id') == 'first'
                view_labels('named-style-layout')
                key('z', ('Control_L',)); assert save('style-created-undone')[1] == original_xml
                assert parts('style-created-undone') == original_parts
                key('y', ('Control_L',)); assert save('style-created-redone',exact_styles(created_parts[1]))[1] == created_xml
                assert parts('style-created-redone') == created_parts
                first_end = original.index('\n')
                go(first_end); key('Return'); check_text(original[:first_end]+'\n'+original[first_end:])
                root,_ = save('style-heading-self',exact_styles(created_parts[1]))
                assert len(root.findall('.//'+TEXT+'h')) == 2
                key('z', ('Control_L',)); check_text(original)
                assert save('style-heading-self-undone',exact_styles(created_parts[1]))[1] == created_xml
                # Editing a definition updates layout without rewriting body XML.
                go(6); x,y = open_styles(); click(x+165,y+390); time.sleep(.2)
                x,y = panel_origin(650,460)
                field(x+300,y+65,'Notice renamed'); choice(x+300,y+145,2,current=1)  # Next Body.
                click(x+250,y+235); time.sleep(.2)
                x,y = panel_origin(560,390); field(x+190,y+105,'24')
                click(x+480,y+355); time.sleep(.3)
                x,y = panel_origin(650,460); click(x+575,y+425); time.sleep(.3)
                x,y = panel_origin(680,480); click(x+605,y+445)
                wait(lambda: not data()['format_dialog'] and data()['modified'])
                root, edited_xml = save('style-edited',definition_check('Notice renamed',24))
                edited_parts = parts('style-edited')
                assert edited_xml == created_xml and edited_parts[1] != created_parts[1]
                view_labels('named-style-edited-layout')
                x,y = open_styles(); click(x+285,y+390); time.sleep(.3)
                assert data()['format_dialog'] and not data()['modified']
                capture_window('named-style-used-delete-rejected')
                key('Escape'); wait(lambda: not data()['format_dialog'])
                assert save('style-delete-used-rejected',exact_styles(edited_parts[1]))[1] == edited_xml
                go(first_end); key('Return'); check_text(original[:first_end]+'\n'+original[first_end:])
                root,_ = save('style-heading-next-body',exact_styles(edited_parts[1]))
                body = root.find(O+'body/'+O+'text')
                assert body[0].tag == TEXT+'h' and body[1].tag == TEXT+'p'
                assert body[1].get(TEXT+'style-name') == 'Body'
                key('z', ('Control_L',)); check_text(original)
                assert save('style-heading-next-undone',exact_styles(edited_parts[1]))[1] == edited_xml
                key('z', ('Control_L',)); assert save('style-definition-undone',exact_styles(created_parts[1]))[1] == created_xml
                assert parts('style-definition-undone') == created_parts
                key('y', ('Control_L',)); assert save('style-definition-redone',exact_styles(edited_parts[1]))[1] == edited_xml
                assert parts('style-definition-redone') == edited_parts
                key('z', ('Control_L',)); key('z', ('Control_L',))
                assert save('style-all-restored')[1] == original_xml
                assert parts('style-all-restored') == original_parts
                layout_report['named_paragraph_styles'] = {'staged_cancel':True,'one_step_source_part_undo':True,
                                                           'heading_self_and_body_next':True,'definition_only_body_retention':True}
                if args.styles_only:
                    key('q', ('Control_L',)); assert process.wait(timeout=15) == 0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,
                        'native_named_paragraph_styles':True,'page_layout':layout_report,
                        'full_native_editor_regressions':False},indent=2)+'\n')
                    print('PASS named paragraph style native UI, staging, heading roles, ZIP/source undo and next styles',flush=True)
                    raise FormatComplete
            if not (args.page_setup_only or args.tabs_only or args.tables_only or args.grid_only):
                # Named character styles replace selected inline overrides and can
                # remain as a named reference for future typing at a caret.
                char_ids = []
                def char_definition(label, size):
                    def check(raw):
                        group = ET.fromstring(raw).find(O+'styles')
                        old_group = original_style_tree.find(O+'styles')
                        assert [canonical(e) for e in list(group)[:len(old_group)]] == [canonical(e) for e in old_group]
                        assert len(group) == len(old_group)+1
                        named = [e for e in group if e.get(STYLE+'display-name') == label]
                        assert len(named) == 1
                        node = named[0]
                        identifier = node.get(STYLE+'name')
                        assert node.get(STYLE+'family') == 'text' and identifier.startswith('WordsCharacterNamed')
                        if char_ids: assert char_ids[0] == identifier
                        else: char_ids.append(identifier)
                        assert node.get(STYLE+'parent-style-name') == 'Bold'
                        props = node.find(STYLE+'text-properties')
                        assert props.get(FO+'font-family') == 'Hack' and props.get(FO+'font-size') == str(size)+'pt'
                        assert props.get(FO+'font-weight') == '400' and props.get(FO+'font-style') == 'italic'
                        assert props.get(FO+'color') == '#165aa7' and props.get(FO+'background-color') == '#fff176'
                        assert props.get(STYLE+'text-underline-style') == 'solid'
                    return check
                def open_character_styles():
                    menu_action('Character styles…'); wait(lambda: data()['format_dialog']); time.sleep(.2)
                    return panel_origin(680,480)
                go(6); key('Right', ('Shift_L',), count=5)
                x,y = open_character_styles(); click(x+65,y+390); time.sleep(.2)
                x,y = panel_origin(650,340); field(x+300,y+65,'Temporary character')
                click(x+575,y+305); time.sleep(.3)
                key('Escape'); wait(lambda: not data()['format_dialog'])
                assert save('character-style-staged-cancelled')[1] == original_xml
                assert parts('character-style-staged-cancelled') == original_parts
                x,y = open_character_styles(); click(x+65,y+390); time.sleep(.2)
                x,y = panel_origin(650,340); field(x+300,y+65,'Emphasis')
                click(x+250,y+155); time.sleep(.2)
                x,y = panel_origin(560,390)
                field(x+190,y+65,'Hack'); field(x+190,y+105,'18')
                choice(x+190,y+145,1,current=2)  # inherited Bold: Yes -> No
                choice(x+190,y+185,2,current=1)
                field(x+190,y+225,'#165aa7'); field(x+190,y+265,'#fff176')
                choice(x+460,y+145,2,current=1)
                click(x+480,y+355); time.sleep(.3)
                x,y = panel_origin(650,340); capture_window('character-style-editor')
                click(x+575,y+305); time.sleep(.3)
                x,y = panel_origin(680,480); click(x+605,y+445)
                wait(lambda: not data()['format_dialog'] and data()['modified'])
                root, char_xml = save('character-style-created',char_definition('Emphasis',18))
                char_parts = parts('character-style-created')
                assert data()['text'] == original
                assert root.find('.//'+TEXT+'p').get(TEXT+'style-name') == 'First'
                assert len([e for e in root.iter() if e.get(XML+'id') == 'first']) == 1
                assert any(e.get(TEXT+'style-name') == char_ids[0] and ''.join(e.itertext()) == 'Český' for e in root.iter(TEXT+'span'))
                view_labels('character-style-layout')
                key('z', ('Control_L',)); assert save('character-style-undone')[1] == original_xml
                assert parts('character-style-undone') == original_parts
                key('y', ('Control_L',)); assert save('character-style-redone',exact_styles(char_parts[1]))[1] == char_xml
                assert parts('character-style-redone') == char_parts
                x,y = open_character_styles(); key('Up',count=2); click(x+605,y+445)
                wait(lambda: not data()['format_dialog'] and data()['modified'])
                root,_=save('character-style-cleared',exact_styles(char_parts[1]))
                assert not any(e.get(TEXT+'style-name')==char_ids[0] for e in root.iter(TEXT+'span'))
                assert any(e.get(TEXT+'style-name')=='Bold' and '日本語' in ''.join(e.itertext()) for e in root.iter(TEXT+'span'))
                key('z', ('Control_L',)); assert save('character-style-cleared-undone',exact_styles(char_parts[1]))[1] == char_xml
                # Definition-only edits retain body bytes and the stable style ID.
                x,y = open_character_styles(); click(x+165,y+390); time.sleep(.2)
                x,y = panel_origin(650,340); field(x+300,y+65,'Emphasis renamed')
                click(x+250,y+155); time.sleep(.2)
                x,y = panel_origin(560,390); field(x+190,y+105,'24'); click(x+480,y+355); time.sleep(.3)
                x,y = panel_origin(650,340); click(x+575,y+305); time.sleep(.3)
                x,y = panel_origin(680,480); click(x+30,y+430); click(x+605,y+445)
                wait(lambda: not data()['format_dialog'] and data()['modified'])
                _, char_edited_xml = save('character-style-edited',char_definition('Emphasis renamed',24))
                char_edited = parts('character-style-edited')
                assert char_edited_xml == char_xml and char_edited[1] != char_parts[1]
                view_labels('character-style-edited-layout')
                x,y = open_character_styles(); click(x+285,y+390); time.sleep(.3)
                assert data()['format_dialog'] and not data()['modified']
                capture_window('character-style-used-delete-rejected')
                key('Escape'); wait(lambda: not data()['format_dialog'])
                # At a caret, assignment changes future typing, with no source edit.
                go(original.index('Section alpha')+3)
                x,y = open_character_styles()
                key('Down',count=2); time.sleep(.2)  # Default -> Bold -> Emphasis renamed
                click(x+605,y+445); wait(lambda: not data()['format_dialog'])
                assert data()['editing']['typing_format'] and not data()['modified']
                key('x'); where=original.index('Section alpha')+3
                check_text(original[:where]+'x'+original[where:],where+1)
                root,_=save('character-style-typed',exact_styles(char_edited[1]))
                assert any(e.get(TEXT+'style-name') == char_ids[0] and ''.join(e.itertext()) == 'x' for e in root.iter(TEXT+'span'))
                key('z', ('Control_L',)); check_text(original)
                assert save('character-style-typed-undone',exact_styles(char_edited[1]))[1] == char_xml
                # Apply the same style to a hyperlink without splitting the link.
                where=original.index('link'); go(where); key('Right', ('Shift_L',), count=4)
                x,y=open_character_styles(); key('Down',count=2); click(x+605,y+445)
                wait(lambda: not data()['format_dialog'] and data()['modified'])
                root,_=save('character-style-link',exact_styles(char_edited[1]))
                links=root.findall('.//'+TEXT+'a'); assert len(links)==1 and links[0].get(XLINK+'href')=='https://example.invalid/document'
                assert any(e.get(TEXT+'style-name') == char_ids[0] for e in links[0].iter(TEXT+'span'))
                key('z', ('Control_L',)); assert save('character-style-link-undone',exact_styles(char_edited[1]))[1] == char_xml
                key('z', ('Control_L',)); assert save('character-style-definition-undone',exact_styles(char_parts[1]))[1] == char_xml
                assert parts('character-style-definition-undone') == char_parts
                key('y', ('Control_L',)); assert save('character-style-definition-redone',exact_styles(char_edited[1]))[1] == char_xml
                key('z', ('Control_L',)); key('z', ('Control_L',))
                assert save('character-style-all-restored')[1] == original_xml
                assert parts('character-style-all-restored') == original_parts
                layout_report['named_character_styles']={'staged_cancel':True,'absolute_range_assignment':True,
                    'one_step_zip_source_undo':True,'definition_only_body_retention':True,'named_caret_typing':True,'link_retention':True}
                if args.character_styles_only:
                    key('q', ('Control_L',)); assert process.wait(timeout=15)==0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,'native_named_character_styles':True,
                        'page_layout':layout_report,'full_native_editor_regressions':False},indent=2)+'\n')
                    print('PASS named character style UI, range/default handling, named typing, source/archive undo and links',flush=True)
                    raise FormatComplete
            if not (args.tabs_only or args.page_setup_only or args.grid_only):
                def open_table():
                    menu_action('Insert table…'); wait(lambda:data()['format_dialog']); time.sleep(.2)
                    return panel_origin(520,300)
                def table_count(root):
                    return len(root.findall('.//'+TABLE+'table'))
                # Cancel and invalid input retain source, selection and redo.
                go(6); key('Right',('Shift_L',),count=3)
                selected={k:data()['editing'][k] for k in ('anchor','cursor')}
                revision=data()['editing']['revision']
                x,y=open_table(); field(x+150,y+80,'4'); click(x+340,y+265)
                wait(lambda:not data()['format_dialog'])
                assert save('table-cancelled')[1]==original_xml
                assert data()['editing']['revision']==revision
                assert {k:data()['editing'][k] for k in selected}==selected
                x,y=open_table(); field(x+150,y+80,'0'); click(x+445,y+265); time.sleep(.3)
                assert data()['format_dialog'] and data()['editing']['revision']==revision
                field(x+150,y+80,'1000'); field(x+400,y+80,'1000'); click(x+445,y+265); time.sleep(.3)
                assert data()['format_dialog'] and data()['editing']['revision']==revision
                capture_window('table-invalid-size')
                field(x+150,y+80,'3'); field(x+400,y+80,'2'); click(x+30,y+130)
                capture_window('table-insert-dialog'); click(x+445,y+265)
                wait(lambda:not data()['format_dialog'] and data()['modified'])
                expected=original[:9]+'\n'*7+original[9:]
                check_text(expected,10)
                root,inserted_xml=save('table-inserted')
                inserted_parts=parts('table-inserted')
                assert table_count(root)==2
                table=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Table1')
                assert len(table.findall('.//'+TABLE+'table-row'))==3
                assert len(table.findall('.//'+TABLE+'table-cell'))==6
                assert table.find(TABLE+'table-header-rows') is not None
                key('z',('Control_L',)); check_text(original,selected['cursor'])
                assert data()['editing']['anchor']==selected['anchor']
                assert save('table-insert-undone')[1]==original_xml
                assert parts('table-insert-undone')==original_parts
                key('y',('Control_L',)); check_text(expected,10)
                assert save('table-insert-redone')[1]==inserted_xml
                assert parts('table-insert-redone')==inserted_parts
                # Fill source cells through real keyboard editing, retaining the
                # other paragraph half, inline style/link and existing table.
                first_cell=expected[:10]+'A1'+expected[10:]
                type_text('A1'); check_text(first_cell,12)
                second_cell=first_cell[:13]+'B1'+first_cell[13:]
                go(13); type_text('B1'); check_text(second_cell,15)
                filled=second_cell[:16]+'A2'+second_cell[16:]
                go(16); type_text('A2'); check_text(filled,18)
                root,filled_xml=save('table-filled')
                assert table_count(root)==2
                assert len(root.findall('.//'+TEXT+'a'))==1
                view_labels('table-filled-layout')
                # Thin bottom borders must retain a preview pixel at the row
                # band edge; PDF comparison alone cannot catch a GPU clip loss.
                paged=data()['page_layout']
                (art/'table-filled-layout.json').write_text(json.dumps(paged,indent=2)+'\n')
                cells=[c for c in paged['table_cells'] if c['table']==0]
                cell=next(c for c in cells if c['row']==2 and c['column']==0)
                vx,vy,vw,vh=paged['viewport']
                zoom=max(.05,min(1.5,(vw-48)/max(p['width'] for p in paged['paper_styles'])))
                px=vx+(vw-cell['page_width']*zoom)/2
                py=vy+12-paged['scroll']
                if args.wayland:
                    box=next(w for w in scene()['windows'] if w['id']==window['id'])['desktop']
                    px+=int(box['x']);py+=int(box['y'])
                picture=Image.open(art/'table-filled-layout.png').convert('RGB')
                left=round(px+cell['x']*zoom)+10
                right=round(px+(cell['x']+cell['width'])*zoom)-10
                bottom=round(py+(cell['y']+cell['height'])*zoom)
                gray=sum(1 for y in range(bottom-2,bottom+2) for x in range(left,right)
                         if max(picture.getpixel((x,y)))-min(picture.getpixel((x,y)))<3
                         and 100<picture.getpixel((x,y))[0]<240)
                assert gray>(right-left)//2,(gray,left,right,bottom)
                go(10); x,y=open_table(); field(x+150,y+80,'2'); field(x+400,y+80,'1')
                click(x+445,y+265); wait(lambda:not data()['format_dialog'] and data()['modified'])
                root,nested_xml=save('table-nested'); assert table_count(root)==3
                assert data()['editing']['cursor']==10
                menu_action('Delete table'); wait(lambda:data()['modified'] and not data()['format_dialog'])
                root,_=save('table-nested-deleted'); assert table_count(root)==2
                assert data()['text']==filled
                key('z',('Control_L',)); assert save('table-nested-delete-undone')[1]==nested_xml
                key('z',('Control_L',)); assert save('table-nested-insert-undone')[1]==filled_xml
                go(10); menu_action('Delete table'); time.sleep(.2)
                root,_=save('table-deleted'); assert table_count(root)==1
                assert 'A1' not in data()['text'] and 'B1' not in data()['text']
                key('z',('Control_L',)); assert save('table-delete-undone')[1]==filled_xml
                # Undo the six real character events before undoing the table.
                for _ in range(6):
                    if data()['text']==expected: break
                    revision=data()['editing']['revision']; key('z',('Control_L',))
                    wait(lambda:data()['editing']['revision']!=revision)
                check_text(expected)
                key('z',('Control_L',)); check_text(original)
                assert save('table-all-restored')[1]==original_xml
                assert parts('table-all-restored')==original_parts
                layout_report['table_blocks']={'insert_delete':True,'invalid_cancel':True,'rich_split_retained':True,
                    'native_cell_typing':True,'nested_scope':True,'header_row':True,'one_step_exact_part_undo':True}
                if args.tables_only:
                    key('q',('Control_L',)); assert process.wait(timeout=15)==0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,'native_table_blocks':True,
                        'page_layout':layout_report,'full_native_editor_regressions':False},indent=2)+'\n')
                    print('PASS native Words table insertion/deletion, nested tables, cell typing and exact structural undo',flush=True)
                    raise FormatComplete
            if args.grid_repeat_only or args.repeated_edit_only or args.repeated_format_only or args.repeated_lists_only or args.merge_only:
                click_records=[]
                def settled_page():
                    previous=None;since=0;reports=set()
                    def ready():
                        nonlocal previous,since,reports
                        snapshot=data();page=snapshot['page_layout']
                        geometry=tuple(snapshot['window_size']+page['viewport'])
                        if geometry!=previous:
                            previous=geometry;since=time.monotonic();reports=set()
                        reports.add(report.stat().st_mtime_ns)
                        if time.monotonic()-since>.5 and len(reports)>=3:
                            return snapshot
                    return wait(ready)
                def repeated_click(row,column,start=False):
                    snapshot=settled_page();page=snapshot['page_layout']
                    cell=next(c for c in page['table_cells'] if c['table']==0 and c['row']==row and c['column']==column)
                    vx,vy,vw,vh=page['viewport']
                    zoom=max(.05,min(1.5,(vw-48)/max(p['width'] for p in page['paper_styles'])))
                    left=vx+(vw-cell['page_width']*zoom)/2
                    top=vy+12-page['scroll']
                    point=[round(left+(cell['x']+(1 if start else 12))*zoom),
                        round(top+(cell['y']+(3 if start else cell['height']/2))*zoom)]
                    click_records.append({'window_size':snapshot['window_size'],'viewport':page['viewport'],
                        'row':row,'column':column,'point':point})
                    (art/'grid-clicks.json').write_text(json.dumps(click_records,indent=2)+'\n')
                    click(*point)
                    wait(lambda:data()['editing']['cell']==[0,row,column,1,1])
                def repeat_action(label,cell):
                    revision=data()['editing']['revision'];menu_action(label)
                    wait(lambda:data()['editing']['revision']!=revision and data()['editing']['cell']==cell)
                def repeat_undo(name,cursor):
                    revision=data()['editing']['revision'];key('z',('Control_L',))
                    wait(lambda:data()['editing']['revision']!=revision)
                    check_text(original,cursor)
                    wait(lambda:data()['editing']['cell']==[0,1,0,1,1])
                    assert save(name)[1]==original_xml
                    assert parts(name)==original_parts
                def pick_repeated(prefix, ranged=True, column=0, nested_row=1, nested_column=1):
                    snapshot=settled_page();page=snapshot['page_layout'];boxes=page['table_cells']
                    ids={c['table'] for c in boxes}
                    if len(ids)==1:
                        cell=next(c for c in boxes if c['row']==1 and c['column']==column)
                        path=[[cell['table'],1,column,1,1]]
                    else:
                        outer_id=max(ids,key=lambda id:max(c['width'] for c in boxes if c['table']==id))
                        outer=next(c for c in boxes if c['table']==outer_id and c['row']==1 and c['column']==2)
                        cell=next(c for c in boxes if c['table']!=outer_id and c['row']==nested_row and c['column']==nested_column
                            and outer['x']<=c['x']<outer['x']+outer['width']
                            and outer['y']<=c['y']<outer['y']+outer['height'])
                        path=[[outer_id,1,2,1,1],[cell['table'],nested_row,nested_column,1,1]]
                    vx,vy,vw,vh=page['viewport'];zoom=max(.05,min(1.5,(vw-48)/max(p['width'] for p in page['paper_styles'])))
                    left=vx+(vw-cell['page_width']*zoom)/2;top=vy+12-page['scroll']
                    click(round(left+(cell['x']+1)*zoom),round(top+(cell['y']+3)*zoom))
                    wait(lambda:data()['editing']['cells']==path)
                    key('Home');check_text(original,original.index(prefix))
                    if ranged:key('Right',('Shift_L',),count=len(prefix))
                    check_text(original,original.index(prefix)+(len(prefix) if ranged else 0))
                    return path
                if args.merge_only:
                    nested='I00' in original
                    prefix='I00' if nested else 'A1'
                    path=pick_repeated(prefix,False,0,0,0)
                    position=original.index(prefix);check_text(original,position)
                    def merge_dialog():
                        menu_action('Merge cells…');wait(lambda:data()['format_dialog'])
                        return panel_origin(520,260)
                    x,y=merge_dialog();key('Escape');wait(lambda:not data()['format_dialog'])
                    assert save('merge-cancelled')[1]==original_xml and parts('merge-cancelled')==original_parts
                    revision=data()['editing']['revision']
                    x,y=merge_dialog();field(x+150,y+85,'0');click(x+445,y+225);time.sleep(.3)
                    assert data()['format_dialog'] and data()['editing']['revision']==revision
                    field(x+150,y+85,'1000');click(x+445,y+225);time.sleep(.3)
                    assert data()['format_dialog'] and data()['editing']['revision']==revision
                    field(x+150,y+85,'2');field(x+400,y+85,'2');click(x+445,y+225)
                    wait(lambda:not data()['format_dialog'] and data()['editing']['revision']!=revision)
                    def cells(table):
                        rows=[]
                        def collect(parent):
                            for node in parent:
                                if node.tag==TABLE+'table-row':
                                    values=[]
                                    for cell in node:
                                        if cell.tag in (TABLE+'table-cell',TABLE+'covered-table-cell'):
                                            values.extend([cell]*int(cell.get(TABLE+'number-columns-repeated','1')))
                                    rows.extend([values]*int(node.get(TABLE+'number-rows-repeated','1')))
                                elif node.tag in tuple(TABLE+t for t in ('table-rows','table-row-group','table-header-rows')):collect(node)
                        collect(table);return rows
                    def area(root):
                        table=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')==('Outer' if nested else 'Grid'))
                        if nested:table=cells(table)[1][2].find(TABLE+'table')
                        rows=cells(table);r=0 if nested else 1
                        return [[rows[r+i][j] for j in range(2)] for i in range(2)]
                    root,merged_xml=save('merge-merged');merged_parts=parts('merge-merged')
                    selected=area(root);anchor=selected[0][0]
                    assert anchor.get(TABLE+'number-rows-spanned')=='2' and anchor.get(TABLE+'number-columns-spanned')=='2'
                    assert [plain(p) for p in anchor.findall(TEXT+'p')]==(['I00','I01','I10','I11'] if nested else ['A1','A2','B1','B2'])
                    assert all(selected[r][c].tag==TABLE+'covered-table-cell' for r,c in ((0,1),(1,0),(1,1)))
                    assert data()['editing']['cell'][-2:]==[2,2]
                    merged_selection=dict(data()['editing'])
                    # An already merged rectangle is a no-op, including the caret.
                    key('Right');before_noop=dict(data()['editing'])
                    x,y=merge_dialog();click(x+445,y+225);wait(lambda:not data()['format_dialog'])
                    assert data()['editing']['revision']==before_noop['revision']
                    assert data()['editing']['cursor']==before_noop['cursor']
                    type_text('X');root,_=save('merge-typed')
                    assert 'X' in plain(area(root)[0][0])
                    key('z',('Control_L',));assert save('merge-typing-undone')[1]==merged_xml
                    revision=data()['editing']['revision'];menu_action('Split cell')
                    wait(lambda:data()['editing']['revision']!=revision)
                    root,split_xml=save('merge-split');selected=area(root)
                    assert all(cell.tag==TABLE+'table-cell' and cell.get(TABLE+'number-rows-spanned','1')=='1' and cell.get(TABLE+'number-columns-spanned','1')=='1' for row in selected for cell in row)
                    assert all(not plain(selected[r][c]).strip() for r,c in ((0,1),(1,0),(1,1)))
                    key('z',('Control_L',));assert save('merge-split-undone')[1]==merged_xml and parts('merge-split-undone')==merged_parts
                    key('y',('Control_L',));assert save('merge-split-redone')[1]==split_xml
                    key('z',('Control_L',));key('z',('Control_L',))
                    check_text(original,position);wait(lambda:data()['editing']['cells']==path)
                    assert save('merge-all-undone')[1]==original_xml and parts('merge-all-undone')==original_parts
                    assert not data()['editing']['can_undo']
                    key('q',('Control_L',));assert process.wait(timeout=15)==0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,'nested':nested,
                        'merge_split_rich_content':True,'typed_merged_cell':True,'invalid_cancel_noop':True,'exact_archive_history':True},indent=2)+'\n')
                    print('PASS native Words merge/split, typing, cancel/invalid/no-op and exact structural history',flush=True)
                    raise FormatComplete
                if args.repeated_lists_only:
                    def list_pick(word='Český Beta'):
                        path=pick_repeated('Alpha',False,2)
                        key('Right',count=original.index(word)-original.index('Alpha'))
                        check_text(original,original.index(word))
                        return path
                    def list_cells(root):
                        def cells(table):
                            result=[]
                            for row in table.findall(TABLE+'table-row'):
                                values=[]
                                for cell in row.findall(TABLE+'table-cell'):
                                    values.extend([cell]*int(cell.get(TABLE+'number-columns-repeated','1')))
                                result.extend([values]*int(row.get(TABLE+'number-rows-repeated','1')))
                            return result
                        outer=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Outer')
                        leaves=[]
                        for r,row in enumerate(cells(outer)):
                            for c,cell in enumerate(row):
                                inner=cell.find(TABLE+'table')
                                if inner is None:leaves.append((cell,r==1 and c==2))
                                else:
                                    for ir,irow in enumerate(cells(inner)):
                                        for ic,leaf in enumerate(irow):leaves.append((leaf,r==1 and c==2 and ir==1 and ic==1))
                        return leaves
                    def check_list(name,mode):
                        root,xml=save(name);leaves=list_cells(root)
                        assert len(leaves) in (9,36)
                        for leaf,target in leaves:
                            paragraphs=[]
                            def walk(node,level=0):
                                if node.tag==TEXT+'list':level+=1
                                if node.tag in (TEXT+'p',TEXT+'h'):paragraphs.append((plain(node),level))
                                else:
                                    for child in node:walk(child,level)
                            walk(leaf)
                            expected=[('Alpha',1),('Český Beta',2 if target and mode=='indent' else 1),('Gamma',1),('',0 if target and mode=='exit' else 1)]
                            assert paragraphs==expected,(name,target,paragraphs,expected)
                        return xml
                    def list_undo(name,path,position):
                        revision=data()['editing']['revision'];key('z',('Control_L',))
                        wait(lambda:data()['editing']['revision']!=revision)
                        check_text(original,position)
                        wait(lambda:data()['editing']['cells']==path)
                        assert save(name)[1]==original_xml and parts(name)==original_parts
                        assert not data()['editing']['can_undo'] and data()['editing']['can_redo']
                    path=list_pick();position=original.index('Český Beta')
                    revision=data()['editing']['revision'];key('Tab',('Shift_L',))
                    assert save('list-key-noop')[1]==original_xml and not data()['editing']['can_undo']
                    # Keyboard preparation/cancellation advances the view revision
                    # while restoring the original package and history.
                    revision=data()['editing']['revision']
                    menu_action('Outdent')
                    assert data()['editing']['revision']==revision and not data()['editing']['can_undo']
                    assert save('list-noop')[1]==original_xml
                    menu_action('Indent');wait(lambda:data()['modified'])
                    changed_xml=check_list('list-menu-indent','indent')
                    cursor=data()['editing']['cursor'];assert cursor>position
                    menu_action('Outdent');check_list('list-menu-outdent','outdent')
                    key('z',('Control_L',));assert save('list-outdent-undone')[1]==changed_xml
                    list_undo('list-menu-indent-undone',path,position)
                    key('Tab');wait(lambda:data()['modified'])
                    check_list('list-key-indent','indent')
                    key('Tab',('Shift_L',));check_list('list-key-outdent','outdent')
                    key('z',('Control_L',))
                    list_undo('list-key-indent-undone',path,position)
                    path=list_pick('Gamma');key('Right',count=len('Gamma')+1)
                    blank=original.index('Gamma')+len('Gamma')+1
                    check_text(original,blank)
                    key('Return');wait(lambda:data()['modified'])
                    check_list('list-empty-exit','exit')
                    list_undo('list-empty-exit-undone',path,blank)
                    key('q',('Control_L',));assert process.wait(timeout=15)==0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,
                        'local_list_menu_and_keys':True,'empty_exit':True,'noop_and_exact_history':True},indent=2)+'\n')
                    print('PASS local repeated/nested list menu, keyboard, empty exit, no-op and exact history',flush=True)
                    raise FormatComplete
                if args.repeated_format_only:
                    leaf='Český link' if args.nested_fixture else 'Cell link   \tend\nsoft'
                    prefix='Český' if args.nested_fixture else 'Cell'
                    expected_count=36 if args.nested_fixture else 3
                    def format_pick(ranged=True):
                        return pick_repeated(prefix,ranged)
                    def property_count(root,kind):
                        with zipfile.ZipFile(source) as archive:styles=ET.fromstring(archive.read('styles.xml'))
                        nodes=[*root.iter(STYLE+'style'),*styles.iter(STYLE+'style')];refs=set()
                        for style in nodes:
                            text=style.find(STYLE+'text-properties');para=style.find(STYLE+'paragraph-properties')
                            match=(kind=='color' and text is not None and text.get(FO+'color')=='#165aa7') or \
                                  (kind=='center' and para is not None and para.get(FO+'text-align')=='center') or \
                                  (kind=='tabs' and para is not None and bool(para.findall(STYLE+'tab-stops/'+STYLE+'tab-stop')))
                            if match:refs.add(style.get(STYLE+'name'))
                        for _ in nodes:
                            refs.update(s.get(STYLE+'name') for s in nodes if s.get(STYLE+'parent-style-name') in refs)
                        def count(node,factor=1):
                            if node.tag==TABLE+'table-row':factor*=int(node.get(TABLE+'number-rows-repeated','1'))
                            if node.tag==TABLE+'table-cell':factor*=int(node.get(TABLE+'number-columns-repeated','1'))
                            if node.tag in (TEXT+'p',TEXT+'h'):
                                return factor if plain(node)==leaf and (kind=='all' or any(e.get(TEXT+'style-name') in refs for e in node.iter())) else 0
                            return sum(count(c,factor) for c in node)
                        return count(root)
                    def format_save(name,kind,count=1,styles=False):
                        root,xml=save(name,(lambda _:None) if styles else None)
                        assert property_count(root,'all')==expected_count,(name,'lost repeated content')
                        assert property_count(root,kind)==count,(name,kind,property_count(root,kind))
                        return xml,parts(name)
                    def format_restore(name,path):
                        key('z',('Control_L',));check_text(original)
                        wait(lambda:data()['editing']['cells']==path)
                        assert save(name)[1]==original_xml and parts(name)==original_parts
                        assert not data()['editing']['can_undo']
                    def open_character():
                        key('d',('Control_L',));wait(lambda:data()['format_dialog']);return panel_origin(560,390)
                    path=format_pick();x,y=open_character();field(x+220,y+225,'#165aa7')
                    key('Escape');wait(lambda:not data()['format_dialog'])
                    assert save('repeat-format-cancelled')[1]==original_xml and parts('repeat-format-cancelled')==original_parts
                    x,y=open_character();field(x+220,y+225,'#165aa7');click(x+480,y+355)
                    wait(lambda:not data()['format_dialog'] and data()['modified'])
                    colored,colored_parts=format_save('repeat-format-color','color')
                    revision=data()['editing']['revision'];x,y=open_character();click(x+480,y+355)
                    wait(lambda:not data()['format_dialog']);assert data()['editing']['revision']==revision
                    assert save('repeat-format-noop')[1]==colored and parts('repeat-format-noop')==colored_parts
                    format_restore('repeat-format-color-undone',path)
                    path=format_pick(False);key('e',('Control_L',));wait(lambda:data()['modified'])
                    format_save('repeat-format-centered','center');format_restore('repeat-format-center-undone',path)
                    path=format_pick(False);menu_action('Tabs…');wait(lambda:data()['format_dialog'])
                    x,y=panel_origin(680,500);click(x+65,y+325);time.sleep(.2)
                    cx,cy=panel_origin(640,400);field(cx+225,cy+65,'30');click(cx+565,cy+365);time.sleep(.2)
                    click(x+605,y+465);wait(lambda:not data()['format_dialog'] and data()['modified'])
                    format_save('repeat-format-tabs','tabs');format_restore('repeat-format-tabs-undone',path)
                    path=format_pick(False);menu_action('Tabs…');wait(lambda:data()['format_dialog'])
                    x,y=panel_origin(680,500);field(x+225,y+385,'20');click(x+605,y+465)
                    wait(lambda:not data()['format_dialog'] and data()['modified'])
                    root,_=save('repeat-format-defaults',lambda _:None)
                    assert parts('repeat-format-defaults')[0]==original_parts[0]
                    format_restore('repeat-format-defaults-undone',path)
                    for character,assign in [(True,False),(True,True),(False,True)]:
                        path=format_pick();menu_action('Character styles…' if character else 'Paragraph styles…')
                        wait(lambda:data()['format_dialog']);x,y=panel_origin(680,480);click(x+65,y+390);time.sleep(.2)
                        cx,cy=panel_origin(650,340 if character else 460)
                        field(cx+300,cy+65,'Repeated character' if character else 'Repeated paragraph')
                        click(cx+250,cy+(155 if character else 235));time.sleep(.2)
                        fx,fy=panel_origin(560,390);field(fx+220,fy+225,'#165aa7');click(fx+480,fy+355);time.sleep(.2)
                        click(cx+575,cy+(305 if character else 425));time.sleep(.2)
                        if not assign:click(x+35,y+430)
                        click(x+605,y+445);wait(lambda:not data()['format_dialog'] and data()['modified'])
                        name='repeat-format-'+('definition' if not assign else 'named-character' if character else 'named-paragraph')
                        format_save(name,'color',1 if assign else 0,True)
                        if not assign:assert parts(name)[0]==original_parts[0]
                        format_restore(name+'-undone',path)
                    capture_window('repeated-format-restored')
                    key('q',('Control_L',));assert process.wait(timeout=15)==0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,
                        'local_character_paragraph_tabs_named':True,'global_definitions_and_defaults':True,
                        'cancel_noop_and_exact_history':True,'nested':bool(args.nested_fixture)},indent=2)+'\n')
                    print('PASS local repeated-cell character, paragraph, tabs and named formats; global defaults, no-op/cancel and exact history',flush=True)
                    raise FormatComplete
                if args.repeated_edit_only:
                    first_text='Cell link   \tend\nsoft'
                    def column_texts(root,column):
                        table=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Cells')
                        result=[]
                        for row in table.findall(TABLE+'table-row'):
                            cells=row.findall(TABLE+'table-cell')
                            value='\n'.join(plain(p) for p in cells[column].iter(TEXT+'p'))
                            result.extend([value]*int(row.get(TABLE+'number-rows-repeated','1')))
                        return result
                    def changed(name,column,expected):
                        root,xml=save(name)
                        texts=column_texts(root,column)
                        assert len(texts)==3 and texts[1]==expected,texts
                        assert texts[0]==texts[2]==(first_text if column==0 else 'Other cell'),texts
                        assert data()['text']=='\n'.join(plain(p) for p in root.iter() if p.tag in (TEXT+'p',TEXT+'h'))
                        wait(lambda:data()['editing']['cell']==[0,1,column,1,1])
                        return xml
                    def restored(name,position,column=0):
                        revision=data()['editing']['revision'];key('z',('Control_L',))
                        wait(lambda:data()['editing']['revision']!=revision)
                        check_text(original,position)
                        wait(lambda:data()['editing']['cell']==[0,1,column,1,1])
                        assert save(name)[1]==original_xml and parts(name)==original_parts
                        assert not data()['editing']['can_undo'] and data()['editing']['can_redo']
                    repeated_click(1,0,True);key('Home')
                    cell_start=original.index('Cell ');check_text(original,cell_start)
                    # Joining across the previous cell/body boundary rejects
                    # the entire event, including speculative materialization.
                    key('BackSpace');check_text(original,cell_start)
                    wait(lambda:data()['editing']['cell']==[0,1,0,1,1])
                    assert not data()['modified'] and not data()['editing']['can_undo']
                    assert save('repeated-boundary-rejected')[1]==original_xml
                    key('Right',count=3);check_text(original,cell_start+3)
                    key('BackSpace');changed('repeated-direct-backspace',0,first_text[:2]+first_text[3:])
                    restored('repeated-direct-backspace-undone',cell_start+3)
                    key('Delete');changed('repeated-direct-delete',0,first_text[:3]+first_text[4:])
                    restored('repeated-direct-delete-undone',cell_start+3)
                    revision=data()['editing']['revision'];key('x')
                    wait(lambda:data()['editing']['revision']!=revision and data()['modified'])
                    typed=changed('repeated-typed',0,first_text[:3]+'x'+first_text[3:])
                    key('BackSpace');changed('repeated-backspaced',0,first_text)
                    key('z',('Control_L',));assert save('repeated-backspace-undone')[1]==typed
                    restored('repeated-typing-undone',cell_start+3)
                    key('y',('Control_L',));assert save('repeated-typing-redone')[1]==typed
                    restored('repeated-typing-restored',cell_start+3)
                    repeated_click(1,0,True);key('Home');check_text(original,cell_start)
                    key('Right',('Shift_L',),count=4);key('x',('Control_L',))
                    changed('repeated-cut',0,first_text[4:])
                    restored('repeated-cut-undone',cell_start+4)
                    # The global menu path bypasses the editor's key handler.
                    repeated_click(1,0,True);key('Home')
                    key('Right',('Shift_L',),count=4);menu_action('Cut')
                    changed('repeated-menu-cut',0,first_text[4:])
                    restored('repeated-menu-cut-undone',cell_start+4)
                    # Cutting copied Cell; deliver it asynchronously into just
                    # the clicked instance of the other repeated column.
                    repeated_click(1,1,True);key('Home')
                    other=original.index('Other cell');check_text(original,other)
                    key('v',('Control_L',));wait(lambda:data()['modified'])
                    pasted=changed('repeated-pasted',1,'CellOther cell')
                    restored('repeated-paste-undone',other,1)
                    key('y',('Control_L',));assert save('repeated-paste-redone')[1]==pasted
                    restored('repeated-all-restored',other,1)
                    for name,modifiers in [('paragraph',()),('soft-break',('Shift_L',))]:
                        repeated_click(1,0,True);key('Home');key('Right',count=3)
                        key('Return',modifiers)
                        changed('repeated-'+name,0,first_text[:3]+'\n'+first_text[3:])
                        root,_=save('repeated-'+name+'-structure')
                        table=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Cells')
                        selected=table.findall(TABLE+'table-row')[1].findall(TABLE+'table-cell')[0]
                        assert len(selected.findall(TEXT+'p'))==(2 if name=='paragraph' else 1)
                        restored('repeated-'+name+'-undone',cell_start+3)
                    capture_window('repeated-edit-restored')
                    key('q',('Control_L',));assert process.wait(timeout=15)==0
                    if args.nested_fixture:
                        source=art/'nested-edit.odt';source.write_bytes(args.nested_fixture.read_bytes())
                        report=art/'nested-report.json'
                        with zipfile.ZipFile(source) as archive:
                            original_styles=archive.read('styles.xml')
                        process=start([args.bin_dir.resolve()/'calligra-words-fltk',source,'--report',report],client,'nested-words')
                        wait(first_report)
                        if args.wayland:
                            window=wait(lambda:next((w for w in scene()['windows'] if w.get('client_pid')==process.pid),None))
                            request=urllib.request.Request(f'http://127.0.0.1:{port}/api/action',
                                data=json.dumps({'op':'focus','id':window['id']}).encode(),headers={'Content-Type':'application/json'})
                            with urllib.request.urlopen(request,timeout=2) as response:response.read()
                            wait(lambda:scene()['focused']==window['id'])
                        else:
                            host=wait(lambda:next((w for w in xd.screen().root.query_tree().children
                                if 'Calligra Words' in str(w.get_wm_name())),None))
                            host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
                        snapshot=settled_page();original=snapshot['text']
                        root,original_xml=save('nested-original');original_parts=parts('nested-original')
                        page=data()['page_layout'];boxes=page['table_cells']
                        ids={c['table'] for c in boxes};assert len(ids)==2,ids
                        outer_id=max(ids,key=lambda id:max(c['width'] for c in boxes if c['table']==id))
                        inner_id=next(id for id in ids if id!=outer_id)
                        outer=next(c for c in boxes if c['table']==outer_id and c['row']==1 and c['column']==2)
                        inner=next(c for c in boxes if c['table']==inner_id and c['row']==1 and c['column']==1
                            and outer['x']<=c['x']<outer['x']+outer['width']
                            and outer['y']<=c['y']<outer['y']+outer['height'])
                        vx,vy,vw,vh=page['viewport'];zoom=max(.05,min(1.5,(vw-48)/max(p['width'] for p in page['paper_styles'])))
                        left=vx+(vw-inner['page_width']*zoom)/2;top=vy+12-page['scroll']
                        click(round(left+(inner['x']+1)*zoom),round(top+(inner['y']+3)*zoom))
                        expected_cells=[[outer_id,1,2,1,1],[inner_id,1,1,1,1]]
                        wait(lambda:data()['editing']['cells']==expected_cells)
                        key('Home');key('Right',count=3);position=original.index('Český')+3
                        check_text(original,position)
                        key('x');wait(lambda:data()['modified'])
                        root,nested_typed=save('repeated-nested-typed')
                        def visible_text(node,value,factor=1):
                            if node.tag==TABLE+'table-row':factor*=int(node.get(TABLE+'number-rows-repeated','1'))
                            if node.tag==TABLE+'table-cell':factor*=int(node.get(TABLE+'number-columns-repeated','1'))
                            if node.tag==TEXT+'p':return factor if plain(node)==value else 0
                            return sum(visible_text(c,value,factor) for c in node)
                        assert visible_text(root,'Česxký link')==1 and visible_text(root,'Český link')==35
                        assert [c[1:] for c in data()['editing']['cells']]==[[1,2,1,1],[1,1,1,1]]
                        key('z',('Control_L',));check_text(original,position)
                        wait(lambda:data()['editing']['cells']==expected_cells)
                        assert save('nested-typing-undone')[1]==original_xml
                        assert parts('nested-typing-undone')==original_parts
                        key('y',('Control_L',));assert save('nested-typing-redone')[1]==nested_typed
                        assert [c[1:] for c in data()['editing']['cells']]==[[1,2,1,1],[1,1,1,1]]
                        capture_window('nested-repeated-typed')
                        key('q',('Control_L',));assert process.wait(timeout=15)==0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,
                        'local_preexisting_repeated_cell_typing':True,'local_backspace_cut_paste':True,
                        'local_direct_delete':True,'rejected_boundary_atomic':True,'paragraph_soft_break':True,
                        'menu_cut':True,'one_step_exact_archive_history':True,'clicked_cell_restored':True,
                        'nested_repeated_ancestors':bool(args.nested_fixture)},indent=2)+'\n')
                    print('PASS preexisting repeated-cell typing, deletion, clipboard, paragraph/soft breaks and exact source/cell undo',flush=True)
                    raise FormatComplete
                repeated_click(1,0); clicked=data()['editing']['cursor']
                repeat_action('Insert below',[0,2,0,1,1])
                root,repeat_xml=save('grid-repeat-row-added')
                cells=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Cells')
                assert len(cells.findall('.//'+TABLE+'table-row'))==4
                repeat_undo('grid-repeat-row-undone',clicked)
                key('y',('Control_L',));wait(lambda:data()['editing']['cell']==[0,2,0,1,1])
                assert save('grid-repeat-row-redone')[1]==repeat_xml
                repeat_undo('grid-repeat-row-restored',clicked)
                repeated_click(1,0); clicked=data()['editing']['cursor']
                repeat_action('Insert right',[0,1,1,1,1])
                root,column_xml=save('grid-repeat-column-added')
                row=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Cells').find(TABLE+'table-row')
                assert len(row.findall(TABLE+'table-cell'))==3
                before_typing=data()['text'];cursor=data()['editing']['cursor']
                key('x');check_text(before_typing[:cursor]+'x'+before_typing[cursor:],cursor+1)
                root,_=save('grid-repeat-column-typed')
                table=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Cells')
                visible_x=0
                for r in table.findall(TABLE+'table-row'):
                    for c in r.findall(TABLE+'table-cell'):
                        if any(plain(p)=='x' for p in c.iter(TEXT+'p')):
                            visible_x+=int(r.get(TABLE+'number-rows-repeated','1'))*int(c.get(TABLE+'number-columns-repeated','1'))
                assert visible_x==1,visible_x
                key('z',('Control_L',));check_text(before_typing)
                assert save('grid-repeat-typing-undone')[1]==column_xml
                repeat_undo('grid-repeat-column-undone',clicked)
                repeated_click(1,0);clicked=data()['editing']['cursor']
                repeat_action('Delete row',[0,1,0,1,1])
                root,_=save('grid-repeat-row-deleted')
                row=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Cells').find(TABLE+'table-row')
                rows=next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Cells').findall(TABLE+'table-row')
                assert sum(int(r.get(TABLE+'number-rows-repeated','1')) for r in rows)==2
                repeat_undo('grid-repeat-delete-undone',clicked)
                capture_window('grid-repeat-restored-layout')
                key('q',('Control_L',));assert process.wait(timeout=15)==0
                (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,'native_repeated_grid':True,
                    'clicked_instance_and_undo':True,'source_archive_exact':True},indent=2)+'\n')
                print('PASS repeated table row targeting, insertion/deletion, source/archive undo and visual cell restoration',flush=True)
                raise FormatComplete
            if not (args.tables_only or args.tabs_only or args.page_setup_only):
                def grid_table(root):
                    return next(t for t in root.iter(TABLE+'table') if t.get(TABLE+'name')=='Cells')
                def grid_rows(root):
                    table=grid_table(root)
                    return table.findall('.//'+TABLE+'table-row')
                def grid_cell_text(root,row,column):
                    cells=grid_rows(root)[row].findall(TABLE+'table-cell')
                    return '\n'.join(plain(p) for p in cells[column].iter(TEXT+'p'))
                def grid_action(label,cell):
                    revision=data()['editing']['revision']; menu_action(label)
                    wait(lambda:data()['editing']['revision']!=revision)
                    if not args.text_view:
                        wait(lambda:data()['editing']['cell']==cell)
                def grid_undo(expected,cell):
                    revision=data()['editing']['revision']; key('z',('Control_L',))
                    wait(lambda:data()['editing']['revision']!=revision)
                    check_text(expected)
                    if not args.text_view:wait(lambda:data()['editing']['cell']==cell)
                # Insert and delete on the existing rich table, keeping its
                # hyperlink, tabs/soft breaks and all surrounding document text.
                go(original.index('Cell ')+1)
                grid_action('Insert below',[0,1,0,1,1])
                root,_=save('grid-row-below'); assert len(grid_rows(root))==2
                grid_action('Insert above',[0,1,0,1,1])
                root,_=save('grid-row-above'); assert len(grid_rows(root))==3
                grid_action('Insert right',[0,1,1,1,1])
                root,_=save('grid-column-right')
                assert all(len(r.findall(TABLE+'table-cell'))==3 for r in grid_rows(root))
                before_typing=data()['text']; cursor=data()['editing']['cursor']
                key('x'); expanded=before_typing[:cursor]+'x'+before_typing[cursor:]
                check_text(expanded,cursor+1)
                root,expanded_xml=save('grid-expanded'); expanded_parts=parts('grid-expanded')
                assert grid_cell_text(root,1,1)=='x'
                assert grid_cell_text(root,0,0)=='Cell link   \tend\nsoft'
                assert grid_cell_text(root,0,2)=='Other cell'
                assert len(root.findall('.//'+TEXT+'a'))==1
                view_labels('grid-expanded-layout')
                grid_action('Delete column',[0,1,1,1,1])
                root,_=save('grid-column-deleted'); assert all(len(r.findall(TABLE+'table-cell'))==2 for r in grid_rows(root))
                grid_undo(expanded,[0,1,1,1,1]); assert save('grid-column-delete-undone')[1]==expanded_xml
                assert parts('grid-column-delete-undone')==expanded_parts
                grid_action('Delete row',[0,1,1,1,1])
                root,_=save('grid-row-deleted'); assert len(grid_rows(root))==2
                grid_undo(expanded,[0,1,1,1,1]); assert save('grid-row-delete-undone')[1]==expanded_xml
                assert parts('grid-row-delete-undone')==expanded_parts
                grid_action('Insert left',[0,1,1,1,1])
                root,_=save('grid-column-left'); assert all(len(r.findall(TABLE+'table-cell'))==4 for r in grid_rows(root))
                assert grid_cell_text(root,1,1)=='' and grid_cell_text(root,1,2)=='x'
                grid_undo(expanded,[0,1,1,1,1]); assert save('grid-column-left-undone')[1]==expanded_xml
                # One character, column insert, and two row inserts are four
                # independent history steps; the original ZIP parts are exact.
                key('z',('Control_L',)); check_text(before_typing)
                key('z',('Control_L',)); key('z',('Control_L',)); key('z',('Control_L',))
                check_text(original)
                assert save('grid-all-restored')[1]==original_xml
                assert parts('grid-all-restored')==original_parts
                layout_report['table_grid']={'six_menu_actions':True,'rich_cell_retention':True,
                    'source_archive_history':True,'caret_cell_after_action_undo':True}
                if args.grid_only:
                    key('q',('Control_L',)); assert process.wait(timeout=15)==0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,'native_table_grid':True,
                        'page_layout':layout_report,'full_native_editor_regressions':False},indent=2)+'\n')
                    print('PASS native Words row/column menu actions, rich cells, source/archive undo and cell focus',flush=True)
                    raise FormatComplete
            if not (args.page_setup_only or args.tables_only or args.grid_only):
                # Direct tab stops stage one transaction, including document defaults.
                def open_tabs():
                    menu_action('Tabs…'); wait(lambda:data()['format_dialog']); time.sleep(.2)
                    return panel_origin(680,500)
                def tab_child(x,y,edit=False,row=0):
                    if edit: click(x+100,y+62+row*18)
                    click(x+(165 if edit else 65),y+325); time.sleep(.2)
                    return panel_origin(640,400)
                def stop_nodes(root):
                    paragraph=next(e for e in root.iter(TEXT+'p') if e.get(XML+'id')=='first')
                    style=next(e for e in root.iter(STYLE+'style') if e.get(STYLE+'name')==paragraph.get(TEXT+'style-name'))
                    return style.findall('./'+STYLE+'paragraph-properties/'+STYLE+'tab-stops/'+STYLE+'tab-stop')
                def tabs_save(name,styles):
                    root,xml=save(name,exact_styles(styles) if styles is not None else lambda _:None)
                    assert data()['text']==tab_text
                    assert next(e for e in root.iter(TEXT+'p') if e.get(XML+'id')=='first') is not None
                    return root,xml,parts(name)
                go(5); key('Tab'); tab_text=original[:5]+'\t'+original[5:]
                check_text(tab_text,6)
                _,tab_xml=save('tabs-source'); tab_parts=parts('tabs-source')
                go(0); key('Right',('Shift_L',),count=13)
                selected={k:data()['editing'][k] for k in ('anchor','cursor')}
                revision=data()['editing']['revision']
                x,y=open_tabs(); click(x+605,y+465); wait(lambda:not data()['format_dialog'])
                assert tabs_save('tabs-no-op',original_styles)[2]==tab_parts
                assert data()['editing']['revision']==revision
                # Adding and deleting within the staged dialog is an exact no-op.
                x,y=open_tabs(); cx,cy=tab_child(x,y); field(cx+225,cy+65,'60')
                click(cx+565,cy+365); time.sleep(.2); click(x+265,y+325)
                click(x+605,y+465); wait(lambda:not data()['format_dialog'])
                assert tabs_save('tabs-staged-no-op',original_styles)[2]==tab_parts
                assert data()['editing']['revision']==revision
                x,y=open_tabs(); cx,cy=tab_child(x,y); field(cx+225,cy+65,'60')
                choice(cx+515,cy+110,2); field(cx+220,cy+155,'-1')
                click(cx+565,cy+365); time.sleep(.3)
                assert data()['format_dialog'] and data()['editing']['revision']==revision
                capture_window('tabs-invalid-width')
                field(cx+220,cy+155,'0.8'); click(cx+435,cy+155)
                field(cx+250,cy+200,'#0066cc'); capture_window('tabs-stop-dialog')
                click(cx+565,cy+365); time.sleep(.2)
                # Duplicate numeric positions are rejected inside the child panel.
                cx,cy=tab_child(x,y); field(cx+225,cy+65,'60'); click(cx+565,cy+365); time.sleep(.2)
                assert data()['editing']['revision']==revision
                capture_window('tabs-duplicate-position')
                field(cx+225,cy+65,'90'); choice(cx+515,cy+65,3)
                choice(cx+515,cy+110,8); field(cx+515,cy+200,'.')
                choice(cx+350,cy+245,1); click(cx+565,cy+365); time.sleep(.2)
                field(x+225,y+385,'-1'); click(x+605,y+465); time.sleep(.3)
                assert data()['format_dialog'] and data()['editing']['revision']==revision and not data()['modified']
                field(x+225,y+385,'20'); capture_window('tabs-dialog')
                click(x+605,y+465); wait(lambda:not data()['format_dialog'] and data()['modified'])
                root,_,created=tabs_save('tabs-created',None)
                stops=stop_nodes(root); assert len(stops)==2
                assert abs(float(stops[0].get(STYLE+'position')[:-2])-60*72/25.4)<1e-5
                assert stops[0].get(STYLE+'leader-style')=='dotted' and stops[0].get(STYLE+'leader-type')=='double'
                assert stops[0].get(STYLE+'leader-width')=='0.8pt' and stops[0].get(STYLE+'leader-color')=='#0066cc'
                assert stops[1].get(STYLE+'type')=='char' and stops[1].get(STYLE+'char')=='.'
                assert stops[1].get(STYLE+'leader-text')=='.' and stops[1].get(STYLE+'leader-text-style')
                default=ET.fromstring(created[1]).find('.//'+STYLE+'default-style/'+STYLE+'paragraph-properties')
                assert abs(float(default.get(STYLE+'tab-stop-distance')[:-2])-20*72/25.4)<1e-5
                assert default.get(TEXT+'relative-tab-stop-position') is None
                assert root.get(O+'version')=='1.2'
                assert {k:data()['editing'][k] for k in selected}==selected
                view_labels('tabs-created-layout')
                leaders=[t for t in data()['page_layout']['tabs'] if t['source']==5]
                assert leaders and leaders[0]['strokes']>0,leaders
                key('z',('Control_L',)); assert tabs_save('tabs-created-undone',original_styles)[2]==tab_parts
                key('y',('Control_L',)); assert tabs_save('tabs-created-redone',created[1])[2]==created
                # A changed stop, deletion, defaults and inherited controls remain
                # local until Apply; Cancel keeps both the source and redo state.
                x,y=open_tabs(); cx,cy=tab_child(x,y,edit=True)
                field(cx+225,cy+65,'65'); click(cx+565,cy+365); time.sleep(.2)
                click(x+500,y+465); wait(lambda:not data()['format_dialog'])
                assert tabs_save('tabs-staged-cancelled',created[1])[2]==created
                x,y=open_tabs(); cx,cy=tab_child(x,y,edit=True)
                field(cx+225,cy+65,'65'); choice(cx+515,cy+65,1)
                choice(cx+515,cy+110,7,current=2); click(cx+565,cy+365); time.sleep(.2)
                click(x+605,y+465); wait(lambda:not data()['format_dialog'] and data()['modified'])
                root,_,edited=tabs_save('tabs-edited',created[1])
                stops=stop_nodes(root); assert len(stops)==2
                assert stops[0].get(STYLE+'type')=='right' and stops[0].get(STYLE+'leader-style')=='wave'
                view_labels('tabs-edited-layout')
                x,y=open_tabs(); click(x+545,y+325); click(x+605,y+465)
                wait(lambda:not data()['format_dialog'] and data()['modified'])
                root,_,_=tabs_save('tabs-inherited',created[1]); assert not stop_nodes(root)
                key('z',('Control_L',)); assert tabs_save('tabs-inherited-undone',created[1])[2]==edited
                x,y=open_tabs(); click(x+370,y+325); click(x+605,y+465)
                wait(lambda:not data()['format_dialog'] and data()['modified'])
                root,_,_=tabs_save('tabs-cleared',created[1]); assert not stop_nodes(root)
                paragraph=next(e for e in root.iter(TEXT+'p') if e.get(XML+'id')=='first')
                style=next(e for e in root.iter(STYLE+'style') if e.get(STYLE+'name')==paragraph.get(TEXT+'style-name'))
                assert style.find('./'+STYLE+'paragraph-properties/'+STYLE+'tab-stops') is not None
                key('z',('Control_L',)); assert tabs_save('tabs-clear-undone',created[1])[2]==edited
                x,y=open_tabs(); click(x+100,y+62); click(x+265,y+325); click(x+605,y+465)
                wait(lambda:not data()['format_dialog'] and data()['modified'])
                root,_,_=tabs_save('tabs-deleted',created[1]); assert len(stop_nodes(root))==1
                key('z',('Control_L',)); assert tabs_save('tabs-delete-undone',created[1])[2]==edited
                key('z',('Control_L',)); assert tabs_save('tabs-edit-undone',created[1])[2]==created
                # Mixed selections retain their differing lists on unchanged Apply.
                select_all(); x,y=open_tabs(); capture_window('tabs-mixed-dialog')
                click(x+605,y+465); wait(lambda:not data()['format_dialog'])
                assert tabs_save('tabs-mixed-no-op',created[1])[2]==created
                key('z',('Control_L',)); assert tabs_save('tabs-all-restored',original_styles)[2]==tab_parts
                key('z',('Control_L',)); check_text(original)
                assert save('tabs-source-restored')[1]==original_xml
                assert parts('tabs-source-restored')==original_parts
                layout_report['tab_stops']={'staged_no_op_cancel':True,'invalid_correction':True,'position_alignment_leaders':True,
                    'document_defaults':True,'older_version_retained':True,'mixed_no_op':True,'clear_inherit_delete':True,
                    'one_step_exact_part_undo':True,'native_leader_strokes':True}
                if args.tabs_only:
                    key('q',('Control_L',)); assert process.wait(timeout=15)==0
                    (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,'native_tab_stops':True,
                        'page_layout':layout_report,'full_native_editor_regressions':False},indent=2)+'\n')
                    print('PASS native Words tab controls, leaders, defaults, no-op/cancel and exact archive-part history',flush=True)
                    raise FormatComplete
            # Page setup changes the active master layout as one archive-part
            # transaction; it does not rewrite body text or paragraph styles.
            def open_page_setup():
                menu_action('Page setup…'); wait(lambda: data()['format_dialog']); time.sleep(.2)
                return panel_origin(620,420)
            def page_geometry_check(width, height, left, right, top, bottom, orientation):
                def check(raw):
                    tree=ET.fromstring(raw)
                    for group in ('styles','master-styles'):
                        assert canonical(tree.find(O+group)) == canonical(original_style_tree.find(O+group))
                    layouts=tree.findall('.//'+STYLE+'page-layout')
                    assert len(layouts)==1 and layouts[0].get(STYLE+'name')=='Page'
                    props=layouts[0].find(STYLE+'page-layout-properties')
                    expected={'page-width':width,'page-height':height,'margin-left':left,'margin-right':right,'margin-top':top,'margin-bottom':bottom}
                    for attr,value in expected.items():
                        raw_value=props.get(FO+attr)
                        if raw_value is not None:
                            unit=raw_value[-2:]; scale={'pt':1,'cm':72/2.54,'mm':72/25.4}[unit]
                            assert abs(float(raw_value[:-2])*scale-value*72/25.4)<.00001,(attr,raw_value,value)
                        else:
                            raw_margin=props.get(FO+'margin'); assert attr.startswith('margin-') and raw_margin is not None
                            scale={'pt':1,'cm':72/2.54,'mm':72/25.4}[raw_margin[-2:]]
                            assert abs(float(raw_margin[:-2])*scale-value*72/25.4)<.00001
                    if orientation is not None: assert props.get(STYLE+'print-orientation')==orientation
                return check
            go(6); key('Right', ('Shift_L',), count=5)
            selected={'anchor':6,'cursor':11}
            wait(lambda:all(data()['editing'][k]==value for k,value in selected.items()))
            initial_revision=data()['editing']['revision']
            x,y=open_page_setup(); click(x+545,y+385)
            wait(lambda:not data()['format_dialog'])
            assert not data()['modified'] and data()['editing']['revision']==initial_revision
            assert save('page-setup-noop')[1]==original_xml and parts('page-setup-noop')==original_parts
            x,y=open_page_setup(); field(x+210,y+195,'18')
            key('Escape'); wait(lambda:not data()['format_dialog'])
            assert save('page-setup-cancelled')[1]==original_xml and parts('page-setup-cancelled')==original_parts
            x,y=open_page_setup(); choice(x+230,y+100,3,current=0)  # Custom -> Letter
            choice(x+490,y+100,1,current=0)  # Portrait -> Landscape
            field(x+210,y+195,'300'); field(x+490,y+195,'12')
            field(x+210,y+240,'16'); field(x+490,y+240,'20')
            click(x+545,y+385); time.sleep(.3)
            assert data()['format_dialog'] and not data()['modified'] and data()['editing']['revision']==initial_revision
            capture_window('page-setup-invalid-margins')
            # The page's own margins fit, but the first paragraph's 5pt
            # inset cannot fit a 1mm paper width. Native pagination must reject
            # the candidate before the source/history transaction commits.
            field(x+210,y+195,'0'); field(x+490,y+195,'0'); field(x+210,y+145,'1')
            click(x+545,y+385); time.sleep(.3)
            assert data()['format_dialog'] and not data()['modified'] and data()['editing']['revision']==initial_revision
            capture_window('page-setup-unprintable-width')
            field(x+210,y+195,'18'); field(x+490,y+195,'12'); field(x+210,y+145,'oops')
            click(x+545,y+385); time.sleep(.3)
            assert data()['format_dialog'] and not data()['modified']
            field(x+210,y+145,'279.4'); capture_window('page-setup-dialog')
            click(x+545,y+385); wait(lambda:not data()['format_dialog'] and data()['modified'])
            _,landscape_xml=save('page-setup-letter-landscape',page_geometry_check(279.4,215.9,18,12,16,20,'landscape'))
            landscape_parts=parts('page-setup-letter-landscape')
            assert landscape_xml==original_xml and landscape_parts[1]!=original_parts[1]
            assert {k:data()['editing'][k] for k in selected}==selected
            paper=wait(lambda:data()['page_layout'].get('paper_styles'))[0]
            for attr,value in [('width',279.4),('height',215.9),('left',18),('right',12),('top',16),('bottom',20)]:
                assert abs(paper[attr]-value*72/25.4)<.00001,(attr,paper[attr])
            view_labels('page-setup-landscape-layout')
            key('z', ('Control_L',)); assert save('page-setup-landscape-undone')[1]==original_xml
            assert parts('page-setup-landscape-undone')==original_parts
            key('y', ('Control_L',)); assert save('page-setup-landscape-redone',exact_styles(landscape_parts[1]))[1]==original_xml
            assert parts('page-setup-landscape-redone')==landscape_parts
            key('z', ('Control_L',)); assert save('page-setup-landscape-restored')[1]==original_xml
            # A margin-only change keeps untouched dimensions in their source
            # cm units and does not invent a print-orientation attribute.
            x,y=open_page_setup(); field(x+210,y+195,'25'); click(x+545,y+385)
            wait(lambda:not data()['format_dialog'] and data()['modified'])
            def margin_only(raw):
                page_geometry_check(180,240,25,10,10,10,None)(raw)
                props=ET.fromstring(raw).find('.//'+STYLE+'page-layout-properties')
                assert props.get(FO+'page-width')=='18cm' and props.get(FO+'page-height')=='24cm'
                assert props.get(FO+'margin')=='1cm' and props.get(STYLE+'print-orientation') is None
            assert save('page-setup-margin-only',margin_only)[1]==original_xml
            key('z', ('Control_L',)); assert save('page-setup-margin-restored')[1]==original_xml
            x,y=open_page_setup(); choice(x+230,y+100,1,current=0); click(x+545,y+385)
            wait(lambda:not data()['format_dialog'] and data()['modified'])
            assert save('page-setup-a4-portrait',page_geometry_check(210,297,10,10,10,10,'portrait'))[1]==original_xml
            view_labels('page-setup-a4-layout')
            key('z', ('Control_L',)); assert save('page-setup-all-restored')[1]==original_xml
            assert parts('page-setup-all-restored')==original_parts
            layout_report['page_setup']={'no_op_cancel':True,'invalid_correction':True,'paper_orientation_margins':True,
                'native_geometry':True,'failed_pagination_rejected':True,'body_retention':True,'one_step_exact_part_undo':True,'unchanged_unit_retention':True}
            if args.page_setup_only:
                key('q', ('Control_L',)); assert process.wait(timeout=15)==0
                (art/'results.json').write_text(json.dumps({'native_wayland':args.wayland,'native_page_setup':True,
                    'page_layout':layout_report,'full_native_editor_regressions':False},indent=2)+'\n')
                print('PASS native Words page setup UI, presets/orientation, margins, source retention and exact archive-part undo',flush=True)
                raise FormatComplete
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
        if args.rudimentary_only:
            caret = data()['page_layout']['caret_rect']
            px, py = round(caret[0]), round(caret[1]+caret[3]/2)
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
        if args.rudimentary_only:
            capture_window('rudimentary-edited')
            key('q', ('Control_L',)); assert process.wait(timeout=15) == 0
            reopened = art / 'rudimentary-reopened.json'
            process = start([args.bin_dir.resolve() / 'calligra-words-fltk', source,
                             '--report', reopened, '--quit-after-ms', '1800'], client, 'rudimentary-reopened')
            assert process.wait(timeout=15) == 0
            restored = json.loads(reopened.read_text())
            assert restored['text'] == expected and not restored['modified']
            assert restored['page_layout']['visible'] and not restored['page_layout']['render_error']
            (art / 'results.json').write_text(json.dumps({
                'native_wayland': args.wayland, 'rudimentary_editing': True,
                'keyboard_and_caret_click': True, 'selection': True,
                'undo_redo': True, 'save_reopen': True, 'archive_retention': True,
                'advanced_regressions': False}, indent=2)+'\n')
            print('PASS rudimentary native Words typing, caret click, selection, undo/redo, save/reopen and archive retention', flush=True)
            raise FormatComplete
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
        # Independently produced short/tall neighboring cells, repeated headers,
        # nested table flow and a spanning source cell exercise vertical lanes.
        source = art / 'table-navigation.odt'
        doc = OpenDocumentText()
        layout = PageLayout(name='NavigationPage')
        layout.addElement(PageLayoutProperties(pagewidth='400pt', pageheight='220pt', margin='20pt'))
        doc.automaticstyles.addElement(layout)
        doc.masterstyles.addElement(MasterPage(name='Standard', pagelayoutname=layout))
        body = Style(name='NavigationBody', family='paragraph')
        body.addElement(ParagraphProperties(lineheight='16pt', margintop='0pt', marginbottom='0pt'))
        body.addElement(TextProperties(fontfamily='Hack', fontsize='12pt'))
        doc.styles.addElement(body)
        grid = Style(name='NavigationGrid', family='table')
        grid.addElement(TableProperties(align='margins'))
        doc.styles.addElement(grid)
        cell_style = Style(name='NavigationCell', family='table-cell')
        cell_style.addElement(TableCellProperties(padding='3pt', border='0.5pt solid #204080', backgroundcolor='#e5efff'))
        doc.styles.addElement(cell_style)
        paragraphs = []
        def paragraph(parent, value):
            p = P(stylename=body)
            for i, line in enumerate(value.split('\n')):
                if i: p.addElement(LineBreak())
                p.addText(line)
            parent.addElement(p)
            paragraphs.append(value)
        def cell(row, value=None, **attributes):
            c = TableCell(stylename=cell_style, **attributes)
            row.addElement(c)
            if value is not None: paragraph(c, value)
            return c
        def row(table):
            r = TableRow(); table.addElement(r); return r
        def table(parent, name):
            t = Table(name=name, stylename=grid)
            t.addElement(TableColumn(numbercolumnsrepeated=2)); parent.addElement(t); return t
        paragraph(doc.text, 'BEFORE TABLE')
        root_table = table(doc.text, 'Navigation')
        headers = TableHeaderRows(); root_table.addElement(headers)
        r = row(headers); cell(r, 'H LEFT'); cell(r, 'H RIGHT')
        r = row(root_table); cell(r, 'LEFT'); cell(r, 'RIGHT0\nRIGHT1\nRIGHT2')
        r = row(root_table); cell(r, 'NEXT LEFT'); cell(r, 'NEXT RIGHT')
        r = row(root_table); parent = cell(r, numbercolumnsspanned=2); r.addElement(CoveredTableCell())
        paragraph(parent, 'BEFORE NESTED')
        inner = table(parent, 'Inner')
        r = row(inner); cell(r, 'NESTED LEFT'); cell(r, 'NESTED RIGHT0\nNESTED RIGHT1\nNESTED RIGHT2')
        paragraph(parent, 'AFTER NESTED')
        r = row(root_table); cell(r, 'MERGED0\nMERGED1', numberrowsspanned=2); cell(r, 'SIDE3')
        r = row(root_table); r.addElement(CoveredTableCell()); cell(r, 'SIDE4\nSIDE4b')
        r = row(root_table); cell(r, 'AFTER MERGE'); cell(r, 'AFTER SIDE')
        paragraph(doc.text, 'AFTER TABLE')
        table_text = '\n'.join(paragraphs)
        doc.save(str(source))
        with zipfile.ZipFile(source, 'a') as archive:
            archive.writestr('Extras/opaque.bin', b'\0keep\xff')
            original_styles = archive.read('styles.xml')
        report = art / 'table-navigation-report.json'
        process = start([args.bin_dir.resolve() / 'calligra-words-fltk', source,
                         '--report', report], client, 'table-navigation')
        wait(lambda: report.exists() and data()['page_layout']['glyphs'] > 0)
        if args.wayland:
            host.set_input_focus(X.RevertToParent, X.CurrentTime); xd.sync()
            window = wait(lambda: next((w for w in scene()['windows']
                                       if 'calligra-words' in w.get('app_id', '').lower() and w.get('client_pid') == process.pid), None))
            request = urllib.request.Request(f'http://127.0.0.1:{port}/api/action',
                data=json.dumps({'op': 'focus', 'id': window['id']}).encode(),
                headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=2) as response: response.read()
            wait(lambda: scene()['focused'] == window['id'])
        else:
            host = wait(lambda: next((w for w in xd.screen().root.query_tree().children
                                     if 'Calligra Words' in str(w.get_wm_name())), None))
            host.set_input_focus(X.RevertToParent, X.CurrentTime); xd.sync()
        if args.ime:
            subprocess.run(['/usr/bin/fcitx5-remote', '-s', 'keyboard-us'], env=client, check=True)
            wait(lambda: input_method() == 'keyboard-us')
        check_text(table_text, 0)
        assert data()['page_layout']['pages'] >= 2 and not data()['page_layout']['render_error']
        _, original_table_xml = save('table-navigation-original')
        starts = {}
        offset = 0
        for i, value in enumerate(paragraphs):
            starts[value] = (i, offset)
            offset += len(value)+1
        def paragraph_start(value):
            i, position = starts[value]
            key('Home', ('Control_L',))
            key('Down', ('Control_L',), count=i)
            check_text(table_text, position)
        def at(value, offset=0):
            check_text(table_text, starts[value][1]+offset)
        paragraph_start('LEFT')
        key('Down'); at('NEXT LEFT')
        key('Up', ('Shift_L',)); at('LEFT')
        assert data()['editing']['anchor'] == starts['NEXT LEFT'][1]
        key('Delete'); time.sleep(.2)
        at('LEFT')
        assert not data()['modified'] and not data()['editing']['can_undo']
        assert save('table-navigation-rejected-range')[1] == original_table_xml
        key('Escape')
        paragraph_start('RIGHT0\nRIGHT1\nRIGHT2')
        key('Down'); at('RIGHT0\nRIGHT1\nRIGHT2', len('RIGHT0\n'))
        key('Down'); at('RIGHT0\nRIGHT1\nRIGHT2', len('RIGHT0\nRIGHT1\n'))
        key('Down'); at('NEXT RIGHT')
        key('Up'); at('RIGHT0\nRIGHT1\nRIGHT2', len('RIGHT0\nRIGHT1\n'))
        paragraph_start('BEFORE NESTED')
        key('Down'); at('NESTED LEFT')
        key('Down'); at('AFTER NESTED')
        key('Up'); at('NESTED LEFT')
        key('Up'); at('BEFORE NESTED')
        paragraph_start('NESTED RIGHT0\nNESTED RIGHT1\nNESTED RIGHT2')
        key('Down'); at('NESTED RIGHT0\nNESTED RIGHT1\nNESTED RIGHT2', len('NESTED RIGHT0\n'))
        key('Down'); at('NESTED RIGHT0\nNESTED RIGHT1\nNESTED RIGHT2', len('NESTED RIGHT0\nNESTED RIGHT1\n'))
        paragraph_start('MERGED0\nMERGED1')
        key('Down'); at('MERGED0\nMERGED1', len('MERGED0\n'))
        key('Down'); at('AFTER MERGE')
        key('Up'); at('MERGED0\nMERGED1', len('MERGED0\n'))
        paragraph_start('H LEFT')
        key('Down'); at('LEFT')
        paragraph_start('H RIGHT')
        key('Down'); at('RIGHT0\nRIGHT1\nRIGHT2')
        # A viewport jump into a nested/merged row retains its horizontal lane,
        # including a short cell whose only glyph line is on a prior page.
        paragraph_start('H LEFT')
        before = data()['page_layout']
        virtual_y = before['caret_rect'][1]+before['scroll']
        key('Page_Down')
        wait(lambda: data()['editing']['cursor'] > starts['H LEFT'][1])
        def current_paragraph():
            cursor = data()['editing']['cursor']
            return next(value for value in reversed(paragraphs) if starts[value][1] <= cursor)
        assert not any(token in current_paragraph() for token in ('RIGHT', 'SIDE')), current_paragraph()
        view = data()['page_layout']
        assert view['caret_rect'][1]+view['scroll'] > virtual_y+view['viewport'][3]*.35
        previous = data()['editing']['cursor']
        key('Page_Up'); wait(lambda: data()['editing']['cursor'] < previous)
        assert not any(token in current_paragraph() for token in ('RIGHT', 'SIDE')), current_paragraph()
        capture_window('table-native-navigation')
        assert not data()['modified']
        paragraph_start('NEXT LEFT')
        position = starts['NEXT LEFT'][1]
        key('x'); check_text(table_text[:position]+'x'+table_text[position:], position+1)
        key('z', ('Control_L',)); at('NEXT LEFT')
        key('y', ('Control_L',)); check_text(table_text[:position]+'x'+table_text[position:], position+1)
        save('table-navigation-edited-final')
        key('q', ('Control_L',)); assert process.wait(timeout=15) == 0
        results = {'native_wayland': args.wayland, 'external_producer': 'odfpy',
                   'native_character_paragraph_formatting': True,
                   'native_underline_strike_highlight': True,
                   'native_named_paragraph_styles': True,
                   'native_named_character_styles': True,
                   'native_page_setup': not args.page_only,
                   'font_validation_and_inline_correction': True,
                   'select_all_clears_typing_format': True,
                   'native_page_click_scalar_position': True,
                   'direct_native_page_editing': True,
                   'native_page_readonly': True,
                   'native_page_mouse_drag': True,
                   'native_page_table_cell_column_nested_merged_navigation': True,
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
except FormatComplete:
    pass
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
