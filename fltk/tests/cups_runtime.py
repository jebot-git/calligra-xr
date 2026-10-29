#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Private unprivileged CUPS scheduler and capture backend; never changes system queues."""
from pathlib import Path
import os
import pwd
import grp
import socket
import subprocess
import time

class PrivateCups:
    name = 'NativePrintTest'
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
    def __enter__(self):
        base = self.directory
        base.mkdir(parents=True, exist_ok=True)
        for name in ('etc', 'state', 'cache', 'spool', 'tmp', 'logs', 'bin', 'output'):
            (base / name).mkdir(exist_ok=True)
        for name in ('daemon', 'filter', 'driver', 'notifier', 'monitor'):
            (base / 'bin' / name).symlink_to('/usr/lib/cups/' + name, target_is_directory=True)
        backend = base / 'bin/backend'
        backend.mkdir()
        capture = backend / 'capture'
        capture.write_text('''#!/usr/bin/python3
import os,sys,shutil
from pathlib import Path
if len(sys.argv)<6:sys.exit(0)
directory=Path(os.environ['DEVICE_URI'].split(':',1)[1]);directory.mkdir(exist_ok=True)
out=directory/(sys.argv[1]+'.pdf')
with out.open('wb') as target:
 if len(sys.argv)>6:
  with open(sys.argv[6],'rb') as source:shutil.copyfileobj(source,target)
 else:shutil.copyfileobj(sys.stdin.buffer,target)
''')
        capture.chmod(0o755)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        self.server = '127.0.0.1:' + str(port)
        user = pwd.getpwuid(os.getuid()).pw_name
        group = grp.getgrgid(os.getgid()).gr_name
        files = base / 'cups-files.conf'
        files.write_text(f'''User {user}
Group {group}
SystemGroup {group}
ServerRoot {base}/etc
StateDir {base}/state
CacheDir {base}/cache
RequestRoot {base}/spool
TempDir {base}/tmp
ServerBin {base}/bin
DataDir /usr/share/cups
DocumentRoot /usr/share/cups/webcontent
AccessLog {base}/logs/access
ErrorLog {base}/logs/error
PageLog {base}/logs/page
FileDevice No
''')
        config = base / 'cupsd.conf'
        config.write_text(f'''Listen {self.server}
Browsing Off
WebInterface No
DefaultEncryption Never
LogLevel debug
PreserveJobFiles Yes
PreserveJobHistory Yes
<Location />
Order allow,deny
Allow all
</Location>
<Policy default>
<Limit All>
Order allow,deny
Allow all
</Limit>
</Policy>
''')
        printers = base / 'etc/printers.conf'
        printers.write_text(f'''<DefaultPrinter {self.name}>
Info Native FLTK print test
DeviceURI capture:{base}/output
State Idle
Accepting Yes
Shared No
JobSheets none none
</DefaultPrinter>
''')
        self.env = {**os.environ, 'CUPS_SERVER': self.server, 'LC_ALL': 'C'}
        self.log = (base / 'cupsd.log').open('w')
        self.process = subprocess.Popen(['cupsd', '-f', '-c', str(config), '-s', str(files)], stdout=self.log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                assert self.process.poll() is None, (base / 'cupsd.log').read_text()
                result = subprocess.run(['lpstat', '-e'], env=self.env, text=True, capture_output=True)
                if self.name in result.stdout:
                    return self
                time.sleep(.1)
            raise AssertionError('Private CUPS scheduler failed to expose its queue')
        except BaseException:
            self.__exit__(None,None,None)
            raise
    def completed(self, count):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            result = subprocess.check_output(['lpstat', '-W', 'completed', '-o'], env=self.env, text=True)
            files = sorted((self.directory / 'output').glob('*.pdf'))
            if len(result.splitlines()) >= count and len(files) >= count:
                return files
            time.sleep(.1)
        raise AssertionError((self.directory / 'logs/error').read_text())
    def __exit__(self, *unused):
        if self.process.poll() is None:
            self.process.terminate()
            try:self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:self.process.kill();self.process.wait()
        self.log.close()

if __name__ == '__main__':
    import sys
    with PrivateCups(sys.argv[1]) as cups:
        print(cups.server, subprocess.check_output(['lpstat','-p','-d'], env=cups.env, text=True), flush=True)
