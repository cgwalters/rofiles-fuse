/*
 * ROFILES - Create a mount point that ensures file content and xattrs
 * of the underlying basepath are read-only.
 *
 * Copyright 2015 Colin Walters <walters@redhat.com>
 *
 * Based on https://github.com/cognusion/fuse-rofs
 * Copyright 2005,2006,2008 Matthew Keller. m@cognusion.com and others.
 * v2008.09.24
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 * 
 * 
 */

#define FUSE_USE_VERSION 26

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <unistd.h>
#include <fuse.h>

static inline void
glnx_cleanup_close_fdp (int *fdp)
{
  int fd;

  fd = *fdp;
  if (fd != -1)
    (void) close (fd);
}

/**
 * glnx_fd_close:
 *
 * Call close() on a variable location when it goes out of scope.
 */
#define glnx_fd_close __attribute__((cleanup(glnx_cleanup_close_fdp)))

static inline const char *
ENSURE_RELPATH (const char *path)
{
  return path + strspn (path, "/");
}

// Global to store our read-write path
static int basefd = -1;

static int
callback_getattr (const char *path, struct stat *st_data)
{
  path = ENSURE_RELPATH (path);
  if (!*path)
    {
      if (fstat (basefd, st_data) == -1)
	return -errno;
    }
  else
    {
      if (fstatat (basefd, path, st_data, 0) == -1)
	return -errno;
    }
  return 0;
}

static int
callback_readlink (const char *path, char *buf, size_t size)
{
  int r;

  path = ENSURE_RELPATH (path);

  /* Note FUSE wants the string to be always nul-terminated, even if
   * truncated.
   */
  r = readlinkat (basefd, path, buf, size - 1);
  if (r == -1)
    return -errno;
  buf[r] = '\0';
  return 0;
}

static int
callback_readdir (const char *path, void *buf, fuse_fill_dir_t filler,
		  off_t offset, struct fuse_file_info *fi)
{
  DIR *dp;
  struct dirent *de;
  int dfd;

  path = ENSURE_RELPATH (path);

  if (!*path)
    {
      dfd = fcntl (basefd, F_DUPFD_CLOEXEC, 3);
      lseek (dfd, 0, SEEK_SET);
    }
  else
    {
      dfd = openat (basefd, path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY);
      if (dfd == -1)
	return -errno;
    }

  /* Transfers ownership of fd */
  dp = fdopendir (dfd);
  if (dp == NULL)
    return -errno;

  while ((de = readdir (dp)) != NULL)
    {
      struct stat st;
      memset (&st, 0, sizeof (st));
      st.st_ino = de->d_ino;
      st.st_mode = de->d_type << 12;
      if (filler (buf, de->d_name, &st, 0))
	break;
    }

  (void) closedir (dp);
  return 0;
}

static int
callback_mknod (const char *path, mode_t mode, dev_t rdev)
{
  return -EROFS;
}

static int
callback_mkdir (const char *path, mode_t mode)
{
  path = ENSURE_RELPATH (path);
  if (mkdirat (basefd, path, mode) == -1)
    return -errno;
  return 0;
}

static int
callback_unlink (const char *path)
{
  path = ENSURE_RELPATH (path);
  if (unlinkat (basefd, path, 0) == -1)
    return -errno;
  return 0;
}

static int
callback_rmdir (const char *path)
{
  path = ENSURE_RELPATH (path);
  if (unlinkat (basefd, path, AT_REMOVEDIR) == -1)
    return -errno;
  return 0;
}

static int
callback_symlink (const char *from, const char *to)
{
  to = ENSURE_RELPATH (to);
  if (symlinkat (from, basefd, to) == -1)
    return -errno;
  return 0;
}

static int
callback_rename (const char *from, const char *to)
{
  from = ENSURE_RELPATH (from);
  to = ENSURE_RELPATH (to);
  if (symlinkat (from, basefd, to) == -1)
  if (renameat (basefd, from, basefd, to) == -1)
    return -errno;
  return 0;
}

static int
callback_link (const char *from, const char *to)
{
  from = ENSURE_RELPATH (from);
  to = ENSURE_RELPATH (to);
  if (linkat (basefd, from, basefd, to, 0) == -1)
    return -errno;
  return 0;
}

static int
can_write (const char *path)
{
  struct stat stbuf;
  path = ENSURE_RELPATH (path);
  if (fstatat (basefd, path, &stbuf, 0) == -1)
    return -errno;
  if (!S_ISDIR (stbuf.st_mode))
    return -EROFS;
  return 0;
}

#define VERIFY_WRITE(path) do { \
  int r = can_write (path); \
  if (r != 0) \
    return r; \
  } while (0)

static int
callback_chmod (const char *path, mode_t mode)
{
  path = ENSURE_RELPATH (path);
  VERIFY_WRITE(path);
  if (fchmodat (basefd, path, mode, 0) != 0)
    return -errno;
  return 0;
}

static int
callback_chown (const char *path, uid_t uid, gid_t gid)
{
  path = ENSURE_RELPATH (path);
  VERIFY_WRITE(path);
  if (fchownat (basefd, path, uid, gid, 0) != 0)
    return -errno;
  return 0;
}

static int
callback_truncate (const char *path, off_t size)
{
  return -EROFS;
}

static int
callback_utime (const char *path, struct utimbuf *buf)
{
  return -EROFS;
}

static int
callback_open (const char *path, struct fuse_file_info *finfo)
{
  const int flags = finfo->flags & O_ACCMODE;
  if (!(flags & O_RDONLY))
    return -EROFS;
  return 0;
}

static int
callback_read (const char *path, char *buf, size_t size, off_t offset,
	       struct fuse_file_info *finfo)
{
  int r;
  glnx_fd_close int fd = -1;

  path = ENSURE_RELPATH (path);

  fd = openat (basefd, path, O_RDONLY);
  if (fd == -1)
    return -errno;

  r = pread (fd, buf, size, offset);
  if (r == -1)
    return -errno;
  return r;
}

static int
callback_write (const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *finfo)
{
  return -EROFS;
}

static int
callback_statfs (const char *path, struct statvfs *st_buf)
{
  if (fstatvfs (basefd, st_buf) == -1)
    return -errno;
  return 0;
}

static int
callback_release (const char *path, struct fuse_file_info *finfo)
{
  return 0;
}

static int
callback_fsync (const char *path, int crap, struct fuse_file_info *finfo)
{
  return -EROFS;
}

static int
callback_access (const char *path, int mode)
{
  if (mode & W_OK)
    return -EROFS;

  path = ENSURE_RELPATH (path);

  if (faccessat (basefd, path, mode, 0) == -1)
    return -errno;
  return 0;
}

static int
callback_setxattr (const char *path, const char *name, const char *value,
		   size_t size, int flags)
{
  return -ENOTSUP;
}

static int
callback_getxattr (const char *path, const char *name, char *value,
		   size_t size)
{
  return -ENOTSUP;
}

/*
 * List the supported extended attributes.
 */
static int
callback_listxattr (const char *path, char *list, size_t size)
{
  return -ENOTSUP;

}

/*
 * Remove an extended attribute.
 */
static int
callback_removexattr (const char *path, const char *name)
{
  return -ENOTSUP;

}

struct fuse_operations callback_oper = {
  .getattr = callback_getattr,
  .readlink = callback_readlink,
  .readdir = callback_readdir,
  .mknod = callback_mknod,
  .mkdir = callback_mkdir,
  .symlink = callback_symlink,
  .unlink = callback_unlink,
  .rmdir = callback_rmdir,
  .rename = callback_rename,
  .link = callback_link,
  .chmod = callback_chmod,
  .chown = callback_chown,
  .truncate = callback_truncate,
  .utime = callback_utime,
  .open = callback_open,
  .read = callback_read,
  .write = callback_write,
  .statfs = callback_statfs,
  .release = callback_release,
  .fsync = callback_fsync,
  .access = callback_access,

  /* Extended attributes support for userland interaction */
  .setxattr = callback_setxattr,
  .getxattr = callback_getxattr,
  .listxattr = callback_listxattr,
  .removexattr = callback_removexattr
};

enum
{
  KEY_HELP,
  KEY_VERSION,
};

static void
usage (const char *progname)
{
  fprintf (stdout,
	   "usage: %s basepath mountpoint [options]\n"
	   "\n"
	   "   Mounts basepath as a read-only mount at mountpoint\n"
	   "\n"
	   "general options:\n"
	   "   -o opt,[opt...]     mount options\n"
	   "   -h  --help          print help\n"
	   "\n", progname);
}

static int
rofs_parse_opt (void *data, const char *arg, int key,
		struct fuse_args *outargs)
{
  (void) data;

  switch (key)
    {
    case FUSE_OPT_KEY_NONOPT:
      if (basefd == -1)
	{
	  basefd = openat (AT_FDCWD, arg, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY);
	  if (basefd == -1)
	    {
	      perror ("openat");
	      exit (1);
	    }
	  return 0;
	}
      else
	{
	  return 1;
	}
    case FUSE_OPT_KEY_OPT:
      return 1;
    case KEY_HELP:
      usage (outargs->argv[0]);
      exit (0);
    default:
      fprintf (stderr, "see `%s -h' for usage\n", outargs->argv[0]);
      exit (1);
    }
  return 1;
}

static struct fuse_opt rofs_opts[] = {
  FUSE_OPT_KEY ("-h", KEY_HELP),
  FUSE_OPT_KEY ("--help", KEY_HELP),
  FUSE_OPT_KEY ("-V", KEY_VERSION),
  FUSE_OPT_KEY ("--version", KEY_VERSION),
  FUSE_OPT_END
};

int
main (int argc, char *argv[])
{
  struct fuse_args args = FUSE_ARGS_INIT (argc, argv);
  int res;

  res = fuse_opt_parse (&args, &basefd, rofs_opts, rofs_parse_opt);
  if (res != 0)
    {
      fprintf (stderr, "Invalid arguments\n");
      fprintf (stderr, "see `%s -h' for usage\n", argv[0]);
      exit (1);
    }
  if (basefd == -1)
    {
      fprintf (stderr, "Missing basepath\n");
      fprintf (stderr, "see `%s -h' for usage\n", argv[0]);
      exit (1);
    }

  fuse_main (args.argc, args.argv, &callback_oper, NULL);

  return 0;
}
