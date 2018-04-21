#include "tweak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef union keytab keytab;

union keytab {
    enum {ACTION, EXTENDED} type;
    struct {
	int type;
	keyact action;
    } a;
    struct {
	int type;
	keytab *extended[256];
    } e;
};

keytab *base[256] = { NULL256 };

/*
 * Bind a key sequence to an action.
 */
void bind_key (char *sequence, int len, keyact action) {
    keytab *(*table)[256];
    int k, i;

    table = &base;
    while (--len) {
	k = (unsigned char) *sequence++;
	if ( !(*table)[k] ) {
	    /*
	     * We must create an EXTENDED entry.
	     */
	    (*table)[k] = malloc(sizeof(base[0]->e));
	    (*table)[k]->type = EXTENDED;
	    for (i=0; i<256; i++)
		(*table)[k]->e.extended[i] = NULL;
	} else if ( (*table)[k]->type == ACTION ) {
	    /*
	     * A subsequence is already bound: fail.
	     */
	    return;
	}
	table = &(*table)[k]->e.extended;
    }
    k = (unsigned char) *sequence;
    if ( !(*table)[k] ) {
	/*
	 * We can bind the key.
	 */
	(*table)[k] = malloc(sizeof(base[0]->a));
	(*table)[k]->type = ACTION;
	(*table)[k]->a.action = action;
    }
}

/*
 * Format an ASCII code into a printable description of the key stroke.
 */
static void strkey (char *s, int k) {
    k &= 255;			       /* force unsigned */
    if (k==27)
	strcpy(s, " ESC");
    else if (k<32 || k==127)
	sprintf(s, " ^%c", k ^ 64);
    else if (k<127)
	sprintf(s, " %c", k);
    else
	sprintf(s, " <0x%2X>", k);
}

/*
 * Get and process a key stroke.
 */
void proc_key (void) {
    keytab *kt;

#if defined(unix) && !defined(GO32)
    if (update_required)
	update();
    safe_update = TRUE;
#endif
    last_char = display_getkey();
#if defined(unix) && !defined(GO32)
    safe_update = FALSE;
#endif
    strcpy(message, "Unknown key sequence");
    strkey(message+strlen(message), last_char);
    kt = base[(unsigned char) last_char];
    if (!kt) {
	display_beep();
	while (display_input_to_flush())
	    strkey(message+strlen(message), display_getkey());
	return;
    }

    while (kt->type == EXTENDED) {
#if defined(unix) && !defined(GO32)
	if (update_required)
	    update();
	safe_update = TRUE;
#endif
	last_char = display_getkey();
#if defined(unix) && !defined(GO32)
	safe_update = FALSE;
#endif
	strkey(message+strlen(message), last_char);
	kt = kt->e.extended[(unsigned char) last_char];
	if (!kt) {
	    display_beep();
	    while (display_input_to_flush())
		strkey(message+strlen(message), display_getkey());
	    return;
	}
    }
    message[0] = '\0';		       /* clear the "unknown" message */
    (*kt->a.action)();
}
