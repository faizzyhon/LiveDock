// LiveDock - tiny live wallpaper + transparent dock for Windows.
// by Muhammad Faizan (faizzyhon) - https://github.com/faizzyhon - https://faizzyhon.com
// A side vibe-coded project. MIT License.
// Native Win32 + GDI+. No runtime needed. Built for low-end PCs.
#define UNICODE
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
using namespace Gdiplus;

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline double dmax(double a, double b) { return a > b ? a : b; }

enum { T_WALL = 1, T_DOCK = 2 };
enum { M_SCENE, M_IMG, M_GIF };
enum { ID_PICK = 1, ID_BUILTIN, ID_AUTO, ID_PIN, ID_EXIT, ID_VIDEO, ID_HIRES, ID_ABOUT, ID_GITHUB, ID_SITE, ID_INSTA };
#define WM_TRAY (WM_APP + 1)
#define WM_CONV (WM_APP + 2)

static HINSTANCE g_inst;
static HWND g_dock, g_wall;
static std::wstring g_exe, g_dir, g_ini;
static int g_dpi = 96, g_sw, g_sh;
static UINT g_msgTaskbar;
static RECT g_origWA;
static bool g_paused = false, g_dockShown = true;

static int S(int v) { return MulDiv(v, g_dpi, 96); }

// ---------------------------------------------------------------- wallpaper
static int g_mode = M_SCENE;
static Image* g_img = nullptr;
static UINT g_frames = 1, g_cur = 0;
static std::vector<UINT> g_delay;
static Bitmap* g_sb = nullptr;      // small built-in scene bitmap
static const int SW_ = 192, SH_ = 108;
static DWORD g_lut[256];
static double g_t = 0;

static void InitLut() {
    // calm dark palette: night blue -> slate teal -> soft dusk
    const int k[4][3] = {{12, 16, 30}, {24, 44, 66}, {44, 84, 104}, {86, 90, 124}};
    for (int i = 0; i < 256; i++) {
        float f = i / 255.0f * 3.0f;
        int a = (int)f; if (a > 2) a = 2;
        float t = f - a;
        int c[3];
        for (int j = 0; j < 3; j++) c[j] = (int)(k[a][j] + (k[a + 1][j] - k[a][j]) * t);
        g_lut[i] = 0xFF000000 | (c[0] << 16) | (c[1] << 8) | c[2];
    }
}

static void SceneStep() {
    BitmapData bd; Rect r(0, 0, SW_, SH_);
    if (g_sb->LockBits(&r, ImageLockModeWrite, PixelFormat32bppRGB, &bd) != Ok) return;
    float t = (float)g_t;
    for (int y = 0; y < SH_; y++) {
        DWORD* row = (DWORD*)((BYTE*)bd.Scan0 + y * bd.Stride);
        for (int x = 0; x < SW_; x++) {
            float dx = x - 96.0f, dy = y - 54.0f;
            float v = sinf(x * 0.045f + t) + sinf(y * 0.07f - t * 0.8f) +
                      sinf((x + y) * 0.03f + t * 0.5f) + sinf(sqrtf(dx * dx + dy * dy) * 0.06f - t);
            int idx = (int)((v + 4.0f) * 31.9f);
            if (idx < 0) idx = 0; if (idx > 255) idx = 255;
            row[x] = g_lut[idx];
        }
    }
    g_sb->UnlockBits(&bd);
}

static void DrawWall(HDC dc) {
    Graphics g(dc);
    if (g_mode == M_SCENE || !g_img) {
        g.SetInterpolationMode(InterpolationModeBilinear);
        g.DrawImage(g_sb, 0, 0, g_sw, g_sh);
    } else {
        g.SetInterpolationMode(g_mode == M_GIF ? InterpolationModeLowQuality : InterpolationModeHighQualityBicubic);
        double iw = g_img->GetWidth(), ih = g_img->GetHeight();
        double s = dmax(g_sw / iw, g_sh / ih);
        double dw = iw * s, dh = ih * s;
        g.DrawImage(g_img, (REAL)((g_sw - dw) / 2), (REAL)((g_sh - dh) / 2), (REAL)dw, (REAL)dh);
    }
}

static void PaintWall() {
    if (!g_wall) return;
    HDC dc = GetDC(g_wall);
    if (dc) { DrawWall(dc); ReleaseDC(g_wall, dc); }
}

static void ScheduleWall(UINT ms) { if (!g_paused) SetTimer(g_dock, T_WALL, ms, nullptr); }

static void WallTick() {
    KillTimer(g_dock, T_WALL);
    if (g_paused) return;
    if (g_mode == M_SCENE) {
        SceneStep(); g_t += 0.05; PaintWall(); ScheduleWall(100);   // 10 fps, slow calm motion
    } else if (g_mode == M_GIF && g_img) {
        g_cur = (g_cur + 1) % g_frames;
        g_img->SelectActiveFrame(&FrameDimensionTime, g_cur);
        PaintWall();
        ScheduleWall(g_cur < g_delay.size() ? g_delay[g_cur] : 100);
    }
}

static void FreeWall() {
    KillTimer(g_dock, T_WALL);
    delete g_img; g_img = nullptr; g_mode = M_SCENE;
}

static void LoadWall() {
    FreeWall();
    wchar_t p[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"Main", L"Wallpaper", L"", p, MAX_PATH, g_ini.c_str());
    std::wstring path = p;
    if (path == L"builtin") path = L"";
    else if (path.empty()) {
        const wchar_t* c[] = {L"wallpaper.gif", L"wallpaper.png", L"wallpaper.jpg", L"wallpaper.jpeg", L"wallpaper.bmp"};
        for (auto n : c) {
            std::wstring f = g_dir + n;
            if (GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES) { path = f; break; }
        }
    }
    if (!path.empty()) {
        Image* im = new Image(path.c_str());
        if (im->GetLastStatus() == Ok) {
            g_img = im;
            UINT n = im->GetFrameCount(&FrameDimensionTime);
            if (n > 1) {
                g_mode = M_GIF; g_frames = n; g_cur = 0; g_delay.assign(n, 100);
                UINT sz = im->GetPropertyItemSize(PropertyTagFrameDelay);
                if (sz) {
                    PropertyItem* pi = (PropertyItem*)malloc(sz);
                    if (pi && im->GetPropertyItem(PropertyTagFrameDelay, sz, pi) == Ok)
                        for (UINT i = 0; i < n && i * 4 < pi->length; i++)
                            g_delay[i] = (UINT)imax((int)(((UINT*)pi->value)[i] * 10), 60);
                    free(pi);
                }
            } else g_mode = M_IMG;
        } else delete im;
    }
    if (g_mode == M_SCENE) SceneStep();
    PaintWall();
    if (g_mode != M_IMG) ScheduleWall(60);
}

// pause drawing when a fullscreen app/game is in front
static bool FullscreenCovering() {
    HWND f = GetForegroundWindow();
    if (!f || f == g_dock || f == g_wall || IsIconic(f)) return false;
    wchar_t cls[64]; GetClassNameW(f, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return false;
    RECT r; GetWindowRect(f, &r);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(f, MONITOR_DEFAULTTOPRIMARY), &mi);
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

static BOOL CALLBACK EnumWW(HWND h, LPARAM lp) {
    if (FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr)) {
        HWND w = FindWindowExW(nullptr, h, L"WorkerW", nullptr);
        if (w) *(HWND*)lp = w;
    }
    return TRUE;
}

static LRESULT CALLBACK WallProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); DrawWall(dc); EndPaint(h, &ps); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void AttachWall() {
    HWND prog = FindWindowW(L"Progman", nullptr);
    if (!prog) return;
    DWORD_PTR r;
    SendMessageTimeoutW(prog, 0x052C, 0xD, 1, SMTO_NORMAL, 1000, &r);   // spawn WorkerW
    HWND host = nullptr;
    EnumWindows(EnumWW, (LPARAM)&host);
    if (!host) host = FindWindowExW(prog, nullptr, L"WorkerW", nullptr);  // newer Windows 11 layout
    if (!host) host = prog;
    if (!g_wall) {
        g_wall = CreateWindowExW(WS_EX_NOACTIVATE, L"LiveDockWall", L"", WS_POPUP, 0, 0, g_sw, g_sh,
                                 nullptr, nullptr, g_inst, nullptr);
    }
    SetParent(g_wall, host);
    SetWindowLongPtrW(g_wall, GWL_STYLE, WS_CHILD | WS_VISIBLE);
    POINT p = {0, 0};
    MapWindowPoints(nullptr, host, &p, 1);
    SetWindowPos(g_wall, nullptr, p.x, p.y, g_sw, g_sh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

// ---------------------------------------------------------------- autostart
static const wchar_t* RUNKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static bool AutoOn() {
    HKEY k; bool on = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_READ, &k) == 0) {
        on = RegQueryValueExW(k, L"LiveDock", nullptr, nullptr, nullptr, nullptr) == 0;
        RegCloseKey(k);
    }
    return on;
}
static void SetAuto(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_WRITE, &k) != 0) return;
    if (on) {
        std::wstring v = L"\"" + g_exe + L"\"";
        RegSetValueExW(k, L"LiveDock", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2));
    } else RegDeleteValueW(k, L"LiveDock");
    RegCloseKey(k);
}

// ---------------------------------------------------------------- dock
struct Item { std::wstring path; HWND hw = nullptr; bool pinned = false, active = false, own = false; HICON ic = nullptr; };
static std::vector<Item> g_items;
static std::vector<std::wstring> g_pins;
static std::map<std::wstring, HICON> g_iconCache;
static std::wstring g_sig;
static int g_hover = -2, g_pad, g_ic, g_gap, g_h, g_w;
static bool g_tracking = false;

static void LoadPins() {
    g_pins.clear();
    int n = GetPrivateProfileIntW(L"Pins", L"Count", 0, g_ini.c_str());
    for (int i = 0; i < n; i++) {
        wchar_t k[16], v[MAX_PATH]; wsprintfW(k, L"%d", i);
        GetPrivateProfileStringW(L"Pins", k, L"", v, MAX_PATH, g_ini.c_str());
        if (v[0]) g_pins.push_back(v);
    }
}
static void SavePins() {
    WritePrivateProfileStringW(L"Pins", nullptr, nullptr, g_ini.c_str());
    wchar_t b[16]; wsprintfW(b, L"%d", (int)g_pins.size());
    WritePrivateProfileStringW(L"Pins", L"Count", b, g_ini.c_str());
    for (size_t i = 0; i < g_pins.size(); i++) {
        wchar_t k[16]; wsprintfW(k, L"%d", (int)i);
        WritePrivateProfileStringW(L"Pins", k, g_pins[i].c_str(), g_ini.c_str());
    }
}

static std::wstring WinPath(HWND h) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t b[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
    std::wstring r;
    if (ph) { if (QueryFullProcessImageNameW(ph, 0, b, &n)) r = b; CloseHandle(ph); }
    return r;
}

static bool EndsWithI(const std::wstring& s, const wchar_t* suf) {
    size_t n = wcslen(suf);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suf) == 0;
}

static HICON IconForPath(const std::wstring& p) {
    auto it = g_iconCache.find(p);
    if (it != g_iconCache.end()) return it->second;
    HICON h = nullptr;
    if (PrivateExtractIconsW(p.c_str(), 0, 48, 48, &h, nullptr, 1, 0) != 1 || !h) {
        SHFILEINFOW fi = {};
        SHGetFileInfoW(p.c_str(), 0, &fi, sizeof(fi), SHGFI_ICON | SHGFI_LARGEICON);
        h = fi.hIcon;
    }
    g_iconCache[p] = h;
    return h;
}

static HICON WinIconCopy(HWND h) {
    DWORD_PTR r = 0;
    SendMessageTimeoutW(h, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 50, &r);
    if (!r) r = GetClassLongPtrW(h, GCLP_HICON);
    if (!r) SendMessageTimeoutW(h, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 50, &r);
    if (!r) r = (DWORD_PTR)LoadIconW(nullptr, IDI_APPLICATION);
    return CopyIcon((HICON)r);
}

struct WinInfo { HWND hw; std::wstring path; };
static BOOL CALLBACK EnumTop(HWND h, LPARAM lp) {
    if (!IsWindowVisible(h) || h == g_dock || h == g_wall) return TRUE;
    if (GetWindow(h, GW_OWNER)) return TRUE;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return TRUE;
    if (ex & WS_EX_NOACTIVATE) return TRUE;
    wchar_t t[4];
    if (!GetWindowTextW(h, t, 4)) return TRUE;
    BOOL cl = FALSE; DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cl, sizeof(cl));
    if (cl) return TRUE;
    wchar_t cls[64]; GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) return TRUE;
    ((std::vector<WinInfo>*)lp)->push_back({h, WinPath(h)});
    return TRUE;
}

static void FreeItems() {
    for (auto& i : g_items) if (i.own && i.ic) DestroyIcon(i.ic);
    g_items.clear();
}

static void DrawDock(); // fwd

static void Refresh(bool force) {
    std::vector<WinInfo> wins;
    EnumWindows(EnumTop, (LPARAM)&wins);
    HWND fg = GetForegroundWindow();
    std::wstring fgPath = fg ? WinPath(fg) : L"";

    std::vector<Item> v;
    for (auto& p : g_pins) { Item it; it.path = p; it.pinned = true; v.push_back(it); }
    for (auto& w : wins) {
        bool uwp = w.path.empty() || EndsWithI(w.path, L"ApplicationFrameHost.exe");
        std::wstring key = uwp ? L"#" + std::to_wstring((unsigned long long)(UINT_PTR)w.hw) : w.path;
        Item* f = nullptr;
        for (auto& it : v) if (!_wcsicmp(it.path.c_str(), key.c_str())) { f = &it; break; }
        if (f) { if (!f->hw) f->hw = w.hw; }
        else { Item it; it.path = key; it.hw = w.hw; it.own = uwp; v.push_back(it); }
    }
    std::wstring sig;
    for (auto& it : v) {
        it.active = it.hw && (it.hw == fg || (!it.own && !fgPath.empty() && !_wcsicmp(it.path.c_str(), fgPath.c_str())));
        sig += it.path; sig += it.hw ? (it.active ? L"|A;" : L"|R;") : L"|-;";
    }
    if (!force && sig == g_sig) return;
    g_sig = sig;
    FreeItems();
    for (auto& it : v) {
        if (it.own) it.ic = it.hw ? WinIconCopy(it.hw) : nullptr;
        else it.ic = IconForPath(it.path);
    }
    g_items = v;
    DrawDock();
}

// premultiplied-alpha rounded rect fill with 1px anti-aliasing
static void FillRR(DWORD* px, int W, int H, int x, int y, int w, int h, int r, DWORD argb) {
    int sa = argb >> 24, sr = (argb >> 16) & 255, sg = (argb >> 8) & 255, sb = argb & 255;
    for (int j = imax(y, 0); j < imin(y + h, H); j++)
        for (int i = imax(x, 0); i < imin(x + w, W); i++) {
            float cx = i + 0.5f, cy = j + 0.5f;
            float dx = imax(0, 0) + fmaxf(fmaxf(x + r - cx, cx - (x + w - r)), 0.0f);
            float dy = fmaxf(fmaxf(y + r - cy, cy - (y + h - r)), 0.0f);
            float d = sqrtf(dx * dx + dy * dy) - r;
            float cov = d <= -0.5f ? 1.0f : (d >= 0.5f ? 0.0f : 0.5f - d);
            if (cov <= 0) continue;
            int a = (int)(sa * cov);
            DWORD dst = px[j * W + i];
            int da = dst >> 24, dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255, ia = 255 - a;
            px[j * W + i] = ((a + da * ia / 255) << 24) | ((sr * a / 255 + dr * ia / 255) << 16) |
                            ((sg * a / 255 + dg * ia / 255) << 8) | (sb * a / 255 + db * ia / 255);
        }
}

static void DrawDock() {
    g_ic = S(40); g_pad = S(10); g_gap = S(8);
    int n = (int)g_items.size() + 1;
    g_w = g_pad * 2 + n * g_ic + (n - 1) * g_gap;
    g_h = g_ic + S(26);
    int w = g_w, h = g_h;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(sdc);
    DWORD* px = nullptr;
    HBITMAP bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, (void**)&px, nullptr, 0);
    if (!bmp || !px) { DeleteDC(mem); ReleaseDC(nullptr, sdc); return; }
    HGDIOBJ old = SelectObject(mem, bmp);
    memset(px, 0, (size_t)w * h * 4);

    int rad = S(22);
    FillRR(px, w, h, 0, 0, w, h, rad, 0x30FFFFFF);                 // thin light edge
    FillRR(px, w, h, 1, 1, w - 2, h - 2, rad - 1, 0x9A12141A);     // translucent dark body

    int y = S(9);
    // start tile (index -1)
    {
        int x = g_pad;
        if (g_hover == -1) FillRR(px, w, h, x - S(4), y - S(4), g_ic + S(8), g_ic + S(8), S(12), 0x30FFFFFF);
        int sq = g_ic * 2 / 9, gp = S(3), gx = x + (g_ic - (sq * 2 + gp)) / 2, gy = y + (g_ic - (sq * 2 + gp)) / 2;
        for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++)
            FillRR(px, w, h, gx + a * (sq + gp), gy + b * (sq + gp), sq, sq, S(2), 0xFFE6E8EC);
    }
    for (size_t i = 0; i < g_items.size(); i++) {
        int x = g_pad + ((int)i + 1) * (g_ic + g_gap);
        if ((int)i == g_hover) FillRR(px, w, h, x - S(4), y - S(4), g_ic + S(8), g_ic + S(8), S(12), 0x30FFFFFF);
    }
    std::vector<DWORD> bk(px, px + (size_t)w * h);
    for (size_t i = 0; i < g_items.size(); i++) {
        int x = g_pad + ((int)i + 1) * (g_ic + g_gap);
        if (g_items[i].ic) DrawIconEx(mem, x, y, g_items[i].ic, g_ic, g_ic, 0, nullptr, DI_NORMAL);
    }
    GdiFlush();
    // GDI does not write alpha: pixels the icon changed become opaque, others keep the dock body
    for (size_t i = 0; i < g_items.size(); i++) {
        int x = g_pad + ((int)i + 1) * (g_ic + g_gap);
        for (int j = y; j < y + g_ic; j++) for (int k = x; k < x + g_ic; k++) {
            DWORD c = px[j * w + k], b = bk[j * w + k];
            px[j * w + k] = ((c & 0xFFFFFF) != (b & 0xFFFFFF)) ? (0xFF000000 | (c & 0xFFFFFF)) : b;
        }
    }
    // running dots
    for (size_t i = 0; i < g_items.size(); i++) {
        if (!g_items[i].hw) continue;
        int x = g_pad + ((int)i + 1) * (g_ic + g_gap);
        int dw = g_items[i].active ? S(16) : S(5), dh = S(4);
        FillRR(px, w, h, x + (g_ic - dw) / 2, y + g_ic + S(6), dw, dh, dh / 2, 0xFFDCDFE4);
    }

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    POINT pt = {(sw - w) / 2, sh - h - S(10)}, src = {0, 0};
    SIZE sz = {w, h};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_dock, sdc, &pt, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(nullptr, sdc);
}

static int HitTest(int mx) {
    for (int i = -1; i < (int)g_items.size(); i++) {
        int x = g_pad + (i + 1) * (g_ic + g_gap);
        if (mx >= x - S(4) && mx < x + g_ic + S(4)) return i;
    }
    return -2;
}

static void HideTray(bool hide) {
    HWND t = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (t && IsWindowVisible(t) == (BOOL)hide) ShowWindow(t, hide ? SW_HIDE : SW_SHOWNA);
    HWND s = nullptr;
    while ((s = FindWindowExW(nullptr, s, L"Shell_SecondaryTrayWnd", nullptr)))
        ShowWindow(s, hide ? SW_HIDE : SW_SHOWNA);
}

static void ApplyWorkArea() {
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    RECT wa = {0, 0, sw, sh - (S(26) + S(40) + S(20))};
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &wa, SPIF_SENDCHANGE);
}

static void AddTray() {
    NOTIFYICONDATAW n = {sizeof(n)};
    n.hWnd = g_dock; n.uID = 1; n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = WM_TRAY; n.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
    if (!n.hIcon) n.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy(n.szTip, L"LiveDock by faizzyhon");
    Shell_NotifyIconW(NIM_ADD, &n);
}

static void PickWallpaper() {
    wchar_t f[MAX_PATH] = L"";
    OPENFILENAMEW o = {sizeof(o)};
    o.hwndOwner = g_dock; o.lpstrFile = f; o.nMaxFile = MAX_PATH;
    o.lpstrFilter = L"GIF / images\0*.gif;*.png;*.jpg;*.jpeg;*.bmp\0\0";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&o)) {
        WritePrivateProfileStringW(L"Main", L"Wallpaper", f, g_ini.c_str());
        LoadWall();
    }
}


// ---- video -> GIF (uses ffmpeg; downloaded once next to the exe if missing)
static std::wstring g_vid;
static volatile LONG g_busy = 0;

static void Balloon(const wchar_t* t, const wchar_t* m) {
    NOTIFYICONDATAW n = {sizeof(n)};
    n.hWnd = g_dock; n.uID = 1; n.uFlags = NIF_INFO; n.dwInfoFlags = NIIF_INFO;
    wcsncpy(n.szInfoTitle, t, 63); wcsncpy(n.szInfo, m, 255);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static DWORD RunHidden(const std::wstring& cmd) {
    std::wstring c = cmd;
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &c[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) return 1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 1; GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return rc;
}

static DWORD WINAPI ConvThread(LPVOID) {
    std::wstring ff = g_dir + L"ffmpeg.exe", out = g_dir + L"wallpaper.gif";
    if (GetFileAttributesW(ff.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Balloon(L"LiveDock", L"Downloading video tool (one time, ~30 MB)...");
        RunHidden(L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ProgressPreference='SilentlyContinue'; "
                  L"[Net.ServicePointManager]::SecurityProtocol='Tls12'; $t=Join-Path $env:TEMP 'ldff'; "
                  L"Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue; New-Item $t -ItemType Directory | Out-Null; "
                  L"$z=Join-Path $t 'f.zip'; Invoke-WebRequest 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip' -OutFile $z; "
                  L"Expand-Archive -Path $z -DestinationPath $t -Force; "
                  L"Copy-Item (Get-ChildItem $t -Recurse -Filter ffmpeg.exe | Select-Object -First 1).FullName '" + ff + L"'; "
                  L"Remove-Item $t -Recurse -Force\"");
    }
    LONG ok = 0;
    if (GetFileAttributesW(ff.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Balloon(L"LiveDock", L"Making GIF... please wait.");
        int width = GetPrivateProfileIntW(L"Main", L"HiRes", 1, g_ini.c_str()) ? 1920 : 1280;
        int secs = GetPrivateProfileIntW(L"Main", L"GifSeconds", 10, g_ini.c_str());
        std::wstring tmp = g_dir + L"wallpaper.tmp.gif";
        std::wstring cmd = L"\"" + ff + L"\" -y -t " + std::to_wstring(secs) + L" -i \"" + g_vid +
            L"\" -vf \"fps=15,scale=" + std::to_wstring(width) +
            L":-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=256:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4\" -loop 0 \"" + tmp + L"\"";
        if (RunHidden(cmd) == 0) {
            DeleteFileW(out.c_str());   // GDI+ may still hold the old gif; LoadWall is run after this
            ok = MoveFileExW(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING) ? 1 : 0;
        }
    }
    PostMessageW(g_dock, WM_CONV, (WPARAM)ok, 0);
    return 0;
}

static void PickVideo() {
    if (g_busy) { Balloon(L"LiveDock", L"Still working on the last video."); return; }
    wchar_t f[MAX_PATH] = L"";
    OPENFILENAMEW o = {sizeof(o)};
    o.hwndOwner = g_dock; o.lpstrFile = f; o.nMaxFile = MAX_PATH;
    o.lpstrFilter = L"Video\0*.mp4;*.mkv;*.mov;*.avi;*.webm;*.wmv;*.m4v\0\0";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&o)) return;
    g_vid = f;
    // free the current gif so the file can be replaced
    WritePrivateProfileStringW(L"Main", L"Wallpaper", L"builtin", g_ini.c_str());
    LoadWall();
    g_busy = 1;
    HANDLE t = CreateThread(nullptr, 0, ConvThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t); else g_busy = 0;
}

static void ShowMenu(int idx) {
    HMENU m = CreatePopupMenu();
    bool canPin = idx >= 0 && idx < (int)g_items.size() && !g_items[idx].own && g_items[idx].path[0] != L'#';
    if (canPin) AppendMenuW(m, MF_STRING, ID_PIN, g_items[idx].pinned ? L"Unpin from dock" : L"Pin to dock");
    if (canPin) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_PICK, L"Choose wallpaper (GIF / image)...");
    AppendMenuW(m, MF_STRING, ID_VIDEO, L"Choose video (makes GIF wallpaper)...");
    AppendMenuW(m, MF_STRING | (GetPrivateProfileIntW(L"Main", L"HiRes", 1, g_ini.c_str()) ? MF_CHECKED : 0), ID_HIRES, L"High resolution GIF (1920)");
    AppendMenuW(m, MF_STRING, ID_BUILTIN, L"Use built-in animation");
    AppendMenuW(m, MF_STRING | (AutoOn() ? MF_CHECKED : 0), ID_AUTO, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    HMENU ab = CreatePopupMenu();
    AppendMenuW(ab, MF_STRING, ID_ABOUT, L"About LiveDock");
    AppendMenuW(ab, MF_STRING, ID_GITHUB, L"GitHub: faizzyhon");
    AppendMenuW(ab, MF_STRING, ID_SITE, L"Website: faizzyhon.com");
    AppendMenuW(ab, MF_STRING, ID_INSTA, L"Instagram: @faizzyhon");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)ab, L"LiveDock by faizzyhon");
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit (brings back Windows taskbar)");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(g_dock);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, g_dock, nullptr);
    PostMessageW(g_dock, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == ID_PICK) PickWallpaper();
    else if (cmd == ID_ABOUT)
        MessageBoxW(g_dock, L"LiveDock 1.0\n\nA tiny live wallpaper + transparent dock for Windows.\nLight on RAM and CPU, made for low-end PCs.\n\nBy Muhammad Faizan (faizzyhon)\nAI Engineer, Madinah, Saudi Arabia\n\ngithub.com/faizzyhon/LiveDock\nfaizzyhon.com\ninstagram.com/faizzyhon\n\nA side vibe-coded project. MIT License.",
                    L"About LiveDock", MB_OK | MB_ICONINFORMATION);
    else if (cmd == ID_GITHUB) ShellExecuteW(nullptr, L"open", L"https://github.com/faizzyhon/LiveDock", nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == ID_SITE) ShellExecuteW(nullptr, L"open", L"https://faizzyhon.com", nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == ID_INSTA) ShellExecuteW(nullptr, L"open", L"https://instagram.com/faizzyhon", nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == ID_VIDEO) PickVideo();
    else if (cmd == ID_HIRES) WritePrivateProfileStringW(L"Main", L"HiRes", GetPrivateProfileIntW(L"Main", L"HiRes", 1, g_ini.c_str()) ? L"0" : L"1", g_ini.c_str());
    else if (cmd == ID_BUILTIN) { WritePrivateProfileStringW(L"Main", L"Wallpaper", L"builtin", g_ini.c_str()); LoadWall(); }
    else if (cmd == ID_AUTO) SetAuto(!AutoOn());
    else if (cmd == ID_EXIT) DestroyWindow(g_dock);
    else if (cmd == ID_PIN && canPin) {
        std::wstring p2 = g_items[idx].path;
        bool was = g_items[idx].pinned;
        if (was) { for (size_t i = 0; i < g_pins.size(); i++) if (!_wcsicmp(g_pins[i].c_str(), p2.c_str())) { g_pins.erase(g_pins.begin() + i); break; } }
        else g_pins.push_back(p2);
        SavePins(); Refresh(true);
    }
}

static void Click(int idx) {
    if (idx == -1) {   // Start
        keybd_event(VK_LWIN, 0, 0, 0); keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
        return;
    }
    if (idx < 0 || idx >= (int)g_items.size()) return;
    Item& it = g_items[idx];
    if (it.hw && IsWindow(it.hw)) {
        if (IsIconic(it.hw)) { ShowWindow(it.hw, SW_RESTORE); SetForegroundWindow(it.hw); }
        else if (GetForegroundWindow() == it.hw) ShowWindow(it.hw, SW_MINIMIZE);
        else SetForegroundWindow(it.hw);
    } else if (it.pinned) {
        ShellExecuteW(nullptr, L"open", it.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    SetTimer(g_dock, T_DOCK, 1500, nullptr);
    Refresh(false);
}

static void TrimMem() { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }

static void Tick() {   // every 1.5s: cheap checks only
    HideTray(true);
    bool fs = FullscreenCovering();
    if (fs && g_dockShown) { ShowWindow(g_dock, SW_HIDE); g_dockShown = false; }
    if (!fs && !g_dockShown) { ShowWindow(g_dock, SW_SHOWNA); g_dockShown = true; g_sig.clear(); }
    if (!fs && g_dockShown) {
        SetWindowPos(g_dock, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Refresh(false);
    }
    if (fs != g_paused) {
        g_paused = fs;
        if (!fs && g_mode != M_IMG) ScheduleWall(60);
        if (fs) KillTimer(g_dock, T_WALL);
    }
}

static void Cleanup() {
    NOTIFYICONDATAW n = {sizeof(n)}; n.hWnd = g_dock; n.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &n);
    HideTray(false);
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &g_origWA, SPIF_SENDCHANGE);
}

static LRESULT CALLBACK DockProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_msgTaskbar && g_msgTaskbar) {   // explorer restarted
        AttachWall(); HideTray(true); AddTray(); ApplyWorkArea(); g_sig.clear(); Refresh(true);
        return 0;
    }
    switch (m) {
    case WM_TIMER:
        if (w == T_WALL) WallTick();
        else if (w == T_DOCK) Tick();
        return 0;
    case WM_CONV:
        g_busy = 0;
        if (w) {
            WritePrivateProfileStringW(L"Main", L"Wallpaper", (g_dir + L"wallpaper.gif").c_str(), g_ini.c_str());
            LoadWall(); Balloon(L"LiveDock", L"Done. GIF wallpaper is on.");
        } else Balloon(L"LiveDock", L"Could not make the GIF. Check internet, then try again.");
        return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        if (!g_tracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); g_tracking = true; }
        int hv = HitTest((short)LOWORD(l));
        if (hv != g_hover) { g_hover = hv; DrawDock(); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false; if (g_hover != -2) { g_hover = -2; DrawDock(); }
        return 0;
    case WM_LBUTTONUP: Click(HitTest((short)LOWORD(l))); return 0;
    case WM_RBUTTONUP: ShowMenu(HitTest((short)LOWORD(l))); return 0;
    case WM_TRAY:
        if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) ShowMenu(-2);
        return 0;
    case WM_DISPLAYCHANGE:
        g_sw = GetSystemMetrics(SM_CXSCREEN); g_sh = GetSystemMetrics(SM_CYSCREEN);
        AttachWall(); ApplyWorkArea(); Refresh(true);
        return 0;
    case WM_ENDSESSION: if (w) Cleanup(); return 0;
    case WM_DESTROY: Cleanup(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"LiveDock_single_instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    g_inst = hi;
    SetProcessDPIAware();
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);   // never fight your apps for CPU

    wchar_t b[MAX_PATH * 2]; GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    g_exe = b; g_dir = g_exe.substr(0, g_exe.find_last_of(L'\\') + 1); g_ini = g_dir + L"livedock.ini";
    HDC sdc = GetDC(nullptr); g_dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc);
    g_sw = GetSystemMetrics(SM_CXSCREEN); g_sh = GetSystemMetrics(SM_CYSCREEN);

    // after login, wait for explorer (desktop + taskbar) to be ready
    for (int i = 0; i < 60 && !(FindWindowW(L"Progman", nullptr) && FindWindowW(L"Shell_TrayWnd", nullptr)); i++) Sleep(500);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &g_origWA, 0);

    GdiplusStartupInput gi; ULONG_PTR gt;
    GdiplusStartup(&gt, &gi, nullptr);
    InitLut();
    g_sb = new Bitmap(SW_, SH_, PixelFormat32bppRGB);

    WNDCLASSW wc = {};
    wc.hInstance = hi; wc.lpszClassName = L"LiveDockWall"; wc.lpfnWndProc = WallProc;
    RegisterClassW(&wc);
    wc.lpszClassName = L"LiveDockDock"; wc.lpfnWndProc = DockProc; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");

    g_dock = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                             L"LiveDockDock", L"LiveDock", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hi, nullptr);
    AttachWall();
    LoadWall();
    LoadPins();
    HideTray(true);
    ApplyWorkArea();
    AddTray();
    Refresh(true);
    ShowWindow(g_dock, SW_SHOWNA);
    SetTimer(g_dock, T_DOCK, 1500, nullptr);

    if (!GetPrivateProfileIntW(L"Main", L"FirstRun", 0, g_ini.c_str())) {   // auto start on first launch
        SetAuto(true);
        WritePrivateProfileStringW(L"Main", L"FirstRun", L"1", g_ini.c_str());
    }
    TrimMem();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    FreeItems(); delete g_img; delete g_sb;
    GdiplusShutdown(gt);
    CloseHandle(mtx);
    return 0;
}
