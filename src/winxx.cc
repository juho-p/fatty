#include <windows.h>
#include <set>
#include <tuple>
#include <vector>
#include <algorithm>
#include <climits>

#include "win.hh"

extern "C" {
#include "winpriv.h"
}

using std::tuple;
using std::get;

typedef void (*CallbackFn)(void*);
typedef tuple<CallbackFn, void*> Callback;
typedef std::set<Callback> CallbackSet;

static CallbackSet callbacks;
static std::vector<Tab> tabs;
static unsigned int active_tab = 0;


Tab::Tab() : terminal(new term), chld(new child) {
    memset(terminal.get(), 0, sizeof(struct term));
    memset(chld.get(), 0, sizeof(struct child));
    info.attention = false;
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

static void invalidate_tabs() {
    win_invalidate_all();
}

term* term_at(unsigned int i) { return tabs.at(i).terminal.get(); }

term* win_active_terminal() {
    return tabs[active_tab].terminal.get();
}

int win_tab_count() { return tabs.size(); }
int win_active_tab() { return active_tab; }

static void update_window_state() {
    Tab& tab = tabs[active_tab];
    win_update_menus();
    SetWindowTextW(wnd, tab.info.title.data());
    win_adapt_term_size();
}

static void set_active_tab(unsigned int index) {
    active_tab = index;
    Tab* active = &tabs[active_tab];
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
        unsigned short width, unsigned short height) {
    tabs.push_back(Tab());
    Tab& tab = tabs.back();
    tab.terminal->child = tab.chld.get();
    term_reset(tab.terminal.get());
    term_resize(tab.terminal.get(), rows, cols);
    tab.chld->cmd = g_cmd;
    tab.chld->home = g_home;
    struct winsize wsz{rows, cols, width, height};
    child_create(tab.chld.get(), tab.terminal.get(),
        g_argv, &wsz);
}

void win_tab_init(char* home, char* cmd, char** argv, int width, int height) {
    g_home = home;
    g_cmd = cmd;
    g_argv = argv;
    newtab(cfg.rows, cfg.cols, width, height);
}
static void set_tab_bar_visibility(bool b);
void win_tab_create() {
    auto& t = *tabs[active_tab].terminal;
    // TODO: have nicer way of getting terminal size
    newtab(t.rows, t.cols, t.cols * font_width, t.rows * font_height);
    set_active_tab(tabs.size() - 1);
    set_tab_bar_visibility(tabs.size() > 1);
}

void win_tab_clean() {
    bool invalidate = false;
    for (;;) {
        auto it = std::find_if(tabs.begin(), tabs.end(), [](Tab& x) {
                return x.chld->pid == 0; });
        if (it == tabs.end()) break;
        if (&*it == &tabs[active_tab])
            invalidate = true;
        tabs.erase(it);
    }
    if (invalidate && tabs.size() > 0) {
        if (active_tab >= tabs.size())
            set_active_tab(tabs.size() - 1);
        else
            set_active_tab(active_tab);
        win_invalidate_all();
        set_tab_bar_visibility(tabs.size() > 1);
    }
}

void win_tab_attention(struct term* term) {
    tab_by_term(term).info.attention = true;
    invalidate_tabs();
}

void win_tab_title(struct term* term, wchar_t* title) {
    auto& tab = tab_by_term(term);
    if (tab.info.title != title) {
        tab_by_term(term).info.title = title;
        invalidate_tabs();
    }
}

bool win_should_die() { return tabs.size() == 0; }

const int tabheight = 18;

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
    fix_window_size();
    win_invalidate_all();
}
int win_tab_height() { return tab_bar_visible ? tabheight : 0; }

// paint a tab to dc (where dc draws to buffer)
static void paint_tab(HDC dc, int width, const Tab& tab) {
    MoveToEx(dc, 0, 0, nullptr);
    LineTo(dc, 0, tabheight);
    TextOutW(dc, width/2, 1, tab.info.title.data(), tab.info.title.size());
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

    const auto bg = RGB(0,0,0);
    const auto fg = RGB(0,255,0);
    const auto active_bg = RGB(50, 50, 50);
    const auto attention_bg = RGB(0, 50, 0);

    const int tabwidth = width / tabs.size();
    tab_paint_width = tabwidth;
    RECT tabrect;
    SetRect(&tabrect, 0, 0, tabwidth, tabheight);

    HDC bufdc = CreateCompatibleDC(dc);
    SetBkMode(bufdc, TRANSPARENT);
    SetTextColor(bufdc, fg);
    SetTextAlign(bufdc, TA_CENTER);
    {
        auto brush = CreateSolidBrush(bg);
        auto obrush = SelectWObj(bufdc, brush);
        auto open = SelectWObj(bufdc, CreatePen(PS_SOLID, 0, fg));
        auto obuf = SelectWObj(bufdc,
                CreateCompatibleBitmap(dc, tabwidth, tabheight+1));
        // i just wanna set font size :(
        // hmm, maybe I should use CreateFontEx instead to get more params??
        // nice API, thanks Bill!
        auto ofont = SelectWObj(bufdc, CreateFont(14,0,0,0,0,0,0,0,1,0,0,0,0,0));

        for (size_t i = 0; i < tabs.size(); i++) {
            if (i == active_tab) {
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
            paint_tab(bufdc, tabwidth, tabs[i]);
            BitBlt(dc, i*tabwidth, 0, tabwidth, tabheight+1,
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
