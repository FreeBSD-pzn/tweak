$!------------------------------------------------------------------------
$! File: MAKEFILE.COM
$! ----------------------------
$! This is template file to create you own makefile.com
$!------------------------------------------------------------------------
$  set noverify
$! Check your system
$!
$  arch_name=f$getsyi("ARCH_NAME")
$  WRITE SYS$OUTPUT "System is ''arch_name'."
$  WRITE SYS$OUTPUT "-------------------------------------"
$  WRITE SYS$OUTPUT "Compiling sources files ..."
$!------------------------------------------------------------------------
$! Put all compile commands and linking here  
$! Example:
$! CC/DEFINE=(VMS,'arch_name') FILE.C
$! LINK FILE
$  WRITE SYS$OUTPUT "Compiling file ACTIONS.C ..."
$  CC /DEFINE=(VMS,'arch_name') ACTIONS.C
$  WRITE SYS$OUTPUT "Compiling file BTREE.C ..."
$  CC /DEFINE=(VMS,'arch_name') BTREE.C
$  WRITE SYS$OUTPUT "Compiling file BUFFER.C ..."
$  CC /DEFINE=(VMS,'arch_name') BUFFER.C
$  WRITE SYS$OUTPUT "Compiling file KEYTAB.C ..."
$  CC /DEFINE=(VMS,'arch_name') KEYTAB.C
$  WRITE SYS$OUTPUT "Compiling file MAIN.C ..."
$  CC /DEFINE=(VMS,'arch_name')/OBJECT=TWEAK MAIN.C
$  WRITE SYS$OUTPUT "Compiling file RCFILE.C ..."
$  CC /DEFINE=(VMS,'arch_name') RCFILE.C
$  WRITE SYS$OUTPUT "Compiling file SEARCH.C ..."
$  CC /DEFINE=(VMS,'arch_name') SEARCH.C
$  WRITE SYS$OUTPUT "Compiling file SMG.C ..."
$  CC /DEFINE=(VMS,'arch_name') SMG.C
$! ----------------------------------------------
$! Need to compile either CURSES.C or SLANG.C
$!  WRITE SYS$OUTPUT "Compiling file CURSES.C ..."
$!  CC /DEFINE=(VMS,'arch_name') CURSES.C
$!  WRITE SYS$OUTPUT "Compiling file SLANG.C ..."
$!  CC /DEFINE=(VMS,'arch_name') SLANG.C
$! ----------------------------------------------
$  WRITE SYS$OUTPUT "Linking TWEAK.EXE ..."
$  LINK  TWEAK,ACTIONS,BTREE,BUFFER,SMG,KEYTAB,RCFILE,SEARCH
$! - Clean directory -----------------------------------------------------
$  WRITE SYS$OUTPUT "Cleaning DIRECTORY ..."
$! - Check if exixst *.obj and *.exe -----
$  IF F$SEARCH("*.OBJ") .EQS. ""
$  THEN
$      WRITE SYS$OUTPUT "->>> There is nothing to delete ..."
$  ELSE
$      WRITE SYS$OUTPUT "->>> Deleting *.OBJ files ..."
$      DELETE *.OBJ;*
$  ENDIF
$  IF F$SEARCH("TWEAK.EXE") .EQS. ""
$  THEN
$      WRITE SYS$OUTPUT "->>> There is nothing to PURGE ..."
$  ELSE
$      WRITE SYS$OUTPUT "->>> Rurging EXE files ..."
$      PURGE TWEAK.EXE
$  ENDIF
$!------------------------------------------------------------------------
$ EXIT
$!------------------------------------------------------------------------
