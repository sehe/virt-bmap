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

/* This is an nbdkit plugin which implements the guts of the 'virt-bmap'
 * command.
 *
 * IMPORTANT NOTES:
 *
 * Do not try to use this plugin directly!  Use the 'virt-bmap'
 * wrapper script which sets up the arguments correctly and calls
 * nbdkit.
 *
 * This is a bit of a weird nbdkit plugin, because it calls itself.
 * Do not use it as an example of how to write plugins!  See the
 * nbdkit 'example*.c' files supplied in the nbdkit source, and also
 * read the nbdkit-plugin(3) documentation.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#include <pthread.h>

#include <nbdkit-plugin.h>

#include <guestfs.h>

#include "cleanups.h"
#include "ranges.h"
#include "visit.h"

static char *output = NULL;
static char *disk = NULL;
static const char *socket = NULL;
static const char *format = "raw";
static int fd = -1;
static int64_t size = -1;
static int thread_running = 0;
static pthread_t thread;

static struct thread_info {
  guestfs_h *g;
  int ret;
} thread_info;

/* Current object being mapped. */
static pthread_mutex_t current_object_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *current_object = NULL;

/* Ranges.  NB: acquire current_object_mutex before accessing. */
static void *ranges = NULL;

static void *start_thread (void *);

static int
bmap_config (const char *key, const char *value)
{
  if (strcmp (key, "disk") == 0) {
    free (disk);
    disk = nbdkit_absolute_path (value);
    if (disk == NULL)
      return -1;
  }
  else if (strcmp (key, "format") == 0) {
    format = value;
  }
  else if (strcmp (key, "output") == 0) {
    free (output);
    output = nbdkit_absolute_path (value);
    if (output == NULL)
      return -1;
  }
  else if (strcmp (key, "socket") == 0) {
    socket = value;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int
bmap_config_complete (void)
{
  struct stat statbuf;
  guestfs_h *g;
  int err;

  if (!output || !disk || !socket) {
    nbdkit_error ("missing parameters: are you using the 'virt-bmap' wrapper?");
    return -1;
  }

  ranges = new_ranges ();

  fd = open (disk, O_RDONLY);
  if (fd == -1) {
    nbdkit_error ("%s: %m", disk);
    return -1;
  }

  if (fstat (fd, &statbuf) == -1) {
    nbdkit_error ("fstat: %m");
    return -1;
  }
  size = statbuf.st_size;

  /* Open the guestfs handle synchronously so we can print errors. */
  g = guestfs_create ();
  if (!g) {
    nbdkit_error ("guestfs_create: %m");
    return -1;
  }

  /* Start the guestfs thread. */
  memset (&thread_info, 0, sizeof thread_info);
  thread_info.g = g;
  err = pthread_create (&thread, NULL, start_thread, &thread_info);
  if (err != 0) {
    nbdkit_error ("cannot start guestfs thread: %s", strerror (err));
    return -1;
  }
  thread_running = 1;

  return 0;
}

static void
bmap_unload (void)
{
  int err;
  void *retv;

  free_ranges (ranges);

  if (thread_running) {
    err = pthread_join (thread, &retv);
    if (err != 0)
      fprintf (stderr, "cannot join guestfs thread: %s\n", strerror (err));
    if (* (int *) retv != 0)
      fprintf (stderr, "ERROR: failed to construct block map, see earlier errors\n");
    /* unfortunately we can't return the correct exit code here XXX */
  }
  if (fd >= 0)
    close (fd);
  free (output);
  free (disk);
}

#define bmap_config_help                                        \
  "output=<OUTPUT>     Output filename (block map)\n"           \
  "disk=<DISK>         Input disk filename"                     \
  "format=raw|qcow2|.. Format of input disk (default: raw)\n"

/* The per-connection handle. */
struct bmap_handle {
  /* empty */
};

/* Create the per-connection handle. */
static void *
bmap_open (int readonly)
{
  struct bmap_handle *h;

  if (!readonly) {
    nbdkit_error ("handle must be opened readonly");
    return NULL;
  }

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }

  return h;
}

/* Free up the per-connection handle. */
static void
bmap_close (void *handle)
{
  struct bmap_handle *h = handle;

  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* Get the file size. */
static int64_t
bmap_get_size (void *handle)
{
  return size;
}

/* Mark the start and end of guestfs bmap operations.  This is called
 * from the guestfs thread, so we must lock any shared data structures.
 */
static void
mark_start (const char *object)
{
  pthread_mutex_lock (&current_object_mutex);
  free (current_object);
  current_object = strdup (object);
  if (current_object == NULL)
    abort ();
  pthread_mutex_unlock (&current_object_mutex);
}

static void
mark_end (void)
{
  pthread_mutex_lock (&current_object_mutex);
  free (current_object);
  current_object = NULL;
  pthread_mutex_unlock (&current_object_mutex);
}

static void
add_range (uint64_t offset, uint32_t count)
{
  pthread_mutex_lock (&current_object_mutex);
  if (current_object)
    insert_range (ranges, offset, offset+count, current_object);
  pthread_mutex_unlock (&current_object_mutex);
}

/* Read data from the file. */
static int
bmap_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  ssize_t r;

  add_range (offset, count);

  if (lseek (fd, offset, SEEK_SET) == -1) {
    nbdkit_error ("lseek: %m");
    return -1;
  }

  while (count > 0) {
    r = read (fd, buf, count);
    if (r == -1) {
      nbdkit_error ("read: %m");
      return -1;
    }
    if (r == 0) {
      nbdkit_error ("read: unexpected end of file");
      return -1;
    }
    count -= r;
    buf += r;
  }

  return 0;
}

/* This is the guestfs thread which calls back into nbdkit. */

static int count_partitions = 0;
static int count_lvs = 0;
static int count_filesystems = 0;
static int count_regular = 0;
static int count_directory = 0;

static int examine_partitions (guestfs_h *g);
static int examine_lvs (guestfs_h *g);
static int examine_filesystems (guestfs_h *g);
static int examine_filesystem (guestfs_h *g, const char *dev, const char *type);
static int visit_fn (const char *dir, const char *name, const struct guestfs_statns *stat, const struct guestfs_xattr_list *xattrs, void *opaque);
static int ranges_to_output (void);

static void *
start_thread (void *infov)
{
  struct thread_info *info = infov;
  guestfs_h *g = info->g;
  size_t i;
  CLEANUP_FREE char *server = NULL;
  const char *servers[2];

  info->ret = -1;

  /* Wait for the socket to appear, ie. for nbdkit to start up. */
  for (i = 0; i < 10; ++i) {
    if (access (socket, R_OK) == 0)
      goto nbdkit_started;
    sleep (1);
  }

  fprintf (stderr, "timed out waiting for nbdkit server to start up\n");
  goto error;

 nbdkit_started:
  if (asprintf (&server, "unix:%s", socket) == -1) {
    perror ("asprintf");
    goto error;
  }

  servers[0] = server;
  servers[1] = NULL;

  if (guestfs_add_drive_opts (g, "" /* export name */,
                              GUESTFS_ADD_DRIVE_OPTS_READONLY, 1,
                              GUESTFS_ADD_DRIVE_OPTS_FORMAT, format,
                              GUESTFS_ADD_DRIVE_OPTS_PROTOCOL, "nbd",
                              GUESTFS_ADD_DRIVE_OPTS_SERVER, servers,
                              -1) == -1)
    goto error;

  if (guestfs_launch (g) == -1)
    goto error;

  /* Examine non-filesystem objects. */
  if (examine_partitions (g) == -1)
    goto error;
  if (examine_lvs (g) == -1)
    goto error;

  /* Examine filesystems. */
  if (examine_filesystems (g) == -1)
    goto error;

  /* Convert ranges to final output file. */
  printf ("virt-bmap: writing %s\n", output);
  if (ranges_to_output () == -1)
    goto error;

  /* Print summary. */
  printf ("virt-bmap: successfully examined %d partitions, %d logical volumes,\n"
          "           %d filesystems, %d directories, %d files\n"
          "virt-bmap: output written to %s\n",
          count_partitions, count_lvs,
          count_filesystems, count_directory, count_regular,
          output);
  info->ret = 0;
 error:
  guestfs_close (g);

  /* Kill the nbdkit process so it exits.  The nbdkit process is us,
   * so we're killing ourself here.
   */
  kill (getpid (), SIGTERM);

  return &info->ret;
}

static int
examine_partitions (guestfs_h *g)
{
  CLEANUP_FREE_STRING_LIST char **parts = NULL;
  size_t i;
  int r;

  /* Get partitions. */
  parts = guestfs_list_partitions (g);
  if (parts == NULL)
    return -1;

  for (i = 0; parts[i] != NULL; ++i) {
    CLEANUP_FREE char *object = NULL;

    printf ("virt-bmap: examining %s ...\n", parts[i]);
    count_partitions++;

    if (asprintf (&object, "p %s", parts[i]) == -1)
      return -1;

    if (guestfs_bmap_device (g, parts[i]) == -1)
      return -1;
    mark_start (object);
    r = guestfs_bmap (g);
    mark_end ();
    if (r == -1) return -1;
  }

  return 0;

}

static int
examine_lvs (guestfs_h *g)
{
  CLEANUP_FREE_STRING_LIST char **lvs = NULL;
  size_t i;
  int r;

  /* Get LVs. */
  lvs = guestfs_lvs (g);
  if (lvs == NULL)
    return -1;

  for (i = 0; lvs[i] != NULL; ++i) {
    CLEANUP_FREE char *object = NULL;

    printf ("virt-bmap: examining %s ...\n", lvs[i]);
    count_lvs++;

    if (asprintf (&object, "lvm_lv %s", lvs[i]) == -1)
      return -1;

    if (guestfs_bmap_device (g, lvs[i]) == -1)
      return -1;
    mark_start (object);
    r = guestfs_bmap (g);
    mark_end ();
    if (r == -1) return -1;
  }

  return 0;
}

static int
examine_filesystems (guestfs_h *g)
{
  CLEANUP_FREE_STRING_LIST char **filesystems = NULL;
  size_t i;

  /* Get the filesystems in the disk image. */
  filesystems = guestfs_list_filesystems (g);
  if (filesystems == NULL)
    return -1;

  for (i = 0; filesystems[i] != NULL; i += 2) {
    if (examine_filesystem (g, filesystems[i], filesystems[i+1]) == -1)
      return -1;
  }

  return 0;
}

static size_t
count_strings (char *const *argv)
{
  size_t r;

  for (r = 0; argv[r]; ++r)
    ;

  return r;
}

struct visit_context {
  guestfs_h *g;
  const char *dev;              /* filesystem */
  size_t nr_files;              /* used for progress bar */
  size_t files_processed;
};

static int
examine_filesystem (guestfs_h *g, const char *dev, const char *type)
{
  int r;

  /* Try to mount it. */
  guestfs_push_error_handler (g, NULL, NULL);
  r = guestfs_mount_ro (g, dev, "/");
  guestfs_pop_error_handler (g);
  if (r == 0) {
    struct visit_context context;
    CLEANUP_FREE_STRING_LIST char **files = NULL;

    /* Mountable, so examine the filesystem. */
    printf ("virt-bmap: examining filesystem on %s (%s) ...\n", dev, type);
    count_filesystems++;

    /* Read how many files/directories there are so we can estimate
     * progress.
     */
    files = guestfs_find (g, "/");

    /* Set filesystem readahead to 0, but ignore error if not possible. */
    guestfs_push_error_handler (g, NULL, NULL);
    guestfs_blockdev_setra (g, dev, 0);
    guestfs_pop_error_handler (g);

    context.g = g;
    context.dev = dev;
    context.nr_files = count_strings (files);
    context.files_processed = 0;
    if (visit (g, "/", visit_fn, &context) == -1)
      return -1;
  }

  guestfs_umount_all (g);

  return 0;
}

static int
visit_fn (const char *dir, const char *name,
          const struct guestfs_statns *stat,
          const struct guestfs_xattr_list *xattrs,
          void *contextv)
{
  struct visit_context *context = contextv;
  guestfs_h *g = context->g;
  char type = '?';
  CLEANUP_FREE char *path = NULL, *object = NULL;
  int r;

  context->files_processed++;
  if ((context->files_processed & 255) == 0) {
    printf ("%zu/%zu (%.1f%%)        \r",
            context->files_processed, context->nr_files,
            100.0 * context->files_processed / context->nr_files);
    fflush (stdout);
  }

  path = full_path (dir, name);
  if (path == NULL)
    return -1;

  if (is_reg (stat->st_mode))
    type = 'f';
  else if (is_dir (stat->st_mode))
    type = 'd';

  /* Note that the object string is structured (space-separated
   * fields), and this is copied directly to the output file.
   */
  if (asprintf (&object, "%c %s %s", type, context->dev, path) == -1)
    return -1;

  if (type == 'f') {            /* regular file */
    count_regular++;
  bmap_file:
    if (guestfs_bmap_file (g, path) == -1)
      return -1;
    mark_start (object);
    r = guestfs_bmap (g);
    mark_end ();
    if (r == -1) return -1;
  }
  else if (type == 'd') {       /* directory */
    count_directory++;
    goto bmap_file;
  }

  return 0;
}

/* Convert ranges to output file format. */
static void print_range (uint64_t start, uint64_t end, const char *object, void *opaque);

static int
ranges_to_output (void)
{
  FILE *fp;

  pthread_mutex_lock (&current_object_mutex);

  /* Write out the ranges to 'output'. */
  fp = fopen (output, "w");
  if (fp == NULL) {
    perror (output);
    return -1;
  }

  iter_range (ranges, print_range, fp);

  fclose (fp);

  pthread_mutex_unlock (&current_object_mutex);

  return 0;
}

static void
print_range (uint64_t start, uint64_t end, const char *object, void *opaque)
{
  FILE *fp = opaque;

  /* Note that the initial '1' is meant to signify the first disk.
   * Currently we can only map a single disk, but in future we
   * should be able to handle multiple disks.
   */
  fprintf (fp, "1 %" PRIx64 " %" PRIx64 " %s\n", start, end, object);
}

/* Register the nbdkit plugin. */

static struct nbdkit_plugin plugin = {
  .name              = "virtbmapexaminer",
  .version           = PACKAGE_VERSION,
  .config            = bmap_config,
  .config_help       = bmap_config_help,
  .config_complete   = bmap_config_complete,
  .open              = bmap_open,
  .close             = bmap_close,
  .get_size          = bmap_get_size,
  .pread             = bmap_pread,
  .unload            = bmap_unload,
};

NBDKIT_REGISTER_PLUGIN(plugin)
