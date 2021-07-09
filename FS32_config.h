#ifndef __FS32_CONFIG_H__
#define __FS32_CONFIG_H__

/*
Configuration file for kittenFS32

FS32_NB_FILES_MAX defines the maximum possible number of *simultaneously* opened files
Set this to 1 if you don't need to access multiple files at the same time to save FLASH and RAM.

FS32_NO_READ == 1 removes f_open('r') (open existing file for reading) and f_read()

FS32_NO_WRITE == 1 removes f_open('w') (create *new* file for writing)

FS32_NO_APPEND == 1 removes f_open('a') (append to *existing* file)

If FS32_NO_WRITE and FS32_NO_APPEND are true f_write() is removed.

FS32_NO_SEEK_TELL == 1 removes f_seek() and f_tell()

FS32_NO_FILE_LISTING == 1 removes f_ls()

(c) 2021 by kittennbfive

AGPLv3+ and NO WARRANTY!
*/

#define FS32_NB_FILES_MAX 2

#define FS32_NO_READ 0

#define FS32_NO_WRITE 0

#define FS32_NO_APPEND 0

#define FS32_NO_SEEK_TELL 0

#define FS32_NO_FILE_LISTING 0

#endif
