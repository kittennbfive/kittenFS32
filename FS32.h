#ifndef __FAT32_H__
#define __FAT32_H__

/*
Public header for kittenFS32

Include this into your project.

(c) 2021-2022 by kittennbfive

version 0.06 - 17.04.22

AGPLv3+ and NO WARRANTY!
*/

#include <stdint.h>

#define FS_SEEK_END 0xFFFFFFFF

typedef enum
{
	STATUS_OK=0,
	
	SET_PART_INVALID_NUMBER,
	SET_PART_INVALID_BOOT_SIG,
	SET_PART_UNKNOWN_PART_TYPE,
	SET_PART_NO_VALID_PART,
	
	INIT_INVALID_JUMP,
	INIT_INVALID_BYTES_PER_SEC,
	INIT_INVALID_SEC_PER_CLUS,
	INIT_NOT_FAT32,
	INIT_MULTIPLE_FAT,
	INIT_INVALID_FSINFO,
	
	OPEN_FILE_ALREADY_OPEN,
	OPEN_NO_FREE_SLOT,
	OPEN_FILE_NOT_FOUND,
	OPEN_FILE_ALREADY_EXISTS,
	OPEN_NO_MORE_SPACE,
	OPEN_APPEND_SEEK_ERR,
	OPEN_INVALID_MODE,
	
	READ_FAILED,
	
	WRITE_NO_OPEN_FILE,
	WRITE_FILE_READ_ONLY,
	WRITE_NO_MORE_SPACE,
	
	CLOSE_NO_OPEN_FILE,
	CLOSE_CREATE_DIR_ENTRY_FAILED,
	
	SEEK_CANT_SEEK_IN_THIS_MODE,
	SEEK_INVALID_POS,
	
	LS_LONG_NAME,
	
} FS32_status_t;

typedef void (*f_ls_callback)(char const * const file);

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

#endif
