/**
 ******************************************************************************
 * @file           : emulated_eeprom.c
 * @brief          : Emulated EEPROM module
 ******************************************************************************
 */

/** Includes ------------------------------------------------------------------*/
#include "Flash.h"
#include <string.h>

/* Private macro -------------------------------------------------------------*/
#define FLASH_END_ADDR										0x8020000
#define EMU_SECTORS_NO										1u
#define FLASH_PAGES_PER_EMU_SECTOR							6u
#define EMU_FLASH_START_ADDR								(FLASH_END_ADDR - (EMU_SECTORS_NO * FLASH_PAGES_PER_EMU_SECTOR * FLASH_PAGE_SIZE))

typedef enum{
	PAGE_STATUS_ACTIVE,
	PAGE_STATUS_ERASED = 0xffff
}tPageStatus;

typedef union{
	struct{
		uint16_t page_status;
		uint16_t place_holder;
	};
	uint32_t value;
}tPageHeader;

typedef struct{
	uint16_t logical_addr;
	uint16_t data;
}tDataEntry;

#define PAGE_AVAILABLE_DATA_BYTES							(FLASH_PAGE_SIZE - sizeof(tPageHeader))
#define PAGE_DATA_ENTRIES_MAX_NO							(PAGE_AVAILABLE_DATA_BYTES / sizeof(tDataEntry))
#define MAX_LOGICAL_ADDR_PER_PAGE							PAGE_DATA_ENTRIES_MAX_NO

typedef struct{
	tPageHeader page_header;
	tDataEntry data_entries_arr[PAGE_DATA_ENTRIES_MAX_NO];
}__packed tPage;

#define GET_SECTOR_OF_LOGIC_ADDR(LOGICAL_ADDR)				((LOGICAL_ADDR) / MAX_LOGICAL_ADDR_PER_PAGE)
#define GET_SECTOR_BASE_ADDR(SECTOR_NO)						((EMU_FLASH_START_ADDR) + (sizeof(tPage) * FLASH_PAGES_PER_EMU_SECTOR * SECTOR_NO))
#define GET_SECTORS_PAGE_BASE_ADDR(SECTOR_NO,PAGE_NO)		((EMU_FLASH_START_ADDR) + (sizeof(tPage) * FLASH_PAGES_PER_EMU_SECTOR * SECTOR_NO) + (PAGE_NO * FLASH_PAGE_SIZE))
#define GET_NEXT_PAGE_NO(PAGE_NO)							(((PAGE_NO) + 1u) % FLASH_PAGES_PER_EMU_SECTOR)

typedef struct{
	uint32_t first_empty_loc_addr;
	uint8_t active_page_no;
}tSector_RuntimeContext;

/* Private variables ---------------------------------------------------------*/
static tSector_RuntimeContext sectors_runtime_info_arr[EMU_SECTORS_NO] = {[0] = {0,UINT8_MAX}};

/* Private function prototypes -----------------------------------------------*/
/*** @defgroup
  * @{
  */
static uint32_t Emulated_EEPROM_GetPagesFirstEmptyLocAddr(uint8_t sector_no,uint8_t page_no);
static uint8_t Emulated_EEPROM_CopyDataToNextPage(uint8_t sector_no,uint8_t page_no,tDataEntry *p_entry);
static uint8_t Emulated_EEPROM_SwapToNextPage(uint8_t sector_no,uint8_t curr_page_no,tDataEntry *p_entry);
static int8_t Emulated_EEPROM_GetLatestEntryForLogicalAddr(uint32_t logical_addr,tDataEntry *p_latest_entry);
static tPageHeader Emulated_EEPROM_GetPageHeader(uint8_t sector_no,uint8_t page_no);
static uint8_t Emulated_EEPROM_SetPageHeader(uint8_t sector_no,uint8_t page_no,tPageHeader header);
static uint8_t Emulated_EEPROM_InitSector(uint8_t sector_no);

/**
    *@brief	used @ init getting sectors run time context
    *@param void
    *@retval void
*/
static uint32_t Emulated_EEPROM_GetPagesFirstEmptyLocAddr(uint8_t sector_no,uint8_t page_no)
{
	uint32_t ret = 0;
	uint32_t data_entry_itr = 0;
	tDataEntry *data_entry_base_ptr = ((tPage*)GET_SECTORS_PAGE_BASE_ADDR(sector_no,page_no))->data_entries_arr;

	for(data_entry_itr = 0 ; data_entry_itr < PAGE_DATA_ENTRIES_MAX_NO ; data_entry_itr++)
	{
		if(data_entry_base_ptr->logical_addr == UINT16_MAX)
		{
			break;
		}

		data_entry_base_ptr++;
	}

	ret = (uint32_t)data_entry_base_ptr;

	return ret;
}

/**
    *@brief Used in Emulated_EEPROM_TransitionToNextPage
    *@param void
    *@retval void
*/
static uint8_t Emulated_EEPROM_CopyDataToNextPage(uint8_t sector_no,uint8_t page_no,tDataEntry *p_entry)
{
	uint8_t ret = 0;
	tPage temp_page = {.page_header = {.value = 0}};
	tDataEntry *temp_page_top_entry_ptr = temp_page.data_entries_arr + 1;
	tDataEntry *temp_page_entry_itr_ptr;
	tDataEntry *old_page_data_entry_end_ptr = &((tPage*)GET_SECTORS_PAGE_BASE_ADDR(sector_no,page_no))->data_entries_arr[PAGE_DATA_ENTRIES_MAX_NO - 1];
	tDataEntry *old_page_data_entry_base_ptr = ((tPage*)GET_SECTORS_PAGE_BASE_ADDR(sector_no,page_no))->data_entries_arr;
	uint8_t any_match = 0;
	tDataEntry *flashing_addr_ptr;
	uint8_t next_page_no = GET_NEXT_PAGE_NO(page_no);

	memset(temp_page.data_entries_arr,0xff,sizeof(tDataEntry) * PAGE_DATA_ENTRIES_MAX_NO);
	temp_page.data_entries_arr[0] = *p_entry;

	while(old_page_data_entry_end_ptr >= old_page_data_entry_base_ptr)
	{
		temp_page_entry_itr_ptr = temp_page.data_entries_arr;
		any_match = 0;

		/*Search if current entry already existed in the temp page*/
		while(temp_page_entry_itr_ptr < temp_page_top_entry_ptr &&
				(old_page_data_entry_end_ptr->logical_addr != UINT16_MAX))
		{
			if(old_page_data_entry_end_ptr->logical_addr == temp_page_entry_itr_ptr->logical_addr)
			{
				any_match = 1;
				break;
			}

			temp_page_entry_itr_ptr++;
		}

		/*if not found in temp page*/
		if((!any_match) &&
				old_page_data_entry_end_ptr->logical_addr != UINT16_MAX)
		{
			temp_page_top_entry_ptr->logical_addr = old_page_data_entry_end_ptr->logical_addr;
			temp_page_top_entry_ptr->data = old_page_data_entry_end_ptr->data;
			temp_page_top_entry_ptr++;
		}

		old_page_data_entry_end_ptr--;

		/*break before underflow if any*/
		if(old_page_data_entry_end_ptr == old_page_data_entry_base_ptr)
		{
			break;
		}
	}

	flashing_addr_ptr = (tDataEntry*)(GET_SECTORS_PAGE_BASE_ADDR(sector_no,next_page_no));

	/*if header is not empty or the constructed page is different than the new page content*/
	if(((tPageHeader*)flashing_addr_ptr)->value != UINT32_MAX ||
			(memcmp(((tPage*)flashing_addr_ptr)->data_entries_arr,temp_page.data_entries_arr,sizeof(tDataEntry) * PAGE_DATA_ENTRIES_MAX_NO)))
	{
		/*Erase new page to make sure no data was written in it*/
		ret = Flash_Erase((uint32_t)flashing_addr_ptr,1);

		if(ret)
		{
			flashing_addr_ptr = ((tPage*)flashing_addr_ptr)->data_entries_arr;
			temp_page_entry_itr_ptr = temp_page.data_entries_arr;

			/*Program data entries in the temp page*/
			Flash_Unlock();
			while(temp_page_entry_itr_ptr < temp_page_top_entry_ptr)
			{
				ret = Flash_Program((uint32_t)flashing_addr_ptr,*((uint32_t*)temp_page_entry_itr_ptr),sizeof(tDataEntry) / 2);

				if(ret == 1)
				{
					temp_page_entry_itr_ptr++;
					flashing_addr_ptr++;
				}
				else
				{
					break;
				}
			}
			Flash_Lock();

			sectors_runtime_info_arr[sector_no].first_empty_loc_addr = (uint32_t)flashing_addr_ptr;
		}
	}
	else/*Handling the case where data was swapped and power goes off before header was written then same data was written again*/
	{
		ret = 1;

		sectors_runtime_info_arr[sector_no].first_empty_loc_addr = Emulated_EEPROM_GetPagesFirstEmptyLocAddr(sector_no,next_page_no);
	}

	return ret;
}

/**
    *@brief
    *@param void
    *@retval void
*/
static uint8_t Emulated_EEPROM_SwapToNextPage(uint8_t sector_no,uint8_t curr_page_no,tDataEntry *p_entry)
{
	uint8_t step_itr = 0;
	const uint8_t steps_no = 3;
	uint8_t ret = 0;
	tPageHeader init_page_header;
	uint8_t next_page_no = GET_NEXT_PAGE_NO(curr_page_no);

	do
	{
		switch(step_itr)
		{
		case 0:
			ret = Emulated_EEPROM_CopyDataToNextPage(sector_no,curr_page_no,p_entry);
		break;
		case 1:
			init_page_header.page_status = PAGE_STATUS_ACTIVE;
			ret = Emulated_EEPROM_SetPageHeader(sector_no,next_page_no,init_page_header);
		break;
		case 2:
			sectors_runtime_info_arr[sector_no].active_page_no = next_page_no;

			/*Clean up old page*/
			ret = Flash_Erase(GET_SECTORS_PAGE_BASE_ADDR(sector_no,curr_page_no),1);
		break;
		default:
			break;
		}

		step_itr++;
	}while(step_itr < steps_no &&
			ret);

	return ret;
}

/**
    *@brief
    *@param void
    *@retval void
*/
static int8_t Emulated_EEPROM_GetLatestEntryForLogicalAddr(uint32_t logical_addr,tDataEntry *p_latest_entry)
{
	int8_t ret = -1; /*init with default fault value*/
	uint8_t sector_no = GET_SECTOR_OF_LOGIC_ADDR(logical_addr);
	uint8_t page_no = sectors_runtime_info_arr[sector_no].active_page_no;

	if(page_no < FLASH_PAGES_PER_EMU_SECTOR)
	{
		if(sectors_runtime_info_arr[sector_no].first_empty_loc_addr >= sizeof(tDataEntry))
		{
			ret = 0; /*no fault not found*/

			tDataEntry *p_entry = (tDataEntry*)(sectors_runtime_info_arr[sector_no].first_empty_loc_addr - sizeof(tDataEntry));

			while((uint32_t)p_entry >= (uint32_t)(((tPage*)GET_SECTORS_PAGE_BASE_ADDR(sector_no,page_no))->data_entries_arr))
			{
				if(p_entry->logical_addr == logical_addr)
				{
					ret = 1; /*found*/
					p_latest_entry->logical_addr = logical_addr;
					p_latest_entry->data = p_entry->data;
					break;
				}

				p_entry--;
			}
		}
	}

	return ret;
}

/**
    *@brief
    *@param void
    *@retval void
*/
static tPageHeader Emulated_EEPROM_GetPageHeader(uint8_t sector_no,uint8_t page_no)
{
	tPageHeader ret = {0};
	tPageHeader *page_header_ptr = (tPageHeader *)GET_SECTORS_PAGE_BASE_ADDR(sector_no,page_no);

	ret.page_status = page_header_ptr->page_status;

	return ret;
}

/**
    *@brief Used after Flash_Erase
    *@param void
    *@retval void
*/
static uint8_t Emulated_EEPROM_SetPageHeader(uint8_t sector_no,uint8_t page_no,tPageHeader header)
{
	uint8_t ret = 0;
	Flash_Unlock();
	ret = Flash_Program(GET_SECTORS_PAGE_BASE_ADDR(sector_no,page_no),header.page_status,1);
	Flash_Lock();
	return ret;
}

/**
    *@brief
    *@param void
    *@retval void
*/
static uint8_t Emulated_EEPROM_InitSector(uint8_t sector_no)
{
	uint8_t ret = 0;
	tPageHeader init_page_header;

	ret = Flash_Erase(GET_SECTORS_PAGE_BASE_ADDR(sector_no,0),FLASH_PAGES_PER_EMU_SECTOR);

	if(ret)
	{
		/*Init first sector page as active*/
		init_page_header.page_status = PAGE_STATUS_ACTIVE;
		ret = Emulated_EEPROM_SetPageHeader(sector_no,0,init_page_header);
	}
	return ret;
}

/**
    *@brief Check  : is initialized ,check and maintain integrity and Get run-time required info per sector
    *@param void
    *@retval void
*/
void Emulated_EEPROM_init(void)
{
	uint8_t ret = 0;
	uint8_t sectors_itr = 0;

	for(sectors_itr = 0 ; sectors_itr < EMU_SECTORS_NO ; sectors_itr++)
	{
		uint8_t page_itr;
		uint8_t active_page_no = 0;
		uint8_t was_active_page_found = 0;

		for(page_itr = 0 ; page_itr < FLASH_PAGES_PER_EMU_SECTOR; page_itr++)
		{
			tPageHeader curr_page_header = Emulated_EEPROM_GetPageHeader(sectors_itr,page_itr);

			if(curr_page_header.page_status == PAGE_STATUS_ACTIVE)
			{
				/*if active page was found before*/
				if(was_active_page_found == 1)
				{
					/*if wrapped around*/
					if(active_page_no == 0 &&
							(page_itr == (FLASH_PAGES_PER_EMU_SECTOR - 1)))
					{
						ret = Flash_Erase(GET_SECTORS_PAGE_BASE_ADDR(sectors_itr,page_itr),1);
					}
					else
					{
						ret = Flash_Erase(GET_SECTORS_PAGE_BASE_ADDR(sectors_itr,active_page_no),1);

						if(ret)
						{
							active_page_no = page_itr;
						}
					}
				}
				else
				{
					active_page_no = page_itr;
				}

				was_active_page_found = 1;
			}
			else if(curr_page_header.page_status == PAGE_STATUS_ERASED)
			{
				/*do nothing*/
			}
			else /*Any other header value*/
			{
				/*Do nothing as swap function make sure that page is clean before writing it*/
			}
		}

		if(was_active_page_found == 1)
		{
			/*Get run-time info per sector*/
			sectors_runtime_info_arr[sectors_itr].active_page_no = active_page_no;
			sectors_runtime_info_arr[sectors_itr].first_empty_loc_addr = Emulated_EEPROM_GetPagesFirstEmptyLocAddr(sectors_itr,active_page_no);
		}
		else	/*if no active page found in this sector*/
		{
			/*sector need init*/
			ret = Emulated_EEPROM_InitSector(sectors_itr);

			if(ret)
			{
				sectors_runtime_info_arr[sectors_itr].active_page_no = 0; /*set first page in the sector as the active page*/
				sectors_runtime_info_arr[sectors_itr].first_empty_loc_addr = Emulated_EEPROM_GetPagesFirstEmptyLocAddr(sectors_itr,active_page_no);
			}
		}
	}
}

/**
    *@brief
    *@param void
    *@retval -1 fault
    *@retval 0 empty location
    *@retval 1 found
*/
int8_t Emulated_EEPROM_ReadHalfWord(const uint32_t logical_addr,uint16_t *const p_half_word)
{
	tDataEntry latest_entry = {0xFFFF,0xFFFF}; /*defaults for empty location*/
	int8_t ret = Emulated_EEPROM_GetLatestEntryForLogicalAddr(logical_addr,&latest_entry);

	if(ret == -1) /*fault*/
	{
		Emulated_EEPROM_init();
	}
	else
	{
		*p_half_word = latest_entry.data;
	}

	return ret;
}
/**
    *@brief
    *@param void
    *@retval void
*/
uint8_t Emulated_EEPROM_WriteHalfWord(const uint32_t logical_addr,const uint16_t half_word)
{
	uint8_t ret = 0;
	tDataEntry entry = {.logical_addr = logical_addr ,.data = half_word};
	uint8_t sector_no = GET_SECTOR_OF_LOGIC_ADDR(logical_addr);
	uint8_t page_no = sectors_runtime_info_arr[sector_no].active_page_no;
	uint8_t has_fault = 0;
	uint16_t old_half_word;

	/*empty or found but different*/
	if((Emulated_EEPROM_ReadHalfWord(logical_addr,&old_half_word) != -1) &&
			old_half_word != half_word)
	{
		if(page_no < FLASH_PAGES_PER_EMU_SECTOR)
		{
			tDataEntry *first_empty_loc_ptr = (tDataEntry *)sectors_runtime_info_arr[sector_no].first_empty_loc_addr;

			if((uint32_t)first_empty_loc_ptr > GET_SECTORS_PAGE_BASE_ADDR(sector_no,page_no))
			{
				if(((uint32_t)first_empty_loc_ptr - GET_SECTORS_PAGE_BASE_ADDR(sector_no,page_no)) >= FLASH_PAGE_SIZE) /*within page range*/
				{
					ret = Emulated_EEPROM_SwapToNextPage(sector_no,page_no,&entry);
				}
				else
				{
					Flash_Unlock();
					ret = Flash_Program((uint32_t)first_empty_loc_ptr,*((uint64_t*)&entry),2);
					Flash_Lock();

					if(ret == 1)
					{
						sectors_runtime_info_arr[sector_no].first_empty_loc_addr += sizeof(tDataEntry);
					}
				}
			}
			else
			{
				has_fault = 1;
			}
		}
		else
		{
			has_fault = 1;
		}

		if(has_fault)
		{
			Emulated_EEPROM_init();
		}
	}

	return ret;
}
