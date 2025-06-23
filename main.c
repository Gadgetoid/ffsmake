#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ftw.h>
#include <unistd.h>
#include <sys/stat.h>
#include <argp.h>
#include <stdbool.h>

#include "ff.h"
#include "diskio.h"
#include "ffconf-micropython.h"

#define DEFAULT_SECTOR_SIZE 4096
#define DEFAULT_SECTOR_COUNT 512
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define MB 1024.0f / 1024.0f

#define FF_DEBUG(...) args.debug ? printf(__VA_ARGS__) : 0
#define DEBUG(...) args.debug ? printf(__VA_ARGS__) : 0
#define VERBOSE(...) args.verbose ? printf(__VA_ARGS__) : 0

FATFS fs;
FIL fil;
FRESULT fr;

const char *argp_program_version = "v0.0.2";
const char *argp_program_bug_address = "<ffsmake@gadgetoid.com>";
static char doc[] = "FatFS filesystem builder for MicroPython.";
static char args_doc[] = "[FILENAME]...";
static struct argp_option options[] = {
    { "directory", 'd', "FILE", 0, "Input directory."},
    { "output", 'o', "FILE", 0, "Output file for FatFS filesystem."},
    { "sector-size", 's', "NUMBER", OPTION_ARG_OPTIONAL, "Sector size (default 4096 bytes)."},
    { "sector-count", 'c', "NUMBER", OPTION_ARG_OPTIONAL, "Sector count (default 512, 2MB disk)."},
    { "no-truncate", 't', 0, 0, "Do not truncate output file to used capacity."},
    { "force", 'f', 0, 0, "Force overwrite an existing file."},
    { "quiet", 'q', 0, 0, "No shouty output."},
    { "debug", 'D', 0, 0, "Very shouty output."},
    { 0 }
};

struct arguments
{
  char *args[2];                // Positional args
  char *input_directory;
  char *output_file;
  bool truncate;
  bool force;
  bool verbose;
  bool debug;
  DWORD sector_count;
  WORD sector_size;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    char *end;
    switch (key) {
    case 'c':
        arguments->sector_count = (DWORD)strtol(arg, &end, 10);
        if(arg == end) {
            argp_failure(state, 1, 0, "invalid value given for sector count.");
        }
        break;
    case 's':
        arguments->sector_size = (WORD)strtol(arg, &end, 10);
        if(arg == end) {
            argp_failure(state, 1, 0, "invalid value given for sector size.");
        }
        break;
    case 'd': arguments->input_directory = arg; break;
    case 'o': arguments->output_file = arg; break;
    case 't': arguments->truncate = false; break;
    case 'f': arguments->force = true; break;
    case 'q': arguments->verbose = false; break;
    case 'D': arguments->debug = true; break;
    case ARGP_KEY_ARG:
        arguments->args[state->arg_num] = arg;
        return 0;
    case ARGP_KEY_END:
        if (arguments->input_directory == NULL || arguments->output_file == NULL){
            argp_error(state, "requires both -d and -o.");
        }
        if (arguments->sector_count == 0 || arguments->sector_size == 0) {
            argp_failure(state, 1, 0, "sector size and/or count must be non-zero.");
        }
        return 0;
    default: return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc, 0, 0, 0 };
struct arguments args;

struct _bdev_t {
    DWORD sector_count;
    WORD sector_size;
    uint8_t *blocks;
    size_t used_bytes;
};

struct _bdev_t mkdisk(DWORD sector_size, DWORD sector_count) {
    DEBUG("-- mkdisk(%u, %u) -> %d bytes\n", sector_size, sector_count, sector_size * sector_count);
    struct _bdev_t bdev = {
        sector_count,
        sector_size,
        malloc(sector_size * sector_count),
        0
    };
    if(bdev.blocks == NULL) {
        DEBUG("mkdisk malloc failed\n");
        bdev.sector_count = 0;
        bdev.sector_size = 0;
    }
    return bdev;
}

DRESULT disk_read(void *bdev_in, BYTE *buff, DWORD sector, UINT count) {
    struct _bdev_t *bdev = bdev_in;
    FF_DEBUG("-- disk_read(%u, %u)\n", sector, count);
    memcpy(buff, bdev->blocks + sector * bdev->sector_size, count * bdev->sector_size);
    return RES_OK;
}

DRESULT disk_write(void *bdev_in, const BYTE *buff, DWORD sector, UINT count) {
    struct _bdev_t *bdev = bdev_in;
    FF_DEBUG("-- disk_write(%u, %u)\n", sector, count);
    memcpy(bdev->blocks + sector * bdev->sector_size, buff, count * bdev->sector_size);
    bdev->used_bytes = MAX(bdev->used_bytes, (sector + count) * bdev->sector_size);
    return RES_OK;
}

DRESULT disk_ioctl (void *bdev_in, BYTE cmd, void* buff) {
    FF_DEBUG("-- disk_ioctl(%d) -> ", cmd);
    struct _bdev_t *bdev = bdev_in;

    switch(cmd) {
        case CTRL_SYNC:           // 0   /* Complete pending write process (needed at FF_FS_READONLY == 0) */
            FF_DEBUG("CTRL_SYNC\n");
            break;
        case GET_SECTOR_COUNT:    // 1   /* Get media size (needed at FF_USE_MKFS == 1) */
            FF_DEBUG("GET_SECTOR_COUNT (%u)\n", bdev->sector_count);
            *((DWORD*)buff) = bdev->sector_count;
            break;
        case GET_SECTOR_SIZE:     // 2   /* Get sector size (needed at FF_MAX_SS != FF_MIN_SS) */
            FF_DEBUG("GET_SECTOR_SIZE (%d)\n", bdev->sector_size);
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
    FF_DEBUG("-- get_fattime()\n");
    return 0;
}

int ends_with(const char *path, const char *suffix) {
    size_t len = strlen(path);
    size_t slen = strlen(suffix);

    return len >= slen && !memcmp(path + len - slen, suffix, slen);
}

int copy_item(const char *filepath, const struct stat *info, const int typeflag, struct FTW *pathinfo)
{
    size_t read;  // Bytes read by system fread
    UINT written; // Bytes written by FatFS
    char targetpath[FF_MAX_LFN];
    strncpy(targetpath, filepath + strlen(args.input_directory), sizeof(targetpath));

    DEBUG("-- copy_item(%s -> %s, %d)\n", filepath, targetpath, typeflag);
    if(typeflag == FTW_D) { // directory
        if(strlen(targetpath)) {
            f_mkdir(&fs, targetpath);
            if(args.verbose) printf("- mkdir %s\n", targetpath);
        }
        return 0;
    }
    if(typeflag == FTW_F) {
        if(ends_with(targetpath, ".DS_Store")) {
            if(args.verbose) printf("- skipping %s\n", targetpath);
            return 0;
        }
        if(args.verbose) printf("- copy %s\n", targetpath);
        struct stat st;
        stat(filepath, &st);
        size_t size = st.st_size;
        DEBUG("-- File size: %lu\n", size);

        FILE *f = fopen(filepath, "r");
        if(f == NULL) {
            fprintf(stderr, "\n⚠️    Failed to open %s for reading!\n", filepath);
            exit(1);
        }

        fr = f_open(&fs, &fil, targetpath, FA_WRITE | FA_CREATE_NEW);
        if(fr != F_OK) {
            fprintf(stderr, "\n⚠️    Failed to open %s for writing!\n", targetpath);
            exit(1);
        }

        while(1) {
            char copy_buffer[1024];
            written = 0;
            read = fread(copy_buffer, 1, sizeof(copy_buffer), f);
            if(read == 0) {
                break;
            }
            fr = f_write(&fil, copy_buffer, read, &written);
            if((UINT)read != written) {
                fprintf(stderr, "\n⚠️    Write error copying %s (disk probably full!)\n", targetpath);
                exit(1);
                return 1;
            }
        }
        f_close(&fil);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    args.verbose = true;
    args.force = false;
    args.debug = false;
    args.truncate = true;
    args.sector_count = DEFAULT_SECTOR_COUNT;
    args.sector_size = DEFAULT_SECTOR_SIZE;
    argp_parse(&argp, argc, argv, 0, 0, &args);

    VERBOSE("ffsmake 💾 -> 🐍\n");

    if (access(args.output_file, F_OK) == 0 && !args.force) {
        fprintf(stderr, "⚠️    Refusing to overwrite %s\n", args.output_file);
        exit(1);
    }

    VERBOSE("\nAllocating %.2fMB disk...\n", (args.sector_count * args.sector_size) / MB);
    struct _bdev_t ramdisk = mkdisk(args.sector_size, args.sector_count);
    if(ramdisk.sector_count == 0) {
        VERBOSE("- Failed!\n");
        if (!args.verbose) fprintf(stderr, "⚠️    Failed to allocate disk!");
        return 1;
    }
    fs.drv = &ramdisk;
    VERBOSE("- OK ✨\n");

    VERBOSE("\nCreating filesystem...\n");
    uint8_t working_buf[FF_MAX_SS];
    fr = f_mkfs(&fs, FM_FAT | FM_SFD, 0, working_buf, sizeof(working_buf));
    if (fr != FR_OK) {
        VERBOSE("- Error %d\n", fr);
        if (!args.verbose) fprintf(stderr, "⚠️    Failed to create filesystem!");
        return 1;
    }
    VERBOSE("- OK ✨\n");

    VERBOSE("\nCopying files...\n");
    VERBOSE("- source directory: %s\n", args.input_directory);
    nftw(args.input_directory, copy_item, 10, FTW_PHYS);
    VERBOSE("- OK ✨\n");


    VERBOSE("\nWriting FatFS binary...\n");
    VERBOSE("- destination file: %s\n", args.output_file);
    FILE *native_file = fopen(args.output_file, "wb");
    size_t output_size = args.truncate ? ramdisk.used_bytes : (args.sector_count * args.sector_size);
    fwrite(ramdisk.blocks, 1, output_size, native_file);
    fclose(native_file);

    VERBOSE("- written: %.2f MB (of %.2f MB FatFS)\n", output_size / MB, args.sector_count * args.sector_size / MB);
    VERBOSE("- DONE! 🎉\n");
    return 0;
}