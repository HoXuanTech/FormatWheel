#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "core.h"
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <ole2.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int width = 760;
constexpr int height = 780;
constexpr int center_x = 380;
constexpr int center_y = 405;
constexpr int outer_r = 285;
constexpr int inner_r = 145;
constexpr double pi = 3.14159265358979323846;
constexpr UINT task_done = WM_APP + 1;

struct State {
    std::vector<std::wstring> files;
    int page = 0;
    int hover = -1;
    bool busy = false;
    bool overlay = false;
    bool was_shift = false;
    DWORD released_at = 0;
    std::wstring message = L"選擇檔案，或把檔案拖到此視窗";
    HFONT font = nullptr;
    HFONT bold = nullptr;
};
State state;

void text(HDC dc, int x, int y, int box_width, int box_height, const std::wstring& value, bool bold = false, UINT align = DT_CENTER) {
    SelectObject(dc, bold ? state.bold : state.font);
    RECT rect = {x, y, x + box_width, y + box_height};
    DrawTextW(dc, value.c_str(), -1, &rect, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

int sector(int x, int y) {
    const double dx = x - center_x, dy = y - center_y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance < inner_r || distance > outer_r) return -1;
    double angle = std::atan2(dy, dx) + pi / 2 + pi / 8;
    while (angle < 0) angle += 2 * pi;
    while (angle >= 2 * pi) angle -= 2 * pi;
    return static_cast<int>(angle / (pi / 4));
}

void paint(HWND hwnd) {
    PAINTSTRUCT paint_info;
    HDC dc = BeginPaint(hwnd, &paint_info);
    RECT window_rect;
    GetClientRect(hwnd, &window_rect);
    HBRUSH background = CreateSolidBrush(RGB(11, 15, 24));
    FillRect(dc, &window_rect, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245, 247, 252));

    HBRUSH top = CreateSolidBrush(RGB(24, 34, 52));
    RECT top_rect = {0, 0, width, 83};
    FillRect(dc, &top_rect, top);
    DeleteObject(top);
    text(dc, 25, 17, 350, 33, L"格式輪盤 · Windows", true, DT_LEFT);
    HBRUSH button = CreateSolidBrush(RGB(43, 116, 208));
    RECT button_rect = {555, 17, 728, 61};
    FillRect(dc, &button_rect, button);
    DeleteObject(button);
    text(dc, 555, 20, 173, 36, L"選擇檔案", true);

    const fw::Kind kind = state.files.empty() ? fw::Kind::other : fw::kind_of(state.files.front());
    const auto slots = fw::actions(kind, state.page);
    const COLORREF colors[] = {RGB(47, 75, 111), RGB(47, 85, 103), RGB(58, 68, 107), RGB(62, 80, 99),
                               RGB(55, 72, 102), RGB(66, 73, 103), RGB(56, 85, 93), RGB(64, 70, 98)};
    HPEN edge = CreatePen(PS_SOLID, 2, RGB(167, 184, 211));
    HGDIOBJ old_pen = SelectObject(dc, edge);
    for (int i = 0; i < 8; ++i) {
        const double start = -pi / 2 - pi / 8 + i * pi / 4;
        POINT wedge[12];
        wedge[0] = {center_x, center_y};
        for (int j = 0; j <= 10; ++j) {
            const double a = start + j * pi / 40;
            wedge[j + 1] = {center_x + static_cast<int>(std::cos(a) * outer_r), center_y + static_cast<int>(std::sin(a) * outer_r)};
        }
        HBRUSH fill = CreateSolidBrush(i == state.hover ? RGB(46, 132, 224) : colors[i]);
        HGDIOBJ old = SelectObject(dc, fill);
        Polygon(dc, wedge, 12);
        SelectObject(dc, old);
        DeleteObject(fill);
    }
    SelectObject(dc, old_pen);
    DeleteObject(edge);
    HBRUSH center = CreateSolidBrush(RGB(19, 28, 45));
    HGDIOBJ old = SelectObject(dc, center);
    Ellipse(dc, center_x - inner_r, center_y - inner_r, center_x + inner_r, center_y + inner_r);
    SelectObject(dc, old);
    DeleteObject(center);

    for (int i = 0; i < static_cast<int>(slots.size()) && i < 8; ++i) {
        const double a = -pi / 2 + i * pi / 4;
        const int x = center_x + static_cast<int>(std::sin(a) * 218);
        const int y = center_y - static_cast<int>(std::cos(a) * 218);
        const bool enabled = fw::unavailable_reason(slots[i].id).empty();
        SetTextColor(dc, enabled ? RGB(255, 255, 255) : RGB(170, 181, 195));
        text(dc, x - 75, y - 22, 150, 30, slots[i].label, true);
        SetTextColor(dc, enabled ? RGB(196, 210, 228) : RGB(145, 155, 172));
        text(dc, x - 80, y + 8, 160, 22, enabled ? slots[i].detail : L"尚未可用");
    }
    SetTextColor(dc, RGB(246, 248, 251));
    text(dc, center_x - 125, center_y - 79, 250, 31, std::wstring(fw::kind_label(kind)) + (state.page ? L" · 工具" : L" · 格式轉換"), true);
    if (!state.files.empty()) text(dc, center_x - 120, center_y - 28, 240, 33, state.files.front().substr(state.files.front().find_last_of(L"\\/") + 1), true);
    else text(dc, center_x - 120, center_y - 28, 240, 33, L"尚未選擇檔案", true);
    text(dc, center_x - 115, center_y + 25, 230, 28, state.page ? L"2 / 2" : L"1 / 2");

    SetTextColor(dc, RGB(210, 221, 237));
    text(dc, 22, 710, 716, 30, state.message, false, DT_LEFT);
    text(dc, 22, 741, 716, 24, L"按 Shift 切換頁面；點選輪盤格子執行。結果儲存在原檔案旁邊。", false, DT_LEFT);
    EndPaint(hwnd, &paint_info);
}

void choose_files(HWND hwnd) {
    std::vector<wchar_t> buffer(65536, 0);
    OPENFILENAMEW request = {};
    request.lStructSize = sizeof(request);
    request.hwndOwner = hwnd;
    request.lpstrFile = buffer.data();
    request.nMaxFile = static_cast<DWORD>(buffer.size());
    request.lpstrFilter = L"所有檔案\0*.*\0\0";
    request.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&request)) return;
    const wchar_t* first = buffer.data();
    const wchar_t* next = first + wcslen(first) + 1;
    state.files.clear();
    if (!*next) state.files.push_back(first);
    else for (const wchar_t* item = next; *item; item += wcslen(item) + 1) state.files.push_back(std::wstring(first) + L"\\" + item);
    state.page = 0;
    state.message = std::to_wstring(state.files.size()) + L" 個檔案已選取";
    InvalidateRect(hwnd, nullptr, TRUE);
}

void start_action(HWND hwnd, const std::wstring& id) {
    if (state.busy || state.files.empty()) return;
    const auto unavailable = fw::unavailable_reason(id);
    if (!unavailable.empty()) {
        state.message = unavailable;
        InvalidateRect(hwnd, nullptr, TRUE);
        return;
    }
    state.busy = true;
    state.message = L"正在處理…";
    InvalidateRect(hwnd, nullptr, TRUE);
    const auto files = state.files;
    std::thread([hwnd, id, files] {
        std::wstring result;
        bool okay = true;
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        try {
            if (id == L"gen.zip" || id == L"gen.path" || id == L"gen.name" || id == L"pdf.merge" || id == L"img.to.pdf") {
                result = fw::run(id, files);
            } else {
                int succeeded = 0;
                for (const auto& file : files) {
                    if (fw::kind_of(file) != fw::kind_of(files.front())) continue;
                    result = fw::run(id, {file});
                    ++succeeded;
                }
                if (!succeeded) throw std::runtime_error("沒有相符的檔案");
                if (succeeded > 1) result = std::to_wstring(succeeded) + L" 個檔案已處理";
            }
        }
        catch (const std::exception&) { okay = false; result = L"此功能需要額外工具，或檔案格式尚未支援。"; }
        if (SUCCEEDED(initialized)) CoUninitialize();
        auto* message = new std::wstring(okay ? L"完成：" + result : L"失敗：" + result);
        PostMessageW(hwnd, task_done, okay ? 1 : 0, reinterpret_cast<LPARAM>(message));
    }).detach();
}

class FileDropTarget final : public IDropTarget {
    LONG refs = 1;
    HWND window;
    bool accepts = false;
public:
    explicit FileDropTarget(HWND hwnd) : window(hwnd) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropTarget) { *object = static_cast<IDropTarget*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = static_cast<ULONG>(InterlockedDecrement(&refs));
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        accepts = data && SUCCEEDED(data->QueryGetData(&format));
        if (effect) *effect = accepts ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        if (effect) *effect = accepts ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { accepts = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL point, DWORD* effect) override {
        if (effect) *effect = accepts ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (!accepts || !data) return S_OK;
        FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM storage = {};
        if (FAILED(data->GetData(&format, &storage))) return S_OK;
        HDROP drop = static_cast<HDROP>(GlobalLock(storage.hGlobal));
        if (drop) {
            state.files.clear();
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::vector<wchar_t> path(length + 1);
                DragQueryFileW(drop, i, path.data(), length + 1);
                state.files.emplace_back(path.data());
            }
            GlobalUnlock(storage.hGlobal);
            state.message = std::to_wstring(count) + L" 個檔案已加入";
            InvalidateRect(window, nullptr, TRUE);
            POINT cursor = {point.x, point.y};
            ScreenToClient(window, &cursor);
            const int selected = sector(cursor.x, cursor.y);
            if (selected >= 0 && !state.files.empty()) {
                const auto slots = fw::actions(fw::kind_of(state.files.front()), state.page);
                if (selected < static_cast<int>(slots.size())) start_action(window, slots[selected].id);
            }
            if (state.overlay) {
                state.overlay = false;
                SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
        }
        ReleaseStgMedium(&storage);
        accepts = false;
        return S_OK;
    }
};

void update_overlay(HWND hwnd) {
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool mouse = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (shift && !state.was_shift && mouse && !state.busy) {
        if (!state.overlay) {
            POINT cursor;
            GetCursorPos(&cursor);
            SetWindowPos(hwnd, HWND_TOPMOST, cursor.x - width / 2, cursor.y - center_y, width, height + 40, SWP_SHOWWINDOW);
            state.overlay = true;
            state.released_at = 0;
        } else {
            state.page = 1 - state.page;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    state.was_shift = shift;
    if (mouse) state.released_at = 0;
    else if (!state.released_at) state.released_at = GetTickCount();
    if (!mouse && state.overlay && GetTickCount() - state.released_at > 300) {
        state.overlay = false;
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 40, nullptr);
        state.font = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");
        state.bold = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");
        return 0;
    case WM_TIMER:
        if (wparam == 1) update_overlay(hwnd);
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        state.files.clear();
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::vector<wchar_t> path(length + 1);
            DragQueryFileW(drop, i, path.data(), length + 1);
            state.files.emplace_back(path.data());
        }
        DragFinish(drop);
        state.page = 0;
        state.message = std::to_wstring(count) + L" 個檔案已加入";
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int current = sector(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (current != state.hover) { state.hover = current; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
        if (x >= 555 && x <= 728 && y >= 17 && y <= 61) { choose_files(hwnd); return 0; }
        const int selected = sector(x, y);
        if (selected >= 0) {
            const fw::Kind kind = state.files.empty() ? fw::Kind::other : fw::kind_of(state.files.front());
            const auto slots = fw::actions(kind, state.page);
            if (selected < static_cast<int>(slots.size())) start_action(hwnd, slots[selected].id);
        } else if (x >= center_x - inner_r && x <= center_x + inner_r && y >= center_y - inner_r && y <= center_y + inner_r) {
            state.page = 1 - state.page;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_SHIFT || wparam == VK_TAB) { state.page = 1 - state.page; InvalidateRect(hwnd, nullptr, TRUE); }
        if (wparam == VK_ESCAPE) { state.files.clear(); state.message = L"已取消"; InvalidateRect(hwnd, nullptr, TRUE); }
        return 0;
    case task_done: {
        std::unique_ptr<std::wstring> result(reinterpret_cast<std::wstring*>(lparam));
        state.busy = false;
        state.message = *result;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_PAINT: paint(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        RevokeDragDrop(hwnd);
        if (state.font) DeleteObject(state.font);
        if (state.bold) DeleteObject(state.bold);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int count = 0;
    LPWSTR* words = CommandLineToArgvW(GetCommandLineW(), &count);
    if (words && count >= 4 && std::wstring(words[1]) == L"--run") {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int result = 1;
        try { fw::run(words[2], {words[3]}); result = 0; } catch (...) { result = 1; }
        CoUninitialize();
        LocalFree(words);
        return result;
    }
    if (words) LocalFree(words);
    OleInitialize(nullptr);
    WNDCLASSW klass = {};
    klass.lpfnWndProc = window_proc;
    klass.hInstance = instance;
    klass.lpszClassName = L"FormatWheelWin8";
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    RegisterClassW(&klass);
    HWND hwnd = CreateWindowExW(0, klass.lpszClassName, L"格式輪盤 FormatWheel", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, width, height + 40, nullptr, nullptr, instance, nullptr);
    if (!hwnd) { OleUninitialize(); return 1; }
    auto* drop_target = new FileDropTarget(hwnd);
    RegisterDragDrop(hwnd, drop_target);
    drop_target->Release();
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    OleUninitialize();
    return static_cast<int>(message.wParam);
}
