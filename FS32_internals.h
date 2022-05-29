#ifndef __FS32_INTERNALS_H__
#define __FS32_INTERNALS_H__
#include <stdint.h>

#include "FS32_config.h"

/*
Internal stuff of kittenFS32

Unless you are chasing bugs or want to hack you don't need to look here.

(c) 2021-2022 by kittennbfive

AGPLv3+ and NO WARRANTY!

version 0.06 - 17.04.22
*/

//Master Boot Record and Partition Table

typedef struct __attribute__((__packed__))
{
	uint8_t Status; //ignored
	uint8_t StartSectorCHS[3]; //ignored
	uint8_t PartitionType;
	uint8_t LastSectorCHS[3]; //ignored
	uint32_t StartSectorLBA;
	uint32_t NumberOfSectors; //ignored, number of _data_ sectors is read from FAT
} partition_entry_t;

typedef struct __attribute__((__packed__))
{
	uint8_t Bootloader[440]; //ignored
	uint32_t DiskSignature; //ignored
	uint16_t CopyProtectionInfo; //ignored
	partition_entry_t Partitions[4];
	uint16_t BootSignature;
} master_boot_record_t;

//Internal FAT32 data structures

typedef struct __attribute__((__packed__))
{
	uint8_t BS_jmpBoot[3];
	char BS_OEMName[8];
	uint16_t BPB_BytsPerSec;
	uint8_t BPB_SecPerClus;
	uint16_t BPB_RsvdSecCnt;
	uint8_t BPB_NumFATs;
	uint16_t BPB_RootEntCnt;
	uint16_t BPB_TotSec16;
	uint8_t BPB_Media;
	uint16_t BPB_FATSz16;
	uint16_t BPB_SecPerTrk;
	uint16_t BPB_NumHeads;
	uint32_t BPB_HiddSec;
	uint32_t BPB_TotSec32;
	uint32_t BPB_FATSz32;
	uint16_t BPB_ExtFlags;
	uint16_t BPB_FSVer;
	uint32_t BPB_RootClus;
	uint16_t BPB_FSInfo;
	uint16_t BPB_BkBootSec;
	uint8_t BPB_Reserved[12];
	uint8_t BS_DrvNum;
	uint8_t BS_Reserved1;
	uint8_t BS_BootSig;
	uint32_t BS_VolID;
	char BS_VolLab[11];
	char BS_FilSysType[8];
} fat32_header_t;

typedef struct __attribute__((__packed__))
{
	uint32_t FSI_LeadSig;
	uint8_t FSI_Reserved1[480];
	uint32_t FSI_StrucSig;
	uint32_t FSI_Free_Count;
// NO!	uint32_t FSI_Nxt_Free; //cluster number at which there are free clusters, invalid/unknown if 0xFFFFFFFF
	uint32_t FSI_Last_Allocated; //number of last allocated cluster, at least for mkfs.fat, see https://github.com/dosfstools/dosfstools/blob/master/src/mkfs.fat.c#L192
	uint8_t FSI_Reserved2[12];
	uint32_t FSI_TrailSig;
} fat32_fsinfo_t;

#define FSI_LEADSIG 0x41615252
#define FSI_STRUCSIG 0x61417272
#define FSI_TRAILSIG 0xAA550000

typedef uint32_t fat32_entry_t;

typedef struct __attribute__((__packed__))
{
	//single parameter DIR_Name[11] in specs, separated here
	char DIR_Name[8]; //UPPERCASE ONLY
	char DIR_Ext[3]; //UPPERCASE ONLY
	uint8_t DIR_Attr;
	uint8_t DIR_NTRes;
	uint8_t DIR_CrtTimeTenth;
	uint8_t empty_space[6];
	uint16_t DIR_FstClusHI;
	uint16_t DIR_WrtTime;
	uint16_t DIR_WrtDate;
	uint16_t DIR_FstClusLO;
	uint32_t DIR_FileSize;
} fat32_directory_entry_t;

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME (ATTR_READ_ONLY|ATTR_HIDDEN|ATTR_SYSTEM|ATTR_VOLUME_ID)

#define DIR_ENTRY_FREE 0xE5
#define DIR_ENTRY_FREE_NO_MORE_DIR 0x00

//Internal data structures

typedef struct
{
	bool noFreeSpace;
	uint32_t LogicalSector;
	uint32_t FAT_SectorNumber;
	uint8_t FAT_EntryIndex;
} pos_fat32_entry_t;

typedef struct
{	
	bool FileFound;
	bool isInUse;
	bool isNewFile; //create new file and write to it, seeking not allowed
	bool OpenendForModify; //read or write existing file, seeking allowed
	bool OpenendForAppending; //append to end of existing file, seeking not allowed
	bool OpenedForReading; //read existing file, seeking allowed
	
	char Name[8+1+3+1];
	
	uint32_t FirstLogicalSector;
	
	uint32_t LogicalSector;
	uint32_t PosInLogicalSector;
	
	uint32_t PosInFile;
	uint32_t FileSize;
	
	uint32_t SectorDirEntry;
	uint32_t IndexDirEntry;
} file_t;

//Some sanity checks on the configuration options and some internal defines depending on those options

#if FS32_NO_READ && FS32_NO_WRITE && FS32_NO_APPEND
#error Either read or write or append must be enabled!
#endif

#if !FS32_NB_FILES_MAX
#error You need at least one open file, dont you?
#endif

#if FS32_NO_WRITE && !FS32_NO_MODIFY
#error To modify files you need write-functionality enabled. 
#endif

#if (!FS32_NO_APPEND || !FS32_NO_MODIFY) && FS32_NO_SEEK_TELL
#error To modify files or append to files you need f_seek enabled.
#endif

#if FS32_NB_FILES_MAX>1
#define FIRST_ARG_FILENR const uint8_t filenr,
#define ONLY_ARG_FILENR const uint8_t filenr
#define FILENR_ARR_INDEX filenr
#define FILENR_PTR_ARR_INDEX *filenr
#define FILENR_ONLY_FUNC_ARG filenr
#define FILENR_FIRST_FUNC_ARG filenr,
#define FILENR_PTR_FUNC_ARG *filenr,
#else
#define SINGLE_FILE_CONFIG 1
#define FIRST_ARG_FILENR
#define ONLY_ARG_FILENR void
#define FILENR_ARR_INDEX 0
#define FILENR_PTR_ARR_INDEX 0
#define FILENR_ONLY_FUNC_ARG
#define FILENR_FIRST_FUNC_ARG
#define FILENR_PTR_FUNC_ARG
#endif

#if FS32_PARTITION_SUPPORT
#define SD_READ_SECTOR(Sector, Buffer) sd_read_sector((StartOfPartition+Sector), Buffer)
#define SD_WRITE_SECTOR(Sector, Buffer) sd_write_sector((StartOfPartition+Sector), Buffer)
#else
#define SD_READ_SECTOR(Sector, Buffer) sd_read_sector(Sector, Buffer)
#define SD_WRITE_SECTOR(Sector, Buffer) sd_write_sector(Sector, Buffer)
#endif

//You need to provide these functions:
void sd_read_sector(const uint32_t sector, uint8_t * const data);
void sd_write_sector(const uint32_t sector, uint8_t const * const data);
uint16_t rtc_get_encoded_date(void);
uint16_t rtc_get_encoded_time(void);

#endif
