#pragma once
#include "Win.h"

namespace ft {

class SearchIcons {
public:
    static void DrawSearchIcon(HDC hdc, HDC scratchDc, const RECT& rcDest, bool dark);
    static void DrawFilterIcon(HDC hdc, HDC scratchDc, const RECT& rcDest, bool dark, bool active);
    static void Cleanup();
};

}  // namespace ft

