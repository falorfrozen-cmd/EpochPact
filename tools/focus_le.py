import ctypes
import ctypes.wintypes as wt
import time

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

def switch_to_default_desktop():
    hDesk = user32.OpenDesktopW("Default", 0, False, 0x01FF)
    if hDesk:
        user32.SetThreadDesktop(hDesk)
    return hDesk

def find_le():
    switch_to_default_desktop()
    hwnds = []
    def enum_cb(hwnd, lparam):
        buf = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(hwnd, buf, 512)
        cls_buf = ctypes.create_unicode_buffer(512)
        user32.GetClassNameW(hwnd, cls_buf, 512)
        if cls_buf.value == "UnityWndClass" and "Last Epoch" in buf.value:
            hwnds.append(hwnd)
        return True
    user32.EnumDesktopWindows(0, ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)(enum_cb), 0)
    return hwnds[0] if hwnds else None

def focus_hwnd(hwnd):
    SW_RESTORE = 9
    user32.ShowWindow(hwnd, SW_RESTORE)
    fg_hwnd = user32.GetForegroundWindow()
    fg_tid = user32.GetWindowThreadProcessId(fg_hwnd, None)
    cur_tid = kernel32.GetCurrentThreadId()
    target_tid = user32.GetWindowThreadProcessId(hwnd, None)
    
    user32.AttachThreadInput(cur_tid, target_tid, True)
    if fg_tid != 0 and fg_tid != cur_tid:
        user32.AttachThreadInput(fg_tid, target_tid, True)
    
    user32.BringWindowToTop(hwnd)
    user32.SetForegroundWindow(hwnd)
    user32.SetActiveWindow(hwnd)
    
    user32.AttachThreadInput(cur_tid, target_tid, False)
    if fg_tid != 0 and fg_tid != cur_tid:
        user32.AttachThreadInput(fg_tid, target_tid, False)

if __name__ == "__main__":
    hwnd = find_le()
    if hwnd:
        print(f"Found Last Epoch HWND: {hex(hwnd)}")
        focus_hwnd(hwnd)
        time.sleep(0.5)
        print("Focused.")
    else:
        print("Last Epoch window not found.")
