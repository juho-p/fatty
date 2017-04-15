#include <windows.h>
#include <set>
#include <tuple>
#include <vector>
#include <algorithm>
#include <climits>
#include <string>
#include <sstream>

#include <unistd.h>
#include <stdlib.h>

#include "win.hh"

#include <d2d1.h>

extern "C" {
#include "winpriv.h"
}

#define lengthof(array) (sizeof(array) / sizeof(*(array)))

using std::tuple;
using std::get;

typedef void (*CallbackFn)(void*);
typedef tuple<CallbackFn, void*> Callback;
typedef std::set<Callback> CallbackSet;

static CallbackSet callbacks;
static std::vector<Tab> tabs;
static unsigned int active_tab = 0;

static float g_xscale, g_yscale;

static void init_scale_factors() {
    static ID2D1Factory* d2d_factory = nullptr;
    if (d2d_factory == nullptr) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory);
    }
    float xdpi, ydpi;
    d2d_factory->ReloadSystemMetrics();
    d2d_factory->GetDesktopDpi(&xdpi, &ydpi);
    g_xscale = xdpi / 96.0f;
    g_yscale = ydpi / 96.0f;
}

Tab::Tab() : terminal(new term), chld(new child) {
    memset(terminal.get(), 0, sizeof(struct term));
    memset(chld.get(), 0, sizeof(struct child));
    info.attention = false;
    info.titles_i = 0;
}
Tab::~Tab() {
    if (terminal)
        term_free(terminal.get());
    if (chld)
        child_free(chld.get());
}
Tab::Tab(Tab&& t) {
    info = t.info;
    terminal = std::move(t.terminal);
    chld = std::move(t.chld);
}
Tab& Tab::operator=(Tab&& t) {
    std::swap(terminal, t.terminal);
    std::swap(chld, t.chld);
    std::swap(info, t.info);
    return *this;
}

extern "C" {

void win_set_timer(CallbackFn cb, void* data, uint ticks) {
    auto result = callbacks.insert(std::make_tuple(cb, data));
    CallbackSet::iterator iter = result.first;
    SetTimer(wnd, reinterpret_cast<UINT_PTR>(&*iter), ticks, NULL);
}

void win_process_timer_message(WPARAM message) {
    void* pointer = reinterpret_cast<void*>(message);
    auto callback = *reinterpret_cast<Callback*>(pointer);
    callbacks.erase(callback);
    KillTimer(wnd, message);

    // call the callback
    get<0>(callback)( get<1>(callback) );
}

void win_active_tab_title_push() {
  Tab& tab = tabs.at(active_tab);
  if (tab.info.titles_i == lengthof(tab.info.titles))
    tab.info.titles_i = 0;
  else
    tab.info.titles_i++;
}
  
wchar_t* win_active_tab_title_pop() {
  Tab& tab = tabs.at(active_tab);
  if (!tab.info.titles_i)
    tab.info.titles_i = lengthof(tab.info.titles);
  else
    tab.info.titles_i--;
  return win_tab_get_title(active_tab);
}

static void invalidate_tabs() {
    win_invalidate_all();
}

term* win_active_terminal() {
    return tabs.at(active_tab).terminal.get();
}

int win_tab_count() { return tabs.size(); }
int win_active_tab() { return active_tab; }

static void update_window_state() {
    win_update_menus();
    if (cfg.title_settable)
      SetWindowTextW(wnd, win_tab_get_title(active_tab));
    win_adapt_term_size();
}

static void set_active_tab(unsigned int index) {
    active_tab = index;
    Tab* active = &tabs.at(active_tab);
    for (auto& tab : tabs) {
        term_set_focus(tab.terminal.get(), &tab == active);
    }
    active->info.attention = false;
    update_window_state();
    win_invalidate_all();
}

static unsigned int rel_index(int change) {
    return (int(active_tab) + change + tabs.size()) % tabs.size();
}

void win_tab_change(int change) {
    set_active_tab(rel_index(change));
}
void win_tab_move(int amount) {
    auto new_idx = rel_index(amount);
    std::swap(tabs[active_tab], tabs[new_idx]);
    set_active_tab(new_idx);
}

static Tab& tab_by_term(struct term* term) {
    auto match = find_if(tabs.begin(), tabs.end(), [=](Tab& tab) {
            return tab.terminal.get() == term; });
    return *match;
}

static char* g_home;
static char* g_cmd;
static char** g_argv;
static void newtab(
        unsigned short rows, unsigned short cols,
        unsigned short width, unsigned short height, const char* cwd, char* title) {
    tabs.push_back(Tab());
    Tab& tab = tabs.back();
    tab.terminal->child = tab.chld.get();
    term_reset(tab.terminal.get());
    term_resize(tab.terminal.get(), rows, cols);
    tab.chld->cmd = g_cmd;
    tab.chld->home = g_home;
    struct winsize wsz{rows, cols, width, height};
    child_create(tab.chld.get(), tab.terminal.get(), g_argv, &wsz, cwd);
    if (title)
      win_set_title(tab.terminal.get(), title);
    else
      win_set_title(tab.terminal.get(), g_cmd);
}

static void set_tab_bar_visibility(bool b);

void win_tab_set_argv(char** argv) {
    g_argv = argv;
}

void win_tab_init(char* home, char* cmd, char** argv, int width, int height, char* title) {
    g_home = home;
    g_cmd = cmd;
    g_argv = argv;
    newtab(cfg.rows, cfg.cols, width, height, nullptr, title);
    set_tab_bar_visibility(tabs.size() > 1);
}
void win_tab_create() {
    auto& t = *tabs[active_tab].terminal;
    std::stringstream cwd_path;
    cwd_path << "/proc/" << t.child->pid << "/cwd";
    char* cwd = realpath(cwd_path.str().c_str(), 0);
    newtab(t.rows, t.cols, t.cols * font_width, t.rows * font_height, cwd, nullptr);
    free(cwd);
    set_active_tab(tabs.size() - 1);
    set_tab_bar_visibility(tabs.size() > 1);
}

void win_tab_clean() {
    bool invalidate = false;
    for (;;) {
        auto it = std::find_if(tabs.begin(), tabs.end(), [](Tab& x) {
                return x.chld->pid == 0; });
        if (it == tabs.end()) break;
        invalidate = true;
        tabs.erase(it);
    }
    if (invalidate && tabs.size() > 0) {
        if (active_tab >= tabs.size())
            set_active_tab(tabs.size() - 1);
        else
            set_active_tab(active_tab);
        set_tab_bar_visibility(tabs.size() > 1);
        win_invalidate_all();
    }
}

void win_tab_attention(struct term* term) {
    tab_by_term(term).info.attention = true;
    invalidate_tabs();
}

void win_tab_set_title(struct term* term, wchar_t* title) {
    auto& tab = tab_by_term(term);
    if (tab.info.titles[tab.info.titles_i] != title) {
        tab.info.titles[tab.info.titles_i] = title;
        invalidate_tabs();
    }
}

wchar_t* win_tab_get_title(unsigned int idx) {
    return (wchar_t *)tabs[idx].info.titles[tabs[idx].info.titles_i].c_str();
}

bool win_should_die() { return tabs.size() == 0; }

static int tabheight() {
    init_scale_factors();
    return 23 * g_yscale;
}

static bool tab_bar_visible = false;
static void fix_window_size() {
    // doesn't work fully when you put fullscreen and then show or hide
    // tab bar, but it's not too terrible (just looks little off) so I
    // don't care. Maybe fix it later?
    if (win_is_fullscreen) {
        win_adapt_term_size();
    } else {
        auto& t = *tabs[active_tab].terminal;
        win_set_chars(t.rows, t.cols);
    }
}
static void set_tab_bar_visibility(bool b) {
    if (b == tab_bar_visible) return;

    tab_bar_visible = b;
    g_render_tab_height = win_tab_height();
    fix_window_size();
    win_invalidate_all();
}
int win_tab_height() { return tab_bar_visible ? tabheight() : 0; }

static int tab_font_size() {
    return 14 * g_yscale;
}

static HGDIOBJ new_tab_font() {
    return CreateFont(tab_font_size(),0,0,0,FW_NORMAL,0,0,0,1,0,0,CLEARTYPE_QUALITY,0,0);
}

static HGDIOBJ new_active_tab_font() {
    return CreateFont(tab_font_size(),0,0,0,FW_BOLD,0,0,0,1,0,0,CLEARTYPE_QUALITY,0,0);
}

// paint a tab to dc (where dc draws to buffer)
static void paint_tab(HDC dc, int width, int tabheight, const Tab& tab) {
    MoveToEx(dc, 0, tabheight, nullptr);
    LineTo(dc, 0, 0);
    LineTo(dc, width, 0);
    TextOutW(dc, width/2, (tabheight - tab_font_size()) / 2, tab.info.titles[tab.info.titles_i].data(), tab.info.titles[tab.info.titles_i].size());
}

// Wrap GDI object for automatic release
struct SelectWObj {
    HDC tdc;
    HGDIOBJ old;
    SelectWObj(HDC dc, HGDIOBJ obj) { tdc = dc; old = SelectObject(dc, obj); }
    ~SelectWObj() { DeleteObject(SelectObject(tdc, old)); }
};

static int tab_paint_width = 0;
void win_paint_tabs(HDC dc, int width) {
    if (!tab_bar_visible) return;

    // the sides of drawable area are not visible, so we really should draw to
    // coordinates 1..(width-2)
    width = width - 2 * PADDING;

    const auto bg = cfg.tab_bg_colour;
    const auto fg = cfg.tab_fg_colour;
    const auto active_bg = cfg.tab_active_bg_colour;
    const auto attention_bg = cfg.tab_attention_bg_colour;

    const int tabwidth = (width / tabs.size()) > 200 ? 200 : width / tabs.size();
    const int loc_tabheight = 18 * g_yscale;
    tab_paint_width = tabwidth;
    RECT tabrect;
    SetRect(&tabrect, 0, 0, tabwidth, loc_tabheight+1);

    HDC bufdc = CreateCompatibleDC(dc);
    SetBkMode(bufdc, TRANSPARENT);
    SetTextColor(bufdc, fg);
    SetTextAlign(bufdc, TA_CENTER);
    {
        auto brush = CreateSolidBrush(bg);
        auto obrush = SelectWObj(bufdc, brush);
        auto open = SelectWObj(bufdc, CreatePen(PS_SOLID, 0, fg));
        auto obuf = SelectWObj(bufdc,
                CreateCompatibleBitmap(dc, tabwidth, tabheight()));

        auto ofont = SelectWObj(bufdc, new_tab_font());

        for (size_t i = 0; i < tabs.size(); i++) {
            bool  active = i == active_tab;
            if (active) {
                auto activebrush = CreateSolidBrush(active_bg);
                FillRect(bufdc, &tabrect, activebrush);
                DeleteObject(activebrush);
            } else if (tabs[i].info.attention) {
                auto activebrush = CreateSolidBrush(attention_bg);
                FillRect(bufdc, &tabrect, activebrush);
                DeleteObject(activebrush);
            } else {
                FillRect(bufdc, &tabrect, brush);
            }

            if (active) {
                auto _f = SelectWObj(bufdc, new_active_tab_font());
                paint_tab(bufdc, tabwidth, loc_tabheight, tabs[i]);
            } else {
                MoveToEx(bufdc, 0, loc_tabheight, nullptr);
                LineTo(bufdc, tabwidth, loc_tabheight);
                paint_tab(bufdc, tabwidth, loc_tabheight, tabs[i]);
            }

            BitBlt(dc, i*tabwidth+PADDING, PADDING, tabwidth, tabheight(),
                    bufdc, 0, 0, SRCCOPY);
        }
        
        if ((int)tabs.size() * tabwidth < width) {
            SetRect(&tabrect, 0, 0, width - (tabs.size() * tabwidth), loc_tabheight+1);
            auto obrush = SelectWObj(bufdc, brush);
            auto obuf = SelectWObj(bufdc, CreateCompatibleBitmap(dc, width - (tabs.size() * tabwidth), tabheight()));
            FillRect(bufdc, &tabrect, brush);
            MoveToEx(bufdc, 0, 0, nullptr);
            LineTo(bufdc, 0, loc_tabheight);
            LineTo(bufdc, width - (tabs.size() * tabwidth), loc_tabheight);
            BitBlt(dc, tabs.size()*tabwidth+PADDING, PADDING, width - (tabs.size() * tabwidth), tabheight(),
                    bufdc, 0, 0, SRCCOPY);
        }
    }
    DeleteDC(bufdc);
}

void win_for_each_term(void (*cb)(struct term* term)) {
    for (Tab& tab : tabs)
        cb(tab.terminal.get());
}

void win_tab_mouse_click(int x) {
    unsigned int tab = x / tab_paint_width;
    if (tab >= tabs.size())
        return;
    set_active_tab(tab);
}

}

std::vector<Tab>& win_tabs() {
    return tabs;
}

static void lambda_callback(void* data) {
    auto callback = static_cast<std::function<void()>*>(data);
    (*callback)();
    delete callback;
}
void win_callback(unsigned int ticks, std::function<void()> callback) {
    win_set_timer(lambda_callback, new std::function<void()>(callback), ticks);
}
