#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland FLTK print dialogs and real CUPS jobs in a private nested DomeD session."""
from pathlib import Path
import argparse,json,os,re,signal,socket,subprocess,sys,tempfile,time,urllib.request
from cups_runtime import PrivateCups
from print_producer import produce
from pypdf import PdfReader
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('artifacts',type=Path);parser.add_argument('--bin-dir',type=Path,default=ROOT/'build-native/fltk');parser.add_argument('--pointer',action='store_true');args=parser.parse_args()
if os.environ.get('NATIVE_PRINT_BUS')!='1':os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'NATIVE_PRINT_BUS':'1'})
signal.pthread_sigmask(signal.SIG_SETMASK,[])
art=args.artifacts.resolve();art.mkdir(parents=True,exist_ok=True)
processes=[];logs=[]
def start(command,env,name,**kw):
 log=(art/(name+'.log')).open('w');logs.append(log);p=subprocess.Popen([str(c) for c in command],env=env,stdout=log,stderr=subprocess.STDOUT,**kw);processes.append(p);return p

def wait(fn):
 end=time.monotonic()+25
 while time.monotonic()<end:
  try:
   result=fn()
   if result:return result
  except (OSError,ValueError,KeyError):pass
  time.sleep(.04)
 raise AssertionError('Timeout in native Wayland printing; inspect '+str(art))
try:
 with tempfile.TemporaryDirectory(prefix='native-print-wayland-') as temp,PrivateCups(art/'cups') as cups:
  base=Path(temp);(base/'home').mkdir()
  sources=produce(art/'fixtures')
  rd,wr=os.pipe();start(['Xvfb','-displayfd',str(wr),'-screen','0','1400x1000x24','-nolisten','tcp'],os.environ.copy(),'xvfb',pass_fds=(wr,));os.close(wr)
  with os.fdopen(rd) as stream:display=':'+stream.readline().strip()
  with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  env={**cups.env,'HOME':str(base/'home'),'DISPLAY':display,'XDG_RUNTIME_DIR':temp,'XDG_CONFIG_HOME':temp+'/config','XDG_DATA_HOME':temp+'/data','XDG_CACHE_HOME':temp+'/cache','XDG_STATE_HOME':temp+'/state','DOME_PREVIEW_PORT':str(port),'DOME_RENDERER':'gles','DOME_START_IME':'0','DOME_LOGIN_SESSION':'0','DOME_START_XR':'0','DOME_START_POLKIT':'0','DOME_START_KDECONNECT':'0','DOME_START_TRAY':'0','LIBGL_ALWAYS_SOFTWARE':'1','RUST_LOG':'warn'}
  for key in ('WAYLAND_DISPLAY','WAYLAND_SOCKET'):env.pop(key,None)
  start(['/usr/local/bin/dome-compositor','--backend','nested'],env,'compositor')
  def scene():
   with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/state',timeout=1) as response:return json.load(response)['scene']
  wait(lambda:scene()['source']=='wayland')
  socket_name=wait(lambda:re.search(r'Dome Wayland socket: (\S+)',(art/'compositor.log').read_text()))[1]
  client={**env,'WAYLAND_DISPLAY':socket_name,'FLTK_BACKEND':'wayland'};client.pop('DISPLAY',None)
  from Xlib import X,XK,display as xdisplay
  from Xlib.ext import xtest
  from PIL import Image
  xd=xdisplay.Display(display)
  def key(letter,control=False):
   code=xd.keysym_to_keycode(XK.string_to_keysym(letter));ctrl=xd.keysym_to_keycode(XK.string_to_keysym('Control_L'))
   events=([(X.KeyPress,ctrl)] if control else [])+[(X.KeyPress,code),(X.KeyRelease,code)]+([(X.KeyRelease,ctrl)] if control else [])
   for event,value in events:xtest.fake_input(xd,event,value);xd.sync();time.sleep(.04)
  def focus(window):
   request=urllib.request.Request(f'http://127.0.0.1:{port}/api/action',data=json.dumps({'op':'focus','id':window['id']}).encode(),headers={'Content-Type':'application/json'})
   with urllib.request.urlopen(request,timeout=2) as response:response.read()
   wait(lambda:scene()['focused']==window['id'])
  results={}
  for count,(app,source) in enumerate(sources.items(),1):
   report=art/(app+'-report.json')
   process=start([args.bin_dir.resolve()/('calligra-'+app+'-fltk'),source,'--report',report],client,app)
   def data():return json.loads(report.read_text())
   window=wait(lambda:next((w for w in scene()['windows'] if 'calligra-'+app in w.get('app_id','').lower() and w.get('title')!='Print'),None))
   hosts=[w for w in xd.screen().root.query_tree().children if w.get_attributes().map_state==X.IsViewable and w.get_geometry().width>100]
   host=max(hosts,key=lambda w:w.get_geometry().width*w.get_geometry().height);host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();focus(window)
   key('p',True)
   wait(lambda:report.exists() and data()['print_dialog']);dialog=window;time.sleep(.2)
   maps=Path(f'/proc/{process.pid}/maps').read_text();libraries=sorted(set(line.split()[-1] for line in maps.splitlines() if any(s in line for s in ('libQt','libfltk','libgtk-'))))
   assert not any('libQt' in library for library in libraries),libraries
   assert any(library.startswith(('/usr/lib64/libfltk','/lib64/libfltk')) for library in libraries),libraries
   geometry=host.get_geometry();snapshot=host.get_image(0,0,geometry.width,geometry.height,X.ZPixmap,0xffffffff)
   Image.frombytes('RGB',(geometry.width,geometry.height),snapshot.data,'raw','BGRX').save(art/(app+'-print-dialog.png'))
   (art/(app+'-dialog-scene.json')).write_text(json.dumps(dialog,indent=2)+'\n')
   if args.pointer:
    point=xd.screen().root.translate_coords(host,0,0);box=dialog['desktop'];width,height=data()['window_size']
    left=max(0,(width-510)//2);top=max(0,(height-(320 if app=='sheets' else 270))//2)
    xtest.fake_input(xd,X.MotionNotify,x=point.x+int(box['x'])+left+435,y=point.y+int(box['y'])+top+(285 if app=='sheets' else 235))
    xtest.fake_input(xd,X.ButtonPress,1);xtest.fake_input(xd,X.ButtonRelease,1);xd.sync()
   else:key('Return')
   wait(lambda:not data()['print_dialog'])
   captured=cups.completed(count)[-1];pages=len(PdfReader(captured).pages);assert pages=={'words':2,'sheets':5,'stage':2}[app]
   focus(window);key('q',True);assert process.wait(timeout=15)==0
   wait(lambda:not any('calligra-'+app in w.get('app_id','').lower() for w in scene()['windows']))
   results[app]={'native_wayland':True,'native_fltk_print_dialog':True,'pointer_submission':args.pointer,'cups_job_completed':True,'pages':pages,'libraries':libraries,'qt_loaded':False}
   print('PASS',app,'native Wayland FLTK dialog, completed CUPS job and no Qt',flush=True)
  xd.close();(art/'results.json').write_text(json.dumps(results,indent=2)+'\n')
finally:
 for process in reversed(processes):
  if process.poll() is None:
   process.terminate()
   try:process.wait(timeout=5)
   except subprocess.TimeoutExpired:process.kill();process.wait()
 for log in logs:log.close()
