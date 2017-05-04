#ifndef WIN_H
#define WIN_H

#include "term.h"

void win_reconfig(void);

void win_update(void);
void win_update_term(struct term* term);
void win_schedule_update(void);

void win_text(int x, int y, wchar_t *text, int len, cattr attr, int lattr);
void win_update_mouse(void);
void win_capture_mouse(void);
void win_bell(struct term* term);

void win_set_title(wchar *);

colour win_get_colour(colour_i);
void win_set_colour(colour_i, colour);
void win_reset_colours(void);
colour win_get_sys_colour(bool fg);

void win_invalidate_all(void);

void win_set_pos(int x, int y);
void win_set_chars(int rows, int cols);
void win_set_pixels(int height, int width);
void win_maximise(int max);
void win_set_zorder(bool top);
void win_set_iconic(bool);
void win_update_scrollbar(void);
bool win_is_iconic(void);
void win_get_pos(int *xp, int *yp);
void win_get_pixels(int *height_p, int *width_p);
void win_get_screen_chars(int *rows_p, int *cols_p);
void win_popup_menu(void);

void win_zoom_font(int);
void win_set_font_size(int);
unsigned int win_get_font_size(void);

void win_check_glyphs(wchar_t *wcs, unsigned int num);

void win_open(wstring path);
void win_copy(const wchar_t *data, unsigned int *attrs, int len);
void win_paste(void);

void win_set_timer(void (*cb)(void*), void* data, unsigned int ticks);

void win_show_about(void);
void win_show_error(wchar_t *);

bool win_is_glass_available(void);

int get_tick_count(void);
int cursor_blink_ticks(void);

int win_char_width(unsigned int);
wchar_t win_combine_chars(wchar_t bc, wchar_t cc);
extern wchar_t win_linedraw_chars[31];

struct term* win_active_terminal();


#endif
