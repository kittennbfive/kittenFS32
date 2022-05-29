#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "FS32_internals.h"

#include "FS32.h"

#include "FS32_config.h"

/*
Code for kittenFS32 - a small (somewhat), simple (i hope) (and restrictive) implementation of FAT32 for interfacing a (SD-)card with a microcontroller

licenced under AGPLv3+

NO WARRANTY! USE AT YOUR OWN RISK! BETA-VERSION, EXPECT LOSS AND/OR CORRUPTION OF DATA! READ THE DOC!

(c) 2021-2022 by kittennbfive

version 0.06 - 17.04.22

This code makes a few IMPORTANT assumptions. Please read the manual!
*/

#if FS32_PARTITION_SUPPORT
static uint32_t StartOfPartition;
#endif
static uint16_t RsvdSecCnt; //number of reserved sectors == first sector of FAT
static uint32_t FATSz32; //number of sectors for one FAT
static uint32_t RootSector; //sector where the root dir is
static uint32_t FirstDataSector;
static uint32_t TotalNbOfDataSectors;
static uint8_t FATIndexLastEntry;
static fat32_entry_t EndOfClusterChainMarker;
static uint32_t NbFreeSectors;
static uint32_t LastAllocatedSector;
static file_t OpenFiles[FS32_NB_FILES_MAX];

static uint8_t Buffer[512];

#define IS_EOC_MARKER(value) (value>=0x0FFFFFF8 && value<=0x0FFFFFFF)

#define LOGICAL_SECTOR_TO_PHYSICAL(datasector) ((datasector-2)+FirstDataSector)

#if !FS32_NO_APPEND || !FS32_NO_WRITE
static void update_fsinfo(void)
{
	SD_READ_SECTOR(1, Buffer);
	fat32_fsinfo_t *fsinfo=(fat32_fsinfo_t*)Buffer;
	fsinfo->FSI_Free_Count=NbFreeSectors;
	fsinfo->FSI_Last_Allocated=LastAllocatedSector;
	SD_WRITE_SECTOR(1, Buffer);
}
#endif

static pos_fat32_entry_t get_pos_fat_entry(const uint32_t sector)
{	
	pos_fat32_entry_t p;
	p.FAT_SectorNumber=RsvdSecCnt+(sector/128);
	p.FAT_EntryIndex=sector%128;
	
	return p;
}

static fat32_entry_t fat32_read_entry(pos_fat32_entry_t const * const pos)
{
	SD_READ_SECTOR(pos->FAT_SectorNumber, Buffer);
	return ((fat32_entry_t*)Buffer)[pos->FAT_EntryIndex]&0x0FFFFFFF;
}

#if !FS32_NO_APPEND || !FS32_NO_WRITE
static void fat32_write_entry(pos_fat32_entry_t const * const pos, const uint32_t nextSector)
{
	SD_READ_SECTOR(pos->FAT_SectorNumber, Buffer);
	((fat32_entry_t*)Buffer)[pos->FAT_EntryIndex]=nextSector;
	SD_WRITE_SECTOR(pos->FAT_SectorNumber, Buffer);
}
#endif

static uint32_t fat32_get_next_sector(const uint32_t sector)
{
	pos_fat32_entry_t pos;
	pos=get_pos_fat_entry(sector);
	
	fat32_entry_t entry;
	entry=fat32_read_entry(&pos);

	return entry;
}

static void read_logical_sector(const uint32_t sector, uint8_t * const data)
{
	uint32_t Physical=LOGICAL_SECTOR_TO_PHYSICAL(sector);
	SD_READ_SECTOR(Physical, data);
}

#if !FS32_NO_APPEND || !FS32_NO_WRITE
static void write_logical_sector(const uint32_t sector, uint8_t const * const data)
{
	uint32_t Physical=LOGICAL_SECTOR_TO_PHYSICAL(sector);
	SD_WRITE_SECTOR(Physical, data);
}
#endif

void fat32_filename_to_string(fat32_directory_entry_t const * const entry, char * const string)
{
	uint8_t i, j;
	for(i=0, j=0; i<8 && (entry->DIR_Name[i]!=' '); i++, j++)
		string[j]=entry->DIR_Name[i];
	if(memcmp(entry->DIR_Ext, "   ", 3))
	{
		string[j++]='.';
		for(i=0; i<3 && (entry->DIR_Ext[i]!=' '); i++, j++)
			string[j]=entry->DIR_Ext[i];
	}
	string[j]='\0';
}

void filename_to_fat32(uint8_t filenr, fat32_directory_entry_t * const entry)
{
#if SINGLE_FILE_CONFIG
	(void)filenr;
#endif
	
	memset(entry->DIR_Name, ' ', 8);
	memset(entry->DIR_Ext, ' ', 3);
	
	uint8_t i,j;
	for(i=0, j=0; OpenFiles[FILENR_ARR_INDEX].Name[i] && OpenFiles[FILENR_ARR_INDEX].Name[i]!='.'; i++, j++)
		entry->DIR_Name[j]=OpenFiles[FILENR_ARR_INDEX].Name[i];
	if(OpenFiles[FILENR_ARR_INDEX].Name[i])
	{
		for(i++, j=0; OpenFiles[FILENR_ARR_INDEX].Name[i]; i++, j++)
			entry->DIR_Ext[j]=OpenFiles[FILENR_ARR_INDEX].Name[i];
	}
}

static void fat32_search_for_file(FIRST_ARG_FILENR char const * const filename)
{
	OpenFiles[FILENR_ARR_INDEX].FileFound=false;
	
	uint32_t cl=RootSector;
	
	while(!IS_EOC_MARKER(cl))
	{
		fat32_directory_entry_t DirEntry;
		uint8_t NbEntry=0;
		
		bool NoMoreEntries=false;
		
		read_logical_sector(cl, Buffer);
		
		for(NbEntry=0; NbEntry<512/sizeof(fat32_directory_entry_t); NbEntry++)
		{
			memcpy(&DirEntry, &(((fat32_directory_entry_t*)Buffer)[NbEntry]), sizeof(fat32_directory_entry_t));
			
			if((uint8_t)DirEntry.DIR_Name[0]==DIR_ENTRY_FREE)
				continue;
			
			if((uint8_t)DirEntry.DIR_Name[0]==DIR_ENTRY_FREE_NO_MORE_DIR)
			{
				NoMoreEntries=true;
				break;
			}
			
			if(DirEntry.DIR_Attr&ATTR_LONG_NAME)
			{
				//LONG NAMES ARE UNSUPPORTED!
				break;
			}
			
			char Name[8+1+3+1];
			fat32_filename_to_string(&DirEntry, Name);
			
			if(!strcmp(Name, filename))
			{
				OpenFiles[FILENR_ARR_INDEX].FileFound=true;
				OpenFiles[FILENR_ARR_INDEX].LogicalSector=((uint32_t)DirEntry.DIR_FstClusHI<<16)|DirEntry.DIR_FstClusLO;
				OpenFiles[FILENR_ARR_INDEX].FirstLogicalSector=OpenFiles[FILENR_ARR_INDEX].LogicalSector; //needed for f_seek for file in modify-mode
				OpenFiles[FILENR_ARR_INDEX].FileSize=DirEntry.DIR_FileSize;
				OpenFiles[FILENR_ARR_INDEX].SectorDirEntry=cl;
				OpenFiles[FILENR_ARR_INDEX].IndexDirEntry=NbEntry;
				break;
			}
		}
		
		if(NoMoreEntries || OpenFiles[FILENR_ARR_INDEX].FileFound)
			break;
		
		cl=fat32_get_next_sector(cl);
	}
}

#if !FS32_NO_APPEND || !FS32_NO_WRITE
static pos_fat32_entry_t fat32_get_next_free_entry(void)
{
	pos_fat32_entry_t p;

	if(NbFreeSectors==0)
	{
		p.noFreeSpace=true;
		return p;
	}
	
	NbFreeSectors--;
	
	p=get_pos_fat_entry(LastAllocatedSector+1);
	p.noFreeSpace=false;
	p.LogicalSector=LastAllocatedSector+1;
	
	uint32_t Sector=p.FAT_SectorNumber;
	uint8_t EntryIndex=p.FAT_EntryIndex;

	bool Found=false;
	
	for(; Sector<RsvdSecCnt+FATSz32; Sector++)
	{
		SD_READ_SECTOR(Sector, Buffer);
		
		for(; EntryIndex<128; EntryIndex++)
		{
			if(Sector==RsvdSecCnt+FATSz32 && EntryIndex>FATIndexLastEntry)
				break;
			
			if((((fat32_entry_t*)Buffer)[EntryIndex]&0x0FFFFFFF)==0x00000000)
			{
				Found=true;
				LastAllocatedSector=(Sector-RsvdSecCnt)*128+EntryIndex;
				break;
			}
		}
		if(Found)
			break;
		
		EntryIndex=0;
	}
	
	update_fsinfo();

	return p;
}
#endif

#if !FS32_NO_WRITE
static bool create_dir_entry(ONLY_ARG_FILENR) //always in root-directory!
{	
	uint32_t previous_cl=RootSector;
	uint32_t cl=RootSector;
	
	bool FoundFreeEntry=false;
	
	fat32_directory_entry_t DirEntry;
	uint8_t Index=0;
	
	while(!IS_EOC_MARKER(cl))
	{
		read_logical_sector(cl, Buffer);
		
		for(Index=0; Index<512/sizeof(fat32_directory_entry_t); Index++)
		{
			memcpy(&DirEntry, &(((fat32_directory_entry_t*)Buffer)[Index]), sizeof(fat32_directory_entry_t));
			
			if((uint8_t)DirEntry.DIR_Name[0]==DIR_ENTRY_FREE || (uint8_t)DirEntry.DIR_Name[0]==DIR_ENTRY_FREE_NO_MORE_DIR)
			{
				FoundFreeEntry=true;
				break;
			}
		}
		
		if(FoundFreeEntry)
			break;
		
		previous_cl=cl;
		cl=fat32_get_next_sector(cl);
	}
	
	if(!FoundFreeEntry)
	{
		pos_fat32_entry_t p_curr=get_pos_fat_entry(previous_cl);
		pos_fat32_entry_t p_new=fat32_get_next_free_entry();
		if(p_new.noFreeSpace)
			return true;
		fat32_write_entry(&p_curr, p_new.LogicalSector);
		fat32_write_entry(&p_new, EndOfClusterChainMarker);
		cl=p_new.LogicalSector;
		Index=0;
		
		memset(Buffer, DIR_ENTRY_FREE_NO_MORE_DIR, 512);
	}
	
	memset(&DirEntry, 0, sizeof(fat32_directory_entry_t));
	filename_to_fat32(FILENR_ARR_INDEX, &DirEntry);
	DirEntry.DIR_WrtTime=rtc_get_encoded_time();
	DirEntry.DIR_WrtDate=rtc_get_encoded_date();
	DirEntry.DIR_FileSize=OpenFiles[FILENR_ARR_INDEX].FileSize;
	DirEntry.DIR_FstClusHI=OpenFiles[FILENR_ARR_INDEX].FirstLogicalSector>>16;
	DirEntry.DIR_FstClusLO=OpenFiles[FILENR_ARR_INDEX].FirstLogicalSector&0xFFFF;
	
	memcpy(&(((fat32_directory_entry_t*)Buffer)[Index]), &DirEntry, sizeof(fat32_directory_entry_t));
	
	write_logical_sector(cl, Buffer);
		
	return false;
}
#endif

#if !FS32_NO_APPEND || !FS32_NO_MODIFY
static void update_dir_entry(ONLY_ARG_FILENR)
{
	read_logical_sector(OpenFiles[FILENR_ARR_INDEX].SectorDirEntry, Buffer);
	
	fat32_directory_entry_t * Entry=(fat32_directory_entry_t*)Buffer;
	
	Entry[OpenFiles[FILENR_ARR_INDEX].IndexDirEntry].DIR_FileSize=OpenFiles[FILENR_ARR_INDEX].FileSize;
	Entry[OpenFiles[FILENR_ARR_INDEX].IndexDirEntry].DIR_WrtTime=rtc_get_encoded_time();
	Entry[OpenFiles[FILENR_ARR_INDEX].IndexDirEntry].DIR_WrtDate=rtc_get_encoded_date();
	
	write_logical_sector(OpenFiles[FILENR_ARR_INDEX].SectorDirEntry, Buffer);
}
#endif

#if !FS32_NO_APPEND || !FS32_NO_SEEK_TELL
static void set_file_pos(FIRST_ARG_FILENR uint32_t pos)
{
	if(pos==FS_SEEK_END)
		pos=OpenFiles[FILENR_ARR_INDEX].FileSize;
	
	OpenFiles[FILENR_ARR_INDEX].PosInFile=pos;
	OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector=pos%512;
	
	uint16_t NbSectors=pos/512;
	
	uint32_t sector=OpenFiles[FILENR_ARR_INDEX].FirstLogicalSector;
	
	while(NbSectors--)
		sector=fat32_get_next_sector(sector);
	
	OpenFiles[FILENR_ARR_INDEX].LogicalSector=sector;
}
#endif

#if !SINGLE_FILE_CONFIG
static int8_t get_free_slot(void)
{
	uint8_t i;
	for(i=0; i<FS32_NB_FILES_MAX; i++)
	{
		if(!OpenFiles[i].isInUse)
			return i;
	}
	
	return -1;
}
#endif

static bool check_if_already_open(char const * const name)
{
	uint8_t i;
	for(i=0; i<FS32_NB_FILES_MAX; i++)
	{
		if(OpenFiles[i].isInUse && !strcmp(OpenFiles[i].Name, name))
			return true;
	}
	
	return false;
}

//public functions

#if FS32_PARTITION_SUPPORT
FS32_status_t f_set_partition(const uint8_t partition)
{
	if(partition>3)
		return SET_PART_INVALID_NUMBER;
	
	sd_read_sector(0, Buffer);
	master_boot_record_t *mbr=(master_boot_record_t*)Buffer;
	
	if(mbr->BootSignature!=0xAA55)
		return SET_PART_INVALID_BOOT_SIG;
	
	if(mbr->Partitions[partition].PartitionType!=0x0C)
		return SET_PART_UNKNOWN_PART_TYPE;
	
	if(mbr->Partitions[partition].NumberOfSectors==0)
		return SET_PART_NO_VALID_PART;
	
	StartOfPartition=mbr->Partitions[partition].StartSectorLBA;
	
	return STATUS_OK;
}
#endif

FS32_status_t f_init(void)
{
	uint8_t i;
	for(i=0; i<FS32_NB_FILES_MAX; i++)
		OpenFiles[i].isInUse=false;
	
	SD_READ_SECTOR(0, Buffer);
	
	fat32_header_t *header=(fat32_header_t*)Buffer;
	
	if(header->BS_jmpBoot[0]!=0xEB)
		return INIT_INVALID_JUMP;
		
	if(header->BPB_BytsPerSec!=512)
		return INIT_INVALID_BYTES_PER_SEC;
	
	if(header->BPB_SecPerClus!=1)
		return INIT_INVALID_SEC_PER_CLUS;
	
	if(header->BPB_TotSec16)
		return INIT_NOT_FAT32;
	
	if(header->BPB_FATSz16)
		return INIT_NOT_FAT32;
		
	if(header->BPB_NumFATs!=1)
		return INIT_MULTIPLE_FAT;

	RsvdSecCnt=header->BPB_RsvdSecCnt;
	RootSector=header->BPB_RootClus;
	FATSz32=header->BPB_FATSz32;
	FirstDataSector=header->BPB_RsvdSecCnt+header->BPB_FATSz32;
	TotalNbOfDataSectors=header->BPB_TotSec32-FirstDataSector;
	FATIndexLastEntry=TotalNbOfDataSectors%128;
	
	//FAT EOC-Marker
	sd_read_sector(header->BPB_RsvdSecCnt, Buffer);
	EndOfClusterChainMarker=((fat32_entry_t*)Buffer)[1];
	
	//FSINFO
	SD_READ_SECTOR(1, Buffer);
	fat32_fsinfo_t *fsinfo=(fat32_fsinfo_t*)Buffer;
	
	if(fsinfo->FSI_LeadSig!=FSI_LEADSIG)
		return INIT_INVALID_FSINFO;
	
	NbFreeSectors=fsinfo->FSI_Free_Count;
	LastAllocatedSector=fsinfo->FSI_Last_Allocated;
	
	return STATUS_OK;
}

FS32_status_t f_open(uint8_t * const filenr, char const * const filename, const char mode)
{
	if(check_if_already_open(filename))
		return OPEN_FILE_ALREADY_OPEN;
	
#if !SINGLE_FILE_CONFIG	
	int8_t slot=get_free_slot();
	if(slot<0)
		return OPEN_NO_FREE_SLOT;
		
	(*filenr)=(uint8_t)slot;
#else
	(void)filenr;
	if(OpenFiles[FILENR_ARR_INDEX].isInUse)
		return OPEN_NO_FREE_SLOT;
#endif

	fat32_search_for_file(FILENR_PTR_FUNC_ARG filename);
	
#if !FS32_NO_READ
	if(mode=='r')
	{
		if(OpenFiles[FILENR_PTR_ARR_INDEX].FileFound==false)
			return OPEN_FILE_NOT_FOUND;
		
		OpenFiles[FILENR_PTR_ARR_INDEX].isNewFile=false;
		OpenFiles[FILENR_PTR_ARR_INDEX].OpenedForReading=true;
		OpenFiles[FILENR_PTR_ARR_INDEX].PosInFile=0;
		OpenFiles[FILENR_PTR_ARR_INDEX].PosInLogicalSector=0;
	}
	else
#endif
#if !FS32_NO_WRITE
	if(mode=='w')
	{
		if(OpenFiles[FILENR_PTR_ARR_INDEX].FileFound==true)
			return OPEN_FILE_ALREADY_EXISTS;
		else
		{
			pos_fat32_entry_t FATEntry=fat32_get_next_free_entry();
			
			if(FATEntry.noFreeSpace)
				return OPEN_NO_MORE_SPACE;
			
			memset(&OpenFiles[FILENR_PTR_ARR_INDEX], 0, sizeof(file_t));
			OpenFiles[FILENR_PTR_ARR_INDEX].isInUse=true;
			OpenFiles[FILENR_PTR_ARR_INDEX].isNewFile=true;
			OpenFiles[FILENR_PTR_ARR_INDEX].FirstLogicalSector=FATEntry.LogicalSector;
			OpenFiles[FILENR_PTR_ARR_INDEX].LogicalSector=FATEntry.LogicalSector;
			
			strncpy(OpenFiles[FILENR_PTR_ARR_INDEX].Name, filename, 8+1+3);
			
			fat32_write_entry(&FATEntry, EndOfClusterChainMarker);
		}
	} else
#endif
#if !FS32_NO_APPEND
	if(mode=='a')
	{
		if(OpenFiles[FILENR_PTR_ARR_INDEX].FileFound==false)
			return OPEN_FILE_NOT_FOUND;
		
		OpenFiles[FILENR_PTR_ARR_INDEX].isInUse=true;
		OpenFiles[FILENR_PTR_ARR_INDEX].isNewFile=false;
		OpenFiles[FILENR_PTR_ARR_INDEX].OpenedForReading=false;
		OpenFiles[FILENR_PTR_ARR_INDEX].OpenendForAppending=true;
		
		set_file_pos(FILENR_PTR_FUNC_ARG OpenFiles[FILENR_PTR_ARR_INDEX].FileSize);
	} else
#endif
#if !FS32_NO_MODIFY
	if(mode=='m')
	{
		if(OpenFiles[FILENR_PTR_ARR_INDEX].FileFound==false)
			return OPEN_FILE_NOT_FOUND;
		
		OpenFiles[FILENR_PTR_ARR_INDEX].isInUse=true;
		OpenFiles[FILENR_PTR_ARR_INDEX].isNewFile=false;
		OpenFiles[FILENR_PTR_ARR_INDEX].OpenedForReading=false;
		OpenFiles[FILENR_PTR_ARR_INDEX].OpenendForAppending=false;
		OpenFiles[FILENR_PTR_ARR_INDEX].OpenendForModify=true;
		OpenFiles[FILENR_PTR_ARR_INDEX].PosInFile=0;
		OpenFiles[FILENR_PTR_ARR_INDEX].PosInLogicalSector=0;
	} else
#endif
		return OPEN_INVALID_MODE;
	
	return STATUS_OK;
}

FS32_status_t f_close(const uint8_t filenr)
{
	
#if SINGLE_FILE_CONFIG
	(void)filenr;
#endif

	if(!OpenFiles[FILENR_ARR_INDEX].isInUse)
		return STATUS_OK;
	
	OpenFiles[FILENR_ARR_INDEX].isInUse=false;
	
#if !FS32_NO_WRITE	
	if(OpenFiles[FILENR_ARR_INDEX].isNewFile)
	{
		if(create_dir_entry(FILENR_ONLY_FUNC_ARG))
			return CLOSE_CREATE_DIR_ENTRY_FAILED;
	}
#endif

#if !FS32_NO_APPEND || !FS32_NO_MODIFY
	if(OpenFiles[FILENR_ARR_INDEX].OpenendForAppending || OpenFiles[FILENR_ARR_INDEX].OpenendForModify)
	{
		update_dir_entry(FILENR_ONLY_FUNC_ARG);
	}
#endif
	
	return STATUS_OK;
}

#if !FS32_NO_READ
FS32_status_t f_read(const uint8_t filenr, void * ptr, const uint16_t size, const uint16_t n)
{
	
#if SINGLE_FILE_CONFIG
	(void)filenr;
#endif

	uint32_t NbBytesToRead=(uint32_t)size*n;
	
	while(NbBytesToRead && OpenFiles[FILENR_ARR_INDEX].PosInFile<OpenFiles[FILENR_ARR_INDEX].FileSize)
	{
		uint32_t NbToCopy=NbBytesToRead;
		if(NbToCopy>512-OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector)
			NbToCopy=512-OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector;
		if(NbToCopy>(OpenFiles[FILENR_ARR_INDEX].FileSize-OpenFiles[FILENR_ARR_INDEX].PosInFile))
			NbToCopy=OpenFiles[FILENR_ARR_INDEX].FileSize-OpenFiles[FILENR_ARR_INDEX].PosInFile;

		read_logical_sector(OpenFiles[FILENR_ARR_INDEX].LogicalSector, Buffer);
		
		memcpy(ptr, Buffer+OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector, NbToCopy);
		
		ptr+=NbToCopy;
		OpenFiles[FILENR_ARR_INDEX].PosInFile+=NbToCopy;
		OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector+=NbToCopy;
		if(OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector>=512)
		{
			OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector-=512;
			OpenFiles[FILENR_ARR_INDEX].LogicalSector=fat32_get_next_sector(OpenFiles[FILENR_ARR_INDEX].LogicalSector);
			if(OpenFiles[FILENR_ARR_INDEX].LogicalSector==EndOfClusterChainMarker)
				break;
		}
		
		NbBytesToRead-=NbToCopy;
	}
	
	if(NbBytesToRead)
		return READ_FAILED;
	else
		return STATUS_OK;
}
#endif

#if !FS32_NO_APPEND || !FS32_NO_WRITE || !FS32_NO_MODIFY
FS32_status_t f_write(const uint8_t filenr, void const * ptr, const uint16_t size, const uint16_t n)
{
	
#if SINGLE_FILE_CONFIG
	(void)filenr;
#endif

	if(!OpenFiles[FILENR_ARR_INDEX].isInUse)
		return WRITE_NO_OPEN_FILE;
	
	if(OpenFiles[FILENR_ARR_INDEX].OpenedForReading)
		return WRITE_FILE_READ_ONLY;
	
	uint32_t NbBytesToWrite=(uint32_t)size*n;
	
	
	while(NbBytesToWrite)
	{
		uint16_t NbBytesToCopy=512-OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector;
		
		bool IncreasingSize=false;
		
		if(OpenFiles[FILENR_ARR_INDEX].PosInFile+NbBytesToWrite>OpenFiles[FILENR_ARR_INDEX].FileSize)
			IncreasingSize=true;
		
		if(NbBytesToCopy>NbBytesToWrite)
			NbBytesToCopy=NbBytesToWrite;
		
		if(NbBytesToCopy) //avoid reading a sector just to write it again without change
		{
			if(OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector!=0 || OpenFiles[FILENR_ARR_INDEX].OpenendForModify)
				read_logical_sector(OpenFiles[FILENR_ARR_INDEX].LogicalSector, Buffer);
			
			memcpy(Buffer+OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector, ptr, NbBytesToCopy);
			
			write_logical_sector(OpenFiles[FILENR_ARR_INDEX].LogicalSector, Buffer);

			NbBytesToWrite-=NbBytesToCopy;
			ptr+=NbBytesToCopy;
			
			if(IncreasingSize)
				OpenFiles[FILENR_ARR_INDEX].FileSize=OpenFiles[FILENR_ARR_INDEX].PosInFile+NbBytesToCopy;

			OpenFiles[FILENR_ARR_INDEX].PosInFile+=NbBytesToCopy;
			OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector+=NbBytesToCopy;
		}
		
		if(NbBytesToWrite)
		{
			bool NeedMoreSpace=false;
			
#if !FS32_NO_MODIFY			
			if(OpenFiles[FILENR_ARR_INDEX].OpenendForModify)
			{
				uint32_t nextSector=fat32_get_next_sector(OpenFiles[FILENR_ARR_INDEX].LogicalSector);
				if(IS_EOC_MARKER(nextSector))
					NeedMoreSpace=true;
				else
				{
					OpenFiles[FILENR_ARR_INDEX].LogicalSector=nextSector;
					OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector=0;
				}
			}
#endif
			
			if(OpenFiles[FILENR_ARR_INDEX].OpenendForAppending || OpenFiles[FILENR_ARR_INDEX].isNewFile || NeedMoreSpace)
			{
				pos_fat32_entry_t p_curr=get_pos_fat_entry(OpenFiles[FILENR_ARR_INDEX].LogicalSector);
				pos_fat32_entry_t p_new=fat32_get_next_free_entry();
				if(p_new.noFreeSpace)
					return WRITE_NO_MORE_SPACE;
				fat32_write_entry(&p_curr, p_new.LogicalSector);
				fat32_write_entry(&p_new, EndOfClusterChainMarker);
				OpenFiles[FILENR_ARR_INDEX].LogicalSector=p_new.LogicalSector;
				OpenFiles[FILENR_ARR_INDEX].PosInLogicalSector=0;
			}
		}
	}
	
	return STATUS_OK;
}
#endif

#if !FS32_NO_SEEK_TELL
FS32_status_t f_seek(const uint8_t filenr, const uint32_t pos)
{
	
#if SINGLE_FILE_CONFIG
	(void)filenr;
#endif

	if(!OpenFiles[FILENR_ARR_INDEX].OpenedForReading && !OpenFiles[FILENR_ARR_INDEX].OpenendForModify)
		return SEEK_CANT_SEEK_IN_THIS_MODE;
	
	if(pos>=OpenFiles[FILENR_ARR_INDEX].FileSize && pos!=FS_SEEK_END)
		return SEEK_INVALID_POS;
		
	set_file_pos(FILENR_FIRST_FUNC_ARG pos);
	
	return STATUS_OK;
}

uint32_t f_tell(const uint8_t filenr)
{
	
#if SINGLE_FILE_CONFIG
	(void)filenr;
#endif

	return OpenFiles[FILENR_ARR_INDEX].PosInFile;
}
#endif

uint32_t get_free_sectors_count(void)
{
	return NbFreeSectors;
}

uint32_t get_file_size(const uint8_t filenr)
{
	
#if SINGLE_FILE_CONFIG
	(void)filenr;
#endif
	
	return OpenFiles[FILENR_ARR_INDEX].FileSize;
}

#if !FS32_NO_FILE_LISTING
FS32_status_t f_ls(const f_ls_callback callback)
{
	uint32_t cl=RootSector;
	
	while(!IS_EOC_MARKER(cl))
	{
		fat32_directory_entry_t DirEntry;
		uint8_t NbEntry=0;
		
		bool NoMoreEntries=false;
		
		read_logical_sector(cl, Buffer);
		
		for(NbEntry=0; NbEntry<512/sizeof(fat32_directory_entry_t); NbEntry++)
		{
			memcpy(&DirEntry, &(((fat32_directory_entry_t*)Buffer)[NbEntry]), sizeof(fat32_directory_entry_t));
			
			if((uint8_t)DirEntry.DIR_Name[0]==DIR_ENTRY_FREE)
				continue;
			
			if((uint8_t)DirEntry.DIR_Name[0]==DIR_ENTRY_FREE_NO_MORE_DIR)
			{
				NoMoreEntries=true;
				break;
			}
			
			if(DirEntry.DIR_Attr&ATTR_LONG_NAME) //UNSUPPORTED!
				return LS_LONG_NAME;
			
			char Filename[8+1+3+1];
			fat32_filename_to_string(&DirEntry, Filename);
			
			callback(Filename);
		}
		
		if(NoMoreEntries)
			break;
		
		cl=fat32_get_next_sector(cl);
	}
	
	callback(NULL); //signal that we have finished to callback
	
	return STATUS_OK;
}
#endif
