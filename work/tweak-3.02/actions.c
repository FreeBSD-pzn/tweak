#include "tweak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void act_exit (void);
static void act_save (void);
static void act_exitsave (void);
static void act_top (void);
static void act_pgup (void);
static void act_up (void);
static void act_home (void);
static void act_left (void);
static void act_right (void);
static void act_end (void);
static void act_down (void);
static void act_pgdn (void);
static void act_bottom (void);
static void act_togins (void);
static void act_chmode (void);
extern void act_self_ins (void);       /* this one must be external */
static void act_delete (void);
static void act_delch (void);
static void act_mark (void);
static void act_cut (void);
static void act_copy (void);
static void act_paste (void);
static void act_susp (void);
static void act_goto (void);
static void act_togstat (void);
static void act_search (void);
static void act_search_backwards (void);
static void act_recentre (void);
static void act_width (void);
static void act_offset (void);
#ifdef TEST_BUFFER
static void act_diagnostics (void);
#endif

static Search *last_search = NULL;

keyact parse_action (char *name) {
    char *names[] = {
	"exit", "top-of-file", "page-up", "move-up",
	"begin-line", "move-left", "move-right", "end-line",
	"move-down", "page-down", "bottom-of-file", "toggle-insert",
	"change-mode", "delete-left", "delete-right", "mark-place",
	"cut", "copy", "paste", "suspend", "goto-position",
	"toggle-status", "search", "search-back", "save-file",
	"exit-and-save", "screen-recentre", "new-width", "new-offset"
#ifdef TEST_BUFFER
	, "diagnostics"
#endif
    };
    keyact actions[] = {
	act_exit, act_top, act_pgup, act_up, act_home, act_left,
	act_right, act_end, act_down, act_pgdn, act_bottom,
	act_togins, act_chmode, act_delete, act_delch, act_mark,
	act_cut, act_copy, act_paste, act_susp, act_goto,
	act_togstat, act_search, act_search_backwards, act_save,
	act_exitsave, act_recentre, act_width, act_offset
#ifdef TEST_BUFFER
	, act_diagnostics
#endif
    };
    int i;

    for (i=0; i<sizeof(names)/sizeof(*names); i++)
	if (!strcmp(name, names[i]))
	    return actions[i];
    return NULL;
}

static fileoffset_t begline(fileoffset_t x) {
    fileoffset_t y = x + width-offset;
    y -= (y % width);
    y -= width-offset;
    if (y < 0)
	y = 0;
    return y;
}

static fileoffset_t endline(fileoffset_t x) {
    fileoffset_t y = x + width-offset;
    y -= (y % width);
    y += width-1;
    y -= width-offset;
    if (y < 0)
	y = 0;
    return y;
}

static void act_exit(void) {
    static char question[] = "File is modified. Save before quitting? [yn] ";
    if (modified) {
	int c;

	display_moveto (display_rows-1, 0);
	display_clear_to_eol ();
	display_set_colour (COL_MINIBUF);
	display_write_str (question);
	display_refresh();
	do {
#if defined(unix) && !defined(GO32)
	    if (update_required) {
		update();
		display_moveto (display_rows-1, 0);
		display_clear_to_eol ();
		display_set_colour (COL_MINIBUF);
		display_write_str (question);
		display_refresh();
	    }
	    safe_update = TRUE;
#endif
	    c = display_getkey();
#if defined(unix) && !defined(GO32)
	    safe_update = FALSE;
#endif
	    if (c >= 'a' && c <= 'z')
		c += 'A'-'a';
	} while (c != 'Y' && c != 'N' && c != '\007');
	if (c == 'Y') {
	    act_save();
	    if (modified)
		return;		       /* couldn't save, so don't quit */
	    draw_scr();		       /* update the ** on status line! */
	} else if (c == '\007') {
	    return;		       /* don't even quit */
	}
    }
    finished = TRUE;
}

static void act_save(void) {
    static int backed_up = FALSE;

    if (!backed_up) {
	if (!backup_file()) {
	    display_beep();
	    strcpy (message, "Unable to back up file!");
	    return;
	}
	backed_up = TRUE;
    }
    if (!save_file()) {
	display_beep();
	strcpy (message, "Unable to save file!");
	return;
    }
    modified = FALSE;
}

static void act_exitsave(void) {
    act_save();
    draw_scr();			       /* update ** on status line */
    act_exit();
}

static void act_top (void) {
    cur_pos = top_pos = 0;
    edit_type = !!edit_type;
}

static void act_pgup(void) {
    cur_pos -= (scrlines-1)*width;
    if (cur_pos < 0) {
	cur_pos = 0;
	edit_type = !!edit_type;
    }
    if (top_pos > cur_pos)
	top_pos = begline(cur_pos);
}

static void act_up(void) {
    cur_pos -= width;
    if (cur_pos < 0) {
	cur_pos = 0;
	edit_type = !!edit_type;
    }
    if (top_pos > cur_pos)
	top_pos = begline(cur_pos);
}

static void act_home(void) {
    cur_pos = begline(cur_pos);
    if (cur_pos < 0)
	cur_pos = 0;
    if (top_pos > cur_pos)
	top_pos = begline(cur_pos);
    edit_type = !!edit_type;
}

static void act_left(void) {
    if (edit_type == 2) {
	edit_type = 1;
	return;
    } else {
	cur_pos--;
	edit_type = 2*!!edit_type;
	if (cur_pos < 0) {
	    cur_pos = 0;
	    edit_type = !!edit_type;
	}
	if (top_pos > cur_pos)
	    top_pos = begline(cur_pos);
    }
}

static void act_right(void) {
    fileoffset_t new_top;

    if (edit_type == 1) {
	if (cur_pos < file_size)
	    edit_type = 2;
	return;
    } else {
	cur_pos++;
	if (cur_pos > file_size)
	    cur_pos = file_size;
	new_top = cur_pos - (scrlines-1) * width;
	if (new_top < 0)
	    new_top = 0;
	new_top = begline(new_top);
	if (top_pos < new_top)
	    top_pos = new_top;
	edit_type = !!edit_type;
    }
}

static void act_end(void) {
    fileoffset_t new_top;

    cur_pos = endline(cur_pos);
    edit_type = !!edit_type;
    if (cur_pos >= file_size)
	cur_pos = file_size;
    new_top = cur_pos - (scrlines-1) * width;
    if (new_top < 0)
	new_top = 0;
    new_top = begline(new_top);
    if (top_pos < new_top)
	top_pos = new_top;
}

static void act_down(void) {
    fileoffset_t new_top;

    cur_pos += width;
    if (cur_pos >= file_size) {
	cur_pos = file_size;
	edit_type = !!edit_type;
    }
    new_top = cur_pos - (scrlines-1) * width;
    if (new_top < 0)
	new_top = 0;
    new_top = begline(new_top);
    if (top_pos < new_top)
	top_pos = new_top;
}

static void act_pgdn(void) {
    fileoffset_t new_top;

    cur_pos += (scrlines-1) * width;
    if (cur_pos >= file_size) {
	cur_pos = file_size;
	edit_type = !!edit_type;
    }
    new_top = cur_pos - (scrlines-1) * width;
    if (new_top < 0)
	new_top = 0;
    new_top = begline(new_top);
    if (top_pos < new_top)
	top_pos = new_top;
}

static void act_bottom (void) {
    fileoffset_t new_top;

    cur_pos = file_size;
    edit_type = !!edit_type;
    new_top = cur_pos - (scrlines-1) * width;
    if (new_top < 0)
	new_top = 0;
    new_top = begline(new_top);
    if (top_pos < new_top)
	top_pos = new_top;
}

static void act_togins(void) {
    if (look_mode || fix_mode) {
	display_beep();
	sprintf(message, "Can't engage Insert mode when in %s mode",
		(look_mode ? "LOOK" : "FIX"));
	insert_mode = FALSE;	       /* safety! */
    } else
	insert_mode = !insert_mode;
}

static void act_chmode(void) {
    if (ascii_enabled)
	edit_type = !edit_type;	       /* 0 -> 1, [12] -> 0 */
    else if (edit_type == 0)	       /* just in case */
	edit_type = 1;
}

void act_self_ins(void) {
    int insert = insert_mode;
    unsigned char c;

    if (look_mode) {
	display_beep();
	strcpy (message, "Can't modify file in LOOK mode");
	return;
    }

    if (edit_type) {
	if (last_char >= '0' && last_char <= '9')
	    last_char -= '0';
	else if (last_char >= 'A' && last_char <= 'F')
	    last_char -= 'A'-10;
	else if (last_char >= 'a' && last_char <= 'f')
	    last_char -= 'a'-10;
	else {
	    display_beep();
	    strcpy(message, "Not a valid character when in hex editing mode");
	    return;
	}
    }

    if ( (!insert || edit_type == 2) && cur_pos == file_size) {
	display_beep();
	strcpy(message, "End of file reached");
	return;
    }

    switch (edit_type) {
      case 0:			       /* ascii mode */
	c = last_char;
	break;
      case 1:			       /* hex, first digit */
	if (insert)
	    c = 0;
	else
	    buf_fetch_data(filedata, &c, 1, cur_pos);
	c &= 0xF;
	c |= 16 * last_char;
	break;
      case 2:			       /* hex, second digit */
	buf_fetch_data(filedata, &c, 1, cur_pos);
	c &= 0xF0;
	c |= last_char;
	insert = FALSE;
	break;
    }

    if (insert) {
	buf_insert_data(filedata, &c, 1, cur_pos);
	file_size++;
	modified = TRUE;
    } else if (cur_pos < file_size) {
	buf_overwrite_data(filedata, &c, 1, cur_pos);
	modified = TRUE;
    } else {
	display_beep();
	strcpy(message, "End of file reached");
    }
    act_right();
}

static void act_delete(void) {
    if (!insert_mode || (edit_type!=2 && cur_pos==0)) {
	display_beep();
	strcpy (message, "Can't delete while not in Insert mode");
    } else if (cur_pos > 0 || edit_type == 2) {
	act_left();
	buf_delete (filedata, 1, cur_pos);
	file_size--;
	edit_type = !!edit_type;
	modified = TRUE;
    }
}

static void act_delch(void) {
    if (!insert_mode) {
	display_beep();
	strcpy (message, "Can't delete while not in Insert mode");
    } else if (cur_pos < file_size) {
	buf_delete (filedata, 1, cur_pos);
	file_size--;
	edit_type = !!edit_type;
	modified = TRUE;
    }
}

static void act_mark (void) {
    if (look_mode) {
	display_beep();
	strcpy (message, "Can't cut or paste in LOOK mode");
	marking = FALSE;	       /* safety */
	return;
    }
    marking = !marking;
    mark_point = cur_pos;
}

static void act_cut (void) {
    fileoffset_t marktop, marksize;

    if (!marking || mark_point==cur_pos) {
	display_beep();
	strcpy (message, "Set mark first");
	return;
    }
    if (!insert_mode) {
	display_beep();
	strcpy (message, "Can't cut while not in Insert mode");
	return;
    }
    marktop = cur_pos;
    marksize = mark_point - cur_pos;
    if (marksize < 0) {
	marktop += marksize;
	marksize = -marksize;
    }
    if (cutbuffer)
	buf_free (cutbuffer);
    cutbuffer = buf_cut (filedata, marksize, marktop);
    file_size -= marksize;
    cur_pos = marktop;
    if (cur_pos < 0)
	cur_pos = 0;
    if (top_pos > cur_pos)
	top_pos = begline(cur_pos);
    edit_type = !!edit_type;
    modified = TRUE;
    marking = FALSE;
}

static void act_copy (void) {
    fileoffset_t marktop, marksize;

    if (!marking) {
	display_beep();
	strcpy (message, "Set mark first");
	return;
    }
    marktop = cur_pos;
    marksize = mark_point - cur_pos;
    if (marksize < 0) {
	marktop += marksize;
	marksize = -marksize;
    }
    if (cutbuffer)
	buf_free (cutbuffer);
    cutbuffer = buf_copy (filedata, marksize, marktop);
    marking = FALSE;
}

static void act_paste (void) {
    fileoffset_t cutsize, new_top;

    if (!cutbuffer)
	return;
    cutsize = buf_length (cutbuffer);
    if (!insert_mode) {
	if (cur_pos + cutsize > file_size) {
	    display_beep();
	    strcpy (message, "Too close to end of file to paste");
	    return;
	}
	buf_delete (filedata, cutsize, cur_pos);
	file_size -= cutsize;
    }
    buf_paste (filedata, cutbuffer, cur_pos);
    modified = TRUE;
    cur_pos += cutsize;
    file_size += cutsize;
    edit_type = !!edit_type;
    new_top = cur_pos - (scrlines-1) * width;
    if (new_top < 0)
	new_top = 0;
    new_top = begline(new_top);
    if (top_pos < new_top)
	top_pos = new_top;
}

static void act_susp (void) {
    suspend();
}

static void act_goto (void) {
    char buffer[80];
    fileoffset_t position, new_top;
    int error;

    if (!get_str("Enter position to go to: ", buffer, FALSE))
	return;			       /* user break */

    position = parse_num (buffer, &error);
    if (error) {
	display_beep();
	strcpy (message, "Unable to parse position value");
	return;
    }

    if (position < 0 || position > file_size) {
	display_beep();
	strcpy (message, "Position is outside bounds of file");
	return;
    }

    cur_pos = position;
    edit_type = !!edit_type;
    new_top = cur_pos - (scrlines-1) * width;
    if (new_top < 0)
	new_top = 0;
    new_top = begline(new_top);
    if (top_pos > cur_pos)
	top_pos = begline(cur_pos);
    if (top_pos < new_top)
	top_pos = new_top;
}

static void act_togstat (void) {
    if (statfmt == decstatus)
	statfmt = hexstatus;
    else
	statfmt = decstatus;
}

static int search_prompt(char *withdef, char *withoutdef)
{
    char buffer[80];
    int len;

    if (!get_str(last_search ? withdef : withoutdef, buffer, TRUE))
	return 0;		       /* user break */
    if (!last_search && !*buffer) {
	strcpy (message, "Search aborted.");
	return 0;
    }

    if (!*buffer) {
	len = last_search->len;
    } else {
	len = parse_quoted (buffer);
	if (len == -1) {
	    display_beep();
	    strcpy (message, "Invalid escape sequence in search string");
	    return 0;
	}
	if (last_search)
	    free_search(last_search);
	last_search = build_search (buffer, len);
    }

    return 1;
}

static void act_search (void) {
    int len;
    fileoffset_t posn, dfapos;
    DFA dfa;
    static unsigned char sblk[SEARCH_BLK];
    static char withdef[] = "Search forward (default=last): ";
    static char withoutdef[] = "Search forward: ";

    if (!search_prompt(withdef, withoutdef))
	return;

    dfa = last_search->forward;
    len = last_search->len;
    dfapos = 0;

    for (posn = cur_pos+1; posn < file_size; posn++) {
	unsigned char *q;
	int size = SEARCH_BLK;

	if (size > file_size-posn)
	    size = file_size-posn;
	buf_fetch_data (filedata, sblk, size, posn);
	q = sblk;
	while (size--) {
	    posn++;
	    dfapos = dfa[dfapos][*q++];
	    if (dfapos == len) {
		fileoffset_t new_top;

		cur_pos = posn - len;
		edit_type = !!edit_type;
		new_top = cur_pos - (scrlines-1) * width;
		new_top = begline(new_top);
		if (top_pos < new_top)
		    top_pos = new_top;
		return;
	    }
	}
    }
    strcpy (message, "Not found.");
}

static void act_search_backwards (void) {
    int len;
    fileoffset_t posn, dfapos;
    DFA dfa;
    static unsigned char sblk[SEARCH_BLK];
    static char withdef[] = "Search backward (default=last): ";
    static char withoutdef[] = "Search backward: ";

    if (!search_prompt(withdef, withoutdef))
	return;

    dfa = last_search->reverse;
    len = last_search->len;
    dfapos = 0;

    posn = cur_pos + len - 1;
    if (posn >= file_size)
	posn = file_size;

    for (; posn >= 0; posn--) {
	unsigned char *q;
	int size = SEARCH_BLK;

	if (size > posn)
	    size = posn;
	buf_fetch_data (filedata, sblk, size, posn-size);
	q = sblk + size;
	while (size--) {
	    posn--;
	    dfapos = dfa[dfapos][*--q];
	    if (dfapos == len) {
		fileoffset_t new_top;

		cur_pos = posn;
		edit_type = !!edit_type;
		new_top = cur_pos - (scrlines-1) * width;
		new_top = begline(new_top);
		if (top_pos > new_top)
		    top_pos = new_top;
		return;
	    }
	}
    }
    strcpy (message, "Not found.");
}

static void act_recentre (void) {
    top_pos = cur_pos - (display_rows-2)/2 * width;
    if (top_pos < 0)
	top_pos = 0;
    top_pos = begline(top_pos);
}

static void act_width (void) {
    char buffer[80];
    char prompt[80];
    fileoffset_t w;
    fileoffset_t new_top;
    int error;

    sprintf (prompt, "Enter screen width in bytes (now %"OFF"d): ", width);
    if (!get_str (prompt, buffer, FALSE))
	return;
    w = parse_num (buffer, &error);
    if (error) {
	display_beep();
	strcpy (message, "Unable to parse width value");
	return;
    }
    if (w > 0) {
	width = w;
	fix_offset();
	new_top = cur_pos - (scrlines-1) * width;
	new_top = begline(new_top);
	if (top_pos < new_top)
	    top_pos = new_top;
    }
}

static void act_offset (void) {
    char buffer[80];
    char prompt[80];
    fileoffset_t o;
    fileoffset_t new_top;
    int error;

    sprintf (prompt, "Enter start-of-file offset in bytes (now %"OFF"d): ",
	     realoffset);
    if (!get_str (prompt, buffer, FALSE))
	return;
    o = parse_num (buffer, &error);
    if (error) {
	display_beep();
	strcpy (message, "Unable to parse offset value");
	return;
    }
    if (o >= 0) {
	realoffset = o;
	fix_offset();
	new_top = cur_pos - (scrlines-1) * width;
	new_top = begline(new_top);
	if (top_pos < new_top)
	    top_pos = new_top;
    }
}

#ifdef TEST_BUFFER
static void act_diagnostics(void)
{
    extern void buffer_diagnostic(buffer *buf, char *title);

    buffer_diagnostic(filedata, "filedata");
    buffer_diagnostic(cutbuffer, "cutbuffer");
}
#endif
