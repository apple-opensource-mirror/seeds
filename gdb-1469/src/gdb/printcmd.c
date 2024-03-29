/* Print values for GNU debugger GDB.

   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "gdb_string.h"
#include "frame.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "value.h"
#include "language.h"
#include "expression.h"
#include "gdbcore.h"
#include "gdbcmd.h"
#include "target.h"
#include "breakpoint.h"
#include "demangle.h"
#include "valprint.h"
#include "annotate.h"
#include "symfile.h"		/* for overlay functions */
#include "objfiles.h"		/* ditto */
#include "completer.h"		/* for completion functions */
#include "ui-out.h"
#include "gdb_assert.h"
#include "block.h"
#include "disasm.h"
#include "objc-lang.h"

#ifdef TUI
#include "tui/tui.h"		/* For tui_active et.al.   */
#endif

extern int asm_demangle;	/* Whether to demangle syms in asm printouts */
extern int addressprint;	/* Whether to print hex addresses in HLL " */

struct format_data
  {
    int count;
    char format;
    char size;
  };

/* Last specified output format.  */

static char last_format = 'x';

/* Last specified examination size.  'b', 'h', 'w' or `q'.  */

static char last_size = 'w';

/* Default address to examine next.  */

static CORE_ADDR next_address;

/* Last address examined.  */

static CORE_ADDR last_examine_address;

/* Contents of last address examined.
   This is not valid past the end of the `x' command!  */

static struct value *last_examine_value;

/* APPLE LOCAL: Use this to truncate the symbolic name in disassembly output.  */
int disassembly_name_length = -1;

/* Largest offset between a symbolic value and an address, that will be
   printed as `0x1234 <symbol+offset>'.  */

static unsigned int max_symbolic_offset = UINT_MAX;
static void
show_max_symbolic_offset (struct ui_file *file, int from_tty,
			  struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
The largest offset that will be printed in <symbol+1234> form is %s.\n"),
		    value);
}

/* Append the source filename and linenumber of the symbol when
   printing a symbolic value as `<symbol at filename:linenum>' if set.  */
static int print_symbol_filename = 0;
static void
show_print_symbol_filename (struct ui_file *file, int from_tty,
			    struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("\
Printing of source filename and line number with <symbol> is %s.\n"),
		    value);
}

/* Number of auto-display expression currently being displayed.
   So that we can disable it if we get an error or a signal within it.
   -1 when not doing one.  */

int current_display_number;

/* Flag to low-level print routines that this value is being printed
   in an epoch window.  We'd like to pass this as a parameter, but
   every routine would need to take it.  Perhaps we can encapsulate
   this in the I/O stream once we have GNU stdio. */

int inspect_it = 0;

struct display
  {
    /* Chain link to next auto-display item.  */
    struct display *next;
    /* Expression to be evaluated and displayed.  */
    struct expression *exp;
    /* Item number of this auto-display item.  */
    int number;
    /* Display format specified.  */
    struct format_data format;
    /* Innermost block required by this expression when evaluated */
    struct block *block;
    /* Status of this display (enabled or disabled) */
    int enabled_p;
  };

/* Chain of expressions whose values should be displayed
   automatically each time the program stops.  */

static struct display *display_chain;

static int display_number;

/* Prototypes for exported functions. */

void output_command (char *, int);

void _initialize_printcmd (void);

/* Prototypes for local functions. */

static void delete_display (int);

static void enable_display (char *, int);

static void disable_display_command (char *, int);

static void printf_command (char *, int);

static void display_info (char *, int);

static void do_one_display (struct display *);

static void undisplay_command (char *, int);

static void free_display (struct display *);

static void display_command (char *, int);

void x_command (char *, int);

static void address_info (char *, int);

static void set_command (char *, int);

static void call_command (char *, int);

static void inspect_command (char *, int);

static void print_command (char *, int);

static void print_command_1 (char *, int, int);

static void validate_format (struct format_data, char *);

static void print_formatted (struct value *, int, int, struct ui_file *);

static struct format_data decode_format (char **, int, int);

static void sym_info (char *, int);


/* Decode a format specification.  *STRING_PTR should point to it.
   OFORMAT and OSIZE are used as defaults for the format and size
   if none are given in the format specification.
   If OSIZE is zero, then the size field of the returned value
   should be set only if a size is explicitly specified by the
   user.
   The structure returned describes all the data
   found in the specification.  In addition, *STRING_PTR is advanced
   past the specification and past all whitespace following it.  */

static struct format_data
decode_format (char **string_ptr, int oformat, int osize)
{
  struct format_data val;
  char *p = *string_ptr;

  val.format = '?';
  val.size = '?';
  val.count = 1;

  if (*p >= '0' && *p <= '9')
    val.count = atoi (p);
  while (*p >= '0' && *p <= '9')
    p++;

  /* Now process size or format letters that follow.  */

  while (1)
    {
      if (*p == 'b' || *p == 'h' || *p == 'w' || *p == 'g')
	val.size = *p++;
      else if (*p >= 'a' && *p <= 'z')
	val.format = *p++;
      /* APPLE LOCAL: OSType formatting */
      else if (*p == 'T')
        val.format = *p++;
      /* APPLE LOCAL: floating point hex formatting */
      else if (*p == 'A')
        val.format = *p++;
      else
	break;
    }

  while (*p == ' ' || *p == '\t')
    p++;
  *string_ptr = p;

  /* Set defaults for format and size if not specified.  */
  if (val.format == '?')
    {
      if (val.size == '?')
	{
	  /* Neither has been specified.  */
	  val.format = oformat;
	  val.size = osize;
	}
      else
	/* If a size is specified, any format makes a reasonable
	   default except 'i'.  */
	val.format = oformat == 'i' ? 'x' : oformat;
    }
  else if (val.size == '?')
    switch (val.format)
      {
      case 'a':
      case 's':
	/* Pick the appropriate size for an address.  */
	if (TARGET_PTR_BIT == 64)
	  val.size = osize ? 'g' : osize;
	else if (TARGET_PTR_BIT == 32)
	  val.size = osize ? 'w' : osize;
	else if (TARGET_PTR_BIT == 16)
	  val.size = osize ? 'h' : osize;
	else
	  /* Bad value for TARGET_PTR_BIT */
	  internal_error (__FILE__, __LINE__, _("failed internal consistency check"));
	break;
      case 'f':
      case 'A':
	/* Floating point has to be word or giantword.  */
	if (osize == 'w' || osize == 'g')
	  val.size = osize;
	else
	  /* Default it to giantword if the last used size is not
	     appropriate.  */
	  val.size = osize ? 'g' : osize;
	break;
      case 'c':
	/* Characters default to one byte.  */
	val.size = osize ? 'b' : osize;
	break;
      case 'i':
	/* Instructions default to one byte, and we don't remember any old
           values.  */
	val.size = 'b';
	break;
      default:
	/* The default is the size most recently specified.  */
	val.size = osize;
      }

  return val;
}

/* Print value VAL on stream according to FORMAT, a letter or 0.
   Do not end with a newline.
   0 means print VAL according to its own type.
   SIZE is the letter for the size of datum being printed.
   This is used to pad hex numbers so they line up.  */
char g_examine_i_size = 'b';
static void
print_formatted (struct value *val, int format, int size,
		 struct ui_file *stream)
{
  struct type *type = check_typedef (value_type (val));
  int len = TYPE_LENGTH (type);

  if (VALUE_LVAL (val) == lval_memory)
    {
      next_address = VALUE_ADDRESS (val) + len;
    }

  switch (format)
    {
    case 's':
      /* FIXME: Need to handle wchar_t's here... */
      next_address = VALUE_ADDRESS (val)
	+ val_print_string (VALUE_ADDRESS (val), -1, 1, stream);
      break;

    /* APPLE LOCAL begin OSType formatting */
    case 'T':
      {
        printf_filtered ("'");
        print_ostype (gdb_stdout, type, (unsigned char *) value_contents (val));
        printf_filtered ("'");
        break;
      }
    /* APPLE LOCAL end OSType formatting */

    case 'i':
      /* The old comment says
         "Force output out, print_insn not using _filtered".
         I'm not completely sure what that means, I suspect most print_insn
         now do use _filtered, so I guess it's obsolete.
         --Yes, it does filter now, and so this is obsolete.  -JB  */

      /* APPLE LOCAL: set a global to allow print_insn() functions to do 
         something different if a different size is specified. The default
         value is 'b' when no size is specified with the instruction format.  */
      g_examine_i_size = size;

      /* We often wrap here if there are long symbolic names.  */
      wrap_here ("    ");
      next_address = VALUE_ADDRESS (val)
	+ gdb_print_insn (VALUE_ADDRESS (val), stream);
      /* APPLE LOCAL: reset a global to byte so print_insn() functions can do
         what they normally do when printing/disassembling.  */
      g_examine_i_size = 'b';
      break;

    default:
      if (format == 0
	  || TYPE_CODE (type) == TYPE_CODE_ARRAY
	  || TYPE_CODE (type) == TYPE_CODE_STRING
	  || TYPE_CODE (type) == TYPE_CODE_STRUCT
	  || TYPE_CODE (type) == TYPE_CODE_UNION
	  || TYPE_CODE (type) == TYPE_CODE_NAMESPACE)
	/* If format is 0, use the 'natural' format for
	 * that type of value.  If the type is non-scalar,
	 * we have to use language rules to print it as
	 * a series of scalars.
	 */
	value_print (val, stream, format, Val_pretty_default);
      else
	/* User specified format, so don't look to the
	 * the type to tell us what to do.
	 */
	print_scalar_formatted (value_contents (val), type,
				format, size, stream);
    }
}

/* Print a scalar of data of type TYPE, pointed to in GDB by VALADDR,
   according to letters FORMAT and SIZE on STREAM.
   FORMAT may not be zero.  Formats s and i are not supported at this level.

   This is how the elements of an array or structure are printed
   with a format.  */

void
print_scalar_formatted (const void *valaddr, struct type *type,
			int format, int size, struct ui_file *stream)
{
  LONGEST val_long = 0;
  unsigned int len = TYPE_LENGTH (type);

  if (len > sizeof(LONGEST) &&
      (TYPE_CODE (type) == TYPE_CODE_INT
       || TYPE_CODE (type) == TYPE_CODE_ENUM))
    {
      switch (format)
	{
	case 'o':
	  print_octal_chars (stream, valaddr, len);
	  return;
	case 'u':
	case 'd':
	  print_decimal_chars (stream, valaddr, len);
	  return;
	case 't':
	  print_binary_chars (stream, valaddr, len);
	  return;
	case 'x':
	  print_hex_chars (stream, valaddr, len);
	  return;
	case 'c':
	  print_char_chars (stream, valaddr, len);
	  return;
	default:
	  break;
	};
    }

  /* APPLE LOCAL: OSType formatting */
  if (format == 'T')
    {
      print_ostype (stream, type, (unsigned char *) valaddr);
      return;
    }

  if (format != 'f' && format != 'A')
    val_long = unpack_long (type, valaddr);

  /* If the value is a pointer, and pointers and addresses are not the
     same, then at this point, the value's length (in target bytes) is
     TARGET_ADDR_BIT/TARGET_CHAR_BIT, not TYPE_LENGTH (type).  */
  if (TYPE_CODE (type) == TYPE_CODE_PTR)
    len = TARGET_ADDR_BIT / TARGET_CHAR_BIT;

  /* If we are printing it as unsigned, truncate it in case it is actually
     a negative signed value (e.g. "print/u (short)-1" should print 65535
     (if shorts are 16 bits) instead of 4294967295).  */
  if (format != 'd')
    {
      if (len < sizeof (LONGEST))
	val_long &= ((LONGEST) 1 << HOST_CHAR_BIT * len) - 1;
    }

  switch (format)
    {
    case 'x':
      if (!size)
	{
	  /* no size specified, like in print.  Print varying # of digits. */
	  print_longest (stream, 'x', 1, val_long);
	}
      else
	switch (size)
	  {
	  case 'b':
	  case 'h':
	  case 'w':
	  case 'g':
	    print_longest (stream, size, 1, val_long);
	    break;
	  default:
	    error (_("Undefined output size \"%c\"."), size);
	  }
      break;

    case 'd':
      print_longest (stream, 'd', 1, val_long);
      break;

    case 'u':
      print_longest (stream, 'u', 0, val_long);
      break;

    case 'o':
      if (val_long)
	print_longest (stream, 'o', 1, val_long);
      else
	fprintf_filtered (stream, "0");
      break;

    case 'a':
      {
	CORE_ADDR addr = unpack_pointer (type, valaddr);
	print_address (addr, stream);
      }
      break;

    case 'c':
      value_print (value_from_longest (builtin_type_true_char, val_long),
		   stream, 0, Val_pretty_default);
      break;

    case 'f':
    case 'A':
      /* APPLE LOCAL: If the type of the value isn't already float, force it 
         to the appropriately sized float for printing.

         We need to do this when printing i386 vector (xmm) registers -- we 
         have a TYPE_CODE_FLT type but it's reverse endian (it's big-endian
         in gdb) so simply replacing it with builtin_type_float will print
         it backwards (it'll treat the bytes as a little-endian formatted
         floating point value).  */
      if (TYPE_CODE (type) != TYPE_CODE_FLT)
        {
          if (len == TYPE_LENGTH (builtin_type_float))
            type = builtin_type_float;
          else if (len == TYPE_LENGTH (builtin_type_double))
            type = builtin_type_double;
          else if (len == TYPE_LENGTH (builtin_type_long_double))
            type = builtin_type_long_double;
        }
      if (format == 'A')
        print_floating_in_hex (valaddr, type, stream);
      else
        print_floating (valaddr, type, stream);
      break;

    case 0:
      internal_error (__FILE__, __LINE__, _("failed internal consistency check"));

    case 't':
      /* Binary; 't' stands for "two".  */
      {
	char bits[8 * (sizeof val_long) + 1];
	char buf[8 * (sizeof val_long) + 32];
	char *cp = bits;
	int width;

	if (!size)
	  width = 8 * (sizeof val_long);
	else
	  switch (size)
	    {
	    case 'b':
	      width = 8;
	      break;
	    case 'h':
	      width = 16;
	      break;
	    case 'w':
	      width = 32;
	      break;
	    case 'g':
	      width = 64;
	      break;
	    default:
	      error (_("Undefined output size \"%c\"."), size);
	    }

	bits[width] = '\0';
	while (width-- > 0)
	  {
	    bits[width] = (val_long & 1) ? '1' : '0';
	    val_long >>= 1;
	  }
	if (!size)
	  {
	    while (*cp && *cp == '0')
	      cp++;
	    if (*cp == '\0')
	      cp--;
	  }
	strcpy (buf, cp);
	fputs_filtered (buf, stream);
      }
      break;

    default:
      error (_("Undefined output format \"%c\"."), format);
    }
}

/* Specify default address for `x' command.
   `info lines' uses this.  */

void
set_next_address (CORE_ADDR addr)
{
  next_address = addr;

  /* Make address available to the user as $_.  */
  set_internalvar (lookup_internalvar ("_"),
		   value_from_pointer (lookup_pointer_type (builtin_type_void),
				       addr));
}

/* Optionally print address ADDR symbolically as <SYMBOL+OFFSET> on STREAM,
   after LEADIN.  Print nothing if no symbolic name is found nearby.
   Optionally also print source file and line number, if available.
   DO_DEMANGLE controls whether to print a symbol in its native "raw" form,
   or to interpret it as a possible C++ name and convert it back to source
   form.  However note that DO_DEMANGLE can be overridden by the specific
   settings of the demangle and asm_demangle variables.  */

void
print_address_symbolic (CORE_ADDR addr, struct ui_file *stream, int do_demangle,
			char *leadin)
{
  char *name = NULL;
  char *filename = NULL;
  int unmapped = 0;
  int offset = 0;
  int line = 0;

  /* throw away both name and filename */
  struct cleanup *cleanup_chain = make_cleanup (free_current_contents, &name);

  /* APPLE LOCAL begin */
  /* See the note on print_address_numeric.  We have to do the same thing
     here or we won't get the location (and thus the name) right when printing
     signed types as addresses if the address is high enough to have the top
     bit set.  */
     
  int addr_bit = TARGET_ADDR_BIT;

  if (addr_bit < (sizeof (CORE_ADDR) * HOST_CHAR_BIT))
    addr &= ((CORE_ADDR) 1 << addr_bit) - 1;
  /* APPLE LOCAL end */

  make_cleanup (free_current_contents, &filename);

  if (build_address_symbolic (addr, do_demangle, &name, &offset, &filename, &line, &unmapped))
    {
      do_cleanups (cleanup_chain);
      return;
    }

  fputs_filtered (leadin, stream);
  if (unmapped)
    fputs_filtered ("<*", stream);
  else
    fputs_filtered ("<", stream);
  fputs_filtered (name, stream);
  if (offset != 0)
    fprintf_filtered (stream, "+%u", (unsigned int) offset);

  /* Append source filename and line number if desired.  Give specific
     line # of this addr, if we have it; else line # of the nearest symbol.  */
  if (print_symbol_filename && filename != NULL)
    {
      if (line != -1)
	fprintf_filtered (stream, " at %s:%d", filename, line);
      else
	fprintf_filtered (stream, " in %s", filename);
    }
  if (unmapped)
    fputs_filtered ("*>", stream);
  else
    fputs_filtered (">", stream);

  do_cleanups (cleanup_chain);
}

/* Given an address ADDR return all the elements needed to print the
   address in a symbolic form. NAME can be mangled or not depending
   on DO_DEMANGLE (and also on the asm_demangle global variable,
   manipulated via ''set print asm-demangle''). Return 0 in case of
   success, when all the info in the OUT paramters is valid. Return 1
   otherwise. */
int
build_address_symbolic (CORE_ADDR addr,  /* IN */
			int do_demangle, /* IN */
			char **name,     /* OUT */
			int *offset,     /* OUT */
			char **filename, /* OUT */
			int *line,       /* OUT */
			int *unmapped)   /* OUT */
{
  struct minimal_symbol *msymbol;
  struct symbol *symbol;
  struct symtab *symtab = 0;
  CORE_ADDR name_location = 0;
  asection *section = 0;
  char *name_temp = "";
  
  /* Let's say it is unmapped. */
  *unmapped = 0;

  /* Determine if the address is in an overlay, and whether it is
     mapped. */
  if (overlay_debugging)
    {
      section = find_pc_overlay (addr);
      if (pc_in_unmapped_range (addr, section))
	{
	  *unmapped = 1;
	  addr = overlay_mapped_address (addr, section);
	}
    }

  /* First try to find the address in the symbol table, then
     in the minsyms.  Take the closest one.  */

  /* This is defective in the sense that it only finds text symbols.  So
     really this is kind of pointless--we should make sure that the
     minimal symbols have everything we need (by changing that we could
     save some memory, but for many debug format--ELF/DWARF or
     anything/stabs--it would be inconvenient to eliminate those minimal
     symbols anyway).  */
  msymbol = lookup_minimal_symbol_by_pc_section (addr, section);
  symbol = find_pc_sect_function (addr, section);

  if (symbol)
    {
      /* APPLE LOCAL begin address ranges  */
      name_location = BLOCK_LOWEST_PC (SYMBOL_BLOCK_VALUE (symbol));
      /* APPLE LOCAL end address ranges  */
      if (do_demangle || asm_demangle)
	name_temp = SYMBOL_PRINT_NAME (symbol);
      else
	name_temp = DEPRECATED_SYMBOL_NAME (symbol);
    }

  if (msymbol != NULL)
    {
      if (SYMBOL_VALUE_ADDRESS (msymbol) > name_location || symbol == NULL)
	{
	  /* The msymbol is closer to the address than the symbol;
	     use the msymbol instead.  */
	  symbol = 0;
	  symtab = 0;
	  name_location = SYMBOL_VALUE_ADDRESS (msymbol);
	  if (do_demangle || asm_demangle)
	    name_temp = SYMBOL_PRINT_NAME (msymbol);
	  else
	    name_temp = DEPRECATED_SYMBOL_NAME (msymbol);
	}
    }
  if (symbol == NULL && msymbol == NULL)
    return 1;

  /* If the nearest symbol is too far away, don't print anything symbolic.  */

  /* Make sure the minimal symbol we found is in the same section as the
     address we're looking for.  If not, then we've found some random
     last-symbol-in-the-address-space and misusing it. */

  /* Get the *actual* section for MSYMBOL, see if the ADDR we're looking
     for is contained within it.  Note that the SYMBOL_BFD_SECTION(msymbol) is
     not necessarily correct.  e.g. a constant string is in __TEXT,__cstring
     but the msymbol will have __TEXT,__text because other parts of gdb assume
     that the SYMBOL_BFD_SECTION for a given minsym is only one of a couple
     sections.  */

  if (msymbol)
    {
      struct obj_section *verify_sect = 
        find_pc_sect_in_ordered_sections (SYMBOL_VALUE_ADDRESS (msymbol), NULL);
      if (verify_sect)
	{
	  /* If ADDR outside the range of this section, we didn't find a real
	     matching symbol.  */
	  if (addr < verify_sect->addr || addr >= verify_sect->endaddr)
	    return 1;
	}
    }

  /* For when CORE_ADDR is larger than unsigned int, we do math in
     CORE_ADDR.  But when we detect unsigned wraparound in the
     CORE_ADDR math, we ignore this test and print the offset,
     because addr+max_symbolic_offset has wrapped through the end
     of the address space back to the beginning, giving bogus comparison.  */
  if (addr > name_location + max_symbolic_offset
      && name_location + max_symbolic_offset > name_location)
    return 1;

  *offset = addr - name_location;

  *name = xstrdup (name_temp);
  /* APPLE LOCAL: Truncate the name in the disassembly output  */
  if (disassembly_name_length >= 0)
    {
      if (strlen (*name) > disassembly_name_length)
	(*name)[disassembly_name_length] = '\0';
    }
  /* END APPLE LOCAL */

  if (print_symbol_filename)
    {
      struct symtab_and_line sal;

      sal = find_pc_sect_line (addr, section, 0);

      if (sal.symtab)
	{
	  *filename = xstrdup (sal.symtab->filename);
	  *line = sal.line;
	}
      else if (symtab && symbol && symbol->line)
	{
	  *filename = xstrdup (symtab->filename);
	  *line = symbol->line;
	}
      else if (symtab)
	{
	  *filename = xstrdup (symtab->filename);
	  *line = -1;
	}
    }
  return 0;
}

/* Print address ADDR on STREAM.  USE_LOCAL means the same thing as for
   print_longest.  */
void
deprecated_print_address_numeric (CORE_ADDR addr, int use_local,
				  struct ui_file *stream)
{
  if (use_local)
    fputs_filtered (paddress (addr), stream);
  else
    {
      int addr_bit = TARGET_ADDR_BIT;

      if (addr_bit < (sizeof (CORE_ADDR) * HOST_CHAR_BIT))
	addr &= ((CORE_ADDR) 1 << addr_bit) - 1;
      print_longest (stream, 'x', 0, (ULONGEST) addr);
    }
}

/* Print address ADDR symbolically on STREAM.
   First print it as a number.  Then perhaps print
   <SYMBOL + OFFSET> after the number.  */

void
print_address (CORE_ADDR addr, struct ui_file *stream)
{
  deprecated_print_address_numeric (addr, 1, stream);
  print_address_symbolic (addr, stream, asm_demangle, " ");
}

/* Print address ADDR symbolically on STREAM.  Parameter DEMANGLE
   controls whether to print the symbolic name "raw" or demangled.
   Global setting "addressprint" controls whether to print hex address
   or not.  */

void
print_address_demangle (CORE_ADDR addr, struct ui_file *stream, int do_demangle)
{
  if (addr == 0)
    {
      fprintf_filtered (stream, "0");
    }
  else if (addressprint)
    {
      deprecated_print_address_numeric (addr, 1, stream);
      print_address_symbolic (addr, stream, do_demangle, " ");
    }
  else
    {
      print_address_symbolic (addr, stream, do_demangle, "");
    }
}


/* These are the types that $__ will get after an examine command of one
   of these sizes.  */

static struct type *examine_i_type;

static struct type *examine_b_type;
static struct type *examine_h_type;
static struct type *examine_w_type;
static struct type *examine_g_type;

/* Examine data at address ADDR in format FMT.
   Fetch it from memory and print on gdb_stdout.  */

static void
do_examine (struct format_data fmt, CORE_ADDR addr)
{
  char format = 0;
  char size;
  int count = 1;
  struct type *val_type = NULL;
  int i;
  int maxelts;

  format = fmt.format;
  size = fmt.size;
  count = fmt.count;
  next_address = addr;

  /* String format implies fetch single bytes regardless of the specified
     size.  */
  if (format == 's') /* APPLE LOCAL: permit 'i' to have custom size.  */
    size = 'b';
  /* APPLE LOCAL: OSType formatting */
  if (format == 'T')
    size = 'w';

  if (format == 'i')
    val_type = examine_i_type;
  else if (size == 'b')
    val_type = examine_b_type;
  else if (size == 'h')
    val_type = examine_h_type;
  else if (size == 'w')
    val_type = examine_w_type;
  else if (size == 'g')
    val_type = examine_g_type;

  maxelts = 8;
  if (size == 'w')
    maxelts = 4;
  if (size == 'g')
    maxelts = 2;
  if (format == 's' || format == 'i')
    maxelts = 1;

  /* Print as many objects as specified in COUNT, at most maxelts per line,
     with the address of the next one at the start of each line.  */

  while (count > 0)
    {
      QUIT;
      print_address (next_address, gdb_stdout);
      printf_filtered (":");
      for (i = maxelts;
	   i > 0 && count > 0;
	   i--, count--)
	{
	  printf_filtered ("\t");
	  /* Note that print_formatted sets next_address for the next
	     object.  */
	  last_examine_address = next_address;

	  if (last_examine_value)
	    value_free (last_examine_value);

	  /* The value to be displayed is not fetched greedily.
	     Instead, to avoid the posibility of a fetched value not
	     being used, its retreval is delayed until the print code
	     uses it.  When examining an instruction stream, the
	     disassembler will perform its own memory fetch using just
	     the address stored in LAST_EXAMINE_VALUE.  FIXME: Should
	     the disassembler be modified so that LAST_EXAMINE_VALUE
	     is left with the byte sequence from the last complete
	     instruction fetched from memory? */
	  last_examine_value = value_at_lazy (val_type, next_address);

	  if (last_examine_value)
	    release_value (last_examine_value);

	  print_formatted (last_examine_value, format, size, gdb_stdout);
	}
      printf_filtered ("\n");
      gdb_flush (gdb_stdout);
    }
}

static void
validate_format (struct format_data fmt, char *cmdname)
{
  if (fmt.size != 0)
    error (_("Size letters are meaningless in \"%s\" command."), cmdname);
  if (fmt.count != 1)
    error (_("Item count other than 1 is meaningless in \"%s\" command."),
	   cmdname);
  if (fmt.format == 'i' || fmt.format == 's')
    error (_("Format letter \"%c\" is meaningless in \"%s\" command."),
	   fmt.format, cmdname);
}

/*  Evaluate string EXP as an expression in the current language and
   print the resulting value.  EXP may contain a format specifier as the
   first argument ("/x myvar" for example, to print myvar in hex).
 */

static int print_command_runs_all_threads_p = 1;

static void
print_command_1 (char *exp, int inspect, int voidprint)
{
  struct expression *expr;
  struct cleanup *old_chain = 0;
  struct cleanup *smart_thread_running_cleanup = NULL;
  char format = 0;
  struct value *val;
  struct format_data fmt;
  int cleanup = 0;

  /* We are trying to get the print command to run only the current thread if it has
     to do an inferior function call.  But sometimes we can detect that that will lead
     to a deadlock and so we allow all threads to run in that case IF it is safe to do
     so.  However, don't do this if the user has explicitly said they only want this
     thread to run, and if they have not explicitly overridden the ObjC runtime checking.  */

  if (print_command_runs_all_threads_p)
    {
      target_setup_safe_print (&smart_thread_running_cleanup);
    }

  /* Pass inspect flag to the rest of the print routines in a global (sigh). */
  inspect_it = inspect;

  if (exp && *exp == '/')
    {
      exp++;
      fmt = decode_format (&exp, last_format, 0);
      validate_format (fmt, "print");
      last_format = format = fmt.format;
    }
  else
    {
      fmt.count = 1;
      fmt.format = 0;
      fmt.size = 0;
    }

  if (exp && *exp)
    {
      /* APPLE LOCAL initialize innermost_block  */
      innermost_block = NULL;
      expr = parse_expression (exp);
      old_chain = make_cleanup (free_current_contents, &expr);
      cleanup = 1;
      val = evaluate_expression (expr);
    }
  else
    val = access_value_history (0);

  if (voidprint || (val && value_type (val) &&
		    TYPE_CODE (value_type (val)) != TYPE_CODE_VOID))
    {
      int histindex = record_latest_value (val);

      if (histindex >= 0)
	annotate_value_history_begin (histindex, value_type (val));
      else
	annotate_value_begin (value_type (val));

      if (inspect)
	printf_unfiltered ("\031(gdb-makebuffer \"%s\"  %d '(\"", exp, histindex);
      else if (histindex >= 0)
	printf_filtered ("$%d = ", histindex);

      if (histindex >= 0)
	annotate_value_history_value ();

      print_formatted (val, format, fmt.size, gdb_stdout);
      printf_filtered ("\n");

      if (histindex >= 0)
	annotate_value_history_end ();
      else
	annotate_value_end ();

      if (inspect)
	printf_unfiltered ("\") )\030");
    }

  if (cleanup)
    do_cleanups (old_chain);

  if (print_command_runs_all_threads_p)
    do_cleanups (smart_thread_running_cleanup);

  inspect_it = 0;		/* Reset print routines to normal */
}

static void
print_command (char *exp, int from_tty)
{
  print_command_1 (exp, 0, 1);
}

/* Same as print, except in epoch, it gets its own window */
static void
inspect_command (char *exp, int from_tty)
{
  extern int epoch_interface;

  print_command_1 (exp, epoch_interface, 1);
}

/* Same as print, except it doesn't print void results. */
static void
call_command (char *exp, int from_tty)
{
  print_command_1 (exp, 0, 0);
}

void
output_command (char *exp, int from_tty)
{
  struct expression *expr;
  struct cleanup *old_chain;
  char format = 0;
  struct value *val;
  struct format_data fmt;

  if (exp && *exp == '/')
    {
      exp++;
      fmt = decode_format (&exp, 0, 0);
      validate_format (fmt, "output");
      format = fmt.format;
    }

  /* APPLE LOCAL initialize innermost_block  */
  innermost_block = NULL;
  expr = parse_expression (exp);
  old_chain = make_cleanup (free_current_contents, &expr);

  val = evaluate_expression (expr);

  annotate_value_begin (value_type (val));

  print_formatted (val, format, fmt.size, gdb_stdout);

  annotate_value_end ();

  wrap_here ("");
  gdb_flush (gdb_stdout);

  do_cleanups (old_chain);
}

static void
set_command (char *exp, int from_tty)
{
  /* APPLE LOCAL begin initialize innermost_block  */
  struct expression *expr;
  struct cleanup *old_chain;

  innermost_block = NULL;
  expr = parse_expression (exp);
  old_chain = make_cleanup (free_current_contents, &expr);
  /* APPLE LOCAL end initialize innermost_block  */
  evaluate_expression (expr);
  do_cleanups (old_chain);
}

static void
sym_info (char *arg, int from_tty)
{
  struct minimal_symbol *msymbol;
  struct objfile *objfile;
  struct obj_section *osect;
  asection *sect;
  CORE_ADDR addr, sect_addr;
  int matches = 0;
  unsigned int offset;

  if (!arg)
    error_no_arg (_("address"));

  addr = parse_and_eval_address (arg);
  ALL_OBJSECTIONS (objfile, osect)
  {
    sect = osect->the_bfd_section;
    sect_addr = overlay_mapped_address (addr, sect);

    if (osect->addr <= sect_addr && sect_addr < osect->endaddr &&
	(msymbol = lookup_minimal_symbol_by_pc_section (sect_addr, sect)))
      {
	matches = 1;
	offset = sect_addr - SYMBOL_VALUE_ADDRESS (msymbol);
	if (offset)
	  printf_filtered ("%s + %u in ",
			   SYMBOL_PRINT_NAME (msymbol), offset);
	else
	  printf_filtered ("%s in ",
			   SYMBOL_PRINT_NAME (msymbol));
	if (pc_in_unmapped_range (addr, sect))
	  printf_filtered (_("load address range of "));
	if (section_is_overlay (sect))
	  printf_filtered (_("%s overlay "),
			   section_is_mapped (sect) ? "mapped" : "unmapped");
	/* APPLE LOCAL objfiles */
	printf_filtered (_("section %s of %s"), sect->name, objfile->name);
	printf_filtered ("\n");
      }
  }
  if (matches == 0)
    printf_filtered (_("No symbol matches %s.\n"), arg);
}

static void
address_info (char *exp, int from_tty)
{
  struct symbol *sym;
  struct block *bl;
  struct minimal_symbol *msymbol;
  long val;
  long basereg;
  asection *section;
  CORE_ADDR load_addr;
  int is_a_field_of_this;	/* C++: lookup_symbol sets this to nonzero
				   if exp is a field of `this'. */

  if (exp == 0)
    error (_("Argument required."));

  /* APPLE LOCAL: Walk the chain of blocks until we get to the function
     block before we pass that to lookup_symbol as the scope.  Else the
     block argument is ignored and we'll mistakenly do an unscoped search.  */

  bl = get_selected_block (NULL);
  if (bl != NULL)
    {
      while (BLOCK_FUNCTION (bl) == 0 && BLOCK_SUPERBLOCK (bl) != 0)
	bl = BLOCK_SUPERBLOCK (bl);
    }

  sym = lookup_symbol (exp, bl, VAR_DOMAIN, &is_a_field_of_this, 
                       (struct symtab **) NULL);
  if (sym == NULL)
    {
      if (is_a_field_of_this)
	{
	  printf_filtered ("Symbol \"");
	  fprintf_symbol_filtered (gdb_stdout, exp,
				   current_language->la_language, DMGL_ANSI);
	  printf_filtered ("\" is a field of the local class variable ");
	  /* APPLE LOCAL ObjC++ */
	  if ((current_language->la_language == language_objc)
	      || (current_language->la_language == language_objcplus))
	    printf_filtered ("`self'\n");	/* ObjC equivalent of "this" */
	  else
	    printf_filtered ("`this'\n");
	  return;
	}

      msymbol = lookup_minimal_symbol (exp, NULL, NULL);

      if (msymbol != NULL)
	{
	  load_addr = SYMBOL_VALUE_ADDRESS (msymbol);

	  printf_filtered ("Symbol \"");
	  fprintf_symbol_filtered (gdb_stdout, exp,
				   current_language->la_language, DMGL_ANSI);
	  printf_filtered ("\" is at ");
	  deprecated_print_address_numeric (load_addr, 1, gdb_stdout);
	  printf_filtered (" in a file compiled without debugging");
	  section = SYMBOL_BFD_SECTION (msymbol);
	  if (section_is_overlay (section))
	    {
	      load_addr = overlay_unmapped_address (load_addr, section);
	      printf_filtered (",\n -- loaded at ");
	      deprecated_print_address_numeric (load_addr, 1, gdb_stdout);
	      printf_filtered (" in overlay section %s", section->name);
	    }
	  printf_filtered (".\n");
	}
      else
	error (_("No symbol \"%s\" in current context."), exp);
      return;
    }

  printf_filtered ("Symbol \"");
  fprintf_symbol_filtered (gdb_stdout, DEPRECATED_SYMBOL_NAME (sym),
			   current_language->la_language, DMGL_ANSI);
  printf_filtered ("\" is ");
  val = SYMBOL_VALUE (sym);
  basereg = SYMBOL_BASEREG (sym);
  section = SYMBOL_BFD_SECTION (sym);

  switch (SYMBOL_CLASS (sym))
    {
    case LOC_CONST:
    case LOC_CONST_BYTES:
      printf_filtered ("constant");
      break;

    case LOC_LABEL:
      printf_filtered ("a label at address ");
      deprecated_print_address_numeric (load_addr = SYMBOL_VALUE_ADDRESS (sym),
			     1, gdb_stdout);
      if (section_is_overlay (section))
	{
	  load_addr = overlay_unmapped_address (load_addr, section);
	  printf_filtered (",\n -- loaded at ");
	  deprecated_print_address_numeric (load_addr, 1, gdb_stdout);
	  printf_filtered (" in overlay section %s", section->name);
	}
      break;

    case LOC_COMPUTED:
    case LOC_COMPUTED_ARG:
      /* FIXME: cagney/2004-01-26: It should be possible to
	 unconditionally call the SYMBOL_OPS method when available.
	 Unfortunately DWARF 2 stores the frame-base (instead of the
	 function) location in a function's symbol.  Oops!  For the
	 moment enable this when/where applicable.  */
      SYMBOL_OPS (sym)->describe_location (sym, gdb_stdout);
      break;

    case LOC_REGISTER:
      printf_filtered (_("a variable in register %s"), REGISTER_NAME (val));
      break;

    case LOC_STATIC:
      printf_filtered (_("static storage at address "));
      deprecated_print_address_numeric (load_addr = SYMBOL_VALUE_ADDRESS (sym),
			     1, gdb_stdout);
      if (section_is_overlay (section))
	{
	  load_addr = overlay_unmapped_address (load_addr, section);
	  printf_filtered (_(",\n -- loaded at "));
	  deprecated_print_address_numeric (load_addr, 1, gdb_stdout);
	  printf_filtered (_(" in overlay section %s"), section->name);
	}
      break;

    case LOC_INDIRECT:
      printf_filtered (_("external global (indirect addressing), at address *("));
      deprecated_print_address_numeric (load_addr = SYMBOL_VALUE_ADDRESS (sym),
			     1, gdb_stdout);
      printf_filtered (")");
      if (section_is_overlay (section))
	{
	  load_addr = overlay_unmapped_address (load_addr, section);
	  printf_filtered (_(",\n -- loaded at "));
	  deprecated_print_address_numeric (load_addr, 1, gdb_stdout);
	  printf_filtered (_(" in overlay section %s"), section->name);
	}
      break;

    case LOC_REGPARM:
      printf_filtered (_("an argument in register %s"), REGISTER_NAME (val));
      break;

    case LOC_REGPARM_ADDR:
      printf_filtered (_("address of an argument in register %s"), REGISTER_NAME (val));
      break;

    case LOC_ARG:
      printf_filtered (_("an argument at offset %ld"), val);
      break;

    case LOC_LOCAL_ARG:
      printf_filtered (_("an argument at frame offset %ld"), val);
      break;

    case LOC_LOCAL:
      printf_filtered (_("a local variable at frame offset %ld"), val);
      break;

    case LOC_REF_ARG:
      printf_filtered (_("a reference argument at offset %ld"), val);
      break;

    case LOC_BASEREG:
      printf_filtered (_("a variable at offset %ld from register %s"),
		       val, REGISTER_NAME (basereg));
      break;

    case LOC_BASEREG_ARG:
      printf_filtered (_("an argument at offset %ld from register %s"),
		       val, REGISTER_NAME (basereg));
      break;

    case LOC_TYPEDEF:
      printf_filtered (_("a typedef"));
      break;

    case LOC_BLOCK:
      /* APPLE LOCAL begin handle missing block */
      {
	struct block *block_ptr;
	block_ptr = SYMBOL_BLOCK_VALUE (sym);
	if (block_ptr != NULL)
	  {
	    printf_filtered ("a function at address ");
	    /* APPLE LOCAL begin address ranges  */
	    deprecated_print_address_numeric (load_addr = BLOCK_LOWEST_PC (block_ptr),
				   1, gdb_stdout);
	    /* APPLE LOCAL end address ranges  */
	    if (section_is_overlay (section))
	      {
		load_addr = overlay_unmapped_address (load_addr, section);
		printf_filtered (",\n -- loaded at ");
		deprecated_print_address_numeric (load_addr, 1, gdb_stdout);
		printf_filtered (" in overlay section %s", section->name);
	      }
	  }
	else
	  printf_filtered ("a function with unknown block.");
	break;
      }
      /* APPLE LOCAL end handle missing block */
    case LOC_UNRESOLVED:
      {
	struct minimal_symbol *msym;

	msym = lookup_minimal_symbol (DEPRECATED_SYMBOL_NAME (sym), NULL, NULL);
	if (msym == NULL)
	  printf_filtered ("unresolved");
	else
	  {
	    section = SYMBOL_BFD_SECTION (msym);
	    printf_filtered (_("static storage at address "));
	    deprecated_print_address_numeric (load_addr = SYMBOL_VALUE_ADDRESS (msym),
				   1, gdb_stdout);
	    if (section_is_overlay (section))
	      {
		load_addr = overlay_unmapped_address (load_addr, section);
		printf_filtered (_(",\n -- loaded at "));
		deprecated_print_address_numeric (load_addr, 1, gdb_stdout);
		printf_filtered (_(" in overlay section %s"), section->name);
	      }
	  }
      }
      break;

    case LOC_HP_THREAD_LOCAL_STATIC:
      printf_filtered (
			"a thread-local variable at offset %ld from the thread base register %s",
			val, REGISTER_NAME (basereg));
      break;

    case LOC_OPTIMIZED_OUT:
      printf_filtered (_("optimized out"));
      break;

    default:
      printf_filtered (_("of unknown (botched) type"));
      break;
    }
  printf_filtered (".\n");
}

void
x_command (char *exp, int from_tty)
{
  struct expression *expr;
  struct format_data fmt;
  struct cleanup *old_chain;
  struct value *val;

  fmt.format = last_format;
  fmt.size = last_size;
  fmt.count = 1;

  if (exp && *exp == '/')
    {
      exp++;
      fmt = decode_format (&exp, last_format, last_size);
    }

  /* If we have an expression, evaluate it and use it as the address.  */

  if (exp != 0 && *exp != 0)
    {
      /* APPLE LOCAL initialize innermost_block  */
      innermost_block = NULL;
      expr = parse_expression (exp);
      /* Cause expression not to be there any more
         if this command is repeated with Newline.
         But don't clobber a user-defined command's definition.  */
      if (from_tty)
	*exp = 0;
      old_chain = make_cleanup (free_current_contents, &expr);
      val = evaluate_expression (expr);
      if (TYPE_CODE (value_type (val)) == TYPE_CODE_REF)
	val = value_ind (val);
      /* In rvalue contexts, such as this, functions are coerced into
         pointers to functions.  This makes "x/i main" work.  */
      if (/* last_format == 'i'  && */ 
	  TYPE_CODE (value_type (val)) == TYPE_CODE_FUNC
	  && VALUE_LVAL (val) == lval_memory)
	next_address = VALUE_ADDRESS (val);
      else
	next_address = value_as_address (val);
      do_cleanups (old_chain);
    }

  do_examine (fmt, next_address);

  /* If the examine succeeds, we remember its size and format for next time.  */
  last_size = fmt.size;
  last_format = fmt.format;

  /* Set a couple of internal variables if appropriate. */
  if (last_examine_value)
    {
      /* Make last address examined available to the user as $_.  Use
         the correct pointer type.  */
      struct type *pointer_type
	= lookup_pointer_type (value_type (last_examine_value));
      set_internalvar (lookup_internalvar ("_"),
		       value_from_pointer (pointer_type,
					   last_examine_address));

      /* Make contents of last address examined available to the user as $__. */
      /* If the last value has not been fetched from memory then don't
         fetch it now - instead mark it by voiding the $__ variable. */
      if (value_lazy (last_examine_value))
	set_internalvar (lookup_internalvar ("__"),
			 allocate_value (builtin_type_void));
      else
	set_internalvar (lookup_internalvar ("__"), last_examine_value);
    }
}


/* Add an expression to the auto-display chain.
   Specify the expression.  */

static void
display_command (char *exp, int from_tty)
{
  struct format_data fmt;
  struct expression *expr;
  struct display *new;
  int display_it = 1;

#if defined(TUI)
  /* NOTE: cagney/2003-02-13 The `tui_active' was previously
     `tui_version'.  */
  if (tui_active && exp != NULL && *exp == '$')
    display_it = (tui_set_layout_for_display_command (exp) == TUI_FAILURE);
#endif

  if (display_it)
    {
      if (exp == 0)
	{
	  do_displays ();
	  return;
	}

      if (*exp == '/')
	{
	  exp++;
	  fmt = decode_format (&exp, 0, 0);
	  if (fmt.size && fmt.format == 0)
	    fmt.format = 'x';
	  if (fmt.format == 'i' || fmt.format == 's')
	    fmt.size = 'b';
	}
      else
	{
	  fmt.format = 0;
	  fmt.size = 0;
	  fmt.count = 0;
	}

      innermost_block = 0;
      expr = parse_expression (exp);

      new = (struct display *) xmalloc (sizeof (struct display));

      new->exp = expr;
      new->block = innermost_block;
      new->next = display_chain;
      new->number = ++display_number;
      new->format = fmt;
      new->enabled_p = 1;
      display_chain = new;

      if (from_tty && target_has_execution)
	do_one_display (new);

      dont_repeat ();
    }
}

static void
free_display (struct display *d)
{
  xfree (d->exp);
  xfree (d);
}

/* Clear out the display_chain.
   Done when new symtabs are loaded, since this invalidates
   the types stored in many expressions.  */

void
clear_displays (void)
{
  struct display *d;

  while ((d = display_chain) != NULL)
    {
      xfree (d->exp);
      display_chain = d->next;
      xfree (d);
    }
}

/* Delete the auto-display number NUM.  */

static void
delete_display (int num)
{
  struct display *d1, *d;

  if (!display_chain)
    error (_("No display number %d."), num);

  if (display_chain->number == num)
    {
      d1 = display_chain;
      display_chain = d1->next;
      free_display (d1);
    }
  else
    for (d = display_chain;; d = d->next)
      {
	if (d->next == 0)
	  error (_("No display number %d."), num);
	if (d->next->number == num)
	  {
	    d1 = d->next;
	    d->next = d1->next;
	    free_display (d1);
	    break;
	  }
      }
}

/* Delete some values from the auto-display chain.
   Specify the element numbers.  */

static void
undisplay_command (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  int num;

  if (args == 0)
    {
      if (query ("Delete all auto-display expressions? "))
	clear_displays ();
      dont_repeat ();
      return;
    }

  while (*p)
    {
      p1 = p;
      while (*p1 >= '0' && *p1 <= '9')
	p1++;
      if (*p1 && *p1 != ' ' && *p1 != '\t')
	error (_("Arguments must be display numbers."));

      num = atoi (p);

      delete_display (num);

      p = p1;
      while (*p == ' ' || *p == '\t')
	p++;
    }
  dont_repeat ();
}

/* Display a single auto-display.  
   Do nothing if the display cannot be printed in the current context,
   or if the display is disabled. */

static void
do_one_display (struct display *d)
{
  int within_current_scope;

  if (d->enabled_p == 0)
    return;

  if (d->block)
    within_current_scope = contained_in (get_selected_block (0), d->block);
  else
    within_current_scope = 1;
  if (!within_current_scope)
    return;

  current_display_number = d->number;

  annotate_display_begin ();
  printf_filtered ("%d", d->number);
  annotate_display_number_end ();
  printf_filtered (": ");
  if (d->format.size)
    {
      CORE_ADDR addr;
      struct value *val;

      annotate_display_format ();

      printf_filtered ("x/");
      if (d->format.count != 1)
	printf_filtered ("%d", d->format.count);
      printf_filtered ("%c", d->format.format);
      if (d->format.format != 'i' && d->format.format != 's')
	printf_filtered ("%c", d->format.size);
      printf_filtered (" ");

      annotate_display_expression ();

      print_expression (d->exp, gdb_stdout);
      annotate_display_expression_end ();

      if (d->format.count != 1)
	printf_filtered ("\n");
      else
	printf_filtered ("  ");

      val = evaluate_expression (d->exp);
      addr = value_as_address (val);
      if (d->format.format == 'i')
	addr = ADDR_BITS_REMOVE (addr);

      annotate_display_value ();

      do_examine (d->format, addr);
    }
  else
    {
      annotate_display_format ();

      if (d->format.format)
	printf_filtered ("/%c ", d->format.format);

      annotate_display_expression ();

      print_expression (d->exp, gdb_stdout);
      annotate_display_expression_end ();

      printf_filtered (" = ");

      annotate_display_expression ();

      print_formatted (evaluate_expression (d->exp),
		       d->format.format, d->format.size, gdb_stdout);
      printf_filtered ("\n");
    }

  annotate_display_end ();

  gdb_flush (gdb_stdout);
  current_display_number = -1;
}

/* Display all of the values on the auto-display chain which can be
   evaluated in the current scope.  */

void
do_displays (void)
{
  struct display *d;

  for (d = display_chain; d; d = d->next)
    do_one_display (d);
}

/* Delete the auto-display which we were in the process of displaying.
   This is done when there is an error or a signal.  */

void
disable_display (int num)
{
  struct display *d;

  for (d = display_chain; d; d = d->next)
    if (d->number == num)
      {
	d->enabled_p = 0;
	return;
      }
  printf_unfiltered (_("No display number %d.\n"), num);
}

void
disable_current_display (void)
{
  if (current_display_number >= 0)
    {
      disable_display (current_display_number);
      fprintf_unfiltered (gdb_stderr, "Disabling display %d to avoid infinite recursion.\n",
			  current_display_number);
    }
  current_display_number = -1;
}

static void
display_info (char *ignore, int from_tty)
{
  struct display *d;

  if (!display_chain)
    printf_unfiltered (_("There are no auto-display expressions now.\n"));
  else
    printf_filtered (_("Auto-display expressions now in effect:\n\
Num Enb Expression\n"));

  for (d = display_chain; d; d = d->next)
    {
      printf_filtered ("%d:   %c  ", d->number, "ny"[(int) d->enabled_p]);
      if (d->format.size)
	printf_filtered ("/%d%c%c ", d->format.count, d->format.size,
			 d->format.format);
      else if (d->format.format)
	printf_filtered ("/%c ", d->format.format);
      print_expression (d->exp, gdb_stdout);
      if (d->block && !contained_in (get_selected_block (0), d->block))
	printf_filtered (_(" (cannot be evaluated in the current context)"));
      printf_filtered ("\n");
      gdb_flush (gdb_stdout);
    }
}

static void
enable_display (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  int num;
  struct display *d;

  if (p == 0)
    {
      for (d = display_chain; d; d = d->next)
	d->enabled_p = 1;
    }
  else
    while (*p)
      {
	p1 = p;
	while (*p1 >= '0' && *p1 <= '9')
	  p1++;
	if (*p1 && *p1 != ' ' && *p1 != '\t')
	  error (_("Arguments must be display numbers."));

	num = atoi (p);

	for (d = display_chain; d; d = d->next)
	  if (d->number == num)
	    {
	      d->enabled_p = 1;
	      goto win;
	    }
	printf_unfiltered (_("No display number %d.\n"), num);
      win:
	p = p1;
	while (*p == ' ' || *p == '\t')
	  p++;
      }
}

static void
disable_display_command (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  struct display *d;

  if (p == 0)
    {
      for (d = display_chain; d; d = d->next)
	d->enabled_p = 0;
    }
  else
    while (*p)
      {
	p1 = p;
	while (*p1 >= '0' && *p1 <= '9')
	  p1++;
	if (*p1 && *p1 != ' ' && *p1 != '\t')
	  error (_("Arguments must be display numbers."));

	disable_display (atoi (p));

	p = p1;
	while (*p == ' ' || *p == '\t')
	  p++;
      }
}


/* Print the value in stack frame FRAME of a variable
   specified by a struct symbol.  */

void
print_variable_value (struct symbol *var, struct frame_info *frame,
		      struct ui_file *stream)
{
  struct value *val = read_var_value (var, frame);

  value_print (val, stream, 0, Val_pretty_default);
}

static void
printf_command (char *arg, int from_tty)
{
  char *f = NULL;
  char *s = arg;
  char *string = NULL;
  struct value **val_args;
  char *substrings;
  char *current_substring;
  int nargs = 0;
  int allocated_args = 20;
  struct cleanup *old_cleanups;

  val_args = (struct value **) xmalloc (allocated_args
					* sizeof (struct value *));
  old_cleanups = make_cleanup (free_current_contents, &val_args);
  target_setup_safe_print (NULL);

  if (s == 0)
    error_no_arg (_("format-control string and values to print"));

  /* Skip white space before format string */
  while (*s == ' ' || *s == '\t')
    s++;

  /* A format string should follow, enveloped in double quotes */
  if (*s++ != '"')
    error (_("Bad format string, missing '\"'."));

  /* Parse the format-control string and copy it into the string STRING,
     processing some kinds of escape sequence.  */

  f = string = (char *) alloca (strlen (s) + 1);

  while (*s != '"')
    {
      int c = *s++;
      switch (c)
	{
	case '\0':
	  error (_("Bad format string, non-terminated '\"'."));

	case '\\':
	  switch (c = *s++)
	    {
	    case '\\':
	      *f++ = '\\';
	      break;
	    case 'a':
	      *f++ = '\a';
	      break;
	    case 'b':
	      *f++ = '\b';
	      break;
	    case 'f':
	      *f++ = '\f';
	      break;
	    case 'n':
	      *f++ = '\n';
	      break;
	    case 'r':
	      *f++ = '\r';
	      break;
	    case 't':
	      *f++ = '\t';
	      break;
	    case 'v':
	      *f++ = '\v';
	      break;
	    case '"':
	      *f++ = '"';
	      break;
	    default:
	      /* ??? TODO: handle other escape sequences */
	      error (_("Unrecognized escape character \\%c in format string."),
		     c);
	    }
	  break;

	default:
	  *f++ = c;
	}
    }

  /* Skip over " and following space and comma.  */
  s++;
  *f++ = '\0';
  while (*s == ' ' || *s == '\t')
    s++;

  if (*s != ',' && *s != 0)
    error (_("Invalid argument syntax"));

  if (*s == ',')
    s++;
  while (*s == ' ' || *s == '\t')
    s++;

  /* Need extra space for the '\0's.  Doubling the size is sufficient.  */
  substrings = alloca (strlen (string) * 2);
  current_substring = substrings;

  {
    /* Now scan the string for %-specs and see what kinds of args they want.
       argclass[I] classifies the %-specs so we can give printf_filtered
       something of the right size.  */
    /* APPLE LOCAL: Added pointer_arg to handle "%p".  */

    enum argclass
      {
	no_arg, int_arg, string_arg, double_arg, long_long_arg, pointer_arg
      };
    enum argclass *argclass;
    enum argclass this_argclass;
    char *last_arg;
    int nargs_wanted;
    int lcount;
    int i;

    argclass = (enum argclass *) alloca (strlen (s) * sizeof *argclass);
    nargs_wanted = 0;
    f = string;
    last_arg = string;
    while (*f)
      if (*f++ == '%')
	{
	  lcount = 0;
	  while (strchr ("0123456789.hlL-+ #", *f))
	    {
	      if (*f == 'l' || *f == 'L')
		lcount++;
	      f++;
	    }
	  switch (*f)
	    {
	    case 's':
	      this_argclass = string_arg;
	      break;

	    case 'e':
	    case 'f':
	    case 'g':
	      this_argclass = double_arg;
	      break;
	      /* APPLE LOCAL: Handle "%p"  */
	    case 'p':
	      this_argclass = pointer_arg;
              break;
	    case '*':
	      error (_("`*' not supported for precision or width in printf"));

	    case 'n':
	      error (_("Format specifier `n' not supported in printf"));

	    case '%':
	      this_argclass = no_arg;
	      break;

	    default:
	      if (lcount > 1)
		this_argclass = long_long_arg;
	      else
		this_argclass = int_arg;
	      break;
	    }
	  f++;
	  if (this_argclass != no_arg)
	    {
	      strncpy (current_substring, last_arg, f - last_arg);
	      current_substring += f - last_arg;
	      *current_substring++ = '\0';
	      last_arg = f;
	      argclass[nargs_wanted++] = this_argclass;
	    }
	}

    /* Now, parse all arguments and evaluate them.
       Store the VALUEs in VAL_ARGS.  */

    while (*s != '\0')
      {
	char *s1;
	if (nargs == allocated_args)
	  val_args = (struct value **) xrealloc ((char *) val_args,
						 (allocated_args *= 2)
						 * sizeof (struct value *));
	s1 = s;
	val_args[nargs] = parse_to_comma_and_eval (&s1);

	/* If format string wants a float, unchecked-convert the value to
	   floating point of the same size */

	if (argclass[nargs] == double_arg)
	  {
	    struct type *type = value_type (val_args[nargs]);
	    if (TYPE_LENGTH (type) == sizeof (float))
	      deprecated_set_value_type (val_args[nargs], builtin_type_float);
	    if (TYPE_LENGTH (type) == sizeof (double))
	      deprecated_set_value_type (val_args[nargs], builtin_type_double);
	  }
	nargs++;
	s = s1;
	if (*s == ',')
	  s++;
      }

    if (nargs != nargs_wanted)
      error (_("Wrong number of arguments for specified format-string"));

    /* Now actually print them.  */
    current_substring = substrings;
    for (i = 0; i < nargs; i++)
      {
	switch (argclass[i])
	  {
	  case string_arg:
	    {
	      char *str;
	      CORE_ADDR tem;
	      int j;
	      tem = value_as_address (val_args[i]);

	      /* This is a %s argument.  Find the length of the string.  */
	      for (j = 0;; j++)
		{
		  char c;
		  QUIT;
		  read_memory (tem + j, &c, 1);
		  if (c == 0)
		    break;
		}

	      /* Copy the string contents into a string inside GDB.  */
	      str = (char *) alloca (j + 1);
	      if (j != 0)
		read_memory (tem, str, j);
	      str[j] = 0;

	      printf_filtered (current_substring, str);
	    }
	    break;
	  case double_arg:
	    {
	      double val = value_as_double (val_args[i]);
	      printf_filtered (current_substring, val);
	      break;
	    }
	  case long_long_arg:
#if defined (CC_HAS_LONG_LONG) && defined (PRINTF_HAS_LONG_LONG)
	    {
	      long long val = value_as_long (val_args[i]);
	      printf_filtered (current_substring, val);
	      break;
	    }
#else
	    error (_("long long not supported in printf"));
#endif
	  case int_arg:
	    {
	      /* FIXME: there should be separate int_arg and long_arg.  */
	      long val = value_as_long (val_args[i]);
	      printf_filtered (current_substring, val);
	      break;
	    }
	  case pointer_arg:
	    {
	      /* APPLE LOCAL: Handle "%p".  */
	      CORE_ADDR val = value_as_address (val_args[i]);
	      printf_filtered (current_substring, val);
	      break;
	    }
	  default:		/* purecov: deadcode */
	    error (_("internal error in printf_command"));		/* purecov: deadcode */
	  }
	/* Skip to the next substring.  */
	current_substring += strlen (current_substring) + 1;
      }
    /* Print the portion of the format string after the last argument.  */
    puts_filtered (last_arg);
  }
  do_cleanups (old_cleanups);
}

/* APPLE LOCAL: invoke-block */
static void
invoke_block_command (char *args, int from_tty)
{
  char **argv;
  struct cleanup *argv_cleanup;
  struct expression *expr;
  struct value *block_val;
  struct value *implementation_fn;
  struct type *block_type;
  struct type *block_deref;
  int nargs;
  struct value **val_argv;
  int i;
  struct value *ret_val;
  int histindex;
  struct cleanup *print_closure_cleanup;

  argv = buildargv (args);
  if (argv == NULL)
    error ("No arguments provided.");

  argv_cleanup = make_cleanup_freeargv (argv);

  /* The first argument is the expression that resolves to the
     block pointer.  */
  if (argv[0] == NULL)
    error ("No arguments provided.");

  innermost_block = NULL;

  /* Turn off "print_closure" or we'll get the full dynamic type and
     have to cast it back to the basic invoke_impl struct.  I don't
     want to be dependent on the details of how to do that.  */
  print_closure_cleanup = make_cleanup_set_restore_print_closure (0);

  expr = parse_expression (argv[0]);
  block_val = evaluate_expression (expr);

  do_cleanups (print_closure_cleanup);

  block_type = check_typedef (value_type (block_val));
  if (!block_type)
    error ("Can't get type of block pointer expression: \"%s\".",
	   argv[0]);

  block_deref = TYPE_TARGET_TYPE (block_type);

  implementation_fn = get_closure_implementation_fn (block_val);
  if (implementation_fn == NULL)
    error ("Could not find block implementation function for \"%s\".\n",
	   argv[0]);

  /* Okay, now we've found the block function, and we can call it,
     passing in the block value, and whatever other arguments were
     provided.  */
  /* Count the args, remember that we have to pass the block pointer
     as the first argument.  */
  nargs = 0;
  while (argv[nargs] != NULL)
    nargs++;

  val_argv = (struct value **) malloc (nargs * sizeof (struct value *));
  make_cleanup (xfree, val_argv);

  val_argv[0] = block_val;
  for (i = 1; i < nargs; i++)
    {
      struct expression *arg_expr;
      struct value *arg_value;
      arg_expr = parse_expression (argv[i]);
      arg_value = evaluate_expression (arg_expr);
      val_argv[i] = arg_value;
    }
  
  ret_val = call_function_by_hand (implementation_fn, nargs, val_argv);
  
  do_cleanups (argv_cleanup);

  /* Now output the value returned, and stick it in the value
     history.  */
  histindex = record_latest_value (ret_val);

  if (histindex >= 0)
    annotate_value_history_begin (histindex, value_type (ret_val));
  else
    annotate_value_begin (value_type (ret_val));

  if (histindex >= 0)
    printf_filtered ("$%d = ", histindex);

  if (histindex >= 0)
    annotate_value_history_value ();

  /* FIXME: Should we allow a /format? */
  print_formatted (ret_val, 0, 0, gdb_stdout);
  printf_filtered ("\n");

  if (histindex >= 0)
    annotate_value_history_end ();
  else
    annotate_value_end ();

}

/* END APPLE LOCAL */

void
_initialize_printcmd (void)
{
  struct cmd_list_element *c;

  current_display_number = -1;

  add_info ("address", address_info,
	    _("Describe where symbol SYM is stored."));

  add_info ("symbol", sym_info, _("\
Describe what symbol is at location ADDR.\n\
Only for symbols with fixed locations (global or static scope)."));

/* APPLE LOCAL: OSType */
  add_com ("x", class_vars, x_command, _("\
Examine memory: x/FMT ADDRESS.\n\
ADDRESS is an expression for the memory address to examine.\n\
FMT is a repeat count followed by a format letter and a size letter.\n\
Format letters are o(octal), x(hex), d(decimal), u(unsigned decimal),\n\
  t(binary), f(float), a(address), i(instruction), c(char) and s(string),\n\
  T(OSType), A(floating point values in hex).\n\
Size letters are b(byte), h(halfword), w(word), g(giant, 8 bytes).\n\
The specified number of objects of the specified size are printed\n\
according to the format.\n\n\
Defaults for format and size letters are those previously used.\n\
Default count is 1.  Default address is following last thing printed\n\
with this command or \"print\"."));

#if 0
  add_com ("whereis", class_vars, whereis_command,
	   _("Print line number and file of definition of variable."));
#endif

  add_info ("display", display_info, _("\
Expressions to display when program stops, with code numbers."));

  add_cmd ("undisplay", class_vars, undisplay_command, _("\
Cancel some expressions to be displayed when program stops.\n\
Arguments are the code numbers of the expressions to stop displaying.\n\
No argument means cancel all automatic-display expressions.\n\
\"delete display\" has the same effect as this command.\n\
Do \"info display\" to see current list of code numbers."),
	   &cmdlist);

  add_com ("display", class_vars, display_command, _("\
Print value of expression EXP each time the program stops.\n\
/FMT may be used before EXP as in the \"print\" command.\n\
/FMT \"i\" or \"s\" or including a size-letter is allowed,\n\
as in the \"x\" command, and then EXP is used to get the address to examine\n\
and examining is done as in the \"x\" command.\n\n\
With no argument, display all currently requested auto-display expressions.\n\
Use \"undisplay\" to cancel display requests previously made."));

  add_cmd ("display", class_vars, enable_display, _("\
Enable some expressions to be displayed when program stops.\n\
Arguments are the code numbers of the expressions to resume displaying.\n\
No argument means enable all automatic-display expressions.\n\
Do \"info display\" to see current list of code numbers."), &enablelist);

  add_cmd ("display", class_vars, disable_display_command, _("\
Disable some expressions to be displayed when program stops.\n\
Arguments are the code numbers of the expressions to stop displaying.\n\
No argument means disable all automatic-display expressions.\n\
Do \"info display\" to see current list of code numbers."), &disablelist);

  add_cmd ("display", class_vars, undisplay_command, _("\
Cancel some expressions to be displayed when program stops.\n\
Arguments are the code numbers of the expressions to stop displaying.\n\
No argument means cancel all automatic-display expressions.\n\
Do \"info display\" to see current list of code numbers."), &deletelist);

  add_com ("printf", class_vars, printf_command, _("\
printf \"printf format string\", arg1, arg2, arg3, ..., argn\n\
This is useful for formatted output in user-defined commands."));

  add_com ("output", class_vars, output_command, _("\
Like \"print\" but don't put in value history and don't print newline.\n\
This is useful in user-defined commands."));

  add_prefix_cmd ("set", class_vars, set_command, _("\
Evaluate expression EXP and assign result to variable VAR, using assignment\n\
syntax appropriate for the current language (VAR = EXP or VAR := EXP for\n\
example).  VAR may be a debugger \"convenience\" variable (names starting\n\
with $), a register (a few standard names starting with $), or an actual\n\
variable in the program being debugged.  EXP is any valid expression.\n\
Use \"set variable\" for variables with names identical to set subcommands.\n\
\n\
With a subcommand, this command modifies parts of the gdb environment.\n\
You can see these environment settings with the \"show\" command."),
		  &setlist, "set ", 1, &cmdlist);
  if (dbx_commands)
    add_com ("assign", class_vars, set_command, _("\
Evaluate expression EXP and assign result to variable VAR, using assignment\n\
syntax appropriate for the current language (VAR = EXP or VAR := EXP for\n\
example).  VAR may be a debugger \"convenience\" variable (names starting\n\
with $), a register (a few standard names starting with $), or an actual\n\
variable in the program being debugged.  EXP is any valid expression.\n\
Use \"set variable\" for variables with names identical to set subcommands.\n\
\nWith a subcommand, this command modifies parts of the gdb environment.\n\
You can see these environment settings with the \"show\" command."));

  /* "call" is the same as "set", but handy for dbx users to call fns. */
  c = add_com ("call", class_vars, call_command, _("\
Call a function in the program.\n\
The argument is the function name and arguments, in the notation of the\n\
current working language.  The result is printed and saved in the value\n\
history, if it is not void."));
  set_cmd_completer (c, location_completer);

  add_cmd ("variable", class_vars, set_command, _("\
Evaluate expression EXP and assign result to variable VAR, using assignment\n\
syntax appropriate for the current language (VAR = EXP or VAR := EXP for\n\
example).  VAR may be a debugger \"convenience\" variable (names starting\n\
with $), a register (a few standard names starting with $), or an actual\n\
variable in the program being debugged.  EXP is any valid expression.\n\
This may usually be abbreviated to simply \"set\"."),
	   &setlist);
  /* APPLE LOCAL: Our 'varobj-print-object' makes "set var" ambiguous, 
     causing a failure in a poorly written testsuite case. */
  add_alias_cmd ("var", "variable", class_vars, 1, &setlist);

  c = add_com ("print", class_vars, print_command, _("\
Print value of expression EXP.\n\
Variables accessible are those of the lexical environment of the selected\n\
stack frame, plus all those whose scope is global or an entire file.\n\
\n\
$NUM gets previous value number NUM.  $ and $$ are the last two values.\n\
$$NUM refers to NUM'th value back from the last one.\n\
Names starting with $ refer to registers (with the values they would have\n\
if the program were to return to the stack frame now selected, restoring\n\
all registers saved by frames farther in) or else to debugger\n\
\"convenience\" variables (any such name not a known register).\n\
Use assignment expressions to give values to convenience variables.\n\
\n\
{TYPE}ADREXP refers to a datum of data type TYPE, located at address ADREXP.\n\
@ is a binary operator for treating consecutive data objects\n\
anywhere in memory as an array.  FOO@NUM gives an array whose first\n\
element is FOO, whose second element is stored in the space following\n\
where FOO is stored, etc.  FOO must be an expression whose value\n\
resides in memory.\n\
\n\
EXP may be preceded with /FMT, where FMT is a format letter\n\
but no count or size letter (see \"x\" command)."));
  set_cmd_completer (c, location_completer);
  add_com_alias ("p", "print", class_vars, 1);

  /* APPLE LOCAL: invoke-block */
  c = add_com ("invoke-block", class_vars, invoke_block_command, "\
Invoke the function associated with the block passed in as the first\n\
argument.  Specify the other arguments to the block function in a space separated\n\
list.  If you have spaces in an argument, use \"s to delimit the argument.  If you\n\
need to put a \" in an argument, pass it as \\\".");
  set_cmd_completer (c, location_completer);
  /* END APPLE LOCAL */

  c = add_com ("inspect", class_vars, inspect_command, _("\
Same as \"print\" command, except that if you are running in the epoch\n\
environment, the value is printed in its own window."));
  set_cmd_completer (c, location_completer);

  add_setshow_uinteger_cmd ("max-symbolic-offset", no_class,
			    &max_symbolic_offset, _("\
Set the largest offset that will be printed in <symbol+1234> form."), _("\
Show the largest offset that will be printed in <symbol+1234> form."), NULL,
			    NULL,
			    show_max_symbolic_offset,
			    &setprintlist, &showprintlist);
  add_setshow_boolean_cmd ("symbol-filename", no_class,
			   &print_symbol_filename, _("\
Set printing of source filename and line number with <symbol>."), _("\
Show printing of source filename and line number with <symbol>."), NULL,
			   NULL,
			   show_print_symbol_filename,
			   &setprintlist, &showprintlist);

  add_setshow_zinteger_cmd ("disassembly-name-length", no_class, 
			   &disassembly_name_length, "\
Set the maximum length of characters to print in the symbol name in disassembly output.\n"
"A value of -1 means unlimited", 
			   "\
Show the maximum length of characters to print in the symbol name in disassembly output", 
			   NULL, NULL, NULL, 
			   &setlist, &showlist);

  /* For examine/instruction a single byte quantity is specified as
     the data.  This avoids problems with value_at_lazy() requiring a
     valid data type (and rejecting VOID). */
  examine_i_type = init_type (TYPE_CODE_INT, 1, 0, "examine_i_type", NULL);

  examine_b_type = init_type (TYPE_CODE_INT, 1, 0, "examine_b_type", NULL);
  examine_h_type = init_type (TYPE_CODE_INT, 2, 0, "examine_h_type", NULL);
  examine_w_type = init_type (TYPE_CODE_INT, 4, 0, "examine_w_type", NULL);
  examine_g_type = init_type (TYPE_CODE_INT, 8, 0, "examine_g_type", NULL);

  add_setshow_boolean_cmd ("runs-all-threads", no_class,
			   &print_command_runs_all_threads_p, _("\
Set whether the print command allows all threads to run if necessary."), _("\
Show whether the print command allows all threads to run if necessary."), NULL,
			   NULL,
			   NULL,
			   &setprintlist, &showprintlist);

}
