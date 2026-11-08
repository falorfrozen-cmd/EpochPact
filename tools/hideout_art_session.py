"""Own a read-only renderer session when Last Epoch's login service is unavailable."""
import argparse,json
from pathlib import Path
from tools import ingame_live_session as session
class RenderOnlyReady(Exception):pass

def start(folder,loader):
 def render_only(name):raise RenderOnlyReady()
 session.select_clone=render_only
 try:session.start(folder,loader)
 except RenderOnlyReady:
  s=json.loads((folder/'session.json').read_text());s['artReviewOnly']=True;s['panelBuilt']={'notRequiredForIsolatedRendering':True};session.write(folder/'session.json',s)
  session.wait(lambda:(lambda r:r if r.get('state')=='Login' and not r.get('transitioning') else None)(session.reply('sessionread')),180,'Login scene asset initialization')
  r=session.reply('hideout testartpreview 1');assert r.get('ok'),r
  print(json.dumps({'artReviewOnly':True,'pid':s['pid'],'catalog':r['catalog']}),flush=True)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('folder',type=Path);a.add_argument('loader',type=Path);x=a.parse_args();start(x.folder.resolve(),x.loader.resolve())
