"""Ask only the owned disposable character's Unity application to quit, then restore."""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import time
from tools import le_session as le
from tools import ingame_live_session as live


def run(folder):
    folder=folder.resolve(strict=True);path=folder/'session.json'
    s=json.loads(path.read_text());p=s['process']
    assert s['phase']=='running' and le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
    identity=json.loads(le.send('identityread',timeout=15,expected_process=p))
    assert identity['offline'] and identity['player']['name']=='EpFeedbackTest' and str(identity['player']['id'])==s['cloneId']
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.WaitForSingleObject.restype=wintypes.DWORD
    kernel.GetExitCodeProcess.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)];kernel.GetExitCodeProcess.restype=wintypes.BOOL
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    handle=kernel.OpenProcess(0x100000|0x1000,False,s['pid'])
    if not handle:raise ctypes.WinError(ctypes.get_last_error())
    try:
        answer=json.loads(le.send('bosstest '+s['cloneId']+' quit',timeout=15,expected_process=p));assert answer.get('quitScheduled'),answer
        deadline=time.monotonic()+120
        while kernel.WaitForSingleObject(handle,1000)==258:
            if time.monotonic()>deadline:raise RuntimeError('Normal Unity quit did not finish. No forced termination attempted.')
        code=wintypes.DWORD()
        if not kernel.GetExitCodeProcess(handle,ctypes.byref(code)):raise ctypes.WinError(ctypes.get_last_error())
        s['rawExitCode']=code.value;s['closeCode']=0 if code.value==0 else 1;s['closeMethod']='Unity Application.Quit'
        path.write_text(json.dumps(s,indent=2),encoding='utf-8')
        print(json.dumps({k:s[k] for k in ('closeMethod','rawExitCode','closeCode')}),flush=True)
    finally:kernel.CloseHandle(handle)
    live.finish(folder)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder',type=Path);run(parser.parse_args().folder)
