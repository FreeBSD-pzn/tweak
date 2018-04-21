#include "tweak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#if defined(unix) && !defined(GO32)
#include <signal.h>
#include <sys/ioctl.h>
#endif

#include <slang.h>

#if defined(unix) && !defined(GO32)
static int sigwinch (int sigtype)
{
    extern void schedule_update(void);
    schedule_update();
    signal (SIGWINCH, (void *) sigwinch);
    return 0;
}
#endif

int display_rows, display_cols;

void display_beep(void)
{
    SLtt_beep();
}

static void get_screen_size (void) {
    int r = 0, c = 0;

#ifdef TIOCGWINSZ
    struct winsize wind_struct;

    if ((ioctl(1,TIOCGWINSZ,&wind_struct) == 0)
	|| (ioctl(0, TIOCGWINSZ, &wind_struct) == 0)
	|| (ioctl(2, TIOCGWINSZ, &wind_struct) == 0)) {
        c = (int) wind_struct.ws_col;
        r = (int) wind_struct.ws_row;
    }
#elif defined(MSDOS)
    union REGS regs;

    regs.h.ah = 0x0F;
    int86 (0x10, &regs, &regs);
    c = regs.h.ah;

    regs.x.ax = 0x1130, regs.h.bh = 0;
    int86 (0x10, &regs, &regs);
    r = regs.h.dl + 1;
#endif

    if ((r <= 0) || (r > 200)) r = 24;
    if ((c <= 0) || (c > 250)) c = 80;
    display_rows = SLtt_Screen_Rows = r;
    display_cols = SLtt_Screen_Cols = c;
}

void display_setup(void)
{
    SLtt_get_terminfo();

    if (SLang_init_tty (ABORT, 1, 0) == -1) {
	fprintf(stderr, "tweak: SLang_init_tty: returned error code\n");
	exit (1);
    }
    SLang_set_abort_signal (NULL);
    SLtt_Use_Ansi_Colors = TRUE;

    get_screen_size ();
    if (SLsmg_init_smg () < 0) {
	fprintf(stderr, "tweak: SLsmg_init_smg: returned error code\n");
	SLang_reset_tty ();
	exit (1);
    }

#if defined(unix) && !defined(GO32)
    signal (SIGWINCH, (void *) sigwinch);
#endif
}

void display_cleanup(void)
{
    SLsmg_reset_smg ();
    SLang_reset_tty ();
}

void display_moveto(int y, int x)
{
    SLsmg_gotorc(y, x);
}

void display_refresh(void)
{
    SLsmg_refresh();
}

void display_write_str(char *str)
{
    SLsmg_write_nchars(str, strlen(str));
}

void display_write_chars(char *str, int len)
{
    SLsmg_write_nchars(str, len);
}

void display_define_colour(int colour, int fg, int bg, int reverse)
{
    static char *colours[16] = {
	"black", "red", "green", "brown",
	"blue", "magenta", "cyan", "lightgray",
	"gray", "brightred", "brightgreen", "yellow",
	"brightblue", "brightmagenta", "brightcyan", "white",
    };
    char cname[40];

    if (fg < 0 && bg < 0) {
        /* FIXME: not sure how to support terminal default fg+bg */
        fg = 7;
        bg = 0;
    }

    sprintf(cname, "colour%d", colour);

    SLtt_set_color(colour, cname, colours[fg], colours[bg]);
}

void display_set_colour(int colour)
{
    SLsmg_set_color(colour);
}

void display_clear_to_eol(void)
{
    SLsmg_erase_eol();
}

int display_getkey(void)
{
    return SLang_getkey();
}

int display_input_to_flush(void)
{
    return SLang_input_pending(0);
}

void display_post_error(void)
{
    SLKeyBoard_Quit = 0;
    SLang_Error = 0;
}

void display_recheck_size(void)
{
    SLsmg_reset_smg ();
    get_screen_size ();
    SLsmg_init_smg ();
}
