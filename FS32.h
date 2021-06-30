#ifndef __FAT32_H__
#define __FAT32_H__

/*
Public header for kittenFS32

Include this into your project.

(c) 2021 by kittennbfive

AGPLv3+ and NO WARRANTY!
*/

#include <stdint.h>

typedef enum
{
	STATUS_OK=0,
	
	INIT_INVALID_JUMP,
	INIT_INVALID_BYTES_PER_SEC,
	INIT_INVALID_SEC_PER_CLUS,
	INIT_NOT_FAT32,
	INIT_MULTIPLE_FAT,
	INIT_INVALID_FSINFO,
	
	OPEN_OTHER_FILE_IS_OPEN,
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
	
	SEEK_CANT_SEEK_ON_WRITABLE,
	SEEK_INVALID_POS,
	
} FS32_status_t;

FS32_status_t f_init(void);
FS32_status_t f_open(char const * const filename, const char mode);
FS32_status_t f_close(void);
FS32_status_t f_read(void * ptr, const uint16_t size, const uint16_t n);
FS32_status_t f_write(void const * ptr, const uint16_t size, const uint16_t n);
FS32_status_t f_seek(const uint32_t pos);
uint32_t f_tell(void);
uint32_t get_free_sectors_count(void);
uint32_t get_file_size(void);
#endif
