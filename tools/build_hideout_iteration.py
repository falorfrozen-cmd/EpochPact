"""Relink a complete stamped build after a hideout-only change.

Defaults to PRIVATE TEST. A player iteration requires its separate complete
player build and matching stamp; test and player object directories never mix.
This builds only, without installing, packaging or publishing anything.
"""
import re
import subprocess
import os
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'native/build-hideout-editor-test'


def build(sources=('hideout',), flavor='test', output=None):
    out=Path(output).resolve() if output else OUT
    if flavor not in ('test','player'):raise ValueError('Unsupported build flavor')
    info=json.loads((out/flavor/'build-info.json').read_text(encoding='utf-8'))
    if info.get('flavor')!=flavor:raise RuntimeError('Build flavor does not match its stamp')
    for name,key in (('version.dll','loaderSha256'),('EpochPact.Core.dll','sha256')):
        if flavor=='test' and key=='loaderSha256' and key not in info:continue
        if hashlib.sha256((out/flavor/name).read_bytes()).hexdigest()!=info.get(key):raise RuntimeError('Build stamp hash mismatch')
    if flavor=='player' and (out/'test').exists():raise RuntimeError('Refusing a mixed test/player output directory')
    source=(ROOT/'native/build.bat').read_text(encoding='utf-8')
    names=re.search(r'set "CORE_SRC=([^"\r\n]+)"',source)[1].split()
    for name in names:
        if not (out/(name+'.obj')).is_file():raise RuntimeError('Run the complete native build first: '+name)
    vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    quote=lambda value:'"'+str(value)+'"'
    flags='/nologo /O2 /MT /W4 /permissive- /std:c++20 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /EHa'+(' /DEPOCHPACT_TESTING' if flavor=='test' else '')
    lines=['@echo off','setlocal','call '+quote(Path(vs)/'VC/Auxiliary/Build/vcvars64.bat')+' >nul || exit /b 1']
    for name in sources:
        if name not in names:raise ValueError('Unknown source: '+name)
        lines += ['cl '+flags+' /c /Fo'+quote(out/(name+'.obj'))+' '+quote(ROOT/('native/core/'+name+'.cpp'))+' || exit /b 1']
    if 'hideout' in sources:
        lines += ['cl '+flags+' /Fo'+quote(out/'tests/hideout_rules_test.obj')+' /Fe'+quote(out/'hideout_rules_test.exe')+' '+quote(ROOT/'native/tests/hideout_rules_test.cpp')+' || exit /b 1']
    if 'monolith' in sources:
        lines += ['cl '+flags+' /Fo'+quote(out/'tests/monolith_test.obj')+' /Fe'+quote(out/'monolith_test.exe')+' '+quote(ROOT/'native/tests/monolith_test.cpp')+' || exit /b 1']
    if 'ingame_hideout' in sources:
        lines += ['cl '+flags+' /Fo'+quote(out/'tests/hideout_hud_rules_test.obj')+' /Fe'+quote(out/'hideout_hud_rules_test.exe')+' '+quote(ROOT/'native/tests/hideout_hud_rules_test.cpp')+' || exit /b 1']
    lines += ['py -3 '+quote(ROOT/'tools/build_ingame_assets.py')+' --out '+quote(out/'ingame')+' || exit /b 1',
              'rc /nologo /fo'+quote(out/'ingame.res')+' '+quote(out/'ingame/ingame.rc')+' || exit /b 1']
    common=[out/(n+'.obj') for n in names]+[out/'stat_thunk.obj',out/'core.res',out/'ingame.res']
    lines += ['link /nologo /DLL /OUT:'+quote(out/'EpochPact.Core.dll')+' '+' '.join(map(quote,common))+' kernel32.lib user32.lib psapi.lib || exit /b 1']
    merged=[out/'merged/version_thunks.obj',out/'merged/version_proxy.obj',out/'merged/core.obj']+[out/(n+'.obj') for n in names if n!='core']+[out/'stat_thunk.obj',out/'merged/version.res',out/'ingame.res']
    map_flag=' /MAP:'+quote(out/'merged/version.map') if flavor=='test' else ''
    lines += ['link /nologo /DLL'+map_flag+' /OUT:'+quote(out/flavor/'version.dll')+' /IMPLIB:'+quote(out/'merged/version.lib')+' /DEF:'+quote(ROOT/'native/proxy/version.def')+' '+' '.join(map(quote,merged))+' kernel32.lib user32.lib psapi.lib || exit /b 1',
              'py -3 '+quote(ROOT/'tools/stamp_build.py')+' '+flavor+' --build-dir '+quote(out)+' || exit /b 1','exit /b 0']
    script=out/'hideout-iteration.cmd';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    subprocess.run(['cmd.exe','/d','/c',str(script)],check=True,cwd=ROOT)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('sources',nargs='*',default=['hideout'])
    parser.add_argument('--flavor',choices=('test','player'),default='test');parser.add_argument('--out',type=Path)
    args=parser.parse_args();build(args.sources,args.flavor,args.out)
