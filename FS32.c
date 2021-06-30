#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "FS32_internals.h"

#include "FS32.h"

#include "FS32_config.h"

/*
Code for kittenFS32 - a small (somewhat), simple (i hope) (and restrictive) implementation of FAT32 for interfacing a SD-card with a microcontroller

licenced under AGPLv3+

NO WARRANTY! USE AT YOUR OWN RISK! BETA-VERSION, EXPECT LOSS AND/OR CORRUPTION OF DATA! READ THE DOC!

version 0.01 - 30.06.21

(c) 2021 by kittennbfive

This code makes a few IMPORTANT assumptions. Please read the manual!
*/

static uint16_t RsvdSecCnt; //number of reserved sectors == first sector of FAT
static uint32_t FATSz32; //number of sectors for one FAT
static uint32_t RootSector; //sector where the root dir is
static uint32_t FirstDataSector;
static uint32_t TotalNbOfDataSectors;
static uint8_t FATIndexLastEntry;
static fat32_entry_t EndOfClusterChainMarker;
static uint32_t NbFreeSectors;
static uint32_t LastAllocatedSector;
static file_t OpenFile;

static uint8_t Buffer[512];

#define IS_EOC_MARKER(value) (value>=0x0FFFFFF8 && value <=0x0FFFFFFF)

#define LOGICAL_SECTOR_TO_PHYSICAL(datasector) ((datasector-2)+FirstDataSector)

static void update_fsinfo(void)
{
	sd_read_sector(1, Buffer);
	fat32_fsinfo_t *fsinfo=(fat32_fsinfo_t*)Buffer;
	fsinfo->FSI_Free_Count=NbFreeSectors;
	fsinfo->FSI_Last_Allocated=LastAllocatedSector;
	sd_write_sector(1, Buffer);
}
	
static pos_fat32_entry_t get_pos_fat_entry(const uint32_t sector)
{	
	pos_fat32_entry_t p;
	p.FAT_SectorNumber=RsvdSecCnt+(sector/128);
	p.FAT_EntryIndex=sector%128;
	
	return p;
}

static fat32_entry_t fat32_read_entry(pos_fat32_entry_t const * const pos)
{
	sd_read_sector(pos->FAT_SectorNumber, Buffer);
	return ((fat32_entry_t*)Buffer)[pos->FAT_EntryIndex]&0x0FFFFFFF;
}

static void fat32_write_entry(pos_fat32_entry_t const * const pos, const uint32_t nextSector)
{
	sd_read_sector(pos->FAT_SectorNumber, Buffer);
	((fat32_entry_t*)Buffer)[pos->FAT_EntryIndex]=nextSector;
	sd_write_sector(pos->FAT_SectorNumber, Buffer);
}
	
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
	sd_read_sector(Physical, data);
}

static void write_logical_sector(const uint32_t sector, uint8_t const * const data)
{
	uint32_t Physical=LOGICAL_SECTOR_TO_PHYSICAL(sector);
	sd_write_sector(Physical, data);
}

static void fat32_search_for_file(char const * const filename)
{
	OpenFile.FileFound=false;
	
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
			uint8_t i, j;
			for(i=0, j=0; i<8 && (DirEntry.DIR_Name[i]!=' '); i++, j++)
				Name[j]=DirEntry.DIR_Name[i];
			if(memcmp(DirEntry.DIR_Ext, "   ", 3))
			{
				Name[j++]='.';
				for(i=0; i<3 && (DirEntry.DIR_Ext[i]!=' '); i++, j++)
					Name[j]=DirEntry.DIR_Ext[i];
			}
			Name[j]='\0';
			
			if(!strcmp(Name, filename))
			{
				OpenFile.FileFound=true;
				OpenFile.LogicalSector=((uint32_t)DirEntry.DIR_FstClusHI<<16)|DirEntry.DIR_FstClusLO;
				OpenFile.FileSize=DirEntry.DIR_FileSize;
				OpenFile.SectorDirEntry=cl;
				OpenFile.IndexDirEntry=NbEntry;
				break;
			}
		}
		
		if(NoMoreEntries || OpenFile.FileFound)
			break;
		
		cl=fat32_get_next_sector(cl);
	}
}

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
		sd_read_sector(Sector, Buffer);
		
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

#if !FS32_NO_WRITE
static bool create_dir_entry(void) //always in root-directory!
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
	memcpy(DirEntry.DIR_Name, OpenFile.Filename, 8);
	memcpy(DirEntry.DIR_Ext, OpenFile.Extension, 3);
	DirEntry.DIR_WrtTime=rtc_get_encoded_time();
	DirEntry.DIR_WrtDate=rtc_get_encoded_date();
	DirEntry.DIR_FileSize=OpenFile.FileSize;
	DirEntry.DIR_FstClusHI=OpenFile.FirstLogicalSector>>16;
	DirEntry.DIR_FstClusLO=OpenFile.FirstLogicalSector&0xFFFF;
	
	memcpy(&(((fat32_directory_entry_t*)Buffer)[Index]), &DirEntry, sizeof(fat32_directory_entry_t));
	
	write_logical_sector(cl, Buffer);
	
	return false;
}
#endif

#if !FS32_NO_APPEND
static void update_dir_entry(void)
{
	read_logical_sector(OpenFile.SectorDirEntry, Buffer);
	
	fat32_directory_entry_t * Entry=(fat32_directory_entry_t*)Buffer;
	
	Entry[OpenFile.IndexDirEntry].DIR_FileSize=OpenFile.FileSize;
	Entry[OpenFile.IndexDirEntry].DIR_WrtTime=rtc_get_encoded_time();
	Entry[OpenFile.IndexDirEntry].DIR_WrtDate=rtc_get_encoded_date();
	
	write_logical_sector(OpenFile.SectorDirEntry, Buffer);
}
#endif

#if !FS32_NO_APPEND || !FS32_NO_SEEK_TELL
static void set_file_pos(const uint32_t pos)
{
	OpenFile.PosInFile=pos;
	OpenFile.PosInLogicalSector=pos%512;
	
	uint16_t NbSectors=pos/512;
	
	uint32_t sector=OpenFile.FirstLogicalSector;
	
	while(NbSectors--)
		sector=fat32_get_next_sector(sector);
	
	OpenFile.LogicalSector=sector;	
}
#endif

FS32_status_t f_init(void)
{
	OpenFile.isInUse=false;
	
	//read main header from sector 0
	sd_read_sector(0, Buffer);
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
	sd_read_sector(1, Buffer);
	fat32_fsinfo_t *fsinfo=(fat32_fsinfo_t*)Buffer;
	
	if(fsinfo->FSI_LeadSig!=FSI_LEADSIG)
		return INIT_INVALID_FSINFO;
	
	NbFreeSectors=fsinfo->FSI_Free_Count;
	LastAllocatedSector=fsinfo->FSI_Last_Allocated;
	
	return STATUS_OK;
}

FS32_status_t f_open(char const * const filename, const char mode)
{
	if(OpenFile.isInUse)
		return OPEN_OTHER_FILE_IS_OPEN;
	
	fat32_search_for_file(filename);
	
#if !FS32_NO_READ
	if(mode=='r')
	{
		if(OpenFile.FileFound==false)
			return OPEN_FILE_NOT_FOUND;
		
		OpenFile.isNewFile=false;
		OpenFile.OpenedForReading=true;
		OpenFile.PosInFile=0;
		OpenFile.PosInLogicalSector=0;
	}
	else
#endif
#if !FS32_NO_WRITE
	if(mode=='w')
	{
		if(OpenFile.FileFound==true)
			return OPEN_FILE_ALREADY_EXISTS;
		else
		{
			pos_fat32_entry_t FATEntry=fat32_get_next_free_entry();
			
			if(FATEntry.noFreeSpace)
				return OPEN_NO_MORE_SPACE;
			
			memset(&OpenFile, 0, sizeof(file_t));
			OpenFile.isInUse=true;
			OpenFile.isNewFile=true;
			OpenFile.FirstLogicalSector=FATEntry.LogicalSector;
			OpenFile.LogicalSector=FATEntry.LogicalSector;
			
			uint8_t i;
			memset(OpenFile.Filename, ' ', 8);
			for(i=0; i<8 && filename[i] && filename[i]!='.'; i++);
			memcpy(OpenFile.Filename, filename, i);
			memset(OpenFile.Extension, ' ', 3);
			if(filename[i]=='.')
				strncpy(OpenFile.Extension, &filename[i+1], 3);
			fat32_write_entry(&FATEntry, EndOfClusterChainMarker);
		}
	} else
#endif
#if !FS32_NO_APPEND
	if(mode=='a')
	{
		if(OpenFile.FileFound==false)
			return OPEN_FILE_NOT_FOUND;
		
		OpenFile.isInUse=true;
		OpenFile.isNewFile=false;
		OpenFile.OpenedForReading=false;
		OpenFile.OpenendForAppending=true;
		
		set_file_pos(OpenFile.FileSize);
	} else
#endif
		return OPEN_INVALID_MODE;
	
	return STATUS_OK;
}

FS32_status_t f_close(void)
{
	if(!OpenFile.isInUse)
		return STATUS_OK;
	
	OpenFile.isInUse=false;
	
#if !FS32_NO_WRITE	
	if(OpenFile.isNewFile)
	{
		if(create_dir_entry())
			return CLOSE_CREATE_DIR_ENTRY_FAILED;
	}
#endif
#if !FS32_NO_APPEND
	if(OpenFile.OpenendForAppending)
		update_dir_entry();
#endif
	
	return STATUS_OK;
}

FS32_status_t f_read(void * ptr, const uint16_t size, const uint16_t n)
{
	uint32_t NbBytesToRead=(uint32_t)size*n;
	
	while(NbBytesToRead && OpenFile.PosInFile<OpenFile.FileSize)
	{
		uint32_t NbToCopy=NbBytesToRead;
		if(NbToCopy>512-OpenFile.PosInLogicalSector)
			NbToCopy=512-OpenFile.PosInLogicalSector;
		if(NbToCopy>(OpenFile.FileSize-OpenFile.PosInFile))
			NbToCopy=OpenFile.FileSize-OpenFile.PosInFile;

		read_logical_sector(OpenFile.LogicalSector, Buffer);
		
		memcpy(ptr, Buffer+OpenFile.PosInLogicalSector, NbToCopy);
		
		ptr+=NbToCopy;
		OpenFile.PosInFile+=NbToCopy;
		OpenFile.PosInLogicalSector+=NbToCopy;
		if(OpenFile.PosInLogicalSector>=512)
		{
			OpenFile.PosInLogicalSector-=512;
			OpenFile.LogicalSector=fat32_get_next_sector(OpenFile.LogicalSector);
			if(OpenFile.LogicalSector==EndOfClusterChainMarker)
				break;
		}
		
		NbBytesToRead-=NbToCopy;
		

	}
	
	if(NbBytesToRead)
		return READ_FAILED;
	else
		return STATUS_OK;
}

FS32_status_t f_write(void const * ptr, const uint16_t size, const uint16_t n)
{
	if(!OpenFile.isInUse)
		return WRITE_NO_OPEN_FILE;
	
	if(OpenFile.OpenedForReading)
		return WRITE_FILE_READ_ONLY;
	
	uint32_t NbBytesToWrite=(uint32_t)size*n;

	while(NbBytesToWrite)
	{
		uint16_t NbBytesToCopy=512-OpenFile.PosInLogicalSector;

		if(NbBytesToCopy>NbBytesToWrite)
			NbBytesToCopy=NbBytesToWrite;
		
		memset(Buffer, 0, 512);
		
		if(OpenFile.PosInLogicalSector!=0)
			read_logical_sector(OpenFile.LogicalSector, Buffer);
		
		memcpy(Buffer+OpenFile.PosInLogicalSector, ptr, NbBytesToCopy);
		
		write_logical_sector(OpenFile.LogicalSector, Buffer);

		NbBytesToWrite-=NbBytesToCopy;
		ptr+=NbBytesToCopy;
		OpenFile.FileSize+=NbBytesToCopy;
		OpenFile.PosInFile+=NbBytesToCopy;
		OpenFile.PosInLogicalSector+=NbBytesToCopy;
		
		if(NbBytesToWrite)
		{
			pos_fat32_entry_t p_curr=get_pos_fat_entry(OpenFile.LogicalSector);
			pos_fat32_entry_t p_new=fat32_get_next_free_entry();
			if(p_new.noFreeSpace)
				return WRITE_NO_MORE_SPACE;
			fat32_write_entry(&p_curr, p_new.LogicalSector);
			fat32_write_entry(&p_new, EndOfClusterChainMarker);
			OpenFile.LogicalSector=p_new.LogicalSector;
			OpenFile.PosInLogicalSector=0;
		}
	}
	
	return STATUS_OK;
}

#if !FS32_NO_SEEK_TELL
FS32_status_t f_seek(const uint32_t pos)
{
	if(!OpenFile.OpenedForReading)
		return SEEK_CANT_SEEK_ON_WRITABLE;
	
	if(pos>=OpenFile.FileSize)
		return SEEK_INVALID_POS;
		
	set_file_pos(pos);
	
	return STATUS_OK;
}

uint32_t f_tell(void)
{
	return OpenFile.PosInFile;
}
#endif

uint32_t get_free_sectors_count(void)
{
	return NbFreeSectors;
}

uint32_t get_file_size(void)
{
	return OpenFile.FileSize;
}
