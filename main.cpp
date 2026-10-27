#include <windows.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <regex>
#include <vector>

// Для доступа к пикселям
#include <wrl\client.h>
#include <robuffer.h>

using namespace winrt;
using namespace Windows::Media::Ocr;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage::Streams;

// --- Глобальные переменные ---
std::atomic<bool> isRunning(false);
int insertCount = 3;

// КООРДИНАТЫ РАМКИ ЗАХВАТА (Лево, Верх, Право, Низ)
// Поменяй эти цифры под то место, где появляется код в игре!
RECT captureRect = { 100, 100, 500, 200 }; 

// --- Эмуляция нажатия клавиш ---
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

// --- Копирование в буфер обмена ---
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

// --- Захват экрана ---
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

// --- Конвертация HBITMAP в SoftwareBitmap ---
SoftwareBitmap HBITMAPToSoftwareBitmap(HBITMAP hbm, int width, int height) {
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

    SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, width, height);
    auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
    
    // Используем COM-интерфейс IMemoryBufferByteAccess для доступа к памяти
    auto reference = buffer.CreateReference();
    com_ptr<::IMemoryBufferByteAccess> access;
    check_hresult(reference.as(access));
    
    byte* data = nullptr;
    UINT32 capacity = 0;
    check_hresult(access->GetBuffer(&data, &capacity));
    
    memcpy(data, pixels.data(), pixels.size());
    return bitmap;
}

// --- Основной цикл ---
void SniperLoop() {
    winrt::init_apartment(); 
    
    // Получаем доступные языки для OCR (обычно английский идет первым)
    auto langs = OcrEngine::AvailableRecognizerLanguages();
    if (langs.Size() == 0) {
        MessageBoxW(NULL, L"Ошибка: OCR языки не найдены!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    auto lang = langs.GetAt(0);
    auto ocrEngine = OcrEngine::TryCreateFromLanguage(lang);
    
    std::wstring lastCode = L"";

    while (isRunning) {
        int w = captureRect.right - captureRect.left;
        int h = captureRect.bottom - captureRect.top;
        if (w <= 0 || h <= 0) { Sleep(100); continue; }

        HBITMAP hbm = CaptureScreen(captureRect);
        SoftwareBitmap sbmp = HBITMAPToSoftwareBitmap(hbm, w, h);
        DeleteObject(hbm);

        auto result = ocrEngine.RecognizeAsync(sbmp).get();
        std::wstring fullText = result.Text().c_str();

        // Ищем текст вида "code is XXXX"
        std::wregex codeRegex(L"code\\s*is\\s*([A-Za-z0-9]+)", std::regex_constants::icase);
        std::wsmatch match;
        
        if (std::regex_search(fullText, match, codeRegex)) {
            std::wstring foundCode = match[1].str();
            
            if (foundCode != lastCode && !foundCode.empty()) {
                lastCode = foundCode;
                SetClipboardText(foundCode);
                Sleep(200); 

                // Вставляем 3 раза
                for (int i = 0; i < insertCount; i++) {
                    PressCtrlV();
                    Sleep(50);
                }
                // Жмем Enter
                PressKey(VK_RETURN);
                
                std::wcout << L"[CODE SNIPED]: " << foundCode << std::endl;
            }
        }
        Sleep(500); 
    }
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleTitleW(L"Code Sniper (F8 to Start/Stop)");
    std::wcout << L"=== CODE SNIPER ===" << std::endl;
    std::wcout << L"Нажми F8, чтобы включить/выключить." << std::endl;
    std::wcout << L"Рамка захвата: [" << captureRect.left << ", " << captureRect.top << "] - [" << captureRect.right << ", " << captureRect.bottom << "]" << std::endl;

    while (true) {
        if (GetAsyncKeyState(VK_F8) & 0x8000) {
            if (!isRunning) {
                isRunning = true;
                std::wcout << L">>> СНАЙПЕР ЗАПУЩЕН" << std::endl;
                std::thread(SniperLoop).detach();
            } else {
                isRunning = false;
                std::wcout << L">>> СНАЙПЕР ОСТАНОВЛЕН" << std::endl;
            }
            Sleep(500); // Антидребезг клавиши
        }
        Sleep(50);
    }
    return 0;
}
