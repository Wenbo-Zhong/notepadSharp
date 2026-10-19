#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <objidl.h>
#include <commctrl.h>
#include <gdiplus.h>
#include "ToolbarIcons.h"
// clang-format on

namespace {
using namespace Gdiplus;
void page(Graphics &g, float offset = 0) {
    Pen outline(Color(255, 65, 95, 128), 1.5f);
    SolidBrush fill(Color(255, 242, 248, 255));
    PointF points[] = {
        {5 + offset, 3}, {16 + offset, 3}, {21 + offset, 8}, {21 + offset, 25}, {5 + offset, 25}};
    g.FillPolygon(&fill, points, 5);
    g.DrawPolygon(&outline, points, 5);
    g.DrawLine(&outline, 16 + offset, 3.0f, 16 + offset, 8.0f);
    g.DrawLine(&outline, 16 + offset, 8.0f, 21 + offset, 8.0f);
}
void folder(Graphics &g) {
    SolidBrush fill(Color(255, 255, 208, 96));
    Pen outline(Color(255, 163, 112, 26), 1.5f);
    PointF points[] = {{2, 7}, {10, 7}, {12, 10}, {25, 10}, {23, 24}, {3, 24}};
    g.FillPolygon(&fill, points, 6);
    g.DrawPolygon(&outline, points, 6);
    g.DrawLine(&outline, 3.0f, 13.0f, 24.0f, 13.0f);
}
void disk(Graphics &g, float offset = 0) {
    SolidBrush fill(Color(255, 83, 145, 210));
    Pen outline(Color(255, 43, 89, 139), 1.5f);
    g.FillRectangle(&fill, 4 + offset, 4.0f, 20.0f, 21.0f);
    g.DrawRectangle(&outline, 4 + offset, 4.0f, 20.0f, 21.0f);
    SolidBrush paper(Color(255, 249, 252, 255));
    g.FillRectangle(&paper, 8 + offset, 4.5f, 11.0f, 7.0f);
    g.FillRectangle(&paper, 8 + offset, 16.0f, 12.0f, 8.0f);
    g.DrawLine(&outline, 17 + offset, 5.0f, 17 + offset, 10.0f);
}
void magnifier(Graphics &g, float x = 11, float y = 11, float radius = 7) {
    Pen pen(Color(255, 47, 105, 151), 2.2f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    SolidBrush lens(Color(190, 225, 243, 255));
    g.FillEllipse(&lens, x - radius, y - radius, radius * 2, radius * 2);
    g.DrawEllipse(&pen, x - radius, y - radius, radius * 2, radius * 2);
    g.DrawLine(&pen, x + radius * .7f, y + radius * .7f, x + radius + 5, y + radius + 5);
}
void icon(Graphics &g, int index) {
    if (index == 0) {
        page(g);
        Pen plus(Color(255, 37, 145, 91), 2.2f);
        g.DrawLine(&plus, 13.0f, 13.0f, 13.0f, 21.0f);
        g.DrawLine(&plus, 9.0f, 17.0f, 17.0f, 17.0f);
    } else if (index == 1)
        folder(g);
    else if (index == 2)
        disk(g);
    else if (index == 3) {
        disk(g, -2);
        Pen back(Color(255, 43, 89, 139), 1.5f);
        g.DrawLine(&back, 25.0f, 8.0f, 25.0f, 27.0f);
        g.DrawLine(&back, 8.0f, 27.0f, 25.0f, 27.0f);
    } else if (index == 4 || index == 5) {
        if (index == 5) {
            g.TranslateTransform(28, 0);
            g.ScaleTransform(-1, 1);
        }
        Pen arrow(Color(255, 48, 125, 166), 2.5f);
        arrow.SetStartCap(LineCapRound);
        arrow.SetEndCap(LineCapRound);
        g.DrawArc(&arrow, 5.0f, 7.0f, 18.0f, 16.0f, 205.0f, 230.0f);
        PointF points[] = {{3, 6}, {3, 15}, {12, 15}};
        g.DrawLines(&arrow, points, 3);
    } else if (index == 6)
        magnifier(g);
    else if (index == 7) {
        Pen arrow(Color(255, 48, 125, 166), 2.0f);
        g.DrawLine(&arrow, 4.0f, 9.0f, 24.0f, 9.0f);
        g.DrawLine(&arrow, 24.0f, 9.0f, 19.0f, 4.0f);
        g.DrawLine(&arrow, 24.0f, 9.0f, 19.0f, 14.0f);
        g.DrawLine(&arrow, 24.0f, 21.0f, 4.0f, 21.0f);
        g.DrawLine(&arrow, 4.0f, 21.0f, 9.0f, 16.0f);
        g.DrawLine(&arrow, 4.0f, 21.0f, 9.0f, 26.0f);
    } else if (index == 8) {
        folder(g);
        magnifier(g, 17, 17, 5);
    } else {
        Pen pen(Color(255, 85, 104, 126), 2.0f);
        SolidBrush fill(Color(255, 222, 232, 242));
        g.DrawArc(&pen, 8.0f, 3.0f, 12.0f, 16.0f, 180.0f, 180.0f);
        g.FillRectangle(&fill, 5, 12, 18, 14);
        g.DrawRectangle(&pen, 5, 12, 18, 14);
        g.DrawLine(&pen, 14, 17, 14, 21);
    }
}
} // namespace
HIMAGELIST createToolbarIcons(UINT dpi) {
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok)
        return nullptr;
    const int size = MulDiv(22, dpi, 96);
    HIMAGELIST list = ImageList_Create(size, size, ILC_COLOR32, 10, 0);
    if (list)
        for (int i = 0; i < 10; ++i) {
            Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
            {
                Gdiplus::Graphics g(&bitmap);
                g.Clear(Gdiplus::Color(0, 0, 0, 0));
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                g.ScaleTransform(static_cast<float>(size) / 30.0f, static_cast<float>(size) / 30.0f);
                icon(g, i);
            }
            HBITMAP handle = nullptr;
            if (bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &handle) != Gdiplus::Ok ||
                ImageList_Add(list, handle, nullptr) < 0) {
                if (handle)
                    DeleteObject(handle);
                ImageList_Destroy(list);
                list = nullptr;
                break;
            }
            DeleteObject(handle);
        }
    Gdiplus::GdiplusShutdown(token);
    return list;
}
