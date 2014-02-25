/**
 * Reverse loopback device - presents a directory as a disk image.
 */

#define _BSD_SOURCE
#define FUSE_USE_VERSION 26

#include <stdint.h>
#include <fuse.h>
#include <fcntl.h>
#include <endian.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <string>

#include <boost/filesystem/operations.hpp>

using namespace std;
using namespace boost;

string directory = "";
string image_filename = "disk.img";
uint64_t image_size = 0;
int64_t num_sectors = 0;

// N.B. using a fixed sector size of 512 bytes
struct fstype {
    int32_t max_sectors;
    bool fat32;
    uint8_t cluster_sectors;
};

fstype types[] = {
        8400, false,  0,
       32680, false,  2,
      262144, false,  4,
      524288, false,  8,
     1048576, false, 16,
    16777216, true,   8,
    33554432, true,  16,
    67108864, true,  32,
          -1, true,  64,
};

uint8_t bootsect[512];
bool fat32;
uint8_t cluster_sectors;
uint16_t res_sectors;
uint16_t root_dir_ents;
uint32_t root_dir_size;
uint32_t root_dir_sectors;
uint64_t free_sectors;
uint32_t free_clusters;
uint32_t fat_size;
uint32_t fat_sectors;
uint32_t data_sectors;
uint32_t data_clusters;

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

void setup_params()
{
    num_sectors = image_size / 512;

    int i;
    for (i = 0; types[i].max_sectors != -1; i++) {
        if (num_sectors < types[i].max_sectors) {
            fat32 = types[i].fat32;
            cluster_sectors = types[i].cluster_sectors;
            break;
        }
    }
    if (types[i].max_sectors == -1) {
        fat32 = types[i].fat32;
        cluster_sectors = types[i].cluster_sectors;
    }

    if (cluster_sectors == 0) {
        printf("Filesystem is too small for FAT16\n");
        return;
    }
    printf("%u sectors per cluster\n", cluster_sectors);

    res_sectors = fat32 ? 32 : 1;
    root_dir_ents = fat32 ? 0 : 512;

    root_dir_size = 32 * root_dir_ents;
    root_dir_sectors = (uint32_t) ceil(root_dir_size / 512.0);
    free_sectors = num_sectors - res_sectors - root_dir_sectors;
    free_clusters = free_sectors / cluster_sectors;
    fat_size = (free_clusters + 2) * (fat32 ? 4 : 2);
    fat_sectors = (uint32_t) ceil(fat_size / 512.0);
    data_sectors = free_sectors - 2 * fat_sectors;
    data_clusters = data_sectors / cluster_sectors;

    printf("FAT%d, %u reserved sectors, %u sectors per FAT (%u entries), %u sectors for root directory, %llu sectors total\n", fat32 ? 32 : 16, res_sectors, fat_sectors, free_clusters, root_dir_sectors, num_sectors);
    printf("%u data sectors, %u data clusters\n", data_sectors, data_clusters);

    memset(bootsect, 0, sizeof(bootsect));
    *(uint16_t *)&bootsect[11] = htole16(512); // bytes per sector
    bootsect[13] = cluster_sectors; // sectors per cluster
    *(uint16_t *)&bootsect[14] = htole16(res_sectors); // reserved sectors
    bootsect[16] = 2; // number of FATs
    *(uint16_t *)&bootsect[17] = htole16(root_dir_ents); // root dir entries
    *(uint16_t *)&bootsect[19] =
        htole16(fat32 || num_sectors > 0xffff ? 0 : num_sectors); // sector count
    bootsect[21] = 0xf0; // media type (removable)
    *(uint16_t *)&bootsect[22] = htole16(fat32 ? 0 : fat_sectors); // FAT sectors
    *(uint32_t *)&bootsect[32] = htole32(num_sectors); // total sectors

    if (fat32) {
        *(uint32_t *)&bootsect[36] = htole32(fat_sectors); // FAT sectors
        *(uint32_t *)&bootsect[44] = htole32(2); // root directory cluster
        *(uint16_t *)&bootsect[48] = htole16(1); // filesystem info sector number
        *(uint16_t *)&bootsect[50] = htole16(6); // backup boot-sector sector
    }
}

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
            continue; // Important: skip option-erasing code
        }

        // These are not the options FUSE is looking for...
        // N.B. copy 1 extra for the terminating NULL pointer
        memcpy(&argv[optind-2], &argv[optind],
               (argc - optind + 1) * sizeof(argv[optind]));
        optind -= 2;
        argc -= 2;
    }

    if (!filesystem::is_directory(directory)) {
        fprintf(stderr, "Please give a valid directory using the -D option\n");
        return 1;
    }

    setup_params();

    //return fuse_main(argc, argv, &rloop_oper, NULL);
    return 0;
}
