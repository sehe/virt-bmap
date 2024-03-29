/* virt-bmap examiner plugin
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

#ifndef RANGES_H
#define RANGES_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern void *new_ranges (void);
extern void free_ranges (void *mapv);
extern void insert_range (void *mapv, uint64_t start, uint64_t end, const char *object);
extern void iter_range (void *mapv, void (*f) (uint64_t start, uint64_t end, const char *object, void *opaque), void *opaque);
extern void find_range (void *mapv, uint64_t start, uint64_t end, void (*f) (uint64_t start, uint64_t end, const char *object, void *opaque), void *opaque);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* RANGES_H */
