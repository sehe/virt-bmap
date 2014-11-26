/* virt-bmap logger plugin
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

/* This is an nbdkit plugin which observes guest activity and displays
 * what files are being accessed.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#include <nbdkit-plugin.h>

#include "cleanups.h"
#include "ranges.h"

static char *file = NULL;
static char *bmap = NULL;
static char *logfile = NULL;
static void *ranges = NULL;
static FILE *logfp = NULL;

static int
logger_config (const char *key, const char *value)
{
  if (strcmp (key, "file") == 0) {
    free (file);
    file = nbdkit_absolute_path (value);
    if (file == NULL)
      return -1;
  }
  else if (strcmp (key, "bmap") == 0) {
    free (bmap);
    bmap = nbdkit_absolute_path (value);
    if (bmap == NULL)
      return -1;
  }
  else if (strcmp (key, "logfile") == 0) {
    free (logfile);
    logfile = nbdkit_absolute_path (value);
    if (logfile == NULL)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
logger_config_complete (void)
{
  FILE *fp;
  const char *bmap_file = bmap ? bmap : "bmap";
  CLEANUP_FREE char *line = NULL;
  size_t alloc;
  ssize_t len;
  size_t count;

  if (!file) {
    nbdkit_error ("missing 'file=...' parameter, see virt-bmap(1)");
    return -1;
  }

  ranges = new_ranges ();

  /* Load ranges from bmap file. */
  fp = fopen (bmap_file, "r");
  if (fp == NULL) {
    nbdkit_error ("open: %s: %m", bmap_file);
    return -1;
  }

  count = 0;
  while (errno = 0, (len = getline (&line, &alloc, fp)) != -1) {
    uint64_t start, end;
    int object_offset;
    const char *object;

    if (len > 0 && line[len-1] == '\n')
      line[--len] = '\0';

    if (sscanf (line, "1 %" SCNx64 " %" SCNx64 " %n",
                &start, &end, &object_offset) >= 2) {
      count++;
      object = line + object_offset;
      insert_range (ranges, start, end, object);
    }
  }

  if (errno) {
    nbdkit_error ("getline: %s: %m", bmap_file);
    fclose (fp);
    return -1;
  }

  if (count == 0) {
    nbdkit_error ("no ranges were read from block map file: %s", bmap_file);
    return -1;
  }

  if (fclose (fp) == -1) {
    nbdkit_error ("fclose: %s: %m", bmap_file);
    return -1;
  }

  /* Set up log file. */
  if (logfile) {
    logfp = fopen (logfile, "w");
    if (logfp == NULL) {
      nbdkit_error ("cannot open log file: %s: %m", logfile);
      return -1;
    }
  }

  return 0;
}

static void
logger_unload (void)
{
  if (logfp)
    fclose (logfp);

  free_ranges (ranges);
  free (logfile);
  free (bmap);
  free (file);
}

#define logger_config_help                                        \
  "file=<DISK>         Input disk filename\n"                     \
  "logfile=<OUTPUT>    Log file (default: stdout)\n"              \
  "bmap=<BMAP>         Block map (default: \"bmap\")"             \

/* See log_operation below. */
struct operation {
  int is_read;
  uint32_t count;
  uint64_t offset;
  int priority;
  uint64_t start;
  uint64_t end;
  const char *object;
};

/* The per-connection handle. */
struct handle {
  int fd;

  /* See log_operation below. */
  struct operation last;
  struct operation current;
};

/* Create the per-connection handle. */
static void *
logger_open (int readonly)
{
  struct handle *h;
  int flags;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  memset (&h->last, 0, sizeof h->last);
  memset (&h->current, 0, sizeof h->current);

  flags = O_CLOEXEC|O_NOCTTY;
  if (readonly)
    flags |= O_RDONLY;
  else
    flags |= O_RDWR;

  h->fd = open (file, flags);
  if (h->fd == -1) {
    nbdkit_error ("open: %s: %m", file);
    free (h);
    return NULL;
  }

  return h;
}

/* Free up the per-connection handle. */
static void
logger_close (void *handle)
{
  struct handle *h = handle;

  close (h->fd);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS

/* Get the file size. */
static int64_t
logger_get_size (void *handle)
{
  struct handle *h = handle;
  struct stat statbuf;

  if (fstat (h->fd, &statbuf) == -1) {
    nbdkit_error ("stat: %m");
    return -1;
  }

  return statbuf.st_size;
}

static int
priority_of_object (const char *object)
{
  switch (object[0]) {
  case 'v': return 1;           /* whole device (least important) */
  case 'p': return 2;
  case 'l': return 3;
  case 'd': return 4;
  case 'f': return 5;           /* file (most important) */
  default: return 6;
  }
}

/* Callback from find_range.  Save the highest priority object into
 * the handle for later printing.
 */
static void
log_callback (uint64_t start, uint64_t end, const char *object, void *opaque)
{
  struct handle *h = opaque;
  int priority = priority_of_object (object);

  if (priority > h->current.priority) {
    h->current.priority = priority;
    h->current.start = start;
    h->current.end = end;
    h->current.object = object;
  }
}

static void
log_operation (struct handle *h, uint64_t offset, uint32_t count, int is_read)
{
  /* Because Boost interval_map is really bloody slow, implement a
   * shortcut here.  We can remove this once Boost performance
   * problems have been fixed.
   */
  if (h->current.is_read == is_read &&
      h->current.count == count &&
      h->current.offset == offset)
    goto skip_find_range;

  h->current.is_read = is_read;
  h->current.count = count;
  h->current.offset = offset;
  h->current.priority = 0;
  find_range (ranges, offset, offset+count, log_callback, h);
 skip_find_range:

  if (h->current.priority > 0) {
    FILE *fp = logfp ? logfp : stdout;

    if (h->current.priority != h->last.priority ||
        h->current.is_read != h->last.is_read ||
        h->last.object == NULL ||
        strcmp (h->current.object, h->last.object) != 0) {
      fprintf (fp,
               "\n"
               "%s %s\n",
               is_read ? "read" : "write", h->current.object);

      h->last = h->current;
    }

    /* It would be nice to print an offset relative to the current
     * object here, but that's not possible since we don't have the
     * information about precisely what file offsets map to what
     * blocks.
     */
    fprintf (fp, " %" PRIx64 "-%" PRIx64, offset, offset+count);
    fflush (fp);
  }
}

/* Read data from the file. */
static int
logger_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  struct handle *h = handle;

  log_operation (h, offset, count, 1);

  while (count > 0) {
    ssize_t r = pread (h->fd, buf, count, offset);
    if (r == -1) {
      nbdkit_error ("pread: %m");
      return -1;
    }
    if (r == 0) {
      nbdkit_error ("pread: unexpected end of file");
      return -1;
    }
    buf += r;
    count -= r;
    offset += r;
  }

  return 0;
}

/* Write data to the file. */
static int
logger_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  struct handle *h = handle;

  log_operation (h, offset, count, 0);

  while (count > 0) {
    ssize_t r = pwrite (h->fd, buf, count, offset);
    if (r == -1) {
      nbdkit_error ("pwrite: %m");
      return -1;
    }
    buf += r;
    count -= r;
    offset += r;
  }

  return 0;
}

/* Flush the file to disk. */
static int
logger_flush (void *handle)
{
  struct handle *h = handle;

  if (fdatasync (h->fd) == -1) {
    nbdkit_error ("fdatasync: %m");
    return -1;
  }

  return 0;
}

/* Register the nbdkit plugin. */

static struct nbdkit_plugin plugin = {
  .name              = "bmaplogger",
  .version           = PACKAGE_VERSION,
  .config            = logger_config,
  .config_help       = logger_config_help,
  .config_complete   = logger_config_complete,
  .open              = logger_open,
  .close             = logger_close,
  .get_size          = logger_get_size,
  .pread             = logger_pread,
  .unload            = logger_unload,
};

NBDKIT_REGISTER_PLUGIN(plugin)
