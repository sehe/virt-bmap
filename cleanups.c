/* cleanup functions (from libguestfs)
 * Copyright (C) 2010-2014 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include <guestfs.h>

void
cleanup_free (void *ptr)
{
  free (* (void **) ptr);
}

void
free_string_list (char **argv)
{
  size_t i;

  if (argv == NULL)
    return;

  for (i = 0; argv[i] != NULL; ++i)
    free (argv[i]);
  free (argv);
}

void
cleanup_free_string_list (char ***ptr)
{
  free_string_list (*ptr);
}

void
cleanup_free_statns (void *ptr)
{
  guestfs_free_statns (* (struct guestfs_statns **) ptr);
}

void
cleanup_free_xattr_list (void *ptr)
{
  guestfs_free_xattr_list (* (struct guestfs_xattr_list **) ptr);
}

void
cleanup_free_stat_list (void *ptr)
{
  guestfs_free_stat_list (* (struct guestfs_stat_list **) ptr);
}
