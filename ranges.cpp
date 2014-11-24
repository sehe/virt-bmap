/* virt-bmap examiner plugin
 * Copyright (C) 2014 Red Hat Inc.
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
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include <boost/icl/interval.hpp>
#include <boost/icl/interval_set.hpp>
#include <boost/icl/interval_map.hpp>

using namespace std;

/* Maps intervals (uint64_t, uint64_t) to a set of strings, where each
 * string represents an object that covers that range.
 */
typedef set<string> objects;
typedef boost::icl::interval_map<uint64_t, objects> ranges;

extern "C" void *
new_ranges (void)
{
  return new ranges ();
}

extern "C" void
free_ranges (void *mapv)
{
  ranges *map = (ranges *) mapv;
  delete map;
}

extern "C" void
insert_range (void *mapv, uint64_t start, uint64_t end, const char *object)
{
  ranges *map = (ranges *) mapv;
  objects obj_set;
  obj_set.insert (object);
  map->add (make_pair (boost::icl::interval<uint64_t>::right_open (start, end),
                       obj_set));
}

extern "C" void
iter_range (void *mapv, void (*f) (uint64_t start, uint64_t end, const char *object, void *opaque), void *opaque)
{
  ranges *map = (ranges *) mapv;
  ranges::iterator iter = map->begin ();
  while (iter != map->end ()) {
    boost::icl::interval<uint64_t>::type range = iter->first;
    uint64_t start = range.lower ();
    uint64_t end = range.upper ();

    objects obj_set = iter->second;
    objects::iterator iter2 = obj_set.begin ();
    while (iter2 != obj_set.end ()) {
      f (start, end, iter2->c_str (), opaque);
      iter2++;
    }
    iter++;
  }
}

extern "C" void
find_range (void *mapv, uint64_t start, uint64_t end, void (*f) (uint64_t start, uint64_t end, const char *object, void *opaque), void *opaque)
{
  ranges *map = (ranges *) mapv;

  boost::icl::interval<uint64_t>::type window;
  window = boost::icl::interval<uint64_t>::right_open (start, end);

  ranges r = *map & window;

  ranges::iterator iter = r.begin ();
  while (iter != r.end ()) {
    boost::icl::interval<uint64_t>::type range = iter->first;
    uint64_t start = range.lower ();
    uint64_t end = range.upper ();

    objects obj_set = iter->second;
    objects::iterator iter2 = obj_set.begin ();
    while (iter2 != obj_set.end ()) {
      f (start, end, iter2->c_str (), opaque);
      iter2++;
    }
    iter++;
  }
}
