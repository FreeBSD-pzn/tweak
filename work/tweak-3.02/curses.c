#include "tweak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <curses.h>

int display_rows, display_cols;

void display_beep(void)
{
    beep();
}

static void get_screen_size (void) {
    getmaxyx(stdscr, display_rows, display_cols);
}

void display_setup(void)
{
    initscr();
    noecho();
    keypad(stdscr, 0);
    raw();
    move(0,0);
    refresh();
    get_screen_size();
    if (has_colors()) {
	start_color();
        use_default_colors();
    }
}

void display_cleanup(void)
{
    endwin();
}

void display_moveto(int y, int x)
{
    wmove(stdscr, y, x);
}

void display_refresh(void)
{
    refresh();
}

void display_write_str(char *str)
{
    waddstr(stdscr, str);
}

void display_write_chars(char *str, int len)
{
    waddnstr(stdscr, str, len);
}

#define MAXCOLOURS 32
int attrs[MAXCOLOURS];

void display_define_colour(int colour, int fg, int bg, int reverse)
{
    static int colours[8] = {
        COLOR_BLACK,
        COLOR_RED,
        COLOR_GREEN,
        COLOR_YELLOW,
        COLOR_BLUE,
        COLOR_MAGENTA,
        COLOR_CYAN,
        COLOR_WHITE,
    };

    if (fg < 0 && bg < 0) {
        attrs[colour] = 0;
    } else {
        assert(colour >= 0 && colour < MAXCOLOURS);
        assert(!(bg & ~7));            /* bold backgrounds are nonportable */
	if (colour < COLOR_PAIRS-2) {
	    init_pair(colour+1, colours[fg & 7], colours[bg]);
	    attrs[colour] = (fg & 8 ? A_BOLD : 0) | COLOR_PAIR(colour+1);
	} else {
	    /* can't allocate a colour pair, so we just use b&w attrs */
	    attrs[colour] = (fg & 8 ? A_BOLD : 0) | (reverse ? A_REVERSE : 0);
	}
    }
}

void display_set_colour(int colour)
{
    wattrset(stdscr, attrs[colour]);
}

void display_clear_to_eol(void)
{
    wclrtoeol(stdscr);
}

int last_getch = ERR;

int display_getkey(void)
{
    int ret;
    extern void schedule_update(void);

    if (last_getch != ERR) {
	int ret = last_getch;
	last_getch = ERR;
	return ret;
    }
    while (1) {
	ret = getch();
	if (ret == KEY_RESIZE) {
	    schedule_update();
	    continue;
	}
	return ret;
    }
}

int display_input_to_flush(void)
{
    int ret;
    if (last_getch != ERR)
	return TRUE;

    nodelay(stdscr, 1);
    ret = getch();
    nodelay(stdscr, 0);
    if (ret == ERR)
	return FALSE;

    last_getch = ret;
    return TRUE;
}

void display_post_error(void)
{
    /* I don't _think_ we need do anything here */
}

void display_recheck_size(void)
{
    get_screen_size ();
}
