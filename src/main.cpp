#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <atomic>
#include <thread>
#include <mutex>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")

struct Step { int x, y, delay; };

std::vector<Step> steps;
std::mutex stepsMutex;
std::atomic<bool> running(false), closing(false);
HWND hList, hDelay, hAdd, hDelete, hStart, hStop, hStatus;
constexpr UINT HOTKEY_ID = 1001;
constexpr UINT WM_UPDATE_STATUS = WM_APP + 1;

void DoClick(int x, int y) {
    SetCursorPos(x, y);
    INPUT in[2]{};
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}

void RefreshList() {
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    std::lock_guard<std::mutex> lock(stepsMutex);
    for (size_t i=0;i<steps.size();++i) {
        std::wstring s = L"#" + std::to_wstring(i+1) +
            L"   X=" + std::to_wstring(steps[i].x) +
            L"  Y=" + std::to_wstring(steps[i].y) +
            L"  delay=" + std::to_wstring(steps[i].delay) + L" ms";
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)s.c_str());
    }
}

void SetStatus(const wchar_t* text) {
    SetWindowTextW(hStatus, text);
}

void ClickerLoop() {
    while (!closing) {
        if (!running) { Sleep(20); continue; }

        std::vector<Step> local;
        { std::lock_guard<std::mutex> lock(stepsMutex); local = steps; }

        if (local.empty()) {
            running = false;
            PostMessageW(GetParent(hList), WM_UPDATE_STATUS, 0, 0);
            continue;
        }

        for (const auto& s : local) {
            if (!running || closing) break;
            DoClick(s.x, s.y);
            int left = std::max(0, s.delay);
            while (left > 0 && running && !closing) {
                Sleep((DWORD)std::min(left, 20));
                left -= std::min(left, 20);
            }
        }
    }
}

void SaveConfig(HWND owner) {
    std::wofstream f(L"clicker_config.txt");
    if (!f) { MessageBoxW(owner,L"Не удалось сохранить настройки.",L"Ошибка",MB_ICONERROR); return; }
    std::lock_guard<std::mutex> lock(stepsMutex);
    for (auto s: steps) f << s.x << L' ' << s.y << L' ' << s.delay << L'\n';
    MessageBoxW(owner,L"Настройки сохранены в clicker_config.txt",L"Готово",MB_OK);
}

void LoadConfig(HWND owner) {
    std::wifstream f(L"clicker_config.txt");
    if (!f) { MessageBoxW(owner,L"Файл clicker_config.txt не найден.",L"Информация",MB_ICONINFORMATION); return; }
    std::vector<Step> loaded; Step s;
    while (f >> s.x >> s.y >> s.delay) loaded.push_back(s);
    { std::lock_guard<std::mutex> lock(stepsMutex); steps = loaded; }
    RefreshList();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC",L"Multi-Task Clicker",WS_CHILD|WS_VISIBLE,20,15,350,30,hwnd,0,0,0);
        CreateWindowW(L"STATIC",L"Задержка (мс):",WS_CHILD|WS_VISIBLE,20,55,110,22,hwnd,0,0,0);
        hDelay=CreateWindowW(L"EDIT",L"100",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,130,52,80,25,hwnd,(HMENU)101,0,0);
        hAdd=CreateWindowW(L"BUTTON",L"Добавить текущую позицию",WS_CHILD|WS_VISIBLE,225,50,210,30,hwnd,(HMENU)102,0,0);
        hList=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|LBS_NOTIFY|WS_VSCROLL,20,95,415,250,hwnd,(HMENU)103,0,0);
        hDelete=CreateWindowW(L"BUTTON",L"Удалить выбранную",WS_CHILD|WS_VISIBLE,20,355,200,32,hwnd,(HMENU)104,0,0);
        CreateWindowW(L"BUTTON",L"Сохранить",WS_CHILD|WS_VISIBLE,235,355,95,32,hwnd,(HMENU)105,0,0);
        CreateWindowW(L"BUTTON",L"Загрузить",WS_CHILD|WS_VISIBLE,340,355,95,32,hwnd,(HMENU)106,0,0);
        hStart=CreateWindowW(L"BUTTON",L"▶ Запустить (F6)",WS_CHILD|WS_VISIBLE,20,405,200,40,hwnd,(HMENU)107,0,0);
        hStop=CreateWindowW(L"BUTTON",L"■ Остановить",WS_CHILD|WS_VISIBLE,235,405,200,40,hwnd,(HMENU)108,0,0);
        hStatus=CreateWindowW(L"STATIC",L"Статус: остановлен",WS_CHILD|WS_VISIBLE,20,465,415,25,hwnd,0,0,0);

        RegisterHotKey(hwnd,HOTKEY_ID,0,VK_F6);
        std::thread(ClickerLoop).detach();
        break;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case 102: {
            wchar_t buf[32]; GetWindowTextW(hDelay,buf,32);
            int delay=_wtoi(buf); POINT p; GetCursorPos(&p);
            { std::lock_guard<std::mutex> lock(stepsMutex); steps.push_back({p.x,p.y,delay}); }
            RefreshList(); break;
        }
        case 104: {
            int sel=(int)SendMessageW(hList,LB_GETCURSEL,0,0);
            if(sel!=LB_ERR) {
                std::lock_guard<std::mutex> lock(stepsMutex);
                if(sel<(int)steps.size()) steps.erase(steps.begin()+sel);
                RefreshList();
            } break;
        }
        case 105: SaveConfig(hwnd); break;
        case 106: LoadConfig(hwnd); break;
        case 107:
            if (!steps.empty()) { running=true; SetStatus(L"Статус: ЗАПУЩЕН — F6 для остановки"); } break;
        case 108:
            running=false; SetStatus(L"Статус: остановлен"); break;
        }
        break;
    case WM_HOTKEY:
        if(wp==HOTKEY_ID && !steps.empty()) {
            running=!running;
            SetStatus(running ? L"Статус: ЗАПУЩЕН — F6 для остановки" : L"Статус: остановлен");
        }
        break;
    case WM_CLOSE:
        running=false; closing=true; UnregisterHotKey(hwnd,HOTKEY_ID); DestroyWindow(hwnd); break;
    case WM_DESTROY: PostQuitMessage(0); break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE, PWSTR,int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hInst; wc.lpszClassName=L"MultiTaskClicker";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&wc);
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"Multi-Task Clicker",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,475,535,nullptr,nullptr,hInst,nullptr);
    ShowWindow(hwnd,nCmdShow); UpdateWindow(hwnd);
    MSG msg; while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return 0;
}
