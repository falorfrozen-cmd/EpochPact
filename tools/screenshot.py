import ctypes
import ctypes.wintypes as wt
import time
from PIL import Image

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ('biSize', ctypes.c_uint32), ('biWidth', ctypes.c_int32), ('biHeight', ctypes.c_int32),
        ('biPlanes', ctypes.c_uint16), ('biBitCount', ctypes.c_uint16), ('biCompression', ctypes.c_uint32),
        ('biSizeImage', ctypes.c_uint32), ('biXPelsPerMeter', ctypes.c_int32), ('biYPelsPerMeter', ctypes.c_int32),
        ('biClrUsed', ctypes.c_uint32), ('biClrImportant', ctypes.c_uint32)
    ]

u = ctypes.windll.user32
g = ctypes.windll.gdi32

def capture(toggle_c=False, out_path=r'research\live\screen.png'):
    hdesk = u.OpenDesktopW('Default', 0, False, 0x01FF)
    if hdesk:
        u.SetThreadDesktop(hdesk)

    hwnds = []
    def enum_cb(hwnd, lparam):
        buf = ctypes.create_unicode_buffer(512)
        u.GetWindowTextW(hwnd, buf, 512)
        cls_buf = ctypes.create_unicode_buffer(512)
        u.GetClassNameW(hwnd, cls_buf, 512)
        if cls_buf.value == "UnityWndClass" and "Last Epoch" in buf.value:
            hwnds.append(hwnd)
        return True
    u.EnumDesktopWindows(0, ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)(enum_cb), 0)
    if not hwnds:
        print("Last Epoch window not found")
        return False

    hwnd = hwnds[0]
    u.SetForegroundWindow(hwnd)
    time.sleep(0.1)

    if toggle_c:
        # Send C press
        u.keybd_event(0x43, 0x2E, 0, 0)
        time.sleep(0.08)
        u.keybd_event(0x43, 0x2E, 2, 0)
        time.sleep(0.3)

    rect = wt.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top

    hdc_win = u.GetDC(hwnd)
    hdc_mem = g.CreateCompatibleDC(hdc_win)
    hbm = g.CreateCompatibleBitmap(hdc_win, w, h)
    g.SelectObject(hdc_mem, hbm)

    u.PrintWindow(hwnd, hdc_mem, 2)

    bmi = BITMAPINFOHEADER()
    bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.biWidth = w
    bmi.biHeight = -h
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    bmi.biCompression = 0

    buf = (ctypes.c_ubyte * (w * h * 4))()
    g.GetDIBits(hdc_mem, hbm, 0, h, ctypes.byref(buf), ctypes.byref(bmi), 0)

    img = Image.frombuffer('RGBA', (w, h), bytes(buf), 'raw', 'BGRA', 0, 1)
    img.save(out_path)

    g.DeleteObject(hbm)
    g.DeleteDC(hdc_mem)
    u.ReleaseDC(hwnd, hdc_win)
    print(f'Screenshot saved to {out_path} ({w}x{h})')
    return True

if __name__ == '__main__':
    import sys
    toggle = '--toggle-c' in sys.argv
    capture(toggle_c=toggle)
