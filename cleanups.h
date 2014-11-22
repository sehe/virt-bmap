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

#ifndef CLEANUPS_H
#define CLEANUPS_H

#ifdef HAVE_ATTRIBUTE_CLEANUP

#define CLEANUP_FREE __attribute__((cleanup(cleanup_free)))
#define CLEANUP_FREE_STRING_LIST                                \
  __attribute__((cleanup(cleanup_free_string_list)))
#define CLEANUP_FREE_STATNS                                     \
  __attribute__((cleanup(cleanup_free_statns)))
#define CLEANUP_FREE_XATTR_LIST                                 \
  __attribute__((cleanup(cleanup_free_xattr_list)))
#define CLEANUP_FREE_STAT_LIST                                  \
  __attribute__((cleanup(cleanup_free_stat_list)))

#else
#define CLEANUP_FREE
#define CLEANUP_FREE_STRING_LIST
#define CLEANUP_FREE_STATNS
#define CLEANUP_FREE_XATTR_LIST
#define CLEANUP_FREE_STAT_LIST
#endif

extern void cleanup_free (void *ptr);
extern void free_string_list (char **argv);
extern void cleanup_free_string_list (char ***ptr);
extern void cleanup_free_statns (void *ptr);
extern void cleanup_free_xattr_list (void *ptr);
extern void cleanup_free_stat_list (void *ptr);

#endif /* CLEANUPS_H */
