# kittenFS32

*Beware: Current master is version 0.06 with API-change compared to the very first version! Look at tag v0.01 if you need the really old version for compatibility. I recommand migrating to v0.06/current master.*

## What is this?
kittenFS32 is a small (somewhat), simple (i hope) (and restrictive) implementation of Microsoft FAT32 file system for interfacing a (SD-)card with a microcontroller. It was written for and tested on Atmel (now Microchip) AVR but - as it is written in standard-C - can be used on other targets.

## You know FatFS, don't you?
Yes, i do. I originally planned to use [FatFS](http://elm-chan.org/fsw/ff/00index_e.html), but it uses too much program memory and the code is too compact for me to understand and stripdown. I also know [PetitFS](http://elm-chan.org/fsw/ff/00index_p.html) by the same author. It is much smaller but you can't create or extend files which was a requirement for me. So i looked at the (somewhat horrible) FAT specifications and wrote my own implementation.

## **Important limitations**
In order to keep things small and simple this code makes a few **IMPORTANT** assumptions, amongst others:
* While FatFS supports several FAT-variants this code is FAT32 only.
* While FatFS is (as far as i know) endian-independant this code assumes that your compiler and your target are little-endian.
* This code assumes that your SD-card contains a single FAT structure instead of the usual two. This simplifies the code but increases the chance of a catastrophic data loss. See disclaimer and command below for formating an SD-card the right way under Linux.
* Basic support for partitions (MBR primary only) can now optionally be enabled, but you can only work on one partition at the same time.
* This code assumes a sector size of 512 bytes and a single sector per cluster. Again, see below for Linux command.
* This code uses uint32_t for stuff like sectorcount so the maximum size of your card is "limited" to about 4 billion sectors or 2TB.
* This code does not know about sub-directories. Every file needs to be / will be created in the root-directory of your card. This is - of course - due to code size and complexity.
* This code is NOT optimized for speed. E.g. there is no support for multi block read (CMD18). If you need to read/write massive amounts of data with high troughput this is not the code you are looking for.
* This code only supports old-styled 8.3 filenames in UPPERCASE. No support for LFN. No support for Unicode.
* Because of code size this code contains really little sanity checks and other precautions. It is up to you to do things right.

## Features
This code allows you to create a new file for writing or to open an existing file for reading or writing or modifying. Seeking is supported in write-modes. For reading/writing the code gives you an `f_read` and an `f_write` function that are somewhat similar to the standard stuff you know (but not entirely compatible!). The code uses and updates the FSINFO data on the card to not be too slow when creating/extending files. You can get the size of a file and the number of free sectors (and free space by multiplying by 512) on the card/partition. You can list all files on the card. You can *not* delete a file on the card or make it smaller. You can *not* format a card. You can define how many files can be opened simultaneously at compile-time.

## API-Overview
This code provides you with a simple but sufficient (for my needs at least...) API:
```
FS32_status_t f_set_partition(const uint8_t partition);
FS32_status_t f_init(void);
FS32_status_t f_open(uint8_t * const filenr, char const * const filename, const char mode);
FS32_status_t f_close(const uint8_t filenr);
FS32_status_t f_read(const uint8_t filenr, void * ptr, const uint16_t size, const uint16_t n);
FS32_status_t f_write(const uint8_t filenr, void const * ptr, const uint16_t size, const uint16_t n);
FS32_status_t f_seek(const uint8_t filenr, const uint32_t pos);
uint32_t f_tell(const uint8_t filenr);
uint32_t get_free_sectors_count(void);
uint32_t get_file_size(const uint8_t filenr);
FS32_status_t f_ls(const f_ls_callback callback);
```
If you don't need some functionality you can disable it a compile-time. Look at `FS32_config.h`.  
Always check the return code if you call a function!  
If `FS32_NB_FILES_MAX` is set to 1 the argument `filenr` of the public functions is ignored and can be any value.

## Licence and Disclaimer
This code is licenced under the AGPLv3+ and provided WITHOUT ANY WARRANTY! It is (still) an early and really sparse tested version. **DO NOT USE FOR IMPORTANT OR CRITICAL STUFF. EXPECT LOSS AND/OR CORRUPTION OF DATA!**

## Detailled API description
Please note that except for `STATUS_OK` (which will be always 0) the actual numerical value of a return code can change between versions of the code. Always use the constants defined in `FS32_status_t` (in file `FS32.h`).

### f_set_partition
#### Overview
This function is only needed if your card has partitions. It must be called BEFORE `f_init` but AFTER the initialization of the card with your own code. *To use this function you must edit `FS32_config.h` and set `FS32_PARTITION_SUPPORT` to `1`*.
#### Parameters
* partition: The number of the partition you want to use. Only partitions of type "MBR primary" are supported. Counting begins at 0 and by specification maximum 4 (primary) partitions can exist on the card, so this parameter must be between 0 and 3.
#### Return Codes
* `STATUS_OK`: Success.
* `SET_PART_INVALID_NUMBER`: The argument is bigger than 3.
* `SET_PART_INVALID_BOOT_SIG`: The boot signature is invalid. Does your card have a Master Boot Record with a partition table or it is directly formatted? Then set `FS32_PARTITION_SUPPORT` to `0` and do not use this function (it will be unavailable anyway).
* `SET_PART_UNKNOWN_PART_TYPE`: The partition you specified has a type value other than for FAT32. Only FAT32 is supported by this code obviously...
* `SET_PART_NO_VALID_PART`: The partition you specified does not exist (number of sectors is 0).

### f_init
#### Overview
This function does some really basic checking of the FS of your card and initializes some internal stuff. It must be called BEFORE using any other function (except `f_set_partition`) but AFTER your card has been initialized by your own code. As i said there is no check for this, be careful!
#### Parameters
None
#### Return Codes
* `STATUS_OK` (always 0): Everything is fine (as far as the function checked).
* `INIT_INVALID_JUMP`: The very first byte of sector 0 (of the card or the partition) does not contain a valid x86 JMP instruction as it should. Is your card correctly formatted? (see below)
* `INIT_INVALID_BYTES_PER_SEC`: Your card does not use 512 bytes per sector, this is mandatory however.
* `INIT_INVALID_SEC_PER_CLUS`: Your card does not use a single sector per cluster, this is mandatory however.
* `INIT_NOT_FAT32`: It looks like your card is not formatted with FAT*32*. (BPB_TotSec16 and/or BPB_FATSz16 is not equal to zero)
* `INIT_MULTIPLE_FAT`: Your card has at least 2 FAT, not only one as needed for this code.
* `INIT_INVALID_FSINFO`: The FSINFO-block in sector 1 does not exist / does not have a valid signature.

### f_open
#### Overview
This function opens a file. It supports four simple modes:
* To read an existing file specify `'r'`. You will get an error if the file does not exist.
* To create a new file and write data to it specify `'w'`. If the file does already exist you will get an error back.
* To append data to the end of an existing(!) file specify `'a'`. The file needs to exist on the card already, if not create it using `'w'`.
* To modify (read/write) an existing(!) file (possibly extending it) specify `'m'`. The file needs to exist on the card already, if not create it using `'w'`.
#### Parameters
* A pointer(!) to an `uint8_t` to save under which internal number the file can be accessed - ignored in single file mode (can be `NULL` in this case).
* filename: 8.3 (8 chars for name and 3 for extension maximum) and uppercase only, this is NOT checked!
* mode: See above. Notice this is a char, not a string as for the traditional `fopen()`.
#### Return Codes
* `STATUS_OK`: Success.
* `OPEN_FILE_ALREADY_OPEN`: You tried to open an already open file.
* `OPEN_NO_FREE_SLOT`: You have reached the maximum number of simultaneous open files. See `FS32_NB_FILES_MAX` in `FS32_config.h`.
* `OPEN_FILE_NOT_FOUND`: The file you want to read from does not exist.
* `OPEN_FILE_ALREADY_EXISTS`: The file you want to create does already exist. You cannot overwrite or delete it.
* `OPEN_NO_MORE_SPACE`: The card is full.
* `OPEN_APPEND_SEEK_ERR`: Seeking to the end of the file for appending data was not successful.
* `OPEN_INVALID_MODE`: unknown mode, only 'r', 'w', 'a' and 'm' are valid (assuming you did not disable stuff in `FS32_config.h`).

### f_close
#### Overview
This function closes the current file so you can open another. It is **really important** as a newly created file is really only created once you call `f_close()`, so don't forget!
#### Parameters
* filenr: The internal number of the opened file as written by `f_open()`.
#### Return Codes
* `STATUS_OK`: Success.
* `CLOSE_NO_OPEN_FILE`: There is no open file. This is harmless on its own but means you probably have a bug in your code somewhere.
* `CLOSE_CREATE_DIR_ENTRY_FAILED`: The file you created with f_open(..., 'w') could not be created, creating the directory entry failed. *This is bad.* You should assume there is something really wrong and use a PC to check the filesystem on the card (or format it again).

### f_read
#### Overview
Read data from a file opened for reading.
#### Parameters
* filenr: The internal number of the opened file as written by `f_open()`.
The rest is the same as for standard `fread()`.
#### Return Codes
* `STATUS_OK`: Success.
* `READ_FAILED`: The code encountered an EndOfChainMarker. Either you asked for more bytes than the file contains or something is broken inside the FAT.

### f_write
#### Overview
Write data to a file opened for writing (newly created) or appending or modifying.
#### Parameters
* filenr: The internal number of the opened file as written by `f_open()`.
The rest is the same as for standard `fwrite()`.
#### Return Codes
* `STATUS_OK`: Success.
* `WRITE_NO_OPEN_FILE`: No file opened.
* `WRITE_FILE_READ_ONLY`: You can't write to a file opened with 'r'.
* `WRITE_NO_MORE_SPACE`: Card is full. The data has not been entirely written.

### f_seek
#### Overview
Seek to position inside file opened for reading.
#### Parameters
* filenr: The internal number of the opened file as written by `f_open()`.
* New file position or constant `FS_SEEK_END` to go to the end of the file for appending data.
#### Return Codes
* `STATUS_OK`: Success.
* `SEEK_CANT_SEEK_IN_THIS_MODE`: Seeking is only possible for files opened with mode 'w', 'a' or 'm', but not 'r'.
* `SEEK_INVALID_POS`: The position you specified is bigger than the size of the file.

### f_tell
#### Overview
Return current position in file.
#### Parameters
* filenr: The internal number of the opened file as written by `f_open()`.
#### Returns
Current file position. Sanity check this before further use.

### get_free_sectors_count
#### Overview
Get the number of free sectors left on the card (from the FSINFO structure).
#### Parameters
None
#### Returns
Number of free sectors, multiply by 512 to get free space in bytes (or divide by 2 to get free space in kilobytes).

### get_file_size
#### Overview
Get the size of an open file.
#### Parameters
* filenr: The internal number of the opened file as written by `f_open()`.
#### Returns
File size in bytes. Sanity check this before further use.

### f_ls
#### Overview
List the content of the card using a callback for each file. Caution: Directories are NOT supported!
#### Parameters
* callback: The function (returning `void`) to be called for each file. The parameter is of type `char const * const` and contains the filename in 8.3 format and null-terminated. The last call is made with `NULL` as a parameter to signal that we are done.
#### Return Codes
* `STATUS_OK`: Success.
* `LS_LONG_NAME`: Encountered a long filename, this is unsupported!

## What you need to provide / low-level-API
This code needs the following functions that you must provide:
```
void sd_read_sector(const uint32_t sector, uint8_t * const data);
void sd_write_sector(const uint32_t sector, uint8_t const * const data);
uint16_t rtc_get_encoded_date(void);
uint16_t rtc_get_encoded_time(void);
```
The first two should be pretty much self-explanatory. Note that a sector is always 512 bytes and always entirely read or written. **Note that your code has to deal by itself with IO-Errors**, probably by switching on some LED and/or printing something over serial or on an attached LCD and stop using the SD-card until a human steps in to fix the mess. I could have make the low-level functions return a status code but all those checks increase code size by quite a lot. I agree that this is not a great situation but i don't know how to fix this without increasing the code size (ideas welcome).  
New: I published an implementation of a suitable low-level SD-card interface, see https://github.com/kittennbfive/avr-sd-interface  
  
The RTC-functions are needed to specify a valid timestamp when creating a new file. They are not used elsewhere. You can replace them with a dummy if you don't care about the timestamps.
### Format of encoded_date
```
uint8_t year; //offset starting at 1980, so 2021 is 41
uint8_t month, day; //starting at 1
//fill those variables with correct data here
return ((uint16_t)(year&0x7F)<<9)|((month&0x0F)<<5)|((day&0x1F)<<0);
```
### Format of encoded_time
```
uint8_t h,m,s;
//fill those variables with correct data here
return ((uint16_t)(h&0x1F)<<11)|((m&0x3F)<<5)|(((s/2)&0x1F)<<0);
```
Notice that seconds are divided by two. The more fine granularity timestamp that FAT32 provides (something like 10ms resolution) is not implemented (written as 0).

## Quick howto for formatting and using your SD-card with this code
The following part is for Linux and Linux only. I can't and won't give any advice or help for Windows as i am not familiar with it. Please ask a local expert or your favourite search engine.
### Formatting the card directly (without partitions)
**MAKE SURE YOU SPECIFY THE RIGHT DEVICE! RISK OF CATASTROPHIC LOSS OF DATA!**
`sudo mkfs.fat -F 32 -s 1 -f 1 /dev/sdX`
### Partitionning and formatting the card
**MAKE SURE YOU SPECIFY THE RIGHT DEVICE! RISK OF CATASTROPHIC LOSS OF DATA!**
This is just an example to be adjusted for your needs. In this example we create 2 partitions of (approx.) equal size and format the first one with FAT32.
`sudo parted --script /dev/sdX mklabel msdos mkpart primary fat32 0 50% mkpart primary fat32 50% 100%`
Note that for the following command we specify a *partition* (0) instead of the entire device!
`sudo mkfs.fat -F 32 -s 1 -f 1 /dev/sdX0`
### See details of FAT-system and check for errors without writing anything
`sudo dosfsck -v -n /dev/sdX`

## FAQ / Other stuff
### There is no f_printf!?!
This is correct. Use `sprintf()` from your standard library with a buffer and `f_write()`.

### You say "developped for AVR" - why didn't you make this compatible with avr-libc  `stdio.h` facilities?
In a nutshell: The API of [avr-libc](https://www.nongnu.org/avr-libc/user-manual/group__avr__stdio.html) is only suitable for stuff like UART because you get only a single byte each time. We could use a buffer but RAM is precious on a small AVR and it would still be horribly inefficient. Also there is no way to specify a custom callback when fclose() is called, but this would be needed to finalize any pending operations like actually writing the buffer to the SD-card. You can always hack avr-libc but this is a yack i didn't want to shave.

## Codesize
Example using avr-gcc (GCC) 5.4.0. Note that you have to add the lower layer code to interface your SD-card and of course your application code!
Notice that i did *not* include `-fshort-enums` as it changes the default ABI. You can probably use it (unless you have huge enums) but you must specify it *for every code file* that is compiled into your project.
*Notice: I removed some gcc options that in most cases make the code actually bigger. If you are really low on FLASH you might want to experiment with all the available optimisations.*
### with all options enabled and a maximum of two simultaneously open files:
```
kittenFS32$ avr-gcc -c FS32.c -Wall -Wextra -Werror -Os -mcall-prologues -mmcu=atmega328p -DF_CPU=10000000 -o avr.elf && avr-size avr.elf
   text	   data	    bss	    dec	    hex	filename
   5030	      0	    637	   5667	   1623	avr.elf
```
### everything except partition-support with maximum two simultaneously open files:
```
kittenFS32$ avr-gcc -c FS32.c -Wall -Wextra -Werror -Os -mcall-prologues -mmcu=atmega328p -DF_CPU=10000000 -o avr.elf && avr-size avr.elf
   text	   data	    bss	    dec	    hex	filename
   4648	      0	    633	   5281	   14a1	avr.elf
```
### with all options enabled but single file configuration:
```
kittenFS32$ avr-gcc -c FS32.c -Wall -Wextra -Werror -Os -mcall-prologues -mmcu=atmega328p -DF_CPU=10000000 -o avr.elf && avr-size avr.elf
   text	   data	    bss	    dec	    hex	filename
   4658	      0	    590	   5248	   1480	avr.elf
```
### read-only configuration without partition-support, without seek, without ls, single file:
```
kittenFS32$ avr-gcc -c FS32.c -Wall -Wextra -Werror -Os -mcall-prologues -mmcu=atmega328p -DF_CPU=10000000 -o avr.elf && avr-size avr.elf
   text	   data	    bss	    dec	    hex	filename
   1706	      0	    577	   2283	    8eb	avr.elf
```
### write-only, no partition-support, no append, no modify, no seek, no ls, single file:
```
kittenFS32$ avr-gcc -c FS32.c -Wall -Wextra -Werror -Os -mcall-prologues -mmcu=atmega328p -DF_CPU=10000000 -o avr.elf && avr-size avr.elf
   text	   data	    bss	    dec	    hex	filename
   3100	      0	    586	   3686	    e66	avr.elf
```

## Benchmark
TODO - but as i said, this code is not optimized for speed!
