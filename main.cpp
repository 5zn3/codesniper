#include <windows.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <string>
#include <thread>
#include <atomic>
#include <regex>
#include <vector>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

// --- INTERFACE FOR OCR ---
struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")) __declspec(novtable)
IMemoryBufferByteAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

// --- GLOBAL VARIABLES ---
std::atomic<bool> isRunning(false);
int insertCount = 3;
RECT captureRect = { 100, 100, 500, 200 };

HWND hMainWnd = NULL;
HWND hSelectorWnd = NULL;
HWND hLogEdit = NULL;
HWND hCountEdit = NULL;
HWND hBtnToggle = NULL;

// --- UTILS ---
void LogMessage(const std::wstring& msg) {
    if (!hLogEdit) return;
    int len = GetWindowTextLengthW(hLogEdit);
    SendMessageW(hLogEdit, EM_SETSEL, len, len);
    std::wstring text = msg + L"\r\n";
    SendMessageW(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

void PressKey(WORD key) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    SendInput(1, &input, sizeof(INPUT));
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void PressCtrlV() {
    INPUT inputs[4] = { 0 };
    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

void SetClipboardText(const std::wstring& text) {
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.length() + 1) * sizeof(wchar_t));
        if (hMem) {
            memcpy(GlobalLock(hMem), text.c_str(), (text.length() + 1) * sizeof(wchar_t));
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

HBITMAP CaptureScreen(RECT rect) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, rect.left, rect.top, SRCCOPY);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return hbm;
}

winrt::Windows::Graphics::Imaging::SoftwareBitmap HBITMAPToSoftwareBitmap(HBITMAP hbm, int width, int height) {
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<BYTE> pixels(width * height * 4);
    HDC hdc = GetDC(NULL);
    GetDIBits(hdc, hbm, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);

    winrt::Windows::Graphics::Imaging::SoftwareBitmap bitmap(winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, width, height);
    auto buffer = bitmap.LockBuffer(winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode::Write);
    auto reference = buffer.CreateReference();
    winrt::com_ptr<::IMemoryBufferByteAccess> access;
    reference.as(access);
    
    BYTE* data = nullptr;
    UINT32 capacity = 0;
    winrt::check_hresult(access->GetBuffer(&data, &capacity));
    memcpy(data, pixels.data(), pixels.size());
    return bitmap;
}

// --- SNIPER THREAD ---
void SniperLoop() {
    winrt::init_apartment(); 
    auto langs = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
    if (langs.Size() == 0) { LogMessage(L"[!] Error: No OCR languages found!"); return; }
    auto ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(langs.GetAt(0));
    
    std::wstring lastCode = L"";

    while (isRunning) {
        int w = captureRect.right - captureRect.left;
        int h = captureRect.bottom - captureRect.top;
        
        // Захватываем область ПОД шапкой (высота шапки 20 пикселей)
        RECT actualCapture = { captureRect.left, captureRect.top + 20, captureRect.right, captureRect.bottom };

        if (w <= 0 || h <= 20) { Sleep(100); continue; }

        HBITMAP hbm = CaptureScreen(actualCapture);
        auto sbmp = HBITMAPToSoftwareBitmap(hbm, w, h - 20);
        DeleteObject(hbm);

        auto result = ocrEngine.RecognizeAsync(sbmp).get();
        std::wstring fullText = result.Text().c_str();

        std::wregex codeRegex(L"code\\s*is\\s*([A-Za-z0-9]+)", std::regex_constants::icase);
        std::wsmatch match;
        
        if (std::regex_search(fullText, match, codeRegex)) {
            std::wstring foundCode = match[1].str();
            if (foundCode != lastCode && !foundCode.empty()) {
                lastCode = foundCode;
                SetClipboardText(foundCode);
                Sleep(200); 

                int count = insertCount;
                for (int i = 0; i < count; i++) { PressCtrlV(); Sleep(50); }
                PressKey(VK_RETURN);
                
                std::wstring logMsg = L"[+] SNIPED: " + foundCode;
                LogMessage(logMsg);
            }
        }
        Sleep(500); 
    }
}

// --- MAIN WINDOW PROC ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"Insert Count (Ctrl+V):", WS_CHILD | WS_VISIBLE, 10, 10, 150, 20, hwnd, NULL, NULL, NULL);
        hCountEdit = CreateWindowW(L"EDIT", L"3", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 160, 10, 50, 20, hwnd, NULL, NULL, NULL);
        
        hBtnToggle = CreateWindowW(L"BUTTON", L"START (F8)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 40, 280, 30, hwnd, (HMENU)1, NULL, NULL);
        
        CreateWindowW(L"STATIC", L"Log:", WS_CHILD | WS_VISIBLE, 10, 80, 50, 20, hwnd, NULL, NULL, NULL);
        hLogEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 10, 100, 280, 150, hwnd, NULL, NULL, NULL);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1) { 
            if (!isRunning) {
                wchar_t buf[10];
                GetWindowTextW(hCountEdit, buf, 10);
                insertCount = _wtoi(buf);
                if (insertCount <= 0) insertCount = 3;

                GetWindowRect(hSelectorWnd, &captureRect);
                isRunning = true;
                SetWindowTextW(hBtnToggle, L"STOP (F8)");
                LogMessage(L">>> SNIPER STARTED. Waiting for code...");
                std::thread(SniperLoop).detach();
            } else {
                isRunning = false;
                SetWindowTextW(hBtnToggle, L"START (F8)");
                LogMessage(L">>> SNIPER STOPPED.");
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- SELECTOR WINDOW PROC ---
LRESULT CALLBACK SelectorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool isDragging = false;
    static bool isResizing = false;
    static POINT startPoint;
    static RECT startRect;

    switch (msg) {
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        GetCursorPos(&startPoint);
        GetWindowRect(hwnd, &startRect);
        
        // Растягивание за правый нижний угол (20x20)
        if (startPoint.x > startRect.right - 20 && startPoint.y > startRect.bottom - 20) {
            isResizing = true;
        } 
        // Перетаскивание строго за верхнюю красную полосу (высота 20 пикселей)
        else if (startPoint.y < startRect.top + 20) {
            isDragging = true;
        }
        return 0;

    case WM_MOUSEMOVE:
        if (isDragging) {
            POINT pt; GetCursorPos(&pt);
            int dx = pt.x - startPoint.x;
            int dy = pt.y - startPoint.y;
            MoveWindow(hwnd, startRect.left + dx, startRect.top + dy, startRect.right - startRect.left, startRect.bottom - startRect.top, TRUE);
        } else if (isResizing) {
            POINT pt; GetCursorPos(&pt);
            int newWidth = startRect.right - startRect.left + (pt.x - startPoint.x);
            int newHeight = startRect.bottom - startRect.top + (pt.y - startPoint.y);
            if (newWidth > 50 && newHeight > 50) {
                SetWindowPos(hwnd, NULL, 0, 0, newWidth, newHeight, SWP_NOMOVE | SWP_NOZORDER);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        isDragging = false;
        isResizing = false;
        ReleaseCapture();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(NULL_BRUSH));

        HBRUSH hBrushTop = CreateSolidBrush(RGB(255, 0, 0));
        RECT topBar = { 0, 0, ps.rcPaint.right, 20 };
        FillRect(hdc, &topBar, hBrushTop);
        DeleteObject(hBrushTop);

        HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
        HGDIOBJ oldPen = SelectObject(hdc, hPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        
        Rectangle(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom);
        
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hPen);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// --- ENTRY POINT ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MainSniperClass";
    RegisterClassW(&wc);

    hMainWnd = CreateWindowW(L"MainSniperClass", L"Code Sniper Beta", WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX, 300, 300, 320, 300, NULL, NULL, hInstance, NULL);

    WNDCLASSW wcSel = { 0 };
    wcSel.lpfnWndProc = SelectorProc;
    wcSel.hInstance = hInstance;
    wcSel.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); 
    wcSel.lpszClassName = L"SelectorClass";
    RegisterClassW(&wcSel);

    hSelectorWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED, L"SelectorClass", L"Selector", WS_POPUP, 100, 100, 400, 150, NULL, NULL, hInstance, NULL);
    
    SetLayeredWindowAttributes(hSelectorWnd, RGB(255, 255, 255), 200, LWA_COLORKEY | LWA_ALPHA);

    ShowWindow(hMainWnd, nCmdShow);
    ShowWindow(hSelectorWnd, SW_SHOW);

    RegisterHotKey(hMainWnd, 1, 0, VK_F8);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY && msg.wParam == 1) {
            SendMessageW(hMainWnd, WM_COMMAND, 1, 0);
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
