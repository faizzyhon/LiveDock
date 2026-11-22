// LiveDock - live wallpaper + liquid glass dock for Windows.
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
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 2
#endif
#define APP_VER L"1.1.0"
#ifndef PAYPAL_URL
#define PAYPAL_URL L"https://paypal.me/faizzyhon"
#endif

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline double dmax(double a, double b) { return a > b ? a : b; }
static inline float fclampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

enum { T_WALL = 1, T_DOCK = 2, T_ANIM = 3 };
enum { M_SCENE, M_IMG, M_GIF };
enum { ID_STUDIO = 1, ID_PIN, ID_GLASS, ID_ANIM, ID_AUTO, ID_ABOUT, ID_GITHUB, ID_SITE, ID_INSTA, ID_PAYPAL, ID_EXIT, ID_FIXWALL };
#define WM_TRAY (WM_APP + 1)
#define WM_CONV (WM_APP + 2)

static HINSTANCE g_inst;
static HWND g_dock, g_wall, g_studio;
static std::wstring g_exe, g_dir, g_ini, g_wdir;
static int g_dpi = 96, g_sw, g_sh;
static float g_sc = 1.0f;
static UINT g_msgTaskbar;
static RECT g_origWA;
static bool g_paused = false, g_dockShown = true, g_glass = true, g_anim = true, g_glassOk = false;

static int S(int v) { return MulDiv(v, g_dpi, 96); }
static void RedrawDock();
static void GlassTick();
static bool g_wallOk = false, g_wall24 = false;
static HWND g_wallParent = nullptr;
static void Log(const std::wstring& s) {
    std::wstring p = g_dir + L"livedock.log";
    HANDLE f = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    if (GetFileSize(f, nullptr) > 20000) { CloseHandle(f); return; }
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u(n, 0); WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &u[0], n, nullptr, nullptr);
    u.resize(n > 0 ? n - 1 : 0); u += "\r\n";
    DWORD w; WriteFile(f, u.data(), (DWORD)u.size(), &w, nullptr); CloseHandle(f);
}

static std::wstring IniStr(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t b[1024]; GetPrivateProfileStringW(sec, key, def, b, 1024, g_ini.c_str()); return b;
}
static void IniSet(const wchar_t* sec, const wchar_t* key, const wchar_t* v) {
    WritePrivateProfileStringW(sec, key, v, g_ini.c_str());
}
static void OpenUrl(const std::wstring& u) { ShellExecuteW(nullptr, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
static std::wstring PayPal() { return IniStr(L"Support", L"PayPal", PAYPAL_URL); }

static std::wstring Base(const std::wstring& p) {
    size_t a = p.find_last_of(L"\\/");
    std::wstring n = a == std::wstring::npos ? p : p.substr(a + 1);
    size_t d = n.find_last_of(L'.');
    if (d != std::wstring::npos) n = n.substr(0, d);
    return n;
}
static std::wstring Ext(const std::wstring& p) {
    size_t d = p.find_last_of(L'.');
    std::wstring e = d == std::wstring::npos ? L"" : p.substr(d);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

// ================================================================ wallpaper
static int g_mode = M_SCENE;
static Image* g_img = nullptr;
static UINT g_frames = 1, g_cur = 0;
static std::vector<UINT> g_delay;
static Bitmap* g_sb = nullptr;
static const int SW_ = 192, SH_ = 108;
static DWORD g_lut[256];
static double g_t = 0;

static void InitLut() {
    const int k[4][3] = {{12, 16, 30}, {24, 44, 66}, {44, 84, 104}, {86, 90, 124}};
    for (int i = 0; i < 256; i++) {
        float f = i / 255.0f * 3.0f;
        int a = (int)f; if (a > 2) a = 2;
        float t = f - a; int c[3];
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
            if (idx < 0) idx = 0;
            if (idx > 255) idx = 255;
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
        SceneStep(); g_t += 0.05; PaintWall(); GlassTick(); ScheduleWall(100);
    } else if (g_mode == M_GIF && g_img) {
        g_cur = (g_cur + 1) % g_frames;
        g_img->SelectActiveFrame(&FrameDimensionTime, g_cur);
        PaintWall(); GlassTick();
        ScheduleWall(g_cur < g_delay.size() ? g_delay[g_cur] : 100);
    }
}

static void FreeWall() {
    KillTimer(g_dock, T_WALL);
    delete g_img; g_img = nullptr; g_mode = M_SCENE;
}

static void MarkGlassDirty();
static void RedrawDockSoon() { MarkGlassDirty(); RedrawDock(); }
static std::wstring ActiveWall() { return IniStr(L"Main", L"Wallpaper", L""); }

static void LoadWall() {
    FreeWall();
    std::wstring path = ActiveWall();
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
                            g_delay[i] = (UINT)imax((int)(((UINT*)pi->value)[i] * 10), 40);
                    free(pi);
                }
            } else g_mode = M_IMG;
        } else delete im;
    }
    if (g_mode == M_SCENE) SceneStep();
    PaintWall();
    RedrawDockSoon();
    if (g_mode != M_IMG) ScheduleWall(60);
}

static void SetWallpaper(const std::wstring& path) { IniSet(L"Main", L"Wallpaper", path.c_str()); LoadWall(); }

static bool FullscreenCovering() {
    HWND f = GetForegroundWindow();
    if (!f || f == g_dock || f == g_wall || f == g_studio || IsIconic(f)) return false;
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
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); DrawWall(dc); EndPaint(h, &ps); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static bool WallStyleOk() { return g_wall && IsWindow(g_wall); }

static void AttachWall() {
    g_wallOk = false;
    HWND prog = FindWindowW(L"Progman", nullptr);
    if (!prog) { Log(L"attach: no Progman"); return; }
    DWORD_PTR r;
    SendMessageTimeoutW(prog, 0x052C, 0xD, 1, SMTO_NORMAL, 1000, &r);
    HWND dv24 = FindWindowExW(prog, nullptr, L"SHELLDLL_DefView", nullptr);   // Windows 11 24H2+: DefView is a child of Progman
    bool n24 = dv24 != nullptr;
    HWND host = nullptr;
    if (!n24) {
        EnumWindows(EnumWW, (LPARAM)&host);                                   // classic layout
        if (!host) host = FindWindowExW(prog, nullptr, L"WorkerW", nullptr);
        if (!host) { Log(L"attach: no WorkerW / DefView found"); return; }
    }
    if (g_wall) { DestroyWindow(g_wall); g_wall = nullptr; }
    DWORD ex = WS_EX_NOACTIVATE | (n24 ? WS_EX_LAYERED : 0);                   // 24H2: plain child is never composited, layered is
    g_wall = CreateWindowExW(ex, L"LiveDockWall", L"", WS_POPUP, 0, 0, g_sw, g_sh, nullptr, nullptr, g_inst, nullptr);
    if (!g_wall) { Log(L"attach: CreateWindow failed"); return; }
    HWND parent = n24 ? prog : host;
    SetParent(g_wall, parent);
    SetWindowLongPtrW(g_wall, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_DISABLED);
    if (n24 && !SetLayeredWindowAttributes(g_wall, 0, 255, LWA_ALPHA))
        Log(L"attach: SetLayeredWindowAttributes failed " + std::to_wstring(GetLastError()));
    POINT p = {0, 0};
    MapWindowPoints(nullptr, parent, &p, 1);
    if (n24) SetWindowPos(g_wall, dv24, p.x, p.y, g_sw, g_sh, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);   // just under the icons
    else SetWindowPos(g_wall, nullptr, p.x, p.y, g_sw, g_sh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_wall24 = n24; g_wallParent = parent; g_wallOk = true;
    Log(n24 ? L"attach: Windows 11 24H2+ layout (child of Progman)" : L"attach: classic layout (WorkerW)");
    PaintWall();
}

static void WallHealth() {
    static int backoff = 0;
    if (!g_wallOk && (backoff++ % 4)) return;      // if it keeps failing, retry slowly
    if (!g_wallOk || !WallStyleOk() || !IsWindow(g_wallParent) || GetParent(g_wall) != g_wallParent) {
        AttachWall(); LoadWall(); return;
    }
    if (g_wall24) {
        HWND dv = FindWindowExW(g_wallParent, nullptr, L"SHELLDLL_DefView", nullptr);
        if (dv && GetWindow(dv, GW_HWNDNEXT) != g_wall)
            SetWindowPos(g_wall, dv, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// ================================================================ autostart
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

// ================================================================ drawing helpers (GDI+)
static FontFamily* g_ff = nullptr;
static std::map<int, Font*> g_fonts;
static Font* F(int px, bool bold) {
    int key = px * 2 + (bold ? 1 : 0);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    Font* f = new Font(g_ff, (REAL)px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    g_fonts[key] = f;
    return f;
}
static Color C(int r, int g, int b, int a = 255) { return Color((BYTE)a, (BYTE)r, (BYTE)g, (BYTE)b); }

static void RRPath(GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}
static void FillRRect(Graphics& g, float x, float y, float w, float h, float r, Color c) {
    GraphicsPath p; RRPath(p, x, y, w, h, r); SolidBrush b(c); g.FillPath(&b, &p);
}
static void StrokeRRect(Graphics& g, float x, float y, float w, float h, float r, Color c, float pw) {
    GraphicsPath p; RRPath(p, x, y, w, h, r); Pen pen(c, pw); g.DrawPath(&pen, &p);
}
static void Txt(Graphics& g, const std::wstring& s, float x, float y, float w, float h, int px, bool bold, Color c,
                int align = 0, bool vcenter = true, bool wrap = false) {
    StringFormat sf;
    sf.SetAlignment(align == 1 ? StringAlignmentCenter : (align == 2 ? StringAlignmentFar : StringAlignmentNear));
    sf.SetLineAlignment(vcenter ? StringAlignmentCenter : StringAlignmentNear);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    if (!wrap) sf.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, F(px, bold), RectF(x, y, w, h), &sf, &b);
}

// ================================================================ dock
struct Item { std::wstring path; HWND hw = nullptr; bool pinned = false, active = false, own = false; HICON ic = nullptr; };
static std::vector<Item> g_items;
static std::vector<std::wstring> g_pins;
static std::map<std::wstring, HICON> g_iconCache;
static std::map<HICON, Bitmap*> g_ibmp;
static std::wstring g_sig;
static int g_hoverSlot = -1, g_mx = -1, g_my = -1, g_ic, g_pad, g_gap;
static bool g_tracking = false;

static void LoadPins() {
    g_pins.clear();
    int n = GetPrivateProfileIntW(L"Pins", L"Count", 0, g_ini.c_str());
    for (int i = 0; i < n; i++) {
        wchar_t k[16]; wsprintfW(k, L"%d", i);
        std::wstring v = IniStr(L"Pins", k, L"");
        if (!v.empty()) g_pins.push_back(v);
    }
}
static void SavePins() {
    WritePrivateProfileStringW(L"Pins", nullptr, nullptr, g_ini.c_str());
    wchar_t b[16]; wsprintfW(b, L"%d", (int)g_pins.size());
    IniSet(L"Pins", L"Count", b);
    for (size_t i = 0; i < g_pins.size(); i++) {
        wchar_t k[16]; wsprintfW(k, L"%d", (int)i);
        IniSet(L"Pins", k, g_pins[i].c_str());
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
    if (PrivateExtractIconsW(p.c_str(), 0, 64, 64, &h, nullptr, 1, 0) != 1 || !h) {
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
static Bitmap* IconBmp(HICON h) {
    if (!h) return nullptr;
    auto it = g_ibmp.find(h);
    if (it != g_ibmp.end()) return it->second;
    const int N = 64;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = N; bi.bmiHeader.biHeight = -N;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(nullptr); HDC m = CreateCompatibleDC(sdc);
    void *pb = nullptr, *pw = nullptr;
    HBITMAP bb = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &pb, nullptr, 0);
    HBITMAP bw = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &pw, nullptr, 0);
    Bitmap* out = nullptr;
    if (bb && bw && pb && pw) {
        // draw the icon on black and on white: the difference gives a clean alpha (works for old and new icons)
        HGDIOBJ o = SelectObject(m, bb); memset(pb, 0, N * N * 4); DrawIconEx(m, 0, 0, h, N, N, 0, nullptr, DI_NORMAL);
        SelectObject(m, bw); memset(pw, 0xFF, N * N * 4); DrawIconEx(m, 0, 0, h, N, N, 0, nullptr, DI_NORMAL);
        GdiFlush(); SelectObject(m, o);
        out = new Bitmap(N, N, PixelFormat32bppPARGB);
        BitmapData bd; Rect rc(0, 0, N, N);
        if (out->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppPARGB, &bd) == Ok) {
            for (int y = 0; y < N; y++) {
                DWORD* d = (DWORD*)((BYTE*)bd.Scan0 + y * bd.Stride);
                for (int x = 0; x < N; x++) {
                    DWORD B = ((DWORD*)pb)[y * N + x], W = ((DWORD*)pw)[y * N + x];
                    int br = (B >> 16) & 255, bg = (B >> 8) & 255, bb2 = B & 255;
                    int wr = (W >> 16) & 255, wg = (W >> 8) & 255, wb = W & 255;
                    int a = 255 - ((wr - br) + (wg - bg) + (wb - bb2)) / 3;
                    if (a < 0) a = 0;
                    if (a > 255) a = 255;
                    if (br > a) br = a;
                    if (bg > a) bg = a;
                    if (bb2 > a) bb2 = a;
                    d[x] = ((DWORD)a << 24) | (br << 16) | (bg << 8) | bb2;
                }
            }
            out->UnlockBits(&bd);
        }
    }
    if (bb) DeleteObject(bb);
    if (bw) DeleteObject(bw);
    DeleteDC(m); ReleaseDC(nullptr, sdc);
    g_ibmp[h] = out;
    return out;
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
    for (auto& i : g_items)
        if (i.own && i.ic) {
            auto it = g_ibmp.find(i.ic);
            if (it != g_ibmp.end()) { delete it->second; g_ibmp.erase(it); }
            DestroyIcon(i.ic);
        }
    g_items.clear();
}

static void LayoutDock();
static void RedrawDock();

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
    LayoutDock();
    RedrawDock();
}

// ---- liquid glass: our own renderer (per-pixel alpha window).
// Follows how Apple describes Liquid Glass: lensing (light bends at the rim), highlights that follow the shape,
// a soft shadow, and a tint that adapts to what is behind. The backdrop is our own wallpaper, blurred.
static int g_M, g_labelH, g_pillW, g_pillH, g_winW, g_winH, g_winX, g_winY, g_px, g_py;
static HDC g_dibDC = nullptr; static HBITMAP g_dibBmp = nullptr; static HGDIOBJ g_dibOld = nullptr;
static DWORD* g_bits = nullptr; static Bitmap* g_dibGp = nullptr;
struct Geo { BYTE cov, sh, rim, sheen; signed char ox, oy; };
static std::vector<Geo> g_geo;
static std::vector<DWORD> g_cache;
static std::vector<BYTE> g_bd; static int g_bdW = 0, g_bdH = 0;
static bool g_cacheDirty = true; static DWORD g_lastBuild = 0;
static std::vector<float> g_scaleCur, g_vel; static float g_labelA = 0;
static int g_downSlot = -1;

static float SdRR(float px, float py, float hx, float hy, float r, float* nx, float* ny) {
    float qx = fabsf(px) - (hx - r), qy = fabsf(py) - (hy - r);
    float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
    float len = sqrtf(mx * mx + my * my);
    float sd = len + fminf(fmaxf(qx, qy), 0.0f) - r;
    if (nx) {
        if (len > 0.0001f) { *nx = mx / len * (px < 0 ? -1.0f : 1.0f); *ny = my / len * (py < 0 ? -1.0f : 1.0f); }
        else if (qx > qy) { *nx = px < 0 ? -1.0f : 1.0f; *ny = 0; }
        else { *nx = 0; *ny = py < 0 ? -1.0f : 1.0f; }
    }
    return sd;
}

// shape only (changes when the dock size changes): coverage, shadow, rim light, sheen, lens offset
static void ComputeGeo() {
    int W = g_winW, H = g_winH;
    g_geo.assign((size_t)W * H, Geo{0, 0, 0, 0, 0, 0});
    float cx = g_px + g_pillW / 2.0f, cy = g_py + g_pillH / 2.0f, hx = g_pillW / 2.0f, hy = g_pillH / 2.0f;
    float r = g_pillH * 0.40f, bw = (float)S(18), rw = (float)S(2), glowW = (float)S(7);
    float sig = (float)S(12), off = (float)S(9), lens = (float)S(9);
    const float Lx = -0.45f, Ly = -0.89f;     // light comes from the top left
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float px = x + 0.5f - cx, py = y + 0.5f - cy, nx = 0, ny = 0;
        float sd = SdRR(px, py, hx, hy, r, &nx, &ny);
        Geo& g = g_geo[(size_t)y * W + x];
        if (sd > -0.5f) {
            float sd2 = SdRR(px, py - off, hx, hy, r, nullptr, nullptr);
            if (sd2 < 0) sd2 = 0;
            g.sh = (BYTE)(0.42f * expf(-(sd2 * sd2) / (2 * sig * sig)) * 255.0f);
        }
        float cov = 0.5f - sd; if (cov < 0) cov = 0; if (cov > 1) cov = 1;
        g.cov = (BYTE)(cov * 255.0f);
        if (sd < 0.5f) {
            float d = -sd; if (d < 0) d = 0;
            float ndl = nx * Lx + ny * Ly, f1 = ndl > 0 ? ndl : 0, f2 = ndl < 0 ? -ndl : 0;
            float inten = 0.18f + 0.82f * powf(f1, 1.4f) + 0.45f * powf(f2, 2.0f);
            float rim = d < rw ? (1.0f - d / rw) * inten : 0;
            if (rim > 1) rim = 1;
            g.rim = (BYTE)(rim * 255.0f);
            float ypos = (py + hy) / (2 * hy); if (ypos < 0) ypos = 0; if (ypos > 1) ypos = 1;
            float sheen = 0.10f * (1 - ypos) * (1 - ypos);
            if (d < glowW) { float t = 1 - d / glowW; sheen += 0.30f * t * t * f1; }
            if (sheen > 0.5f) sheen = 0.5f;
            g.sheen = (BYTE)(sheen * 510.0f);
            if (d < bw) { float t = 1 - d / bw, m = lens * t * t; g.ox = (signed char)(nx * m * 2); g.oy = (signed char)(ny * m * 2); }
        }
    }
}

static void BlurBd(int rad) {
    std::vector<BYTE> tmp(g_bd.size());
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < g_bdH; y++) for (int x = 0; x < g_bdW; x++) for (int c = 0; c < 3; c++) {
            int sum = 0, n = 0;
            for (int k = -rad; k <= rad; k++) { int xx = x + k; if (xx < 0) xx = 0; if (xx >= g_bdW) xx = g_bdW - 1; sum += g_bd[((size_t)y * g_bdW + xx) * 3 + c]; n++; }
            tmp[((size_t)y * g_bdW + x) * 3 + c] = (BYTE)(sum / n);
        }
        for (int y = 0; y < g_bdH; y++) for (int x = 0; x < g_bdW; x++) for (int c = 0; c < 3; c++) {
            int sum = 0, n = 0;
            for (int k = -rad; k <= rad; k++) { int yy = y + k; if (yy < 0) yy = 0; if (yy >= g_bdH) yy = g_bdH - 1; sum += tmp[((size_t)yy * g_bdW + x) * 3 + c]; n++; }
            g_bd[((size_t)y * g_bdW + x) * 3 + c] = (BYTE)(sum / n);
        }
    }
}

// what is behind the dock = our wallpaper (same crop and scale as on screen), shrunk 4x and blurred
static void BuildBackdrop() {
    g_bdW = imax((g_winW + 3) / 4, 2); g_bdH = imax((g_winH + 3) / 4, 2);
    g_bd.assign((size_t)g_bdW * g_bdH * 3, 40);
    if (!(g_glass && g_wallOk)) return;
    Bitmap sb(g_bdW, g_bdH, PixelFormat32bppRGB);
    {
        Graphics g(&sb);
        g.SetInterpolationMode(InterpolationModeBilinear);
        ImageAttributes ia; ia.SetWrapMode(WrapModeTileFlipXY);
        RectF dst(0, 0, (REAL)g_bdW, (REAL)g_bdH);
        if (g_mode == M_SCENE || !g_img) {
            float fx = (float)SW_ / g_sw, fy = (float)SH_ / g_sh;
            g.DrawImage(g_sb, dst, g_winX * fx, g_winY * fy, g_winW * fx, g_winH * fy, UnitPixel, &ia);
        } else {
            double iw = g_img->GetWidth(), ih = g_img->GetHeight(), sc = dmax(g_sw / iw, g_sh / ih);
            double ox = (g_sw - iw * sc) / 2, oy = (g_sh - ih * sc) / 2;
            g.DrawImage(g_img, dst, (REAL)((g_winX - ox) / sc), (REAL)((g_winY - oy) / sc), (REAL)(g_winW / sc), (REAL)(g_winH / sc), UnitPixel, &ia);
        }
    }
    BitmapData bd; Rect rc(0, 0, g_bdW, g_bdH);
    if (sb.LockBits(&rc, ImageLockModeRead, PixelFormat32bppRGB, &bd) != Ok) return;
    for (int y = 0; y < g_bdH; y++) {
        const DWORD* row = (const DWORD*)((const BYTE*)bd.Scan0 + y * bd.Stride);
        for (int x = 0; x < g_bdW; x++) {
            DWORD c = row[x]; size_t i = ((size_t)y * g_bdW + x) * 3;
            g_bd[i] = (BYTE)((c >> 16) & 255); g_bd[i + 1] = (BYTE)((c >> 8) & 255); g_bd[i + 2] = (BYTE)(c & 255);
        }
    }
    sb.UnlockBits(&bd);
    BlurBd(2);
}

static void SampleBd(float sx, float sy, float& r, float& g, float& b) {
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx > g_bdW - 1) sx = (float)(g_bdW - 1);
    if (sy > g_bdH - 1) sy = (float)(g_bdH - 1);
    int x0 = (int)sx, y0 = (int)sy, x1 = imin(x0 + 1, g_bdW - 1), y1 = imin(y0 + 1, g_bdH - 1);
    float fx = sx - x0, fy = sy - y0;
    const BYTE* a = &g_bd[((size_t)y0 * g_bdW + x0) * 3]; const BYTE* b2 = &g_bd[((size_t)y0 * g_bdW + x1) * 3];
    const BYTE* c = &g_bd[((size_t)y1 * g_bdW + x0) * 3]; const BYTE* d = &g_bd[((size_t)y1 * g_bdW + x1) * 3];
    float o[3];
    for (int k = 0; k < 3; k++) {
        float top = a[k] + (b2[k] - a[k]) * fx, bot = c[k] + (d[k] - c[k]) * fx;
        o[k] = top + (bot - top) * fy;
    }
    r = o[0]; g = o[1]; b = o[2];
}

static void RebuildGlass() {
    int W = g_winW, H = g_winH;
    g_cache.assign((size_t)W * H, 0);
    BuildBackdrop();
    bool real = g_glass && g_wallOk;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        const Geo& g = g_geo[(size_t)y * W + x];
        if (!g.cov && !g.sh && !g.rim) continue;
        float cov = g.cov / 255.0f, shA = g.sh / 255.0f;
        float r = 30, gg = 35, b = 44, bodyA = 0.88f * cov;
        if (cov > 0 && real) {
            SampleBd((x + g.ox * 0.5f) * 0.25f, (y + g.oy * 0.5f) * 0.25f, r, gg, b);   // lensing: sample bent toward the rim
            float L = 0.299f * r + 0.587f * gg + 0.114f * b;
            r = L + (r - L) * 1.30f; gg = L + (gg - L) * 1.30f; b = L + (b - L) * 1.30f;   // vibrancy
            float mag = (float)(abs((int)g.ox) + abs((int)g.oy)) / 18.0f; if (mag > 1) mag = 1;
            float k = (L > 170 ? 0.80f : 0.92f) + 0.14f * mag;          // adaptive tint, brighter where light concentrates
            float wmix = L > 170 ? 0.02f : 0.11f;
            r = r * k + 255 * wmix; gg = gg * k + 255 * wmix; b = b * k + 255 * wmix;
            if (r > 255) r = 255;
            if (gg > 255) gg = 255;
            if (b > 255) b = 255;
            bodyA = 0.80f * cov;
        }
        float a = bodyA + shA * (1 - bodyA);
        float pr = r * bodyA, pg = gg * bodyA, pb = b * bodyA;
        float w2 = (g.rim / 255.0f * 0.95f);
        float sa = g.sheen / 510.0f;
        w2 = (w2 + sa * (1 - w2)) * cov;
        pr = 255 * w2 + pr * (1 - w2); pg = 255 * w2 + pg * (1 - w2); pb = 255 * w2 + pb * (1 - w2);
        a = w2 + a * (1 - w2);
        int A = (int)(a * 255.0f + 0.5f); if (A > 255) A = 255;
        int R = (int)(pr + 0.5f), G = (int)(pg + 0.5f), B = (int)(pb + 0.5f);
        if (R > A) R = A;
        if (G > A) G = A;
        if (B > A) B = A;
        g_cache[(size_t)y * W + x] = ((DWORD)A << 24) | (R << 16) | (G << 8) | B;
    }
}

static void MarkGlassDirty() { g_cacheDirty = true; }
static void GlassTick() {
    if (!g_glass || !g_dockShown || !g_dock || g_paused || g_mode == M_IMG) return;
    DWORD n = GetTickCount();
    if (n - g_lastBuild < 220) return;      // glass follows the wallpaper at ~5 fps: cheap
    g_cacheDirty = true; RedrawDock();
}

static std::wstring SlotName(int j) {
    int ns = (int)g_items.size() + 2;
    if (j == 0) return L"Start";
    if (j == ns - 1) return L"LiveDock Studio";
    const Item& it = g_items[j - 1];
    std::wstring n;
    if (it.path[0] == L'#') { wchar_t t[64] = L""; if (it.hw) GetWindowTextW(it.hw, t, 63); n = t; }
    else n = Base(it.path);
    if (!n.empty()) n[0] = (wchar_t)towupper(n[0]);
    return n;
}

static void GlassChip(Graphics& g, float x, float y, float s) {
    GraphicsPath p; RRPath(p, x, y, s, s, s * 0.30f);
    LinearGradientBrush fb(PointF(x, y), PointF(x, y + s), C(255, 255, 255, 74), C(255, 255, 255, 26));
    g.FillPath(&fb, &p);
    LinearGradientBrush rb(PointF(x, y), PointF(x, y + s), C(255, 255, 255, 150), C(255, 255, 255, 36));
    Pen pen(&rb, 1.2f); g.DrawPath(&pen, &p);
}
static void DrawStartGlyph(Graphics& g, float cx, float cy, float size) {
    GlassChip(g, cx - size / 2, cy - size / 2, size);
    float sq = size * 0.20f, gp = size * 0.07f, tot = sq * 2 + gp;
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++)
        FillRRect(g, cx - tot / 2 + a * (sq + gp), cy - tot / 2 + b * (sq + gp), sq, sq, sq * 0.22f, C(255, 255, 255, 235));
}
static void DrawStudioGlyph(Graphics& g, float cx, float cy, float size) {
    GlassChip(g, cx - size / 2, cy - size / 2, size);
    float bw = size * 0.50f, bh = size * 0.06f, kx[3] = {0.32f, 0.70f, 0.48f};
    for (int i = 0; i < 3; i++) {
        float y = cy + (i - 1) * size * 0.19f;
        FillRRect(g, cx - bw / 2, y - bh / 2, bw, bh, bh / 2, C(255, 255, 255, 170));
        float kxp = cx - bw / 2 + bw * kx[i], kr = size * 0.075f;
        SolidBrush kb(C(255, 255, 255, 245)); g.FillEllipse(&kb, kxp - kr, y - kr, kr * 2, kr * 2);
    }
}

static void PaintDock() {
    if (!g_dibDC || !g_bits || !g_dibGp || !g_dock) return;
    if (g_cacheDirty) { RebuildGlass(); g_cacheDirty = false; g_lastBuild = GetTickCount(); }
    memcpy(g_bits, g_cache.data(), (size_t)g_winW * g_winH * 4);
    {
        Graphics g(g_dibGp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        int ns = (int)g_items.size() + 2;
        float cy = g_py + g_pillH / 2.0f - S(2);
        for (int j = 0; j < ns; j++) {
            float s = j < (int)g_scaleCur.size() ? g_scaleCur[j] : 1.0f;
            float size = g_ic * s;
            float cx = g_px + g_pad + j * (g_ic + g_gap) + g_ic / 2.0f;
            float yy = cy - (s - 1.0f) * g_ic * 0.30f;
            if (j == 0) DrawStartGlyph(g, cx, yy, size);
            else if (j == ns - 1) DrawStudioGlyph(g, cx, yy, size);
            else {
                const Item& it = g_items[j - 1];
                Bitmap* b = IconBmp(it.ic);
                if (b) {
                    SolidBrush sb(C(0, 0, 0, 22));
                    for (int k = 0; k < 3; k++) {   // soft contact shadow
                        float e = k * size * 0.05f, w = size * 0.74f + 2 * e;
                        g.FillEllipse(&sb, cx - w / 2, yy + size * 0.36f - e * 0.3f, w, size * 0.14f + e * 0.6f);
                    }
                    g.DrawImage(b, RectF(cx - size / 2, yy - size / 2, size, size));
                }
                if (it.hw) {
                    float dw = it.active ? (float)S(14) : (float)S(4), dh = (float)S(4);
                    FillRRect(g, cx - dw / 2, (float)(g_py + g_pillH - S(11)), dw, dh, dh / 2, C(255, 255, 255, 215));
                }
            }
        }
        if (g_labelA > 0.02f && g_hoverSlot >= 0 && g_mx >= 0) {
            std::wstring nm = SlotName(g_hoverSlot);
            RectF bb; g.MeasureString(nm.c_str(), -1, F(13, true), PointF(0, 0), &bb);
            float cw = bb.Width + S(24), ch = (float)S(28);
            float lx = g_px + g_pad + g_hoverSlot * (g_ic + g_gap) + g_ic / 2.0f - cw / 2;
            if (lx < 2) lx = 2;
            if (lx > g_winW - cw - 2) lx = g_winW - cw - 2;
            float ly = g_py - ch - S(8);
            int al = (int)(g_labelA * 255);
            FillRRect(g, lx, ly, cw, ch, ch / 2, C(22, 26, 34, (int)(al * 0.84f)));
            StrokeRRect(g, lx + 0.5f, ly + 0.5f, cw - 1, ch - 1, ch / 2, C(255, 255, 255, (int)(al * 0.26f)), 1.0f);
            Txt(g, nm, lx, ly, cw, ch, 13, true, C(244, 246, 249, al), 1);
        }
    }
    HDC sdc = GetDC(nullptr);
    POINT pt = {g_winX, g_winY}, src = {0, 0}; SIZE sz = {g_winW, g_winH};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_dock, sdc, &pt, &sz, g_dibDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, sdc);
}

static void RedrawDock() { PaintDock(); }

static void LayoutDock() {
    g_ic = S(40); g_pad = S(14); g_gap = S(10);
    int ns = (int)g_items.size() + 2;
    g_pillW = g_pad * 2 + ns * g_ic + (ns - 1) * g_gap;
    g_pillH = g_ic + S(30);
    g_M = S(28); g_labelH = S(40); g_px = g_M; g_py = g_labelH;
    int nw = g_pillW + 2 * g_M, nh = g_labelH + g_pillH + g_M;
    bool resize = nw != g_winW || nh != g_winH || !g_bits;
    g_winW = nw; g_winH = nh;
    g_winX = (g_sw - g_winW) / 2; g_winY = g_sh - S(10) - g_pillH - g_labelH;
    g_scaleCur.resize(ns, 1.0f); g_vel.resize(ns, 0.0f);
    if (resize) {
        delete g_dibGp; g_dibGp = nullptr;
        if (g_dibBmp) { SelectObject(g_dibDC, g_dibOld); DeleteObject(g_dibBmp); g_dibBmp = nullptr; }
        if (!g_dibDC) { HDC sd = GetDC(nullptr); g_dibDC = CreateCompatibleDC(sd); ReleaseDC(nullptr, sd); }
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = g_winW; bi.bmiHeader.biHeight = -g_winH;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        g_dibBmp = CreateDIBSection(g_dibDC, &bi, DIB_RGB_COLORS, (void**)&g_bits, nullptr, 0);
        g_dibOld = SelectObject(g_dibDC, g_dibBmp);
        g_dibGp = new Bitmap(g_winW, g_winH, g_winW * 4, PixelFormat32bppPARGB, (BYTE*)g_bits);
        ComputeGeo();
    }
    g_cacheDirty = true;
    SetWindowPos(g_dock, HWND_TOPMOST, g_winX, g_winY, g_winW, g_winH, SWP_NOACTIVATE);
}

static RECT SlotRect(int j) {
    RECT r;
    r.left = g_winX + g_px + g_pad + j * (g_ic + g_gap);
    r.top = g_winY + g_py + g_pillH / 2 - S(2) - g_ic / 2;
    r.right = r.left + g_ic; r.bottom = r.top + g_ic;
    return r;
}
static int SlotAt(int mx, int my) {
    int ns = (int)g_items.size() + 2;
    if (my < g_py - S(6) || my > g_py + g_pillH) return -1;
    int j = (int)floorf(((float)(mx - g_px) - g_pad + g_gap / 2.0f) / (float)(g_ic + g_gap));
    return (j < 0 || j >= ns) ? -1 : j;
}

// springs: icons grow near the mouse and settle with a small bounce (fluid, not stiff)
static void AnimStep() {
    int ns = (int)g_items.size() + 2;
    g_scaleCur.resize(ns, 1.0f); g_vel.resize(ns, 0.0f);
    float f = g_mx >= 0 ? ((float)(g_mx - g_px) - g_pad - g_ic / 2.0f) / (float)(g_ic + g_gap) : -99.0f;
    bool moving = false;
    for (int j = 0; j < ns; j++) {
        float d = fabsf(j - f);
        float tgt = g_mx >= 0 ? 1.0f + 0.34f * expf(-d * d / 0.9f) : 1.0f;
        if (j == g_downSlot) tgt *= 0.90f;
        float acc = (tgt - g_scaleCur[j]) * 0.20f;
        g_vel[j] = (g_vel[j] + acc) * 0.74f;
        g_scaleCur[j] += g_vel[j];
        if (fabsf(tgt - g_scaleCur[j]) > 0.002f || fabsf(g_vel[j]) > 0.002f) moving = true;
        else { g_scaleCur[j] = tgt; g_vel[j] = 0; }
    }
    float ta = (g_mx >= 0 && g_hoverSlot >= 0) ? 1.0f : 0.0f;
    g_labelA += (ta - g_labelA) * 0.30f;
    if (fabsf(ta - g_labelA) > 0.01f) moving = true; else g_labelA = ta;
    PaintDock();
    if (!moving) KillTimer(g_dock, T_ANIM);
}

// ================================================================ window genie animation
struct Snap { HBITMAP bmp = nullptr; int w = 0, h = 0; };
static std::map<HWND, Snap> g_snaps;

static void AnimOff(int& old) {
    ANIMATIONINFO ai = {sizeof(ai), 0};
    SystemParametersInfoW(SPI_GETANIMATION, sizeof(ai), &ai, 0);
    old = ai.iMinAnimate; ai.iMinAnimate = 0;
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, 0);
}
static void AnimOn(int old) {
    ANIMATIONINFO ai = {sizeof(ai), old};
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, 0);
}

static RECT Genie(const RECT& W, const RECT& I, double p) {
    double pe = p * p * (3 - 2 * p);
    double pt = pow(pe, 1.6), pb = pow(pe, 0.7), pw = pe;      // top lags, bottom is pulled first = liquid feel
    RECT r;
    r.left = (LONG)(W.left + (I.left - W.left) * pw);
    r.right = (LONG)(W.right + (I.right - W.right) * pw);
    r.top = (LONG)(W.top + (I.top - W.top) * pt);
    r.bottom = (LONG)(W.bottom + (I.bottom - W.bottom) * pb);
    if (r.bottom < r.top + 4) r.bottom = r.top + 4;
    if (r.right < r.left + 4) r.right = r.left + 4;
    return r;
}

static void RunAnim(HBITMAP snap, int sw, int sh, const RECT& W, const RECT& I, bool shrink, HWND ov, HWND minimizeAfterFirst) {
    HDC sdc = GetDC(nullptr);
    HDC src = CreateCompatibleDC(sdc); HGDIOBJ so = SelectObject(src, snap);
    int fw = imax(imax(W.right - W.left, I.right - I.left), 16), fh = imax(imax(W.bottom - W.top, I.bottom - I.top), 16);
    HDC fr = CreateCompatibleDC(sdc); HBITMAP fb = CreateCompatibleBitmap(sdc, fw, fh); HGDIOBJ fo = SelectObject(fr, fb);
    SetStretchBltMode(fr, COLORONCOLOR);
    LARGE_INTEGER fq, t0, t; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);
    const double dur = 0.30;
    bool first = true;
    for (;;) {
        QueryPerformanceCounter(&t);
        double e = (double)(t.QuadPart - t0.QuadPart) / (double)fq.QuadPart;
        double p = e / dur; if (p > 1) p = 1;
        double q = shrink ? p : 1 - p;
        RECT r = Genie(W, I, q);
        int w = imin(r.right - r.left, fw), h = imin(r.bottom - r.top, fh);
        StretchBlt(fr, 0, 0, w, h, src, 0, 0, sw, sh, SRCCOPY);
        double pe = q * q * (3 - 2 * q);
        int a = (int)(255.0 * (1.0 - pow(pe, 2.2)));
        if (a < 0) a = 0;
        POINT pt = {r.left, r.top}, sp = {0, 0}; SIZE sz = {w, h};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)a, 0};
        UpdateLayeredWindow(ov, sdc, &pt, &sz, fr, &sp, 0, &bf, ULW_ALPHA);
        if (first) {
            ShowWindow(ov, SW_SHOWNA);
            if (minimizeAfterFirst) ShowWindow(minimizeAfterFirst, SW_MINIMIZE);
            first = false;
        }
        if (p >= 1) break;
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(6);
    }
    SelectObject(fr, fo); DeleteObject(fb); DeleteDC(fr);
    SelectObject(src, so); DeleteDC(src); ReleaseDC(nullptr, sdc);
}

static HWND MakeOverlay() {
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                           L"LiveDockAnim", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_inst, nullptr);
}

static void DropSnap(HWND h) {
    auto it = g_snaps.find(h);
    if (it != g_snaps.end()) { if (it->second.bmp) DeleteObject(it->second.bmp); g_snaps.erase(it); }
}

static void MinimizeAnimated(HWND hw, const RECT& icon) {
    RECT W; GetWindowRect(hw, &W);
    int w = W.right - W.left, h = W.bottom - W.top;
    if (!g_anim || w < 80 || h < 60) { ShowWindow(hw, SW_MINIMIZE); return; }
    HDC sdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = PrintWindow(hw, mem, PW_RENDERFULLCONTENT);
    SelectObject(mem, old); DeleteDC(mem); ReleaseDC(nullptr, sdc);
    if (!ok) { DeleteObject(bmp); ShowWindow(hw, SW_MINIMIZE); return; }
    DropSnap(hw);
    Snap s; s.bmp = bmp; s.w = w; s.h = h; g_snaps[hw] = s;
    HWND ov = MakeOverlay();
    int oldAnim; AnimOff(oldAnim);
    RunAnim(bmp, w, h, W, icon, true, ov, hw);
    DestroyWindow(ov);
    AnimOn(oldAnim);
}

static void RestoreAnimated(HWND hw, const RECT& icon) {
    auto it = g_snaps.find(hw);
    if (!g_anim || it == g_snaps.end()) { ShowWindow(hw, SW_RESTORE); SetForegroundWindow(hw); return; }
    WINDOWPLACEMENT wp = {sizeof(wp)}; GetWindowPlacement(hw, &wp);
    RECT W = wp.rcNormalPosition;
    if (wp.flags & WPF_RESTORETOMAXIMIZED) {
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hw, MONITOR_DEFAULTTOPRIMARY), &mi);
        W = mi.rcWork;
    }
    HWND ov = MakeOverlay();
    int oldAnim; AnimOff(oldAnim);
    RunAnim(it->second.bmp, it->second.w, it->second.h, W, icon, false, ov, nullptr);
    ShowWindow(hw, SW_RESTORE);
    SetForegroundWindow(hw);
    Sleep(60);
    DestroyWindow(ov);
    AnimOn(oldAnim);
    DropSnap(hw);
}

static LRESULT CALLBACK AnimProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

// ================================================================ studio window (UI)
static const float LW = 900.0f, LH = 620.0f;
static int g_tab = 0, g_scroll = 0;
static float g_smx = -1, g_smy = -1;
struct Btn { float x, y, w, h; int id, arg; };
static std::vector<Btn> g_btns;

enum { B_TAB = 1, B_WALL, B_ADD, B_OPENF, B_PICKVID, B_MAKE, B_Q, B_SZ, B_FPS, B_LEN, B_GH, B_WEB, B_IG, B_MAIL, B_PAY, B_GOCONV };

// convert options
static int o_q = 1, o_sz = 1, o_fps = 1, o_len = 1;
static std::wstring g_vid;
static wchar_t g_status[256] = L"";
static volatile LONG g_busy = 0;
static volatile LONG g_prog1000 = -1;     // -1 = unknown, else 0..1000
static std::wstring g_jobOut;

struct Wp { std::wstring path, name; Bitmap* thumb; };
static std::vector<Wp> g_gal;
static Bitmap* g_logo = nullptr;

static const int TW = 264, TH = 148;

static Bitmap* MakeThumb(Image* im) {
    Bitmap* b = new Bitmap(TW, TH, PixelFormat32bppRGB);
    Graphics g(b);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    float iw = (float)im->GetWidth(), ih = (float)im->GetHeight();
    float sc = (float)dmax(TW / iw, TH / ih);
    float sw = TW / sc, sh = TH / sc;
    g.DrawImage(im, RectF(0, 0, (REAL)TW, (REAL)TH), (iw - sw) / 2, (ih - sh) / 2, sw, sh, UnitPixel);
    return b;
}

static void FreeGallery() {
    for (auto& w : g_gal) delete w.thumb;
    g_gal.clear();
}
static void LoadGallery() {
    FreeGallery();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_wdir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring p = g_wdir + fd.cFileName, e = Ext(p);
        if (e != L".gif" && e != L".png" && e != L".jpg" && e != L".jpeg" && e != L".bmp") continue;
        if (g_gal.size() >= 60) break;
        Image im(p.c_str());
        if (im.GetLastStatus() != Ok) continue;
        Wp w; w.path = p; w.name = Base(p); w.thumb = MakeThumb(&im);
        g_gal.push_back(w);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static bool Button(Graphics& g, float x, float y, float w, float h, const std::wstring& txt, int id, int style, int arg = 0,
                   bool enabled = true) {
    bool hov = enabled && g_smx >= x && g_smx < x + w && g_smy >= y && g_smy < y + h;
    Color fill, tc, border = C(0, 0, 0, 0);
    switch (style) {
    case 1: fill = hov ? C(138, 184, 214) : C(111, 163, 199); tc = C(15, 19, 24); break;                 // primary
    case 3: fill = C(111, 163, 199, 46); tc = C(232, 234, 237); border = C(111, 163, 199); break;        // selected
    case 2: fill = hov ? C(255, 255, 255, 12) : C(0, 0, 0, 0); tc = hov ? C(232, 234, 237) : C(152, 160, 173); border = C(52, 59, 71); break; // ghost
    default: fill = hov ? C(46, 52, 63) : C(34, 39, 49); tc = C(232, 234, 237); break;
    }
    if (!enabled) { fill = C(30, 34, 42); tc = C(98, 106, 119); border = C(0, 0, 0, 0); }
    FillRRect(g, x, y, w, h, 9, fill);
    if (border.GetA()) StrokeRRect(g, x + 0.5f, y + 0.5f, w - 1, h - 1, 9, border, 1.2f);
    Txt(g, txt, x, y, w, h, 14, style == 1, tc, 1);
    if (enabled) g_btns.push_back({x, y, w, h, id, arg});
    return hov;
}

static void Seg(Graphics& g, float x, float y, const wchar_t** labels, int n, int sel, int id, float bw) {
    float total = n * bw + 8;
    FillRRect(g, x, y, total, 40, 13, C(24, 28, 35));
    StrokeRRect(g, x + 0.5f, y + 0.5f, total - 1, 39, 13, C(44, 51, 62), 1.0f);
    for (int i = 0; i < n; i++) {
        float bx = x + 4 + i * bw;
        bool hov = g_smx >= bx && g_smx < bx + bw && g_smy >= y + 4 && g_smy < y + 36;
        if (i == sel) {
            FillRRect(g, bx, y + 4, bw, 32, 10, C(111, 163, 199, 54));
            StrokeRRect(g, bx + 0.5f, y + 4.5f, bw - 1, 31, 10, C(111, 163, 199, 150), 1.0f);
        } else if (hov) FillRRect(g, bx, y + 4, bw, 32, 10, C(255, 255, 255, 12));
        Txt(g, labels[i], bx, y + 4, bw, 32, 14, i == sel, i == sel ? C(240, 242, 245) : C(152, 160, 173), 1);
        g_btns.push_back({bx, y + 4, bw, 32, id, i});
    }
}

static void PaintHeader(Graphics& g) {
    SolidBrush hb(C(20, 24, 30)); g.FillRectangle(&hb, 0.0f, 0.0f, LW, 76.0f);
    SolidBrush ln(C(34, 39, 49)); g.FillRectangle(&ln, 0.0f, 75.0f, LW, 1.0f);
    if (g_logo) g.DrawImage(g_logo, RectF(28, 16, 44, 44));
    Txt(g, L"LiveDock", 84, 13, 200, 30, 23, true, C(240, 242, 245));
    Txt(g, std::wstring(L"by faizzyhon  -  v") + APP_VER, 85, 42, 260, 20, 12, false, C(140, 148, 161));
    const wchar_t* tabs[3] = {L"Wallpapers", L"Video to GIF", L"About & Support"};
    Seg(g, LW - 34 - 404, 18, tabs, 3, g_tab, B_TAB, 132);
}

static void PaintWallTab(Graphics& g) {
    Txt(g, L"Wallpapers", 34, 92, 400, 36, 26, true, C(240, 242, 245));
    Txt(g, L"Click one to use it. Drop a GIF or image on this window to add it.", 34, 128, 760, 22, 14, false, C(140, 148, 161));
    float vt = 162, vb = 548;
    int total = (int)g_gal.size() + 1;
    int rows = (total + 2) / 3;
    float cw = 264, ch = 198, gap = 20;
    int content = (int)(rows * (ch + gap));
    int maxScroll = imax(0, content - (int)(vb - vt));
    if (g_scroll > maxScroll) g_scroll = maxScroll;
    if (g_scroll < 0) g_scroll = 0;
    GraphicsState st = g.Save();
    g.SetClip(RectF(0, vt, LW, vb - vt));
    std::wstring active = ActiveWall();
    for (int k = 0; k < total; k++) {
        int col = k % 3, row = k / 3;
        float x = 34 + col * (cw + gap), y = vt + row * (ch + gap) - g_scroll;
        if (y + ch < vt || y > vb) continue;
        bool isBuiltin = k == 0;
        const Wp* wp = isBuiltin ? nullptr : &g_gal[k - 1];
        bool inUse = isBuiltin ? (active == L"builtin" || (active.empty() && g_mode == M_SCENE))
                               : (!_wcsicmp(active.c_str(), wp->path.c_str()));
        bool hov = g_smx >= x && g_smx < x + cw && g_smy >= y && g_smy < y + ch && g_smy >= vt && g_smy < vb;
        FillRRect(g, x, y, cw, ch, 16, hov ? C(30, 35, 44) : C(25, 29, 36));
        StrokeRRect(g, x + 0.5f, y + 0.5f, cw - 1, ch - 1, 16, inUse ? C(111, 163, 199) : (hov ? C(66, 76, 92) : C(40, 46, 57)), inUse ? 2.0f : 1.0f);
        GraphicsState s2 = g.Save();
        GraphicsPath cp; RRPath(cp, x + 8, y + 8, cw - 16, 142, 11);
        g.SetClip(&cp, CombineModeIntersect);
        if (isBuiltin) g.DrawImage(g_sb, RectF(x + 8, y + 8, cw - 16, 142));
        else if (wp->thumb) g.DrawImage(wp->thumb, RectF(x + 8, y + 8, cw - 16, 142));
        g.Restore(s2);
        std::wstring nm = isBuiltin ? L"Built-in calm" : wp->name;
        Txt(g, nm, x + 14, y + 156, cw - 28, 34, 14, false, C(232, 234, 237));
        if (!isBuiltin) {
            std::wstring e = Ext(wp->path); for (auto& c : e) c = (wchar_t)towupper(c);
            if (!e.empty()) e = e.substr(1);
            FillRRect(g, x + cw - 8 - 46, y + 16, 38, 20, 10, C(14, 17, 22, 190));
            Txt(g, e, x + cw - 8 - 46, y + 16, 38, 20, 10, true, C(214, 218, 225), 1);
        }
        if (inUse) {
            FillRRect(g, x + 16, y + 16, 62, 22, 11, C(111, 163, 199));
            Txt(g, L"IN USE", x + 16, y + 16, 62, 22, 11, true, C(15, 19, 24), 1);
        }
        if (y + ch / 2 > vt && y + ch / 2 < vb) g_btns.push_back({x, y, cw, ch, B_WALL, k - 1});
    }
    g.Restore(st);
    if (g_gal.empty())
        Txt(g, L"No files yet. Click 'Add file' or make a GIF from a video.", 34 + cw + 44, vt + 50, 480, 60, 14, false, C(140, 148, 161), 0, false, true);
    SolidBrush fb(C(20, 24, 30)); g.FillRectangle(&fb, 0.0f, 556.0f, LW, 64.0f);
    SolidBrush fl(C(34, 39, 49)); g.FillRectangle(&fl, 0.0f, 556.0f, LW, 1.0f);
    Button(g, 34, 568, 130, 40, L"Add file...", B_ADD, 0);
    Button(g, 174, 568, 150, 40, L"Open folder", B_OPENF, 2);
    Button(g, LW - 34 - 250, 568, 250, 40, L"Make GIF from a video  >", B_GOCONV, 1);
}

static void PaintConvTab(Graphics& g) {
    Txt(g, L"Video to GIF wallpaper", 34, 92, 600, 36, 26, true, C(240, 242, 245));
    Txt(g, L"Pick a video. LiveDock makes a high quality GIF and sets it as your wallpaper.", 34, 128, 800, 22, 14, false, C(140, 148, 161));
    bool busy = g_busy != 0;
    FillRRect(g, 34, 160, LW - 68, 268, 16, C(25, 29, 36));
    StrokeRRect(g, 34.5f, 160.5f, LW - 69, 267, 16, C(40, 46, 57), 1.0f);
    float lx = 58, cx = 206, y = 176;
    Txt(g, L"Video", lx, y, 140, 40, 14, false, C(140, 148, 161));
    FillRRect(g, cx, y, 470, 40, 10, C(18, 21, 26));
    Txt(g, g_vid.empty() ? L"No video chosen. Click 'Choose video' or drop a file here." : Base(g_vid) + Ext(g_vid), cx + 14, y, 442, 40, 14, false,
        g_vid.empty() ? C(110, 118, 131) : C(232, 234, 237));
    Button(g, cx + 482, y, 148, 40, L"Choose video...", B_PICKVID, 0, 0, !busy);
    y += 52;
    const wchar_t* ql[3] = {L"Best", L"Balanced", L"Light"};
    Txt(g, L"Quality", lx, y, 140, 40, 14, false, C(140, 148, 161)); Seg(g, cx, y, ql, 3, o_q, B_Q, 120);
    y += 52;
    const wchar_t* sl[4] = {L"Original", L"1920", L"1280", L"854"};
    Txt(g, L"Size (width)", lx, y, 140, 40, 14, false, C(140, 148, 161)); Seg(g, cx, y, sl, 4, o_sz, B_SZ, 100);
    y += 52;
    const wchar_t* fl[3] = {L"30 fps", L"24 fps", L"15 fps"};
    Txt(g, L"Smoothness", lx, y, 140, 40, 14, false, C(140, 148, 161)); Seg(g, cx, y, fl, 3, o_fps, B_FPS, 100);
    y += 52;
    const wchar_t* ll[4] = {L"5 sec", L"10 sec", L"20 sec", L"30 sec"};
    Txt(g, L"Length", lx, y, 140, 40, 14, false, C(140, 148, 161)); Seg(g, cx, y, ll, 4, o_len, B_LEN, 100);
    Txt(g, L"A GIF can show only 256 colors in each frame. 'Best' makes a new set of colors for every frame, "
           L"the best a GIF can do. Big GIFs use more memory, so use 1280 or Light on a slow PC.",
        34, 438, LW - 68, 44, 13, false, C(140, 148, 161), 0, false, true);
    float py = 496;
    FillRRect(g, 34, py, LW - 68, 8, 4, C(34, 39, 49));
    LONG pr = g_prog1000;
    if (busy) {
        if (pr < 0) {
            float ph = (float)((GetTickCount() / 6) % 1000) / 1000.0f;
            FillRRect(g, 34 + (LW - 68 - 160) * ph, py, 160, 8, 4, C(111, 163, 199));
        } else FillRRect(g, 34, py, (LW - 68) * (pr / 1000.0f), 8, 4, C(111, 163, 199));
    } else if (pr == 1000) FillRRect(g, 34, py, LW - 68, 8, 4, C(123, 179, 138));
    Txt(g, g_status, 34, py + 12, LW - 68, 26, 14, false, C(232, 234, 237));
    SolidBrush fb(C(20, 24, 30)); g.FillRectangle(&fb, 0.0f, 556.0f, LW, 64.0f);
    SolidBrush fl2(C(34, 39, 49)); g.FillRectangle(&fl2, 0.0f, 556.0f, LW, 1.0f);
    Button(g, LW - 34 - 260, 568, 260, 40, busy ? L"Working..." : L"Make GIF wallpaper", B_MAKE, 1, 0, !busy && !g_vid.empty());
}

static void PaintAboutTab(Graphics& g) {
    if (g_logo) g.DrawImage(g_logo, RectF(34, 98, 76, 76));
    Txt(g, L"LiveDock", 128, 94, 400, 46, 36, true, C(240, 242, 245));
    Txt(g, L"Live wallpaper. Glass dock. Light on your PC.", 130, 140, 600, 26, 16, false, C(140, 148, 161));
    Txt(g, L"LiveDock gives Windows a live wallpaper and a floating glass dock instead of the normal taskbar. "
           L"It is tiny, uses very little RAM and CPU, and is made to run on low-end PCs.",
        34, 196, LW - 68, 50, 14, false, C(210, 214, 220), 0, false, true);
    Txt(g, L"Made by Muhammad Faizan (faizzyhon) - AI Engineer, Madinah, Saudi Arabia", 34, 254, LW - 68, 24, 14, true, C(232, 234, 237));
    float x = 34, y = 292;
    Button(g, x, y, 150, 40, L"GitHub", B_GH, 0); x += 160;
    Button(g, x, y, 150, 40, L"faizzyhon.com", B_WEB, 0); x += 160;
    Button(g, x, y, 150, 40, L"Instagram", B_IG, 0); x += 160;
    Button(g, x, y, 150, 40, L"Email me", B_MAIL, 0);
    FillRRect(g, 34, 360, LW - 68, 140, 16, C(25, 29, 36));
    StrokeRRect(g, 34.5f, 360.5f, LW - 69, 139, 16, C(40, 46, 57), 1.0f);
    Txt(g, L"Like LiveDock? Support my work", 60, 378, 500, 30, 18, true, C(240, 242, 245));
    Txt(g, L"LiveDock is free and open source. If it helps you, a small support on PayPal means a lot. It is optional.",
        60, 412, 560, 50, 13, false, C(140, 148, 161), 0, false, true);
    Button(g, LW - 34 - 26 - 230, 398, 230, 44, L"Support on PayPal", B_PAY, 1);
    SolidBrush fb(C(20, 24, 30)); g.FillRectangle(&fb, 0.0f, 556.0f, LW, 64.0f);
    SolidBrush fl(C(34, 39, 49)); g.FillRectangle(&fl, 0.0f, 556.0f, LW, 1.0f);
    Txt(g, std::wstring(L"A side vibe-coded project  -  v") + APP_VER + L"  -  MIT License  -  github.com/faizzyhon/LiveDock", 34, 568, LW - 68, 40, 13, false, C(110, 118, 131));
}

static void StudioPaint(HDC dc, int W, int H) {
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.ScaleTransform(g_sc, g_sc);
        g_btns.clear();
        SolidBrush bg(C(18, 21, 26)); g.FillRectangle(&bg, 0.0f, 0.0f, LW, LH);
        if (g_tab == 0) PaintWallTab(g); else if (g_tab == 1) PaintConvTab(g); else PaintAboutTab(g);
        PaintHeader(g);   // header last so scrolled cards never overlap it
    }
    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
}

static void StudioRefresh() { if (g_studio) InvalidateRect(g_studio, nullptr, FALSE); }

// ================================================================ video -> GIF
static DWORD RunHidden(const std::wstring& cmd, const std::wstring& progFile = L"", double totalSec = 0) {
    std::wstring c = cmd;
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &c[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) return 1;
    while (WaitForSingleObject(pi.hProcess, 400) == WAIT_TIMEOUT) {
        if (progFile.empty() || totalSec <= 0) continue;
        HANDLE f = CreateFileW(progFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        DWORD sz = GetFileSize(f, nullptr);
        if (sz != INVALID_FILE_SIZE) {
            DWORD want = sz > 2048 ? 2048 : sz;
            SetFilePointer(f, (LONG)(sz - want), nullptr, FILE_BEGIN);
            char buf[2049]; DWORD got = 0;
            if (ReadFile(f, buf, want, &got, nullptr)) {
                buf[got] = 0;
                const char* key = "out_time_us="; char* last = nullptr; char* p = buf;
                while ((p = strstr(p, key))) { last = p; p += 12; }
                if (last) {
                    double us = atof(last + 12);
                    LONG v = (LONG)(us / 1e6 / totalSec * 1000.0);
                    if (v < 0) v = 0;
                    if (v > 990) v = 990;
                    g_prog1000 = v;
                    wsprintfW(g_status, L"Making GIF...  %d%%", (int)(v / 10));
                }
            }
        }
        CloseHandle(f);
    }
    DWORD rc = 1; GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return rc;
}

static std::wstring UniqueOut(const std::wstring& base) {
    CreateDirectoryW(g_wdir.c_str(), nullptr);
    std::wstring safe = base;
    for (auto& ch : safe) if (wcschr(L"\\/:*?\"<>|", ch)) ch = L'_';
    std::wstring p = g_wdir + safe + L".gif";
    for (int i = 2; GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES && i < 999; i++)
        p = g_wdir + safe + L" (" + std::to_wstring(i) + L").gif";
    return p;
}

static DWORD WINAPI ConvThread(LPVOID) {
    std::wstring ff = g_dir + L"ffmpeg.exe";
    if (GetFileAttributesW(ff.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_prog1000 = -1;
        lstrcpynW(g_status, L"Getting the video tool (one time, about 90 MB)... please wait", 255);
        RunHidden(L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ProgressPreference='SilentlyContinue'; "
                  L"[Net.ServicePointManager]::SecurityProtocol='Tls12'; $t=Join-Path $env:TEMP 'ldff'; "
                  L"Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue; New-Item $t -ItemType Directory | Out-Null; "
                  L"$z=Join-Path $t 'f.zip'; Invoke-WebRequest 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip' -OutFile $z; "
                  L"tar -xf $z -C $t; "
                  L"Copy-Item (Get-ChildItem $t -Recurse -Filter ffmpeg.exe | Select-Object -First 1).FullName '" + ff + L"'; "
                  L"Remove-Item $t -Recurse -Force\"");
    }
    LONG ok = 0;
    if (GetFileAttributesW(ff.c_str()) != INVALID_FILE_ATTRIBUTES) {
        lstrcpynW(g_status, L"Making GIF...  0%", 255);
        const int widths[4] = {0, 1920, 1280, 854}, fpss[3] = {30, 24, 15}, lens[4] = {5, 10, 20, 30};
        int fps = fpss[o_fps], secs = lens[o_len];
        std::wstring scale = widths[o_sz] ? L"scale=" + std::to_wstring(widths[o_sz]) + L":-1:flags=lanczos"
                                          : L"scale='min(iw,3840)':-1:flags=lanczos";
        std::wstring pal;
        if (o_q == 0) pal = L"palettegen=max_colors=256:stats_mode=single[p];[b][p]paletteuse=dither=sierra2_4a:new=1";
        else if (o_q == 1) pal = L"palettegen=max_colors=256:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle";
        else pal = L"palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle";
        std::wstring pf = g_dir + L"ffprogress.txt";
        DeleteFileW(pf.c_str());
        std::wstring tmp = g_dir + L"convert.tmp.gif";
        std::wstring cmd = L"\"" + ff + L"\" -y -hide_banner -loglevel error -nostats -progress \"" + pf + L"\" -hwaccel auto -t " +
            std::to_wstring(secs) + L" -i \"" + g_vid + L"\" -vf \"fps=" + std::to_wstring(fps) + L"," + scale +
            L",split[a][b];[a]" + pal + L"\" -loop 0 \"" + tmp + L"\"";
        DWORD rc = RunHidden(cmd, pf, secs);
        DeleteFileW(pf.c_str());
        if (rc == 0 && GetFileAttributesW(tmp.c_str()) != INVALID_FILE_ATTRIBUTES) {
            g_jobOut = UniqueOut(Base(g_vid));
            ok = MoveFileExW(tmp.c_str(), g_jobOut.c_str(), MOVEFILE_REPLACE_EXISTING) ? 1 : 0;
        } else DeleteFileW(tmp.c_str());
    }
    if (ok) { g_prog1000 = 1000; lstrcpynW(g_status, L"Done. Your GIF wallpaper is on.", 255); }
    else { g_prog1000 = -1; lstrcpynW(g_status, L"Could not make the GIF. Check your internet and the video file, then try again.", 255); }
    InterlockedExchange(&g_busy, 0);
    PostMessageW(g_dock, WM_CONV, (WPARAM)ok, 0);
    return 0;
}

static void StartConvert() {
    if (g_busy || g_vid.empty()) return;
    g_busy = 1; g_prog1000 = -1;
    lstrcpynW(g_status, L"Starting...", 255);
    HANDLE t = CreateThread(nullptr, 0, ConvThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t); else g_busy = 0;
    StudioRefresh();
}

static bool IsVideoExt(const std::wstring& e) {
    return e == L".mp4" || e == L".mkv" || e == L".mov" || e == L".avi" || e == L".webm" || e == L".wmv" || e == L".m4v";
}

static bool PickFile(HWND owner, const wchar_t* filter, std::wstring& out) {
    wchar_t f[MAX_PATH] = L"";
    OPENFILENAMEW o = {sizeof(o)};
    o.hwndOwner = owner; o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.lpstrFilter = filter;
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&o)) return false;
    out = f; return true;
}

static void AddToGallery(const std::wstring& src, bool use) {
    CreateDirectoryW(g_wdir.c_str(), nullptr);
    std::wstring dst = g_wdir + Base(src) + Ext(src);
    if (_wcsicmp(src.c_str(), dst.c_str())) CopyFileW(src.c_str(), dst.c_str(), FALSE);
    LoadGallery();
    if (use) SetWallpaper(dst);
    StudioRefresh();
}

static void HandleBtn(int id, int arg) {
    switch (id) {
    case B_TAB: g_tab = arg; g_scroll = 0; if (arg == 0) LoadGallery(); break;
    case B_GOCONV: g_tab = 1; break;
    case B_WALL:
        if (arg < 0) SetWallpaper(L"builtin"); else if (arg < (int)g_gal.size()) SetWallpaper(g_gal[arg].path);
        break;
    case B_ADD: {
        std::wstring p;
        if (PickFile(g_studio, L"GIF / images\0*.gif;*.png;*.jpg;*.jpeg;*.bmp\0\0", p)) AddToGallery(p, true);
        break; }
    case B_OPENF: CreateDirectoryW(g_wdir.c_str(), nullptr); OpenUrl(g_wdir); break;
    case B_PICKVID: {
        std::wstring p;
        if (PickFile(g_studio, L"Video\0*.mp4;*.mkv;*.mov;*.avi;*.webm;*.wmv;*.m4v\0\0", p)) g_vid = p;
        break; }
    case B_MAKE: StartConvert(); break;
    case B_Q: o_q = arg; break;
    case B_SZ: o_sz = arg; break;
    case B_FPS: o_fps = arg; break;
    case B_LEN: o_len = arg; break;
    case B_GH: OpenUrl(L"https://github.com/faizzyhon"); break;
    case B_WEB: OpenUrl(L"https://faizzyhon.com"); break;
    case B_IG: OpenUrl(L"https://instagram.com/faizzyhon"); break;
    case B_MAIL: OpenUrl(L"mailto:faizzyhon@gmail.com?subject=LiveDock"); break;
    case B_PAY: { std::wstring u = PayPal(); if (!u.empty()) OpenUrl(u); break; }
    }
    StudioRefresh();
}

static int HitBtn(float mx, float my, Btn* out) {
    for (int i = (int)g_btns.size() - 1; i >= 0; i--) {
        const Btn& b = g_btns[i];
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) { if (out) *out = b; return i; }
    }
    return -1;
}

static LRESULT CALLBACK StudioProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        StudioPaint(dc, rc.right, rc.bottom);
        EndPaint(h, &ps); return 0; }
    case WM_MOUSEMOVE:
        g_smx = (short)LOWORD(l) / g_sc; g_smy = (short)HIWORD(l) / g_sc;
        InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_MOUSELEAVE: g_smx = g_smy = -1; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && HitBtn(g_smx, g_smy, nullptr) >= 0) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONUP: {
        Btn b; float mx = (short)LOWORD(l) / g_sc, my = (short)HIWORD(l) / g_sc;
        if (HitBtn(mx, my, &b) >= 0) HandleBtn(b.id, b.arg);
        return 0; }
    case WM_MOUSEWHEEL:
        if (g_tab == 0) { g_scroll -= (short)HIWORD(w) / 120 * 60; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_DROPFILES: {
        wchar_t f[MAX_PATH];
        if (DragQueryFileW((HDROP)w, 0, f, MAX_PATH)) {
            std::wstring p = f, e = Ext(p);
            if (IsVideoExt(e)) { g_vid = p; g_tab = 1; }
            else if (e == L".gif" || e == L".png" || e == L".jpg" || e == L".jpeg" || e == L".bmp") { g_tab = 0; AddToGallery(p, true); }
        }
        DragFinish((HDROP)w); InvalidateRect(h, nullptr, FALSE); return 0; }
    case WM_TIMER:
        if (g_busy || g_prog1000 >= 0) InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 10); FreeGallery(); g_studio = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void OpenStudio(int tab) {
    if (g_studio) { g_tab = tab; ShowWindow(g_studio, SW_SHOW); if (IsIconic(g_studio)) ShowWindow(g_studio, SW_RESTORE); SetForegroundWindow(g_studio); StudioRefresh(); return; }
    g_tab = tab; g_scroll = 0;
    if (!g_logo) {
        HICON ic = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 128, 128, LR_DEFAULTCOLOR);
        if (ic) { g_logo = Bitmap::FromHICON(ic); DestroyIcon(ic); }
    }
    CreateDirectoryW(g_wdir.c_str(), nullptr);
    LoadGallery();
    int ww = (int)(LW * g_sc), wh = (int)(LH * g_sc);
    RECT r = {0, 0, ww, wh};
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, st, FALSE);
    int w = r.right - r.left, hh = r.bottom - r.top;
    g_studio = CreateWindowExW(0, L"LiveDockStudio", L"LiveDock Studio - by faizzyhon", st,
                               (g_sw - w) / 2, (g_sh - hh) / 2, w, hh, nullptr, nullptr, g_inst, nullptr);
    BOOL dark = TRUE; DwmSetWindowAttribute(g_studio, 20, &dark, sizeof(dark));
    HICON big = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
    if (big) { SendMessageW(g_studio, WM_SETICON, ICON_BIG, (LPARAM)big); SendMessageW(g_studio, WM_SETICON, ICON_SMALL, (LPARAM)big); }
    DragAcceptFiles(g_studio, TRUE);
    SetTimer(g_studio, 10, 250, nullptr);
    ShowWindow(g_studio, SW_SHOW);
    SetForegroundWindow(g_studio);
}

// ================================================================ shell glue
static void HideTray(bool hide) {
    HWND t = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (t && IsWindowVisible(t) == (BOOL)hide) ShowWindow(t, hide ? SW_HIDE : SW_SHOWNA);
    HWND s = nullptr;
    while ((s = FindWindowExW(nullptr, s, L"Shell_SecondaryTrayWnd", nullptr))) ShowWindow(s, hide ? SW_HIDE : SW_SHOWNA);
}
static void ApplyWorkArea() {
    RECT wa = {0, 0, g_sw, g_sh - (g_pillH + S(10) + S(16))};
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &wa, SPIF_SENDCHANGE);
}
static void AddTray() {
    NOTIFYICONDATAW n = {sizeof(n)};
    n.hWnd = g_dock; n.uID = 1; n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
    if (!n.hIcon) n.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy(n.szTip, L"LiveDock by faizzyhon");
    Shell_NotifyIconW(NIM_ADD, &n);
}
static void Balloon(const wchar_t* t, const wchar_t* m) {
    NOTIFYICONDATAW n = {sizeof(n)};
    n.hWnd = g_dock; n.uID = 1; n.uFlags = NIF_INFO; n.dwInfoFlags = NIIF_INFO;
    wcsncpy(n.szInfoTitle, t, 63); wcsncpy(n.szInfo, m, 255);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void ShowMenu(int slot) {
    int idx = slot - 1;
    bool canPin = idx >= 0 && idx < (int)g_items.size() && !g_items[idx].own && g_items[idx].path[0] != L'#';
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_STUDIO, L"Open LiveDock Studio");
    SetMenuDefaultItem(m, ID_STUDIO, FALSE);
    if (canPin) AppendMenuW(m, MF_STRING, ID_PIN, g_items[idx].pinned ? L"Unpin from dock" : L"Pin to dock");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_glass ? MF_CHECKED : 0), ID_GLASS, L"Liquid glass (backdrop blur)");
    AppendMenuW(m, MF_STRING | (g_anim ? MF_CHECKED : 0), ID_ANIM, L"Window animations");
    AppendMenuW(m, MF_STRING | (AutoOn() ? MF_CHECKED : 0), ID_AUTO, L"Start with Windows");
    AppendMenuW(m, MF_STRING, ID_FIXWALL, L"Fix wallpaper (reconnect)");
    HMENU ab = CreatePopupMenu();
    AppendMenuW(ab, MF_STRING, ID_ABOUT, L"About LiveDock");
    AppendMenuW(ab, MF_STRING, ID_GITHUB, L"GitHub: faizzyhon");
    AppendMenuW(ab, MF_STRING, ID_SITE, L"Website: faizzyhon.com");
    AppendMenuW(ab, MF_STRING, ID_INSTA, L"Instagram: @faizzyhon");
    AppendMenuW(ab, MF_STRING, ID_PAYPAL, L"Support on PayPal");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)ab, L"LiveDock by faizzyhon");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit (brings back Windows taskbar)");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(g_dock);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, g_dock, nullptr);
    PostMessageW(g_dock, WM_NULL, 0, 0);
    DestroyMenu(m);
    switch (cmd) {
    case ID_STUDIO: OpenStudio(0); break;
    case ID_GLASS: g_glass = !g_glass; IniSet(L"Main", L"Glass", g_glass ? L"1" : L"0"); g_cacheDirty = true; RedrawDock(); break;
    case ID_ANIM: g_anim = !g_anim; IniSet(L"Main", L"Animations", g_anim ? L"1" : L"0"); break;
    case ID_AUTO: SetAuto(!AutoOn()); break;
    case ID_FIXWALL: AttachWall(); LoadWall(); break;
    case ID_ABOUT: OpenStudio(2); break;
    case ID_GITHUB: OpenUrl(L"https://github.com/faizzyhon"); break;
    case ID_SITE: OpenUrl(L"https://faizzyhon.com"); break;
    case ID_INSTA: OpenUrl(L"https://instagram.com/faizzyhon"); break;
    case ID_PAYPAL: OpenUrl(PayPal()); break;
    case ID_EXIT: DestroyWindow(g_dock); break;
    case ID_PIN:
        if (canPin) {
            std::wstring p2 = g_items[idx].path; bool was = g_items[idx].pinned;
            if (was) { for (size_t i = 0; i < g_pins.size(); i++) if (!_wcsicmp(g_pins[i].c_str(), p2.c_str())) { g_pins.erase(g_pins.begin() + i); break; } }
            else g_pins.push_back(p2);
            SavePins(); Refresh(true);
        }
        break;
    }
}

static void Click(int slot) {
    int ns = (int)g_items.size() + 2;
    if (slot < 0) return;
    if (slot == 0) { keybd_event(VK_LWIN, 0, 0, 0); keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0); return; }
    if (slot == ns - 1) { OpenStudio(0); return; }
    Item& it = g_items[slot - 1];
    RECT ir = SlotRect(slot);
    if (it.hw && IsWindow(it.hw)) {
        if (IsIconic(it.hw)) RestoreAnimated(it.hw, ir);
        else if (GetForegroundWindow() == it.hw) MinimizeAnimated(it.hw, ir);
        else SetForegroundWindow(it.hw);
    } else if (it.pinned) {
        ShellExecuteW(nullptr, L"open", it.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    Refresh(false);
}

static void TrimMem() { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }

static void Tick() {
    HideTray(true);
    WallHealth();
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
    if (m == g_msgTaskbar && g_msgTaskbar) {
        AttachWall(); LoadWall(); HideTray(true); AddTray(); ApplyWorkArea(); g_sig.clear(); Refresh(true);
        return 0;
    }
    switch (m) {
    case WM_TIMER:
        if (w == T_WALL) WallTick();
        else if (w == T_DOCK) Tick();
        else if (w == T_ANIM) AnimStep();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        if (!g_tracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); g_tracking = true; }
        g_mx = (short)LOWORD(l); g_my = (short)HIWORD(l);
        g_hoverSlot = SlotAt(g_mx, g_my);
        if (g_hoverSlot < 0) g_mx = -1;
        SetTimer(h, T_ANIM, 16, nullptr);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false; g_mx = -1; g_hoverSlot = -1; g_downSlot = -1; SetTimer(h, T_ANIM, 16, nullptr);
        return 0;
    case WM_LBUTTONDOWN:
        g_downSlot = SlotAt((short)LOWORD(l), (short)HIWORD(l)); SetTimer(h, T_ANIM, 16, nullptr); return 0;
    case WM_LBUTTONUP: {
        int sl = SlotAt((short)LOWORD(l), (short)HIWORD(l));
        g_downSlot = -1; SetTimer(h, T_ANIM, 16, nullptr);
        if (sl >= 0) Click(sl);
        return 0; }
    case WM_RBUTTONUP: { int sl = SlotAt((short)LOWORD(l), (short)HIWORD(l)); if (sl >= 0) ShowMenu(sl); return 0; }
    case WM_TRAY:
        if (l == WM_LBUTTONUP) OpenStudio(0);
        else if (l == WM_RBUTTONUP) ShowMenu(-1);
        return 0;
    case WM_CONV:
        if (w) {
            SetWallpaper(g_jobOut);
            LoadGallery();
            Balloon(L"LiveDock", L"Done. GIF wallpaper is on.");
        } else Balloon(L"LiveDock", L"Could not make the GIF.");
        StudioRefresh();
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
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    wchar_t b[MAX_PATH * 2]; GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    g_exe = b; g_dir = g_exe.substr(0, g_exe.find_last_of(L'\\') + 1);
    g_ini = g_dir + L"livedock.ini"; g_wdir = g_dir + L"wallpapers\\";
    HDC sdc = GetDC(nullptr); g_dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc);
    g_sc = g_dpi / 96.0f;
    g_sw = GetSystemMetrics(SM_CXSCREEN); g_sh = GetSystemMetrics(SM_CYSCREEN);
    g_glass = GetPrivateProfileIntW(L"Main", L"Glass", 1, g_ini.c_str()) != 0;
    g_anim = GetPrivateProfileIntW(L"Main", L"Animations", 1, g_ini.c_str()) != 0;

    for (int i = 0; i < 60 && !(FindWindowW(L"Progman", nullptr) && FindWindowW(L"Shell_TrayWnd", nullptr)); i++) Sleep(500);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &g_origWA, 0);

    GdiplusStartupInput gi; ULONG_PTR gt;
    GdiplusStartup(&gt, &gi, nullptr);
    g_ff = new FontFamily(L"Segoe UI Variable Display");
    if (!g_ff->IsAvailable()) { delete g_ff; g_ff = new FontFamily(L"Segoe UI"); }
    InitLut();
    g_sb = new Bitmap(SW_, SH_, PixelFormat32bppRGB);

    WNDCLASSW wc = {};
    wc.hInstance = hi; wc.lpszClassName = L"LiveDockWall"; wc.lpfnWndProc = WallProc;
    RegisterClassW(&wc);
    wc.lpszClassName = L"LiveDockAnim"; wc.lpfnWndProc = AnimProc;
    RegisterClassW(&wc);
    wc.lpszClassName = L"LiveDockDock"; wc.lpfnWndProc = DockProc; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    wc.lpszClassName = L"LiveDockStudio"; wc.lpfnWndProc = StudioProc; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");

    g_dock = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                             L"LiveDockDock", L"LiveDock", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hi, nullptr);
    AttachWall();
    LoadWall();
    LoadPins();
    HideTray(true);
    Refresh(true);
    ApplyWorkArea();
    AddTray();
    ShowWindow(g_dock, SW_SHOWNA);
    SetTimer(g_dock, T_DOCK, 1500, nullptr);

    bool first = !GetPrivateProfileIntW(L"Main", L"FirstRun", 0, g_ini.c_str());
    if (first) { SetAuto(true); IniSet(L"Main", L"FirstRun", L"1"); }
    if (first) OpenStudio(2);
    TrimMem();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    FreeItems(); FreeGallery(); delete g_img; delete g_sb;
    GdiplusShutdown(gt);
    CloseHandle(mtx);
    return 0;
}
