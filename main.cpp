#pragma execution_character_set("utf-8")
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <mmsystem.h>
#include <sapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <cmath>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "sapi.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")

// ================== КОНФИГ (пункт 9) ==================
namespace Cfg {
    constexpr int kPrepMs        = 6000;
    constexpr int kGdiMs         = 12000;
    constexpr int kBallMs        = 31000;
    constexpr int kBsodMs        = 3500;
    constexpr int kPizdaCount    = 7;
    constexpr int kPizdaSpeedMin = 15;
    constexpr int kPizdaSpeedMax = 35;
    constexpr int kPizdaW        = 300;
    constexpr int kPizdaH        = 80;
    constexpr int kBallRadius    = 150;
}

// ================== КЛАССЫ ОКОН ==================
const wchar_t* MAIN_CLASS  = L"WinUpdSimMain";
const wchar_t* PIZDA_CLASS = L"WinUpdSimPizda";
const wchar_t* BALL_CLASS  = L"WinUpdSimBall";

// ================== ГЛОБАЛЬНОЕ СОСТОЯНИЕ ==================
std::atomic<int>  g_phase{0};      // 1=подготовка, 2=прогресс, 3=BSOD
std::atomic<int>  g_progress{0};
std::atomic<bool> g_stopThreads{false};   // для GDI/pizda/bass
std::atomic<bool> g_appExit{false};       // для hotkey-потока
std::atomic<bool> g_killSwitch{false};    // аварийный стоп
std::atomic<bool> g_debugMode{false};

HWND g_mainWnd  = nullptr;
HWND g_ballWnd  = nullptr;
std::vector<HWND> g_pizdaWnds;

struct BallState { double x, y, vx, vy, r; } g_ball;

// Пункт 11: убраны «шутка» и «пранк», добавлены мрачные вариации
const wchar_t* kPizdaTexts[] = {
    L"TEBE PIZDA",
    L"СИСТЕМА ВЗЛОМАНА",
    L"ERROR 0xDEADBEEF",
    L"ВСЕ ФАЙЛЫ ЗАШИФРОВАНЫ",
    L"ДАННЫЕ УТЕРЯНЫ",
    L"Я ТЕБЯ ВИЖУ",
    L"ПОПРОЩАЙСЯ",
    L"ТЫ СЛЕДУЮЩИЙ",
    L"КЛЮЧ УТЕРЯН НАВСЕГДА",
    L"НЕ ОТКЛЮЧАЙ ПИТАНИЕ",
    L"ЭТО НЕ ОБНОВЛЕНИЕ"
};
constexpr int kPizdaTextCount = 11;

// ================== УТИЛИТЫ ==================
void PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void SleepPump(int ms) {
    DWORD start = GetTickCount();
    while ((int)(GetTickCount() - start) < ms) {
        PumpMessages();
        Sleep(15);
    }
}

void RepaintNow(HWND hwnd) {
    if (!hwnd) return;
    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);
    PumpMessages();
}

// ================== SAPI (пункт 6) ==================
void Speak(const wchar_t* text) {
    ISpVoice* v = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr,
        CLSCTX_ALL, IID_ISpVoice, (void**)&v))) {
        v->Speak(text, 0, nullptr);
        v->Release();
    }
}

// ================== ОКНО ОБНОВЛЕНИЯ ==================
LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        int ph = g_phase.load();

        if (ph == 3) {
            // === ФЕЙКОВЫЙ BSOD (пункт 4 доработок) ===
            HBRUSH bg = CreateSolidBrush(RGB(0, 120, 215));
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));

            HFONT fBig = CreateFontW(140, 0, 0, 0, FW_LIGHT, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
            HFONT fMid = CreateFontW(34, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
            HFONT fSml = CreateFontW(20, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");

            HFONT old = (HFONT)SelectObject(hdc, fBig);
            RECT rBig = { 80, 100, rc.right, 340 };
            DrawTextW(hdc, L":(", -1, &rBig, DT_LEFT | DT_TOP | DT_SINGLELINE);

            SelectObject(hdc, fMid);
            RECT rMid1 = { 80, 340, rc.right - 80, 400 };
            DrawTextW(hdc,
                L"На вашем ПК возникла проблема, и его необходимо перезагрузить.",
                -1, &rMid1, DT_LEFT | DT_TOP | DT_WORDBREAK);
            RECT rMid2 = { 80, 480, rc.right - 80, 560 };
            DrawTextW(hdc,
                L"Мы собираем сведения об ошибке, после чего выполним перезагрузку.",
                -1, &rMid2, DT_LEFT | DT_TOP | DT_WORDBREAK);

            SelectObject(hdc, fSml);
            RECT rSml1 = { 80, 620, rc.right - 80, 660 };
            DrawTextW(hdc,
                L"Код остановки: CRITICAL_PROCESS_DIED",
                -1, &rSml1, DT_LEFT | DT_TOP | DT_SINGLELINE);
            RECT rSml2 = { 80, 660, rc.right - 80, 700 };
            DrawTextW(hdc,
                L"Ошибка: 0x000000EF (0xFFFFF8032A41B080, 0x0)",
                -1, &rSml2, DT_LEFT | DT_TOP | DT_SINGLELINE);
            RECT rSml3 = { 80, 700, rc.right - 80, 740 };
            DrawTextW(hdc,
                L"Прогресс: 0% завершено",
                -1, &rSml3, DT_LEFT | DT_TOP | DT_SINGLELINE);

            SelectObject(hdc, old);
            DeleteObject(fBig);
            DeleteObject(fMid);
            DeleteObject(fSml);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // === Обычная синяя заливка ===
        HBRUSH bg = CreateSolidBrush(RGB(0, 120, 215));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        std::wstring text;
        if (ph == 1) text = L"Подготовка к Обновлению Windows";
        else if (ph == 2)
            text = L"Обновление Windows: " + std::to_wstring(g_progress.load()) + L"%";

        HFONT font = CreateFontW(52, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT old = (HFONT)SelectObject(hdc, font);
        DrawTextW(hdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(font);

        if (ph == 2) {
            int w = rc.right, h = rc.bottom;
            int barW = 500, barH = 22;
            int bx = (w - barW) / 2;
            int by = h / 2 + 80;
            RECT bgR = { bx, by, bx + barW, by + barH };
            HBRUSH bgb = CreateSolidBrush(RGB(0, 60, 120));
            FillRect(hdc, &bgR, bgb);
            DeleteObject(bgb);

            int fillW = barW * g_progress.load() / 100;
            if (fillW > 0) {
                RECT fgR = { bx, by, bx + fillW, by + barH };
                HBRUSH fgb = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(hdc, &fgR, fgb);
                DeleteObject(fgb);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ================== ОКНО TEBE PIZDA ==================
LRESULT CALLBACK PizdaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(160, 0, 0));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        int idx = (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (idx < 0 || idx >= kPizdaTextCount) idx = 0;
        const wchar_t* txt = kPizdaTexts[idx];

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 0));
        HFONT f = CreateFontW(30, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Impact");
        HFONT old = (HFONT)SelectObject(hdc, f);
        DrawTextW(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(f);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ================== ОКНО 3D-ШАРА ==================
LRESULT CALLBACK BallProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        int cx = (int)g_ball.x, cy = (int)g_ball.y, R = (int)g_ball.r;
        for (int r = R; r > 0; --r) {
            double t = (double)r / (double)R;
            int base = (int)(255.0 * (1.0 - t * t));
            COLORREF col = RGB(base / 5, base / 10, base);
            HBRUSH b = CreateSolidBrush(col);
            HPEN   p = CreatePen(PS_SOLID, 1, col);
            HGDIOBJ ob = SelectObject(hdc, b);
            HGDIOBJ op = SelectObject(hdc, p);
            int ox = cx - (int)(t * R * 0.35);
            int oy = cy - (int)(t * R * 0.35);
            Ellipse(hdc, ox - r, oy - r, ox + r, oy + r);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(b);
            DeleteObject(p);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ================== РЕГИСТРАЦИЯ КЛАССОВ ==================
void RegisterClasses(HINSTANCE hInst) {
    WNDCLASSW wc = {};
    wc.hInstance = hInst;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassW(&wc);

    wc.lpfnWndProc = PizdaProc;
    wc.lpszClassName = PIZDA_CLASS;
    RegisterClassW(&wc);

    wc.lpfnWndProc = BallProc;
    wc.lpszClassName = BALL_CLASS;
    RegisterClassW(&wc);
}

// ================== ПЛАВНЫЙ ПРОГРЕСС (пункт 5) ==================
void AdvanceProgress(int from, int to, int totalMs) {
    DWORD start = GetTickCount();
    int cur = from;
    g_progress = cur;
    RepaintNow(g_mainWnd);
    while (cur < to) {
        int elapsed = (int)(GetTickCount() - start);
        if (elapsed >= totalMs) break;
        if (g_killSwitch.load()) { cur = to; break; }

        if (rand() % 12 != 0) cur++;
        if (cur > to) cur = to;
        g_progress = cur;
        RepaintNow(g_mainWnd);

        // «залипания» как в настоящем Windows Update
        int pause = (rand() % 9 == 0) ? (350 + rand() % 600) : 70;
        SleepPump(pause);
    }
    g_progress = to;
    RepaintNow(g_mainWnd);
}

// ================== GDI-ХАОС (пункт 7: тряска) ==================
void GDIChaosThread(int durationMs) {
    HDC hdcScreen = GetDC(nullptr);
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    DWORD start = GetTickCount();
    int iter = 0;

    while (!g_stopThreads.load() && !g_killSwitch.load() &&
           (int)(GetTickCount() - start) < durationMs) {
        for (int i = 0; i < 40; ++i) {
            int x = rand() % w;
            int y = rand() % h;
            int rw = 50 + rand() % 600;
            int rh = 30 + rand() % 300;
            RECT r = { x, y, x + rw, y + rh };
            InvertRect(hdcScreen, &r);
        }
        if (rand() % 8 == 0) {
            BitBlt(hdcScreen, 0, 0, w, h, hdcScreen, 0, 0, NOTSRCCOPY);
        }
        // Пункт 7: подёргивание экрана каждые ~10 итераций
        if (++iter % 10 == 0) {
            int dx = (rand() % 5) - 2;
            int dy = (rand() % 5) - 2;
            BitBlt(hdcScreen, dx, dy, w, h, hdcScreen, 0, 0, SRCCOPY);
        }
        Sleep(60);
    }
    ReleaseDC(nullptr, hdcScreen);
}

// ================== ДВИЖЕНИЕ TEBE PIZDA ==================
void MovePizdaThread(int durationMs) {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    int n = (int)g_pizdaWnds.size();
    if (n == 0) return;

    std::vector<double> vx(n), vy(n);
    std::vector<int>    x(n),  y(n);
    for (int i = 0; i < n; ++i) {
        x[i] = rand() % (w - Cfg::kPizdaW);
        y[i] = rand() % (h - Cfg::kPizdaH);
        int sp = Cfg::kPizdaSpeedMin +
                 rand() % (Cfg::kPizdaSpeedMax - Cfg::kPizdaSpeedMin + 1);
        vx[i] = (rand() % 2 ? 1 : -1) * sp;
        vy[i] = (rand() % 2 ? 1 : -1) * sp;
    }

    DWORD start = GetTickCount();
    while (!g_stopThreads.load() && !g_killSwitch.load() &&
           (int)(GetTickCount() - start) < durationMs) {
        for (int i = 0; i < n; ++i) {
            x[i] += (int)vx[i];
            y[i] += (int)vy[i];
            if (x[i] < 0 || x[i] > w - Cfg::kPizdaW) vx[i] = -vx[i];
            if (y[i] < 0 || y[i] > h - Cfg::kPizdaH) vy[i] = -vy[i];
            SetWindowPos(g_pizdaWnds[i], HWND_TOPMOST, x[i], y[i],
                         Cfg::kPizdaW, Cfg::kPizdaH, SWP_NOACTIVATE);
        }
        Sleep(25);
    }
}

// ================== БАСОВЫЙ ЗВУК ==================
void BassSoundThread(int durationMs) {
    DWORD start = GetTickCount();
    while (!g_stopThreads.load() && !g_killSwitch.load() &&
           (int)(GetTickCount() - start) < durationMs) {
        Beep(60, 400);
        Beep(45, 300);
        Beep(90, 500);
    }
}

// ================== АВАРИЙНЫЙ СТОП (пункт 10, оставили) ==================
void HotkeyThread() {
    while (!g_appExit.load()) {
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_MENU)    & 0x8000) &&
            (GetAsyncKeyState(VK_SHIFT)   & 0x8000) &&
            (GetAsyncKeyState('Q')        & 0x8000)) {
            g_killSwitch = true;
            g_stopThreads = true;
            return;
        }
        Sleep(50);
    }
}

// ================== РЕЕСТР / ВТОРОЙ ЗАПУСК ==================
bool IsSecondRun() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\WinUpdatePrank",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

void MarkForSecondRun() {
    HKEY hKey; DWORD disp;
    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WinUpdatePrank", 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disp);
    RegCloseKey(hKey);

    HKEY hRun;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hRun) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        RegSetValueExW(hRun, L"WinUpdatePrank", 0, REG_SZ,
            (const BYTE*)path,
            (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
        RegCloseKey(hRun);
    }
}

// ================== УБОРКА ПОСЛЕ ВТОРОГО ЗАПУСКА (пункт 8) ==================
void ScheduleSelfDelete() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    MoveFileExW(path, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

void CleanupAfterSecondRun() {
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\WinUpdatePrank");
    HKEY hRun;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hRun) == ERROR_SUCCESS) {
        RegDeleteValueW(hRun, L"WinUpdatePrank");
        RegCloseKey(hRun);
    }
    // Пункт 8: запланировать удаление самого .exe
    ScheduleSelfDelete();
}

// ================== РЕБУТ ==================
void DoReboot() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    if (OpenProcessToken(GetCurrentProcess(),
                         TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME,
                              &tkp.Privileges[0].Luid);
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, nullptr);
        CloseHandle(hToken);
    }
    ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_APPLICATION);
}

// ================== ВОССТАНОВЛЕНИЕ UI (пункт 3) ==================
void HideShellUI(HWND& hTray, HWND& hProg) {
    ShowCursor(FALSE);
    hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    hProg = FindWindowW(L"Progman", nullptr);
    if (hTray) ShowWindow(hTray, SW_HIDE);
    if (hProg) ShowWindow(hProg, SW_HIDE);
}

void RestoreShellUI(HWND hTray, HWND hProg) {
    if (hTray) ShowWindow(hTray, SW_SHOW);
    if (hProg) ShowWindow(hProg, SW_SHOW);
    ShowCursor(TRUE);
}

// ================== ТОЧКА ВХОДА ==================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // Пункт 1: мьютекс от повторного запуска
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"WinUpdatePrankMutex_v1");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Пункт 2: проверка отладчика
    g_debugMode = (IsDebuggerPresent() != FALSE);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    srand((unsigned)time(nullptr));

    // ===== Второй запуск (после ребута) =====
    if (IsSecondRun()) {
        CleanupAfterSecondRun();
        Speak(L"Это был пранк");
        MessageBoxW(nullptr,
            L"Ха-ха! Это был ПРАНК! :)\n\n"
            L"Никакого обновления Windows не было.\n"
            L"Это была безобидная шутка.",
            L"Пранк",
            MB_OK | MB_ICONINFORMATION | MB_TOPMOST);

        CoUninitialize();
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 0;
    }

    // ===== Шаг 1: вопрос =====
    int r = MessageBoxW(nullptr,
        L"Доступно обновление Windows.\n\nУстановить сейчас?",
        L"Центр обновления Windows",
        MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
    if (r != IDYES) {
        CoUninitialize();
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 0;
    }

    MarkForSecondRun();
    RegisterClasses(hInst);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    // Пункт 3: спрятать курсор и панель задач
    HWND hTray = nullptr, hProg = nullptr;
    HideShellUI(hTray, hProg);

    // Старт hotkey-потока
    std::thread tHot(HotkeyThread);

    // Главное окно
    g_mainWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        MAIN_CLASS, L"Windows Update",
        WS_POPUP, 0, 0, sw, sh,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_mainWnd, SW_SHOW);
    SetWindowPos(g_mainWnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);

    // ===== Шаг 2: подготовка 6 сек =====
    g_phase = 1;
    RepaintNow(g_mainWnd);
    Speak(L"Подготовка к обновлению Windows");
    SleepPump(Cfg::kPrepMs);

    // ===== Шаг 3: прогресс (плавный — пункт 5) =====
    g_phase = 2;
    AdvanceProgress(0, 14, 3000);
    if (!g_killSwitch.load()) AdvanceProgress(14, 32, 6000);

    // Ошибка
    MessageBoxW(g_mainWnd,
        L"Не удалось обновить Windows.\n\nКод ошибки: 0x80070005",
        L"Ошибка обновления",
        MB_OK | MB_ICONERROR | MB_TOPMOST);

    DestroyWindow(g_mainWnd);
    g_mainWnd = nullptr;
    PumpMessages();

    // ===== Шаг 4: 7 окон TEBE PIZDA + GDI + бас =====
    for (int i = 0; i < Cfg::kPizdaCount; ++i) {
        HWND hw = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            PIZDA_CLASS, L"",
            WS_POPUP,
            rand() % (sw - Cfg::kPizdaW),
            rand() % (sh - Cfg::kPizdaH),
            Cfg::kPizdaW, Cfg::kPizdaH,
            nullptr, nullptr, hInst, nullptr);
        SetWindowLongPtrW(hw, GWLP_USERDATA, rand() % kPizdaTextCount);
        ShowWindow(hw, SW_SHOW);
        g_pizdaWnds.push_back(hw);
    }

    g_stopThreads = false;
    std::thread tGdi (GDIChaosThread,  Cfg::kGdiMs);
    std::thread tMove(MovePizdaThread, Cfg::kGdiMs);
    std::thread tBass(BassSoundThread, Cfg::kGdiMs);

    SleepPump(Cfg::kGdiMs);

    g_stopThreads = true;
    tGdi.join();
    tMove.join();
    tBass.join();

    for (HWND hw : g_pizdaWnds) DestroyWindow(hw);
    g_pizdaWnds.clear();
    PumpMessages();

    // Перерисовать экран, убрать остатки GDI-инверсии
    RedrawWindow(nullptr, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
    SleepPump(500);

    // ===== Шаг 5: 3D-шар 31 сек =====
    g_ballWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        BALL_CLASS, L"",
        WS_POPUP, 0, 0, sw, sh,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_ballWnd, SW_SHOW);
    SetWindowPos(g_ballWnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);

    g_ball.x  = sw / 2.0;
    g_ball.y  = sh / 2.0;
    g_ball.r  = (double)Cfg::kBallRadius;
    g_ball.vx = 6.0;
    g_ball.vy = 4.5;

    DWORD ballStart = GetTickCount();
    while ((int)(GetTickCount() - ballStart) < Cfg::kBallMs &&
           !g_killSwitch.load()) {
        g_ball.x += g_ball.vx;
        g_ball.y += g_ball.vy;
        if (g_ball.x - g_ball.r < 0)  { g_ball.x = g_ball.r;      g_ball.vx = -g_ball.vx; }
        if (g_ball.x + g_ball.r > sw) { g_ball.x = sw - g_ball.r; g_ball.vx = -g_ball.vx; }
        if (g_ball.y - g_ball.r < 0)  { g_ball.y = g_ball.r;      g_ball.vy = -g_ball.vy; }
        if (g_ball.y + g_ball.r > sh) { g_ball.y = sh - g_ball.r; g_ball.vy = -g_ball.vy; }

        RepaintNow(g_ballWnd);
        SleepPump(20);
    }

    DestroyWindow(g_ballWnd);
    g_ballWnd = nullptr;
    PumpMessages();

    // ===== Шаг 5.5: фейковый BSOD (пункт 4 доработок) =====
    if (!g_killSwitch.load()) {
        g_phase = 3;
        g_mainWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            MAIN_CLASS, L"",
            WS_POPUP, 0, 0, sw, sh,
            nullptr, nullptr, hInst, nullptr);
        ShowWindow(g_mainWnd, SW_SHOW);
        SetWindowPos(g_mainWnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
        RepaintNow(g_mainWnd);
        Speak(L"Сбой системы. Перезагрузка.");
        SleepPump(Cfg::kBsodMs);
        DestroyWindow(g_mainWnd);
        g_mainWnd = nullptr;
        PumpMessages();
    }

    // ===== Шаг 6: реальная перезагрузка =====
    g_appExit = true;
    RestoreShellUI(hTray, hProg);

    if (!g_killSwitch.load() && !g_debugMode.load()) {
        DoReboot();
    } else {
        const wchar_t* reason = g_debugMode.load()
            ? L"Debug mode: reboot skipped."
            : L"Kill switch: reboot aborted.";
        MessageBoxW(nullptr, reason, L"WinUpdatePrank", MB_OK | MB_TOPMOST);
    }

    if (tHot.joinable()) tHot.join();
    CoUninitialize();
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return 0;
  }
