/**
 * Reverse loopback device - presents a directory as a disk image.
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <string>

using namespace std;

string directory;
string image_filename = "disk.img";
unsigned long long image_size = 0;

static int rloop_getattr(const char *path, struct stat* stbuf)
{
    return -ENOSYS;
}

static int rloop_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    return -ENOSYS;
}

static int rloop_open(const char *path, struct fuse_file_info *fi)
{
    return -ENOSYS;
}

static int rloop_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    return -ENOSYS;
}

static int rloop_write(const char *path, const char *buf, size_t size,
                       off_t offset, struct fuse_file_info *fi)
{
    return -ENOSYS;
}

static struct fuse_operations rloop_oper;

int main(int argc, char **argv)
{
    static const char *optstring = "D:n:S:";
    int ch;

    memset(&rloop_oper, 0, sizeof(rloop_oper));
    rloop_oper.getattr = rloop_getattr,
    rloop_oper.readdir = rloop_readdir,
    rloop_oper.open    = rloop_open,
    rloop_oper.read    = rloop_read,
    rloop_oper.write   = rloop_write,

    opterr = 0; // prevent getopt(3) from writing messages

    while (-1 != (ch = getopt(argc, argv, optstring))) {
        if (ch == 'D') {
            directory = optarg;
        } else if (ch == 'n') {
            image_filename = optarg;
        } else if (ch == 'S') {
            char *endptr = NULL;
            image_size = strtoull(optarg, &endptr, 10);
            if (endptr != optarg && *endptr) {
                char mult = tolower(*endptr);
                if (mult == 'k') {
                    image_size <<= 10;
                } else if (mult == 'm') {
                    image_size <<= 20;
                } else if (mult == 'g') {
                    image_size <<= 30;
                } else if (mult == 't') {
                    image_size <<= 40;
                }
            }
        } else if (ch == '?') {
            continue;
        }

        // These are not the options FUSE is looking for...
        // N.B. copy 1 extra for the terminating NULL pointer
        memcpy(&argv[optind-2], &argv[optind],
               (argc - optind + 1) * sizeof(argv[optind]));
        optind -= 2;
        argc -= 2;
    }

    return fuse_main(argc, argv, &rloop_oper, NULL);
}
