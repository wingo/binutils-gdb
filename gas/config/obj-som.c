/* SOM object file format.
   Copyright (C) 1993 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2,
   or (at your option) any later version.

   GAS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with GAS; see the file COPYING.  If not, write
   to the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

   Written by the Center for Software Science at the University of Utah
   and by Cygnus Support.  */

#include "as.h"
#include "subsegs.h"
#include "aout/stab_gnu.h"
#include "obstack.h"

/* SOM does not need any pseudo-ops.  */

const pseudo_typeS obj_pseudo_table[] =
{
  {NULL}
};

static int version_seen = 0;
static int copyright_seen = 0;

/* Handle a .version directive.  */

void
obj_som_version (unused)
     int unused;
{
  char *version, c;

  if (version_seen)
    {
      as_bad ("Only one .version pseudo-op per file!");
      ignore_rest_of_line ();
      return;
    }

  SKIP_WHITESPACE ();
  if (*input_line_pointer == '\"')
    {
      version = input_line_pointer;
      ++input_line_pointer;
      while (is_a_char (next_char_of_string ()))
	;
      c = *input_line_pointer;
      *input_line_pointer = '\000';
    }
  else
    {
      as_bad ("Expected quoted string");
      ignore_rest_of_line ();
      return;
    }

  version_seen = 1;
  bfd_som_attach_aux_hdr (stdoutput, VERSION_AUX_ID, version);
  *input_line_pointer = c;
  demand_empty_rest_of_line ();
}

/* Handle a .copyright directive.   This probably isn't complete, but
   it's of dubious value anyway and (IMHO) not worth the time to finish.
   If you care about copyright strings that much, you fix it.  */

void
obj_som_copyright (unused)
     int unused;
{
  char *copyright, c;

  if (copyright_seen)
    {
      as_bad ("Only one .copyright pseudo-op per file!");
      ignore_rest_of_line ();
      return;
    }

  SKIP_WHITESPACE ();
  if (*input_line_pointer == '\"')
    {
      copyright = input_line_pointer;
      ++input_line_pointer;
      while (is_a_char (next_char_of_string ()))
	;
      c = *input_line_pointer;
      *input_line_pointer = '\000';
    }
  else
    {
      as_bad ("Expected quoted string");
      ignore_rest_of_line ();
      return;
    }

  copyright_seen = 1;
  bfd_som_attach_aux_hdr (stdoutput, COPYRIGHT_AUX_ID, copyright);
  *input_line_pointer = c;
  demand_empty_rest_of_line ();
}

/* Perform any initialization necessary for stabs support.

   For SOM we need to create the space which will contain the
   two stabs subspaces.  Additionally we need to set up the
   space/subspace relationships and set space/subspace attributes
   which BFD does not understand.  */

void
obj_som_init_stab_section (seg)
     segT seg;
{
  segT saved_seg = now_seg;
  segT space;
  subsegT saved_subseg = now_subseg;
  char *p, *file;
  unsigned int stroff;

  /* Make the space which will contain the debug subspaces.  */
  space = bfd_make_section_old_way (stdoutput, "$GDB_DEBUG$");

  /* Set SOM specific attributes for the space.  In particular we set
     the space "defined", "private", "sort_key", and "spnum" values. 

     Due to a bug in pxdb (called by hpux linker), the sort keys
     of the various stabs spaces/subspaces need to be "small".  We
     reserve range 72/73 which appear to work well.  */
  obj_set_section_attributes (space, 1, 1, 72, 2);
  bfd_set_section_alignment (stdoutput, space, 2);

  /* Set the containing space for both stab sections to be $GDB_DEBUG$
     (just created above).  Also set some attributes which BFD does
     not understand.  In particular, access bits, sort keys, and load
     quadrant.  */
  obj_set_subsection_attributes (seg, space, 0x1f, 73, 0);
  bfd_set_section_alignment (stdoutput, seg, 2);

  /* Make some space for the first stab entry which is special.
     It contains information about the length of this file's
     stab string and the like.  Using it avoids the need to 
     relocate the stab strings.

     The $GDB_STRINGS$ space will be created as a side effect of
     the call to get_stab_string_offset.  */
  p = frag_more (12);
  as_where (&file, (unsigned int *) NULL);
  stroff = get_stab_string_offset (file, "$GDB_STRINGS$");
  know (stroff == 1);
  md_number_to_chars (p, stroff, 4);
  seg_info (seg)->stabu.p = p;

  /* Set the containing space for both stab sections to be $GDB_DEBUG$
     (just created above).  Also set some attributes which BFD does
     not understand.  In particular, access bits, sort keys, and load
     quadrant.  */
  seg = bfd_get_section_by_name (stdoutput, "$GDB_STRINGS$");
  obj_set_subsection_attributes (seg, space, 0x1f, 72, 0);
  bfd_set_section_alignment (stdoutput, seg, 2);

  subseg_set (saved_seg, saved_subseg);
}

/* Fill in the counts in the first entry in a .stabs section.  */

static void
adjust_stab_sections (abfd, sec, xxx)
     bfd *abfd;
     asection *sec;
     PTR xxx;
{
  asection *strsec;
  char *p;
  int strsz, nsyms;

  if (strcmp ("$GDB_SYMBOLS$", sec->name))
    return;

  strsec = bfd_get_section_by_name (abfd, "$GDB_STRINGS$");
  if (strsec)
    strsz = bfd_section_size (abfd, strsec);
  else
    strsz = 0;
  nsyms = bfd_section_size (abfd, sec) / 12 - 1;

  p = seg_info (sec)->stabu.p;
  assert (p != 0);

  bfd_h_put_16 (abfd, (bfd_vma) nsyms, (bfd_byte *) p + 6);
  bfd_h_put_32 (abfd, (bfd_vma) strsz, (bfd_byte *) p + 8);
}

/* Called late in the asssembly phase to adjust the special
   stab entry.  This is where any other late object-file dependent
   processing which should happen.  */

void
som_frob_file ()
{
  bfd_map_over_sections (stdoutput, adjust_stab_sections, (PTR) 0);
}
