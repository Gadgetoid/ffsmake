#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ftw.h>
#include <unistd.h>
#include <sys/stat.h>

#include "ff.h"
#include "diskio.h"
#include "ffconf-micropython.h"

#define SECTOR_SIZE 4096
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define FF_DEBUG(...) //printf
#define DEBUG(...)

struct _bdev_t {
    size_t sector_count, sector_size;
    uint8_t *blocks;
    size_t used_bytes;
};

struct _bdev_t mkdisk(DWORD sector_size, DWORD sector_count) {
    DEBUG("mkdisk(%u, %u) -> %d bytes\n", sector_size, sector_count, sector_size * sector_count);
    struct _bdev_t bdev = {
        sector_count,
        sector_size,
        malloc(sector_size * sector_count),
        0
    };
    if(bdev.blocks == NULL) {
        printf("mkdisk malloc failed\n");
    }
    return bdev;
}

DRESULT disk_read(void *bdev_in, BYTE *buff, DWORD sector, UINT count) {
    struct _bdev_t *bdev = bdev_in;
    FF_DEBUG("disk_read(%u, %u)\n", sector, count);
    memcpy(buff, bdev->blocks + sector * bdev->sector_size, count * bdev->sector_size);
    return RES_OK;
}

DRESULT disk_write(void *bdev_in, const BYTE *buff, DWORD sector, UINT count) {
    struct _bdev_t *bdev = bdev_in;
    FF_DEBUG("disk_write(%u, %u)\n", sector, count);
    memcpy(bdev->blocks + sector * bdev->sector_size, buff, count * bdev->sector_size);
    bdev->used_bytes = MAX(bdev->used_bytes, (sector + count) * bdev->sector_size);
    return RES_OK;
}

DRESULT disk_ioctl (void *bdev_in, BYTE cmd, void* buff) {
    FF_DEBUG("disk_ioctl(%d) -> ", cmd);
    struct _bdev_t *bdev = bdev_in;
    FF_DEBUG("count: %d size: %d\n", bdev->sector_count, bdev->sector_size);

    switch(cmd) {
        case CTRL_SYNC:           // 0   /* Complete pending write process (needed at FF_FS_READONLY == 0) */
            FF_DEBUG("CTRL_SYNC\n");
            break;
        case GET_SECTOR_COUNT:    // 1   /* Get media size (needed at FF_USE_MKFS == 1) */
            FF_DEBUG("GET_SECTOR_COUNT (%u)\n", bdev->sector_count);
            *((DWORD*)buff) = bdev->sector_count;
            break;
        case GET_SECTOR_SIZE:     // 2   /* Get sector size (needed at FF_MAX_SS != FF_MIN_SS) */
            FF_DEBUG("GET_SECTOR_SIZE (%u)\n", bdev->sector_size);
            *((WORD*)buff) = bdev->sector_size;
            break;
        case GET_BLOCK_SIZE:      // 3   /* Get erase block size (needed at FF_USE_MKFS == 1) */
            FF_DEBUG("GET_BLOCK_SIZE (%d)\n", 1);
            *((DWORD*)buff) = 1;
            break;
        case CTRL_TRIM:           // 4   /* Inform device that the data on the block of sectors is no longer used (needed at FF_USE_TRIM == 1) */
            FF_DEBUG("CTRL_TRIM\n");
            break;
        case IOCTL_INIT:          // 5
            FF_DEBUG("IOCTL_INIT\n");
            *((DSTATUS*)buff) = 0;
            break;
        case IOCTL_STATUS:        // 6
            FF_DEBUG("IOCTL_STATUS\n");
            *((DSTATUS*)buff) = 0;
            break;
        default:
            FF_DEBUG("UNKNOWN?\n");
            return RES_ERROR;
    }
    return RES_OK;
}

DWORD get_fattime (void) {
    FF_DEBUG("get_fattime\n");
    return 0;
}

FATFS fs;
FIL fil;
FRESULT fr;

char copy_buffer[1024];
char copy_path[8192]; // Ugh
char output_file[8192]; // Also ugh

int copy_item(const char *filepath, const struct stat *info, const int typeflag, struct FTW *pathinfo)
{
    size_t read, written;
    char *targetpath = malloc(1024);
    strcpy(targetpath, filepath + strlen(copy_path));
    DEBUG("copy_item(%s -> %s, %d)\n", filepath, targetpath, typeflag);
    if(typeflag == FTW_D) { // directory 
        f_mkdir(&fs, targetpath);
        printf("- mkdir %s\n", targetpath);
    }
    if(typeflag == FTW_F) {
        printf("- copy %s\n", targetpath);
        struct stat st;
        stat(filepath, &st);
        size_t size = st.st_size;
        DEBUG("File size: %u\n", size);

        FILE *f = fopen(filepath, "r");
        if(f == NULL) {
            DEBUG("Failed to open source file!");
            return 1;
        }

        fr = f_open(&fs, &fil, targetpath, FA_WRITE | FA_CREATE_NEW);
        if(fr != F_OK) {
            DEBUG("Failed to create target file!");
        }

        while(1) {
            written = 0;
            read = fread(copy_buffer, 1, sizeof(copy_buffer), f);
            if(read == 0) {
                break;
            }
            f_write(&fil, copy_buffer, read, &written);
            if(read != written) {
                DEBUG("Write Error!");
                return 1;
            }
        }
        f_close(&fil);

        //char *buf = malloc(size);
        //fread(buf, 1, 1024, f);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    FILE *native_file;
    printf("ffsmake 0.1\n"
    "- MicroPython FatFS filesystem builder.\n"
    "- usage: ffsmake source_dir output_file.bin\n");

    printf("\nAllocating 2MB disk...\n");
    struct _bdev_t ramdisk = mkdisk(SECTOR_SIZE, (2 * 1024 * 1024) / SECTOR_SIZE);
    fs.drv = &ramdisk;
    printf("- OK\n");

    printf("\nCreating filesystem...\n");
    uint8_t working_buf[FF_MAX_SS];
    fr = f_mkfs(&fs, FM_FAT | FM_SFD, 0, working_buf, sizeof(working_buf));
    if (fr != FR_OK) {
        printf("- Error %d\n", fr);
        return 1;
    }
    printf("- OK\n");

    printf("\nCopying files...\n");
    if(argc >= 2) {
        strcpy(copy_path, argv[1]);
    } else {
        strcpy(copy_path, "/Users/gadgetoid/Development/rp2350/august/august/badger2350/examples/badger_os");
    }
    printf("- using path: %s\n", copy_path);
    nftw(copy_path, copy_item, 10, FTW_PHYS);
    printf("- OK\n");


    printf("\nSaving filesystem...\n");
    if(argc >= 3) {
        strcpy(output_file, argv[2]);
    } else {
        strcpy(output_file, "filesystem.bin");
    }
    printf("- using path: %s\n", output_file);
    native_file = fopen(output_file, "wb");
    fwrite(ramdisk.blocks, 1, ramdisk.used_bytes, native_file);
    fclose(native_file);

    printf("- written: %lu bytes\n", ramdisk.used_bytes);
    printf("- DONE!\n");
    return 0;
}