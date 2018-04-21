#ifndef TWEAK_TWEAK_H
#define TWEAK_TWEAK_H

#if defined(__APPLE__) && !defined(unix)
#define unix 1
#endif

#ifndef NO_LARGE_FILES

#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#endif

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define EVER ;;

#ifdef MSDOS
#define ABORT 34		       /* scan code for ^G */
#else
#define ABORT 7			       /* character code for ^G */
#endif

#ifndef VER
#define VER "3.02"
#endif

#define SEARCH_BLK 65536	       /* so can this */
#define SAVE_BLKSIZ 32768	       /* and this too */

#define COL_BUFFER 0		       /* normal buffer colour */
#define COL_SELECT 1		       /* selected-area colour */
#define COL_STATUS 2		       /* status-line colour */
#define COL_ESCAPE 3		       /* escape sequences in minibuffer */
#define COL_INVALID 4		       /* invalid escape sequence in m/b */
#define COL_MINIBUF COL_BUFFER	       /* these should be the same */

#define NULL4   NULL,  NULL,  NULL,  NULL
#define NULL16  NULL4, NULL4, NULL4, NULL4
#define NULL64  NULL16,NULL16,NULL16,NULL16
#define NULL256 NULL64,NULL64,NULL64,NULL64

#include <stdio.h>

#ifdef NO_LARGE_FILES
typedef int fileoffset_t;              /* used for all file offsets */
#define OFF
#define ATOOFF atoi
#define STRTOOFF strtol
#define fseeko fseek
#define ftello ftell
#else
typedef long long fileoffset_t;	       /* used for all file offsets */
#define OFF "ll"
#define ATOOFF atoll
#define STRTOOFF strtoll
#endif

typedef int (*DFA)[256];
typedef struct {
    int len;
    DFA forward, reverse;
} Search;

typedef void (*keyact) (void);

typedef struct buffer buffer;

extern char toprint[256], hex[256][3], message[512];
extern char decstatus[], hexstatus[], *statfmt;
extern char last_char, *pname, *filename;
extern buffer *filedata, *cutbuffer;
extern int fix_mode, look_mode, insert_mode, edit_type, finished, marking;
extern fileoffset_t file_size, top_pos, cur_pos, mark_point;
extern int scrlines, modified, new_file;
extern fileoffset_t width, offset, realoffset;
extern int ascii_enabled;

#ifdef unix
extern volatile int safe_update, update_required;
extern void update (void);
#endif

extern void fix_offset(void);
extern fileoffset_t parse_num (char *buffer, int *error);

extern void draw_scr (void);
extern int backup_file (void);
extern int save_file (void);

extern void act_self_ins (void);
extern keyact parse_action (char *);

extern void proc_key (void);
extern void bind_key (char *, int, keyact);

extern Search *build_search (char *, int);
void free_search(Search *s);

extern int get_str (char *, char *, int);
extern int parse_quoted (char *);
extern void suspend (void);

extern void read_rc (void);
extern void write_default_rc (void);

extern buffer *buf_new_empty(void);
extern buffer *buf_new_from_file(FILE *fp);
extern void buf_free(buffer *buf);

extern void buf_insert_data(buffer *buf, void *data, int len,
                            fileoffset_t pos);
extern void buf_fetch_data(buffer *buf, void *data, int len, fileoffset_t pos);
extern void buf_overwrite_data(buffer *buf, void *data, int len,
                               fileoffset_t pos);
extern void buf_delete(buffer *buf, fileoffset_t len, fileoffset_t pos);
extern buffer *buf_cut(buffer *buf, fileoffset_t len, fileoffset_t pos);
extern buffer *buf_copy(buffer *buf, fileoffset_t len, fileoffset_t pos);
extern void buf_paste(buffer *buf, buffer *cutbuffer, fileoffset_t pos);
extern fileoffset_t buf_length(buffer *buf);

extern void display_setup(void);
extern void display_cleanup(void);
extern void display_beep(void);
extern int display_rows, display_cols;
extern void display_moveto(int y, int x);
extern void display_refresh(void);
extern void display_write_str(char *str);
extern void display_write_chars(char *str, int len);
extern void display_define_colour(int colour, int fg, int bg, int reverse);
extern void display_set_colour(int colour);
extern void display_clear_to_eol(void);
extern int display_getkey(void);
extern int display_input_to_flush(void);
extern void display_post_error(void);
extern void display_recheck_size(void);

#endif /* TWEAK_TWEAK_H */
