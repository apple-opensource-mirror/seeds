/* DWARF 2 location expression support for GDB.

   Copyright 2003, 2005 Free Software Foundation, Inc.

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

#if !defined (DWARF2LOC_H)
#define DWARF2LOC_H

#include "dwarf2read.h"

struct symbol_ops;

/* This header is private to the DWARF-2 reader.  It is shared between
   dwarf2read.c and dwarf2loc.c.  */

/* The symbol location baton types used by the DWARF-2 reader (i.e.
   SYMBOL_LOCATION_BATON for a LOC_COMPUTED symbol).  "struct
   dwarf2_locexpr_baton" is for a symbol with a single location
   expression; "struct dwarf2_loclist_baton" is for a symbol with a
   location list.  */

struct dwarf2_locexpr_baton
{
  /* Pointer to the start of the location expression.  */
  gdb_byte *data;

  /* Length of the location expression.  */
  unsigned short size;

  /* The objfile containing the symbol whose location we're computing.  */
  struct objfile *objfile;

  /* APPLE LOCAL we need to translate addresses for location list expressions
     from .o file addresses to final executable addresses.  */
  struct oso_to_final_addr_map *addr_map;
};

struct dwarf2_loclist_baton
{
  /* The initial base address for the location list, based on the compilation
     unit.  */
  /* APPLE LOCAL: In the non-dSYM or kext dSYM case where we must
     translate addresses in the DWARF, BASE_ADDRESS_UNTRANSLATED
     has not been translated to a final-executable address.  It is
     accurate only in terms of the .o file. */
  CORE_ADDR base_address_untranslated;

  /* Pointer to the start of the location list.  */
  gdb_byte *data;

  /* Length of the location list.  */
  unsigned int size;

  /* The objfile containing the symbol whose location we're computing.  */
  /* Used (only???) by thread local variables.  The objfile in which
     this symbol is defined.  To find a thread-local variable (e.g., a
     variable declared with the `__thread' storage class), we may need
     to know which object file it's in.  */
  struct objfile *objfile;

  /* APPLE LOCAL */
  struct oso_to_final_addr_map *addr_map;
};

extern const struct symbol_ops dwarf2_locexpr_funcs;
extern const struct symbol_ops dwarf2_loclist_funcs;

#endif /* dwarf2loc.h */
