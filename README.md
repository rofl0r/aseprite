why is there no issue tracker here ? anyway:

Linking CXX executable grid_ui_unittest
[ 74%] Building CXX object src/CMakeFiles/aseprite-library.dir/commands/filters/cmd_replace_color.cpp.o
../lib/liblibpng.a(png.c.o): In function `png_reset_crc':
aseprite/third_party/libpng/png.c:111: undefined reference to `crc32'
../lib/liblibpng.a(png.c.o): In function `png_calculate_crc':
aseprite/third_party/libpng/png.c:152: undefined reference to `crc32'
../lib/liblibpng.a(png.c.o): In function `png_reset_zstream':
aseprite/third_party/libpng/png.c:757: undefined reference to `inflateReset'
../lib/liblibpng.a(pngread.c.o): In function `png_create_read_struct_2':
aseprite/third_party/libpng/pngread.c:123: undefined reference to `inflateInit_'
../lib/liblibpng.a(pngread.c.o): In function `png_read_row':
aseprite/third_party/libpng/pngread.c:561: undefined reference to `inflate'
../lib/liblibpng.a(pngread.c.o): In function `png_read_destroy':
aseprite/third_party/libpng/pngread.c:1067: undefined reference to `inflateEnd'
../lib/liblibpng.a(pngrutil.c.o): In function `png_inflate':
aseprite/third_party/libpng/pngrutil.c:333: undefined reference to `inflate'
aseprite/third_party/libpng/pngrutil.c:362: undefined reference to `inflateReset'
../lib/liblibpng.a(pngrutil.c.o): In function `png_read_finish_row':

this happens when using the default configuration, i.e. build everything static except libc (on linux).
the problem here is that libpng depends on libz, but static libs do not pull in their deps automatically, so it's needed to add -lz to the commandline when linking against static libpng

since i dont know how cmake works, someone else needs to figure how to fix this...
