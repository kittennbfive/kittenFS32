#ifndef __FAT32_CONFIG_H__
#define __FAT32_CONFIG_H__

/*
Configuration file for kittenFS32

FS32_NO_READ == 1 disables f_open(filename 'r')

FS32_NO_WRITE == 1 disables f_open(filename 'w')

FS32_NO_APPEND == 1 disables f_open(filename 'a')

FS32_NO_SEEK_TELL == 1 disables f_seek() and f_tell()

(c) 2021 by kittennbfive

AGPLv3+ and NO WARRANTY!
*/

#define FS32_NO_READ 0

#define FS32_NO_WRITE 0

#define FS32_NO_APPEND 0

#define FS32_NO_SEEK_TELL 0

#if FS32_NO_READ && FS32_NO_WRITE
#error Either read or write must be enabled!
#endif

#endif
