#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>

// Developer-only memory surface for drawing the real UI without a compositor.
namespace fixture {
struct Bitmap {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    void* pixels = nullptr;
    int width, height;
    Bitmap(int w, int h) : width(w), height(h) {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !bitmap || !pixels) throw std::runtime_error("Could not create UI rendering surface");
        previous = SelectObject(dc, bitmap);
    }
    ~Bitmap() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    void Fill(COLORREF colour) {
        HBRUSH brush = CreateSolidBrush(colour);
        RECT bounds{0,0,width,height}; FillRect(dc,&bounds,brush); DeleteObject(brush);
    }
    void Copy(const Bitmap& other, int x, int y) {
        BitBlt(dc, x, y, other.width, other.height, other.dc, 0, 0, SRCCOPY);
    }
    bool Save(const std::wstring& path) {
        GdiFlush();
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
        header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        DWORD bytes = static_cast<DWORD>(width * height * 4); header.bfSize = header.bfOffBits + bytes;
        BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
        info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB; info.biSizeImage = bytes;
        DWORD written = 0;
        bool okay = WriteFile(file, &header, sizeof(header), &written, nullptr) && written == sizeof(header);
        okay = okay && WriteFile(file, &info, sizeof(info), &written, nullptr) && written == sizeof(info);
        okay = okay && WriteFile(file, pixels, bytes, &written, nullptr) && written == bytes;
        CloseHandle(file); return okay;
    }
};
}
