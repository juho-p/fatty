#pragma once

#include <climits>

extern "C" {
// some typedef for mintty header compat
typedef unsigned int uint;
typedef unsigned short ushort;
typedef wchar_t wchar;
typedef unsigned char uchar;
typedef const char *string;
typedef const wchar *wstring;

#include "term.h"
#include "child.h"
}

#include <memory>
#include <vector>
#include <string>

struct Tab {
    std::unique_ptr<term> terminal;
    std::unique_ptr<child> chld;
    struct {
        std::wstring title;
        bool attention;
    } info;

    Tab();
    ~Tab();
    Tab(Tab&& t);
    Tab& operator=(Tab&& t);
private:
    Tab(const Tab&) = delete;
    Tab& operator=(const Tab&) = delete;
};

std::vector<Tab>& win_tabs();
