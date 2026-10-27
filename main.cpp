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
        
        if (startPoint.x > startRect.right - 20 && startPoint.y > startRect.bottom - 20) {
            isResizing = true;
        } 
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
            if (newWidth > 60 && newHeight > 60) {
                SetWindowPos(hwnd, NULL, 0, 0, newWidth, newHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        isDragging = false;
        isResizing = false;
        ReleaseCapture();
        return 0;

    case WM_ERASEBKGND:
        // Говорим Windows НЕ стирать фон (чтобы не было черного прямоугольника)
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        // Рисуем ТОЛЬКО красную шапку
        HBRUSH hBrushTop = CreateSolidBrush(RGB(255, 0, 0));
        RECT topBar = { 0, 0, ps.rcPaint.right, 20 };
        FillRect(hdc, &topBar, hBrushTop);
        DeleteObject(hBrushTop);

        // Рисуем ТОЛЬКО красный контур (внутри ничего не заливаем)
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
