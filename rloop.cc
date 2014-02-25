/**
 * Reverse loopback device - presents a directory as a FAT16/32 disk image.
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
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

using namespace std;
using namespace boost;

string directory = "";
string image_filename = "/disk.img";
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
uint32_t cluster_bytes;
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
uint32_t root_dir_start_sector;
uint32_t data_start_sector;
uint8_t fat_ent_size;

uint32_t next_cluster = 2;

struct file_ent {
    filesystem::path path_;
    uint32_t start_cluster_;
    uint32_t num_clusters_;
    virtual bool is_root() { return false; }
    virtual ~file_ent() {}
};

struct dir_ent : public file_ent {
    bool is_root_;
    vector<file_ent *> files_;
    bool is_root() { return is_root_; }
    ~dir_ent() {
        for (size_t i = 0; i < files_.size(); i++) {
            delete files_[i];
        }
    }
};

struct fat_ent {
    file_ent *ent;
    uint32_t cluster;
    bool valid;
    fat_ent(file_ent *ent, uint32_t cluster)
    : ent(ent), cluster(cluster), valid(true) {
    }
};

dir_ent* root = NULL;
vector<fat_ent> fat;

static int rloop_getattr(const char *path, struct stat* stbuf)
{
    int res = 0;

    memset(stbuf, 0, sizeof(*stbuf));

    if (0 == strcmp(path, "/")) {
        stbuf->st_mode = S_IFDIR | 0555;
        stbuf->st_nlink = 2;
    } else if (0 == strcmp(path, image_filename.c_str())) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = image_size;
    } else {
        res = -ENOENT;
    }

    return res;
}

static int rloop_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    if (0 != strcmp(path, "/")) {
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, image_filename.c_str() + 1, NULL, 0);

    return 0;
}

static int rloop_open(const char *path, struct fuse_file_info *fi)
{
    if (0 != strcmp(path, image_filename.c_str())) {
        return -ENOENT;
    }

    if ((fi->flags & 3) != O_RDONLY) {
        return -EACCES;
    }

    return 0;
}

static int rloop_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    if (0 != strcmp(path, image_filename.c_str())) {
        return -ENOENT;
    }

    if ((uint64_t) offset >= image_size) {
        size = 0;
    } else if ((uint64_t) offset + size > image_size) {
        size = image_size - offset;
    }

    uint32_t sector = offset / 512;
    uint16_t chunk_size, chunk_offset;
    size_t done_size = 0;
    while (size > 0) {
        chunk_offset = offset % 512;
        chunk_size = 512 - chunk_offset;
        if (size < chunk_size) {
            chunk_size = size;
        }

        if (sector >= data_start_sector) {
            // data segment
            memset(buf, 0, chunk_size);
        } else if (sector >= res_sectors && sector < root_dir_start_sector) {
            // FAT segment
            // N.B. this is 2 FATs in 1, hence the modulo
            uint32_t fat_sector = (sector - res_sectors) % fat_sectors;
            uint32_t start_cluster = (fat_sector*512 + chunk_offset)
                                     / fat_ent_size;
            uint32_t value = 0;
            for (uint16_t i = 0; i < chunk_size; ) {
                if (start_cluster + i < fat.size()) {
                    fat_ent &fent = fat[start_cluster + i];
                    if (fent.cluster < fent.ent->num_clusters_ - 1) {
                        value = 2 + start_cluster + i/fat_ent_size + 1;
                    } else if (fent.cluster == fent.ent->num_clusters_ - 1) {
                        value = fat32 ? 0x0fffffff : 0xffff;
                    } else {
                        fprintf(stderr, "Warning: fat_ent has cluster beyond file");
                        value = 0;
                    }
                } else {
                    value = 0;
                }
                uint8_t j = 0;
                uint8_t lim = fat_ent_size - i % fat_ent_size;
                // Nasty...
                if (fat32) {
                    uint32_t temp = htole32(value);
                    for (; j < lim && i + j < chunk_size; j++) {
                        buf[i + j] = ((uint8_t *)&temp)[j];
                    }
                } else {
                    uint16_t temp = htole16(value);
                    for (; j < lim && i + j < chunk_size; j++) {
                        buf[i + j] = ((uint8_t *)&temp)[j];
                    }
                }
                i += j; // mustn't forget this
            }
        } else if (sector >= root_dir_start_sector) {
            // root directory segment (FAT16 only)
            memset(buf, 0, chunk_size);
        } else {
            // reserved segment
            if (sector == 0 || sector == 6) {
                // boot sector
                memcpy(buf, &bootsect[chunk_offset], chunk_size);
            } else {
                memset(buf, 0, chunk_size);
            }
        }

        done_size += chunk_size;
        buf += chunk_size;
        offset += chunk_size;
        size -= chunk_size;
        sector++;
    }

    return done_size;
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
    cluster_bytes = cluster_sectors * 512;
    printf("%u sectors / %u bytes per cluster\n",
           cluster_sectors, cluster_bytes);

    res_sectors = fat32 ? 32 : 1;
    root_dir_ents = fat32 ? 0 : 512;

    root_dir_size = 32 * root_dir_ents;
    root_dir_sectors = (uint32_t) ceil(root_dir_size / 512.0);
    free_sectors = num_sectors - res_sectors - root_dir_sectors;
    free_clusters = free_sectors / cluster_sectors;
    fat_ent_size = fat32 ? 4 : 2;
    fat_size = (free_clusters + 2) * fat_ent_size;
    fat_sectors = (uint32_t) ceil(fat_size / 512.0);
    data_sectors = free_sectors - 2 * fat_sectors;
    data_clusters = data_sectors / cluster_sectors;

    root_dir_start_sector = res_sectors + 2 * fat_sectors;
    data_start_sector = root_dir_start_sector + root_dir_sectors;

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

file_ent *build_dir_tree(filesystem::path& file, bool is_root = false)
{
    file_ent *ent;
    if (filesystem::is_directory(file)) {
        dir_ent *dent = new dir_ent;
        ent = dent;
        dent->is_root_ = is_root;
        filesystem::directory_iterator iter(file), end;
        for (; iter != end; ++iter) {
            filesystem::path cpath = iter->path();
            dent->files_.push_back(build_dir_tree(cpath, false));
        }
        uint32_t size = 32 * (dent->files_.size() + (is_root ? 0 : 2));
        double denom = cluster_bytes;
        if (is_root) {
            denom = 512;
        }
        ent->num_clusters_ = (uint32_t) ceil(size / denom);
        //fprintf(stderr, "dir - num_clusters == %u, denom == %g\n", ent->num_clusters_, denom);
    } else {
        ent = new file_ent;
        uintmax_t size = filesystem::file_size(file);
        ent->num_clusters_ = (uint32_t) ceil((double) size / cluster_bytes);
        //fprintf(stderr, "file - num_clusters == %u, denom == %u (file size is %llu)\n", ent->num_clusters_, cluster_bytes, size);
    }
    //fprintf(stderr, "Got %s\n", file.string().c_str());
    ent->path_ = file;
    return ent;
}

void allocate_fat(file_ent *ent) {
    if (fat32 || !ent->is_root()) {
        if (ent->num_clusters_ != 0) {
            ent->start_cluster_ = next_cluster;
            for (uint32_t i = 0; i < ent->num_clusters_; i++) {
                fat.push_back(fat_ent(ent, i));
            }
        } else {
            ent->start_cluster_ = 0; // empty
        }
        next_cluster += ent->num_clusters_;
        //printf("next_cluster is now %u\n", next_cluster);
    } else {
        ent->start_cluster_ = 0; // root dir on FAT16
    }
    dir_ent* dent = dynamic_cast<dir_ent *>(ent);
    if (dent) {
        for (size_t i = 0; i < dent->files_.size(); i++) {
            allocate_fat(dent->files_[i]);
        }
    }
}

dir_ent* dir_tree(filesystem::path& file)
{
    file_ent* ent = build_dir_tree(file, true);
    dir_ent* dent = dynamic_cast<dir_ent *>(ent);
    if (dent) {
        allocate_fat(dent);
    } else {
        fprintf(stderr, "dent == NULL\n");
        delete ent;
    }
    return dent;
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
            image_filename = "/";
            image_filename += optarg;
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

    printf("Building FAT...\n");
    filesystem::path pdirectory = directory;
    root = dir_tree(pdirectory);

    printf("%u out of %u clusters allocated\n", next_cluster-2, data_clusters);
    if (!fat32) {
        printf("%u out of %u root directory sectors used\n",
               root->num_clusters_, root_dir_sectors);
    }

    int result = fuse_main(argc, argv, &rloop_oper, NULL);

    delete root;

    return result;
}
