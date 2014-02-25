/**
 * Reverse loopback device - presents a directory as a disk image.
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <errno.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>

static int rloop_getattr(const char *path, struct stat* stbuf)
{
}

static int rloop_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
}

static int rloop_open(const char *path, struct fuse_file_info *fi)
{
}

static int rloop_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
}

static int rloop_write(const char *path, const char *buf, size_t size,
                       off_t offset, struct fuse_file_info *fi)
{
}

static struct fuse_operations rloop_oper = {
    .getattr = rloop_getattr,
    .readdir = rloop_readdir,
    .open    = rloop_open,
    .read    = rloop_read,
    .write   = rloop_write,
};

int main(int argc, char **argv)
{
    return fuse_main(argc, argv, &rloop_oper, NULL);
}
