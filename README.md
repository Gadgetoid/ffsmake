# ffsmake 💾 -> 🐍

A utility for building MicroPython-compatible FAT filesystems.

# Building

```
git submodule update --init
mkdir build
cd build
cmake ..
```

macOS users will need to `brew install argp-standalone` and might need to update the paths in `CMakeLists.txt`.

# Usage

```
Usage: ffsmake [OPTION...] [FILENAME]...
FatFS filesystem builder for MicroPython.

  -c, --sector-count[=NUMBER]   Sector count (default 512, 2MB disk).
  -d, --directory=FILE       Input directory.
  -D, --debug                Very shouty output.
  -f, --force                Force overwrite an existing file.
  -o, --output=FILE          Output file for FatFS filesystem.
  -q, --quiet                No shouty output.
  -s, --sector-size[=NUMBER] Sector size (default 4096 bytes).
  -t, --no-truncate          Do not truncate output file to used capacity.
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version

Mandatory or optional arguments to long options are also mandatory or optional
for any corresponding short options.
```

For example you might build with `ffsmake -d /directory/of/files -o filesystem.bin -c 512`

For a 2MB file on a 4MB flash, load with: `picotool load -o 0x10200000 filesystem.bin`
