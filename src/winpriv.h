#ifndef WINPRIV_H
#define WINPRIV_H

#include "win.h"
#include "winids.h"

#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <imm.h>

extern HINSTANCE inst;  // The all-important instance handle
extern HWND wnd;        // the main terminal window
extern HIMC imc;        // the input method context
extern HWND config_wnd; // the options window

extern COLORREF colours[COLOUR_NUM];

extern LOGFONT lfont;

extern int font_width, font_height;

extern int g_render_tab_height; // current tab height in pixels

enum { PADDING = 1 };

void win_paint(void);

void win_init_fonts(int size);

void win_adapt_term_size(void);

void win_open_config(void);

void win_show_tip(int x, int y, int cols, int rows);
void win_destroy_tip(void);

void win_init_menus(void);
void win_update_menus(void);

void win_show_mouse(void);
void win_mouse_click(mouse_button, LPARAM);
void win_mouse_release(mouse_button, LPARAM);
void win_mouse_wheel(WPARAM, LPARAM);
void win_mouse_move(bool nc, LPARAM);

bool win_key_down(WPARAM, LPARAM);
bool win_key_up(WPARAM, LPARAM);

void win_init_drop_target(void);

void win_copy_title(void);

void win_switch(bool back);

void win_set_ime_open(bool);

bool win_is_fullscreen;

void win_process_timer_message(WPARAM message);

void win_tab_set_argv(char** argv);
void win_tab_init(char* home, char* cmd, char** argv, int width, int height, char* title);
int win_tab_count();
int win_active_tab();
void win_tab_change(int change);
void win_tab_move(int amount);
void win_tab_create();
void win_tab_clean();
void win_tab_attention(struct term* term);
void win_tab_set_title(struct term* term, wchar_t* title);
wchar_t* win_tab_get_title(unsigned int idx);

void win_active_tab_title_push();
wchar_t* win_active_tab_title_pop();

void win_tab_mouse_click(int x);
int win_tab_height();
void win_paint_tabs(HDC dc, int width);
void win_for_each_term(void (*cb)(struct term* term));

bool win_should_die();

#endif
