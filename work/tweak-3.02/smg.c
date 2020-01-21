/* SMG.C
 * smg.c has been wrote to exchange either curses.c or slang.c
 * to make all display and keyboard functions on OpenVMS
 *---------------------------------------------------------------------------
 * HISTORY
 * 2018.10
 *   The first release SMG.C has exeptions:
 *   - B/W display output;
 *   - fixed terminal size, can't resize the screen if using X Window system;
 *   - do not work PgUp, PgDn, Home, End keys;
 *   - do not keep file structures.
 ----------------------------------------------------------------------------*/
#include "tweak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/*-----------------------------
 * VMS dependent HEADER files
 *-----------------------------*/
#include <descrip.h>           /* Defined data types in DECC      */
#include <SMG$ROUTINES.H>      /* Defined function SMG            */
#include <SMGDEF.H>            /* Defined data types & keyboard   */
#include <SMGMSG.H>            /* Defined messages from functions */
#include <SSDEF.H>             /* Defined messages SS$_xx         */
#include <LIBDEF.H>	       /* Defined messages LIB$_xx        */
#include <LIB$ROUTINES.H>      /* Defined lib$ routines           */

/*
 * The next variable declare here
 * and in the tweak.h extern 
 *
 */
int display_rows, display_cols;


/*-----------------------------
 * VMS dependent VARIABLES
 *-----------------------------*/
unsigned int attrib;              /* Global variable for attibute text output */
                                  /* using to BOLD, INVERT, UNDERLINE text    */
/*
 * The next variable declare here
 * and in the tweak.h extern
 */
int stdscr, stdpas, stdkbd;


/*-------------------------------
 * Defines
 *------------------------------*/

#define TRUE  1
#define FALSE 0

/*-------------------------------
 * Functions
 *------------------------------*/

void parse_status( unsigned int status,
                   char *fname,
                   char *fname_smg )
{
  int critical=0;

  if( !strcmp( fname_smg, "smg$create_pasteboard" ) )
      {
	switch( status )
	   {
		case LIB$_INSVIRMEM:   /* Insufficient memory to create buffer */
                        critical = 1;
			break;
		case SMG$_WRONUMARG:   /* Wrong number of arguments            */
                        critical = 1;
			break;
		default:
			break;
	   }
      }
  else if( !strcmp( fname_smg, "smg$create_virtual_display" ) )
      {
        switch( status )
           {
                case LIB$_INSVIRMEM:   /* Insufficient memory to create buffer  */
                        critical = 1;
                        break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments             */
                        critical = 1;
                        break;
		case SMG$_DSPIN_USE:   /* SMG$ call was made from an AST func   */
			break;
		case SMG$_INVARG:      /* Invalid args. video-attr or disp-attr */
			break;
                default:               /* Other happens */
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$create_virtual_keyboard" ) )
      {
        switch( status )
           {
                case LIB$_INSVIRMEM:   /* Insufficient memory to create buffer  */
                        critical = 1;
                        break;
                case LIB$_INSEF:       /* Insufficient number of event flags    */
                        critical = 1;
                        break;
                case LIB$_INVSTRDES:   /* Invalid string descriptor             */
                        critical = 1;
                        break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments             */
                        critical = 1;
                        break;
                case SMG$_FILTOOLON:   /* File specification is too long > 255  */
                        critical = 1;
                        break;
                default:               /* Other happens */
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$delete_virtual_display" ) )
      {
        switch( status )
           {
                case SMG$_INVDIS_ID:   /* Ivalid display-id                    */
                        critical = 1;
                        break;
                case SMG$_NOTPASTED:   /* The virtual display is not pasted    */
                        critical = 1;
                        break;
                case SMG$_WILUSERMS:   /* Pasterboard is not a video terminal  */
                        critical = 1;
                        break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments            */
                        critical = 1;
                        break;
                default:               /* Other happens  */
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$delete_virtual_keyboard" ) )
      {
        switch( status )
           {
                case SMG$_INVKBD_ID:   /* Ivalid keyboard-id                   */
                        critical = 1;
                        break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments            */
                        critical = 1;
                        break;
                default:               /* Other happens  */
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$delete_pasteboard" ) )
      {
        switch( status )
           {
                case SMG$_INVPAS_ID:   /* Ivalid pasteboard-id                 */
                        critical = 1;
                        break;
		case SMG$_NOTPASTED:   /* The virtual display is not pasted    */
                        critical = 1;
			break;
		case SMG$_WILUSERMS:   /* Pasterboard is not a video terminal  */
                        critical = 1;
			break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments            */
                        critical = 1;
                        break;
                default:               /* Other happens  */
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$set_cursor_abs" ) )
      {
        switch( status )
           {
		case SMG$_INVCOL:      /* Invalid column                       */
			break;
		case SMG$_INVROW:      /* Invalid row                          */
			break;
                case SMG$_INVDIS_ID:   /* Ivalid display-id                    */
                        critical = 1;
                        break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments            */
                        critical = 1;
                        break;
                default:               /* Other happens  */
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$paste_virtual_display" ) )
      {
        switch( status )
           {
                case SMG$_ILLBATFNC:   /* Dispaly is being batched; illegal oper */
                        critical = 1;
                        break;
                case SMG$_INVDIS_ID:   /* Ivalid display-id                      */
                        critical = 1;
                        break;
                case SMG$_INVPAS_ID:   /* Invalid pasterboard-id                 */
                        critical = 1;
                        break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments              */
                        critical = 1;
                        break;
                default:               /* Other happens  */
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$put_chars" ) )
      {
        switch( status )
           {
                case SMG$_INVCOL:      /* Invalid column                       */
                        critical = 1;
                        break;
                case SMG$_INVROW:      /* Invalid row                          */
                        critical = 1;
                        break;
                case SMG$_INVDIS_ID:   /* Ivalid display-id                    */
                        critical = 1;
                        break;
                case SMG$_WILUSERMS:   /* Pasterboard is not a video terminal  */
                        critical = 1;
                        break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments            */
                        critical = 1;
                        break;
                case LIB$_INVSTRDES:   /* Invalid string descriptor             */
                        critical = 1;
                        break;
                default:               /* Other happens  */
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$read_keystroke" ) )
      {
        switch( status )
           {
                case SMG$_EOF:         /* End of file */
                        break;
                case SMG$_INVDIS_ID:   /* Ivalid display-id                       */
                        critical = 1;
                        break;
                case SMG$_INVKBD_ID:   /* Invalid keyboard-id                     */
                        critical = 1;
                        break;
		case SMG$_KBDIN_USE:   /* Multiple QIOs were attempted on the kbd */
			break;
                case SMG$_WRONUMARG:   /* Wrong number of arguments               */
                        critical = 1;
                        break;
		case SS$_ABORT:        /* I/O operation aborted during execution  */
			break;
		case SS$_CANCEL:       /* I/O operation canceled while queued     */
			break;
                default:               /* Other happens SS$_xx, LIB$_xx, RMS$_xx
				      	*
					* Any error from $QIOW
					* Any error from LIB$SCOPY_R_DX
					* Any error form $GET (exept RMS$_EOF)
					*/
                        break;
           }
      }
  else if( !strcmp( fname_smg, "smg$ring_bell" ) )
      {
        switch( status )
           {
                case SMG$_INVDIS_ID:   /* Ivalid display-id  */
                        critical = 1;
                        break;
                default:               /* Other happens  */
                        break;		
	   }
      }
  else
      {
        ;  /* Other function which does not include in the parse if-else if */
      }

  if( critical )
     {
       lib$stop( &status );
     }
  else
     {
       if( sprintf( message, "Warning [%08x] in [%s].", status, fname ) >= sizeof( message) )
       /* Am thinking, that it does not will happen, but ... */
          {
            fprintf( stderr,
                     "\nThe data has been corrupt function sprintf() in module parse_status.\n" );
            exit( 3 );
          } 
          
     }
}  /* End of parse_status() */


void display_beep(void)
{
    unsigned int status; 

    status = smg$ring_bell( &stdscr );
    /* Check is it OK ? */
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_beep", "smg$ring_bell" );
        }

}  /* Enf of display_beep */

static void get_screen_size(void)
{
/*
    getmaxyx(stdscr, display_rows, display_cols);
*/
    unsigned int status; 
/*    
    status = smg$get_pasteboard( &stdpas, NULL, &display_rows, &display_cols );
     Check is it OK ?
    if( status != SS$_NORMAL )
        {
           parse_status( status, "get_screen_size", "smg$get_pasteboard" );
        }
*/
}


int has_colors( void )
{
  return FALSE;
}  /* End of has_colors()  */


int start_color( void )
{
  return 0;    /* No errors occured */
}  /* End of start_color() */


int use_default_colors( void )
{
  return 0;    /* No errors occured */
}  /* End of use_default_color() */



void display_setup(void)
{
    unsigned int status; 
    int row, col;
    int border;


    border =SMG$M_BORDER;

    status = smg$create_pasteboard( &stdpas, NULL, &display_rows, &display_cols );
    /* Check is it OK ? */
    if( status != SS$_NORMAL || status != SMG$_PASALREXI )
        {
           parse_status( status, "display_setup", "smg$create_pasteboard" );
        }

    row = display_rows;
    col = display_cols;
    status = smg$create_virtual_display( &row, &col, &stdscr );
    /* Check is it OK ? */
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_setup", "smg$create_virtual_display" );
        }
    /*
    noecho();
     */
    status = smg$create_virtual_keyboard( &stdkbd );
    /* Check is it OK ? */
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_setup", "smg$create_virtual_keyboard" );
        }
    /*
    raw();
    */
    display_moveto(0,0);
    /*
     refresh();
     */
    /* in VMS do not need call get_screen_size, because
     * smg$create_pasteboard got screen size
     * and paste them into display_rows display_cols
    get_screen_size();
     */
    
    if (has_colors())
      {
	if( !start_color() )
           if( use_default_colors() )
             {
             ; /* Error occured when use_default_colors()  */
             }
        else   /* Error occured when start_color() */
          {
	    ;
          }
      }

}

void display_cleanup(void)
{
    /*
     endwin();
     */
    unsigned int status; 
    unsigned int nFlags;

    /* move cursor to the bottom left corner */
    display_moveto( display_rows-1, 0 );
    display_clear_to_eol();

    /* If clear the screen uncomment next lines,
     * overwize the screen will be look like tweak
     */
    /*    status = smg$delete_virtual_display( &stdscr ); */
    /* Check is it OK ? */
    /*
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_cleanup", "smg$delete_virtual_display" );
        }
    */
    status = smg$delete_virtual_keyboard( &stdkbd );
    /* Check is it OK ? */
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_cleanup", "smg$delete_virtual_keyboard" );
        }
    
    nFlags = 0;  /* Does not clear the screen */
    status = smg$delete_pasteboard( &stdpas, &nFlags );
    /* Check is it OK ? */
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_cleanup", "smg$delete_pasteboard" );
        }
}

void display_moveto(int y, int x)
{
    unsigned int status;
    int row, col;
 
    /* 
     *In VMS the smg routines start position is 1,1
     *curses - 0,0, so x+1, y+1
     */ 
    col = x+1;
    row = y+1;

    status = smg$set_cursor_abs( &stdscr, &row, &col );
    /* Check is it OK ? */
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_moveto", "smg$set_cursor_abs" );
        }
}


void display_refresh(void)
{
    unsigned int status;
    int row,col;
    
    row = 1;
    col = 1;
    status = smg$paste_virtual_display( &stdscr, &stdpas, &row, &col );
    /* Check is it OK ? */    
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_refresh", "smg$paste_virtual_display" );
        }
}

void display_write_str(char *str)
{
    unsigned int status;

    $DESCRIPTOR( sLine, str );
    sLine.dsc$w_length = strlen( str );

    status = smg$put_chars( &stdscr, &sLine, NULL, NULL, NULL, &attrib );
    /* Check is it OK ? */
    if( status != SS$_NORMAL )
	{
	   parse_status( status, "display_write_str", "smg$put_chars" );
	}
}

void display_write_chars(char *str, int len)
{
    unsigned int status;        

    $DESCRIPTOR( sChars, str );
    sChars.dsc$w_length = len;

    status = smg$put_chars( &stdscr, &sChars, NULL, NULL, NULL, &attrib );
    /* Check is it OK ? */
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_write_chars", "smg$put_chars" );
        }

}

#define MAXCOLOURS 32
unsigned int attrs[MAXCOLOURS];

void init_pair( int col, int f, int b )
{
  /*
   * Setting color for foreground -> f and backgroud -> b
   */
}  /* End of init_pair() */


void display_define_colour(int colour, int fg, int bg, int reverse)
{
    static int colours[8] =
    {
      SMG$C_COLOR_BLACK,
      SMG$C_COLOR_RED,
      SMG$C_COLOR_GREEN,
      SMG$C_COLOR_YELLOW,
      SMG$C_COLOR_BLUE,
      SMG$C_COLOR_MAGENTA,
      SMG$C_COLOR_CYAN,
      SMG$C_COLOR_WHITE
    };



    if (fg < 0 && bg < 0) {
       if ( colour >=0 && colour < MAXCOLOURS )
            attrs[colour] = 0;
    } else {
        assert(colour >= 0 && colour < MAXCOLOURS);
        assert(!(bg & ~7));            /* bold backgrounds are nonportable */
/*
	if (colour < COLOR_PAIRS-2) {
	    init_pair(colour+1, colours[fg & 7], colours[bg]);
	    attrs[colour] = (fg & 8 ? A_BOLD : 0) | COLOR_PAIR(colour+1);
	} else {
*/
	/* can't allocate a colour pair, so we just use b&w attrs */
	attrs[colour] = (fg & 8 ? SMG$M_BOLD : 0) | (reverse ? SMG$M_REVERSE : 0);
/*	}      */

    }
}

void display_set_colour(int colour)
{

   attrib = attrs[colour];

}

void display_clear_to_eol(void)
{
    unsigned int status;        
    unsigned int nFlags;
    $DESCRIPTOR( sChars, "" );

    sChars.dsc$w_length = strlen( sChars.dsc$a_pointer );
    nFlags = SMG$M_ERASE_TO_EOL;
    
    /* Clear to end of lile
     * from current position
     * of cursor.
     * The 3d and 4s arguments is NULL pointer
     */
    status = smg$put_chars( &stdscr, &sChars, NULL, NULL, &nFlags, &attrib );
    if( status != SS$_NORMAL )
        {
           parse_status( status, "display_clear_to_eol", "smg$put_chars" );
        }
}    /* End of display_clear_to_eol */


/* In the curses ERR = -1 */
#define ERR -1
int last_getch = ERR;

unsigned int display_getkey(void)
{
    unsigned int status;
    unsigned int ret;
/*    extern void schedule_update(void);    */

    if (last_getch != ERR) {
	ret = (unsigned int) last_getch;
	last_getch = ERR;
	return ret;
    }
    while (1)
    {
        status = smg$read_keystroke( &stdkbd, &ret );
        if( status != SS$_NORMAL )
          {
           parse_status( status, "display_getkey", "smg$read_keystroke" );
          }
/*
	if (ret == KEY_RESIZE) {
	    schedule_update();
	    continue;
	}
*/
	return ret;
    }
}

int display_input_to_flush(void)
{
    /*
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
    */
    /*---------------------------*/
    /* In VMS SMG$ function do not
     * buffered at all.
     * So we simple return FASLE
     * to stop loop in the
     * up function
     */

    return FALSE;
}   /* End of display_input_to_flush() */

void display_post_error(void)
{
    /* I don't _think_ we need do anything here */
}

void display_recheck_size(void)
{
    /*
    get_screen_size ();
    */
}

/*-----------------------------------------------------*/
/*   End of file smg.c                                 */
/*-----------------------------------------------------*/
