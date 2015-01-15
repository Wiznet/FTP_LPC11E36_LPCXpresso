/*
===============================================================================
 Name        : W5500EVB.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
*/

#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include "spi_handler.h"
#include "w5500_init.h"
#include "common.h"
#include "loopback.h"
#include "mmcHandler.h"
#include "dataflashHandler.h"
#include "ftpd.h"
#include "wizchip_conf.h"
#include "ffconf.h"
#include "eepromHandler.h"

#include <cr_section_macros.h>

// TODO: insert other include files here

// TODO: insert other definitions and declarations here

typedef struct __Cfg_Info {
	uint8_t spiflash_flag[2];
} __attribute__((packed)) Cfg_Info;

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#define TICKRATE_HZ1 (1000)		/* 1000 ticks per second, for SysTick */
#define TICKRATE_HZ2 (1)		/* 1 ticks per second, for Timer0 */
volatile uint32_t msTicks; 		/* counts 1ms timeTicks */

////////////////////////////////////////////////
// Shared Buffer Definition for LOOPBACK TEST //
////////////////////////////////////////////////

uint8_t gDATABUF[DATA_BUF_SIZE];
uint8_t gFTPBUF[_MAX_SS];

int g_mkfs_done = 0;
int g_sdcard_done = 0;

////////////////////
// Button Control //
////////////////////
#define BUTTONS_PRESSED_TICKS		10		// ms
bool button1_enable = false;
bool button1_pressed_flag = false;

static void display_SDcard_Info(uint8_t mount_ret);
static uint8_t Check_Buttons_Pressed(void);
void SysTick_Handler(void);

int main(void) {
	uint8_t ret = 0;
	uint8_t sn = 0;
#if defined(F_APP_FTP)
	wiz_NetInfo gWIZNETINFO;
#endif

#if defined (__USE_LPCOPEN)
#if !defined(NO_BOARD_LIB)
    // Read clock settings and update SystemCoreClock variable
    SystemCoreClockUpdate();
    // Set up and initialize all required blocks and
    // functions related to the board hardware
    Board_Init();
    // Set the LED to the state of "On"
    Board_LED_Set(0, true);
    //Board_LED_Set(1, true);
    //Board_LED_Set(2, true);
#endif
#endif

#if 0
    // TODO: insert code here

    // Force the counter to be placed into memory
    volatile static int i = 0 ;
    // Enter an infinite loop, just incrementing a counter
    while(1) {
        i++ ;
    }
#else
	SPI_Init();
	W5500_Init();
	Net_Conf();

	/* Enable and setup SysTick Timer at a periodic rate */
	SysTick_Config(SystemCoreClock / TICKRATE_HZ1);

	/* Initialize buttons on the W5500 EVB board */
	Board_Buttons_Init();

	g_sdcard_done = 0;

#if defined(F_APP_FTP)
	ctlnetwork(CN_GET_NETINFO, (void*) &gWIZNETINFO);
	ftpd_init(CTRL_SOCK, DATA_SOCK, gWIZNETINFO.ip);
#endif

	ret = flash_mount();
	if(ret > 0)
	{
		display_SDcard_Info(ret);
	}

	//df_read_probe();

	while(1) {
#if LOOPBACK_MODE == LOOPBACK_NONBLOCK_API
		//Accept for client
		if((ret = loopback_tcps(sn, gDATABUF, 3000)) < 0)
		{
			printf("%d:loopback_tcps error:%ld\r\n",sn,ret);
			break;
		}
		if((ret = loopback_tcps(sn+1, gDATABUF, 3000 + 1)) < 0)
		{
			printf("%d:loopback_tcps error:%ld\r\n",sn+1,ret);
			break;
		}
		if((ret = loopback_tcps(sn+2, gDATABUF, 3000 + 2)) < 0)
		{
			printf("%d:loopback_tcps error:%ld\r\n",sn+2,ret);
			break;
		}
		if((ret=loopback_udps(sn+3,gDATABUF,10000)) < 0)
		{
			printf("%d:loopback_udps error:%ld\r\n",sn+1,ret);
			break;
		}
#endif
	   	/* Button: SW1 */
		if(Check_Buttons_Pressed() == BUTTONS_BUTTON1)
		{
			printf("\r\n########## SW1 is pressed.\r\n");
			printf("########## spiflash flag is reset.\r\n");
			release_factory_flag();
		}

#if defined(F_APP_FTP)
		ftpd_run(gFTPBUF);
#endif
	}
#endif
    return 0 ;
}

static void display_SDcard_Info(uint8_t mount_ret)
{
	uint32_t totalSize = 0, availableSize = 0;

	printf("\r\n - Storage mount succeed\r\n");
	printf(" - Type : ");

	switch(mount_ret)
	{
		case CARD_MMC: printf("MMC\r\n"); 	break;
		case CARD_SD: printf("SD\r\n"); 	break;
		case CARD_SD2: printf("SD2\r\n"); 	break;
		case CARD_SDHC: printf("SDHC\r\n"); break;
		case SPI_FLASHM: printf("sFlash\r\n"); break;
		default: printf("\r\n"); 	break;
	}

	if(_MAX_SS == 512)
	{
		getMountedMemorySize(mount_ret, &totalSize, &availableSize);
		printf(" - Available Memory Size : %ld kB / %ld kB ( %ld kB is used )\r\n", availableSize, totalSize, (totalSize - availableSize));
	}
	printf("\r\n");
}

static uint8_t Check_Buttons_Pressed(void)
{
	static uint8_t buttons_status;
	static uint8_t ret;

	buttons_status = Buttons_GetStatus();

	if((buttons_status & BUTTONS_BUTTON1) == BUTTONS_BUTTON1) button1_enable = true; // button pressed check timer enable
	else button1_enable = false;

	if(button1_pressed_flag)	// button1 pressed (Specified time elapsed, enabled by sysTick_Handler function)
	{
		button1_pressed_flag = false; // pressed button clear
		ret = BUTTONS_BUTTON1; // return pressed button status
	}
	else
	{
		ret = 0;
	}

	return ret;
}

void SysTick_Handler(void)
{
	static uint16_t button1_pressed_check_cnt = 0;
	static bool button1_press_detected = false;

	msTicks++; // increment counter

	// Button1 control
	if(button1_enable == true)
	{
		if(!button1_press_detected)
		{
			button1_pressed_check_cnt++;
			if(button1_pressed_check_cnt >= BUTTONS_PRESSED_TICKS)
			{
				button1_pressed_flag = true;
				button1_pressed_check_cnt = 0;
				button1_enable = false;

				button1_press_detected = true;
			}
		}
	}
	else
	{
		button1_pressed_check_cnt = 0;
		button1_press_detected = false;
	}
}

#if defined(F_SPI_FLASH)
int check_spiflash_flag(void)
{
	int ret = 0;
	Cfg_Info cfg_info;

	read_eeprom(0, &cfg_info, sizeof(Cfg_Info));

	if(cfg_info.spiflash_flag[0] == 0xAE && cfg_info.spiflash_flag[1] == 0xAE)
	{
		ret = 0;
	}
	else
	{
		ret = 1;
	}

	return ret;
}

void save_spiflash_flag(void)
{
	Cfg_Info cfg_info;

	cfg_info.spiflash_flag[0] = 0xAE;
	cfg_info.spiflash_flag[1] = 0xAE;

	write_eeprom(0, &cfg_info, sizeof(Cfg_Info));
}

void release_factory_flag(void)
{
	Cfg_Info cfg_info;

	cfg_info.spiflash_flag[0] = 0xFF;
	cfg_info.spiflash_flag[1] = 0xFF;

	write_eeprom(0, &cfg_info, sizeof(Cfg_Info));
}
#endif
