/*
 * evm.c
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <asm/cache.h>
#include <asm/arch/cpu.h>
#include <asm/arch/ddr_defs.h>
#include <asm/arch/hardware.h>
#include <asm/arch/sys_proto.h>
#include <asm/arch/mem.h>
#include <asm/arch/nand.h>
#include <linux/mtd/nand.h>
#include <nand.h>
#include <net.h>
#include <miiphy.h>
#include <netdev.h>
#include <spi_flash.h>
#include "common_def.h"
#include <i2c.h>
#include <serial.h>

DECLARE_GLOBAL_DATA_PTR;

/* UART Defines */
#define UART_SYSCFG_OFFSET	(0x54)
#define UART_SYSSTS_OFFSET	(0x58)

#define UART_RESET		(0x1 << 1)
#define UART_CLK_RUNNING_MASK	0x1
#define UART_SMART_IDLE_EN	(0x1 << 0x3)

/* Timer Defines */
#define TSICR_REG		0x54
#define TIOCP_CFG_REG		0x10
#define TCLR_REG		0x38

/* CPLD registers */
#define CFG_REG			0x10

/*
 * I2C Address of various board
 */
#define I2C_BASE_BOARD_ADDR	0x50
#define I2C_DAUGHTER_BOARD_ADDR 0x51
#define I2C_LCD_BOARD_ADDR	0x52

#define I2C_CPLD_ADDR		0x35

/* RGMII mode define */
#define RGMII_MODE_ENABLE	0xA

/* TLK110 PHY registers */
#define TLK110_COARSEGAIN_REG	0x00A3
#define TLK110_LPFHPF_REG	0x00AC
#define TLK110_SPAREANALOG_REG	0x00B9
#define TLK110_VRCR_REG		0x00D0
#define TLK110_SETFFE_REG	0x0107
#define TLK110_FTSP_REG		0x0154
#define TLK110_ALFATPIDL_REG	0x002A
#define TLK110_PSCOEF21_REG	0x0096
#define TLK110_PSCOEF3_REG	0x0097
#define TLK110_ALFAFACTOR1_REG	0x002C
#define TLK110_ALFAFACTOR2_REG	0x0023
#define TLK110_CFGPS_REG	0x0095
#define TLK110_FTSPTXGAIN_REG	0x0150
#define TLK110_SWSCR3_REG	0x000B
#define TLK110_SCFALLBACK_REG	0x0040
#define TLK110_PHYRCR_REG	0x001F

/* TLK110 register writes values */
#define TLK110_COARSEGAIN_VAL	0x0000
#define TLK110_LPFHPF_VAL	0x8000
#define TLK110_SPAREANALOG_VAL	0x0000
#define TLK110_VRCR_VAL		0x0008
#define TLK110_SETFFE_VAL	0x0605
#define TLK110_FTSP_VAL		0x0255
#define TLK110_ALFATPIDL_VAL	0x7998
#define TLK110_PSCOEF21_VAL	0x3A20
#define TLK110_PSCOEF3_VAL	0x003F
#define TLK110_ALFAFACTOR1_VAL	0xFF80
#define TLK110_ALFAFACTOR2_VAL	0x021C
#define TLK110_CFGPS_VAL	0x0000
#define TLK110_FTSPTXGAIN_VAL	0x6A88
#define TLK110_SWSCR3_VAL	0x0000
#define TLK110_SCFALLBACK_VAL	0xC11D
#define TLK110_PHYRCR_VAL	0x4000
#define TLK110_PHYIDR1		0x2000
#define TLK110_PHYIDR2		0xA201

struct dghtr_brd_id_hdr {
	unsigned int  header;
	char board_name[8];
	char version[4];
	char serial_num[12];
	unsigned char config[32];
};

#define NO_OF_MAC_ADDR          3
#define ETH_ALEN		6
struct basebrd_id_hdr {
	unsigned int  header;
	char board_name[8];
	char version[4];
	char serial_num[12];
	unsigned char config[32];
	unsigned char mac_addr[NO_OF_MAC_ADDR][ETH_ALEN];
};

static struct basebrd_id_hdr brd_id_hdr;

static struct dghtr_brd_id_hdr db_header[NO_OF_DAUGHTER_BOARDS] = {
	{
	.header = 0xAA5533EE,
	.board_name = "A335GPBD", /* General purpose daughter board */
	.version = "1.0A",
	},
	{
	.header = 0xAA5533EE,
	.board_name = "A335IAMC", /* Industrial automation board */
	.version = "1.0A",

	},
	{
	.header = 0xAA5533EE,
	.board_name = "A335IPPH", /* IP Phone daughter board */
	.version = "1.0A",
	}
};

extern void cpsw_eth_set_mac_addr(const u_int8_t *addr);

static void init_timer(void)
{
	/* Reset the Timer */
	__raw_writel(0x2, (DM_TIMER2_BASE + TSICR_REG));

	/* Wait until the reset is done */
	while (__raw_readl(DM_TIMER2_BASE + TIOCP_CFG_REG) & 1);

	/* Start the Timer */
	__raw_writel(0x1, (DM_TIMER2_BASE + TCLR_REG));
}

int dram_init(void)
{
	/* Fill up board info */
	gd->bd->bi_dram[0].start = PHYS_DRAM_1;
	gd->bd->bi_dram[0].size = PHYS_DRAM_1_SIZE;

	return 0;
}

int misc_init_r(void)
{
#ifdef CONFIG_AM335X_MIN_CONFIG
	printf("The 2nd stage U-Boot will now be auto-loaded\n");
	printf("Please do not interrupt the countdown till TIAM335X_EVM prompt \
		if 2nd stage is already flashed\n");
#endif

	return 0;
}

#ifdef CONFIG_AM335X_CONFIG_DDR

static void Data_Macro_Config(int dataMacroNum)
{
	u32 BaseAddrOffset;

	if (dataMacroNum == 0)
		BaseAddrOffset = 0x00;
	else if (dataMacroNum == 1)
		BaseAddrOffset = 0xA4;

	__raw_writel(((DDR2_RD_DQS<<30)|(DDR2_RD_DQS<<20)
			|(DDR2_RD_DQS<<10)|(DDR2_RD_DQS<<0)),
			(DATA0_RD_DQS_SLAVE_RATIO_0 + BaseAddrOffset));
	__raw_writel(DDR2_RD_DQS>>2,
			(DATA0_RD_DQS_SLAVE_RATIO_1 + BaseAddrOffset));
	__raw_writel(((DDR2_WR_DQS<<30)|(DDR2_WR_DQS<<20)
			|(DDR2_WR_DQS<<10)|(DDR2_WR_DQS<<0)),
			(DATA0_WR_DQS_SLAVE_RATIO_0 + BaseAddrOffset));
	__raw_writel(DDR2_WR_DQS>>2,
			(DATA0_WR_DQS_SLAVE_RATIO_1 + BaseAddrOffset));
	__raw_writel(((DDR2_PHY_WRLVL<<30)|(DDR2_PHY_WRLVL<<20)
			|(DDR2_PHY_WRLVL<<10)|(DDR2_PHY_WRLVL<<0)),
			(DATA0_WRLVL_INIT_RATIO_0 + BaseAddrOffset));
	__raw_writel(DDR2_PHY_WRLVL>>2,
			(DATA0_WRLVL_INIT_RATIO_1 + BaseAddrOffset));
	__raw_writel(((DDR2_PHY_GATELVL<<30)|(DDR2_PHY_GATELVL<<20)
			|(DDR2_PHY_GATELVL<<10)|(DDR2_PHY_GATELVL<<0)),
			(DATA0_GATELVL_INIT_RATIO_0 + BaseAddrOffset));
	__raw_writel(DDR2_PHY_GATELVL>>2,
			(DATA0_GATELVL_INIT_RATIO_1 + BaseAddrOffset));
	__raw_writel(((DDR2_PHY_FIFO_WE<<30)|(DDR2_PHY_FIFO_WE<<20)
			|(DDR2_PHY_FIFO_WE<<10)|(DDR2_PHY_FIFO_WE<<0)),
			(DATA0_FIFO_WE_SLAVE_RATIO_0 + BaseAddrOffset));
	__raw_writel(DDR2_PHY_FIFO_WE>>2,
			(DATA0_FIFO_WE_SLAVE_RATIO_1 + BaseAddrOffset));
	__raw_writel(((DDR2_PHY_WR_DATA<<30)|(DDR2_PHY_WR_DATA<<20)
			|(DDR2_PHY_WR_DATA<<10)|(DDR2_PHY_WR_DATA<<0)),
			(DATA0_WR_DATA_SLAVE_RATIO_0 + BaseAddrOffset));
	__raw_writel(DDR2_PHY_WR_DATA>>2,
			(DATA0_WR_DATA_SLAVE_RATIO_1 + BaseAddrOffset));
	__raw_writel(PHY_DLL_LOCK_DIFF,
			(DATA0_DLL_LOCK_DIFF_0 + BaseAddrOffset));
}

static void Cmd_Macro_Config(void)
{
	__raw_writel(DDR2_RATIO, CMD0_CTRL_SLAVE_RATIO_0);
	__raw_writel(CMD_FORCE, CMD0_CTRL_SLAVE_FORCE_0);
	__raw_writel(CMD_DELAY, CMD0_CTRL_SLAVE_DELAY_0);
	__raw_writel(DDR2_DLL_LOCK_DIFF, CMD0_DLL_LOCK_DIFF_0);
	__raw_writel(DDR2_INVERT_CLKOUT, CMD0_INVERT_CLKOUT_0);

	__raw_writel(DDR2_RATIO, CMD1_CTRL_SLAVE_RATIO_0);
	__raw_writel(CMD_FORCE, CMD1_CTRL_SLAVE_FORCE_0);
	__raw_writel(CMD_DELAY, CMD1_CTRL_SLAVE_DELAY_0);
	__raw_writel(DDR2_DLL_LOCK_DIFF, CMD1_DLL_LOCK_DIFF_0);
	__raw_writel(DDR2_INVERT_CLKOUT, CMD1_INVERT_CLKOUT_0);

	__raw_writel(DDR2_RATIO, CMD2_CTRL_SLAVE_RATIO_0);
	__raw_writel(CMD_FORCE, CMD2_CTRL_SLAVE_FORCE_0);
	__raw_writel(CMD_DELAY, CMD2_CTRL_SLAVE_DELAY_0);
	__raw_writel(DDR2_DLL_LOCK_DIFF, CMD2_DLL_LOCK_DIFF_0);
	__raw_writel(DDR2_INVERT_CLKOUT, CMD2_INVERT_CLKOUT_0);
}

static void config_emif(void)
{
	u32 i;

	__raw_writel(__raw_readl(VTP0_CTRL_REG) | VTP_CTRL_ENABLE,
			VTP0_CTRL_REG);
	__raw_writel(__raw_readl(VTP0_CTRL_REG) & (~VTP_CTRL_START_EN),
			VTP0_CTRL_REG);
	 __raw_writel(__raw_readl(VTP0_CTRL_REG) | VTP_CTRL_START_EN,
			VTP0_CTRL_REG);

	/* Poll for READY */
	while ((__raw_readl(VTP0_CTRL_REG) & VTP_CTRL_READY) != VTP_CTRL_READY);

	__raw_writel(__raw_readl(EMIF4_0_IODFT_TLGC) | DDR_PHY_RESET,
			EMIF4_0_IODFT_TLGC); /* Reset DDRr PHY */
	/* Wait while PHY is ready  */
	while ((__raw_readl(EMIF4_0_SDRAM_STATUS) & DDR_PHY_READY) == 0);

	__raw_writel(__raw_readl(EMIF4_0_IODFT_TLGC) | DDR_FUNCTIONAL_MODE_EN,
			EMIF4_0_IODFT_TLGC); /* Start Functional Mode */

	/*Program EMIF0 CFG Registers*/
	__raw_writel(EMIF_READ_LATENCY, EMIF4_0_DDR_PHY_CTRL_1);
	__raw_writel(EMIF_READ_LATENCY, EMIF4_0_DDR_PHY_CTRL_1_SHADOW);
	__raw_writel(EMIF_READ_LATENCY, EMIF4_0_DDR_PHY_CTRL_2);
	__raw_writel(EMIF_TIM1, EMIF4_0_SDRAM_TIM_1);
	__raw_writel(EMIF_TIM1, EMIF4_0_SDRAM_TIM_1_SHADOW);
	__raw_writel(EMIF_TIM2, EMIF4_0_SDRAM_TIM_2);
	__raw_writel(EMIF_TIM2, EMIF4_0_SDRAM_TIM_2_SHADOW);
	__raw_writel(EMIF_TIM3, EMIF4_0_SDRAM_TIM_3);
	__raw_writel(EMIF_TIM3, EMIF4_0_SDRAM_TIM_3_SHADOW);

	__raw_writel(EMIF_SDCFG, EMIF4_0_SDRAM_CONFIG);
	__raw_writel(EMIF_SDCFG, EMIF4_0_SDRAM_CONFIG2);

	/* __raw_writel(EMIF_SDMGT, EMIF0_0_SDRAM_MGMT_CTRL);
	__raw_writel(EMIF_SDMGT, EMIF0_0_SDRAM_MGMT_CTRL_SHD); */
	__raw_writel(0x00004650, EMIF4_0_SDRAM_REF_CTRL);
	__raw_writel(0x00004650, EMIF4_0_SDRAM_REF_CTRL_SHADOW);

	for(i = 0; i<10000; i++)
	{

	}

	__raw_writel(EMIF_SDCFG, EMIF4_0_SDRAM_CONFIG);
	__raw_writel(EMIF_SDCFG, EMIF4_0_SDRAM_CONFIG2);

	/* __raw_writel(EMIF_SDMGT, EMIF0_0_SDRAM_MGMT_CTRL);
	__raw_writel(EMIF_SDMGT, EMIF0_0_SDRAM_MGMT_CTRL_SHD); */
	__raw_writel(EMIF_SDREF,EMIF4_0_SDRAM_REF_CTRL);
	__raw_writel(EMIF_SDREF, EMIF4_0_SDRAM_REF_CTRL_SHADOW);
}

static void config_am335x_mddr(void)
{
	int data_macro_0 = 0;
	int data_macro_1 = 1;
	enable_ddr_clocks();

	Cmd_Macro_Config();

	Data_Macro_Config(data_macro_0);
	Data_Macro_Config(data_macro_1);

	__raw_writel(PHY_RANK0_DELAY, DATA0_RANK0_DELAYS_0);
	__raw_writel(PHY_RANK0_DELAY, DATA1_RANK0_DELAYS_0);

	__raw_writel(DDR_IOCTRL_VALUE, DDR_CMD0_IOCTRL);
	__raw_writel(DDR_IOCTRL_VALUE, DDR_CMD1_IOCTRL);
	__raw_writel(DDR_IOCTRL_VALUE, DDR_CMD2_IOCTRL);
	__raw_writel(DDR_IOCTRL_VALUE, DDR_DATA0_IOCTRL);
	__raw_writel(DDR_IOCTRL_VALUE, DDR_DATA1_IOCTRL);

	__raw_writel(__raw_readl(DDR_IO_CTRL) | 0x10000000, DDR_IO_CTRL);
	__raw_writel(__raw_readl(DDR_CKE_CTRL) | 0x00000001, DDR_CKE_CTRL);

	config_emif();	/* vtp enable is here */
}

static void config_am335x_ddr2(void)
{
	int data_macro_0 = 0;
	int data_macro_1 = 1;
	enable_ddr_clocks();

	Cmd_Macro_Config();

	Data_Macro_Config(data_macro_0);
	Data_Macro_Config(data_macro_1);

	__raw_writel(PHY_RANK0_DELAY, DATA0_RANK0_DELAYS_0);
	__raw_writel(PHY_RANK0_DELAY, DATA1_RANK0_DELAYS_0);


	__raw_writel(__raw_readl(DDR_IO_CTRL) | 0x10000000, DDR_IO_CTRL);

	__raw_writel(__raw_readl(DDR_CKE_CTRL) | 0x00000001, DDR_CKE_CTRL);

	config_emif();
}

static void config_am335x_ddr(void)
{
	config_am335x_mddr(); /* Do DDR settings for 13x13 */
	/* config_am335x_ddr2(); */  /* TODO DDR settings for 15x15 */
}

#endif

/*
 * early system init of muxing and clocks.
 */
void s_init(u32 in_ddr)
{
	/* Can be removed as A8 comes up with L2 enabled */
	l2_cache_enable();

	/* WDT1 is already running when the bootloader gets control
	 * Disable it to avoid "random" resets
	 */
	__raw_writel(0xAAAA, WDT_WSPR);
	while(__raw_readl(WDT_WWPS) != 0x0);
	__raw_writel(0x5555, WDT_WSPR);
	while(__raw_readl(WDT_WWPS) != 0x0);

	/* Setup the PLLs and the clocks for the peripherals */
#ifdef CONFIG_SETUP_PLL
	pll_init();
#endif

#ifdef CONFIG_AM335X_CONFIG_DDR
	if (!in_ddr)
		config_am335x_ddr();
#endif
}


static unsigned char daughter_board_id = BASE_BOARD_ONLY;
/*
 * Daughter board detection: All boards have i2c based EEPROM in all the
 * profiles. Base boards and daughter boards are assigned unique I2C Address.
 * We probe for daughter card and then if sucessful, read the EEPROM to find
 * daughter card type.
 */
static void detect_daughter_board(void)
{
	struct dghtr_brd_id_hdr st_dghtr_brd_id_hdr;
	unsigned char db_board_id = GP_DAUGHTER_BOARD;

	/* Check if daughter board is conneted */
	if (i2c_probe(I2C_DAUGHTER_BOARD_ADDR))
		return;

	/* read the eeprom using i2c */
	if (i2c_read(I2C_DAUGHTER_BOARD_ADDR, 0, 1, (uchar *)
		(&st_dghtr_brd_id_hdr), sizeof(struct dghtr_brd_id_hdr)))
		return;

	do {
		if (!strncmp(db_header[db_board_id].board_name,
				st_dghtr_brd_id_hdr.board_name, 8)) {
			daughter_board_id = db_board_id;
			break;
		}
	} while (++db_board_id < NO_OF_DAUGHTER_BOARDS);
}

static unsigned char evm_profile = PROFILE_NONE;
static void detect_daughter_board_profile(void)
{
	unsigned short val;

	if (i2c_probe(I2C_CPLD_ADDR))
		return;

	if (i2c_read(I2C_CPLD_ADDR, CFG_REG, 1, (unsigned char *)(&val), 2))
		return;

	evm_profile = (val & 0x7);
}

/*
 * Basic board specific setup
 */
#ifdef CONFIG_AM335X_MIN_CONFIG
int board_min_init(void)
{
	u32 regVal;
	u32 uart_base = DEFAULT_UART_BASE;

	/* IA Motor Control Board has default console on UART3*/
	if (daughter_board_id == IA_DAUGHTER_BOARD) {
		uart_base = UART3_BASE;
	}

	/* UART softreset */
	regVal = __raw_readl(uart_base + UART_SYSCFG_OFFSET);
	regVal |= UART_RESET;
	__raw_writel(regVal, (uart_base + UART_SYSCFG_OFFSET) );
	while ((__raw_readl(uart_base + UART_SYSSTS_OFFSET) &
			UART_CLK_RUNNING_MASK) != UART_CLK_RUNNING_MASK);

	/* Disable smart idle */
	regVal = __raw_readl((uart_base + UART_SYSCFG_OFFSET));
	regVal |= UART_SMART_IDLE_EN;
	__raw_writel(regVal, (uart_base + UART_SYSCFG_OFFSET));

	/* Initialize the Timer */
	init_timer();

	return 0;
}
#else
int board_evm_init(void)
{
	/* mach type passed to kernel */
	gd->bd->bi_arch_number = MACH_TYPE_TIAM335EVM;

	/* address of boot parameters */
	gd->bd->bi_boot_params = PHYS_DRAM_1 + 0x100;

	return 0;
}
#endif

struct serial_device *default_serial_console(void)
{

	if (daughter_board_id != IA_DAUGHTER_BOARD) {
		return &eserial1_device;	/* UART0 */
	} else {
		/* Change console to tty03 for IA Motor Control EVM */
		setenv("console", "ttyO3,115200n8");

		return &eserial4_device;	/* UART3 */
	}
}

int board_init(void)
{
	char sku_config[7];
	unsigned char dghtr_brd_valid = FALSE;

	/* Configure the i2c0 pin mux */
	enable_i2c0_pin_mux();

	i2c_init(CONFIG_SYS_I2C_SPEED, CONFIG_SYS_I2C_SLAVE);

	/* Check if baseboard eeprom is available */
	if (i2c_probe(I2C_BASE_BOARD_ADDR))
		return 1; /* something is seriously wrong */

	/* read the eeprom using i2c */
	if (i2c_read(I2C_BASE_BOARD_ADDR, 0, 1, (uchar *)
		(&brd_id_hdr), sizeof(struct basebrd_id_hdr)))
		return 1;	/* something is seriously wrong */

	detect_daughter_board();

	if ((daughter_board_id == GP_DAUGHTER_BOARD) ||
				(daughter_board_id == IA_DAUGHTER_BOARD))
		detect_daughter_board_profile();

	memcpy(sku_config, brd_id_hdr.config, 6);
	sku_config[6] = '\0';

	/*
	* If any daughter board is already detected, daughter board detection
	* logic	detect_daughter_board() would have changed
	* daughter_board_id to appropriate evm id. If not set means no daughter
	* board is detected.
	*/
	if (daughter_board_id != BASE_BOARD_ONLY)
		dghtr_brd_valid = TRUE;

	if (!strncmp("SKU#00", sku_config, 6) &&
			(daughter_board_id == BASE_BOARD_ONLY))
		configure_evm_pin_mux(BASE_BOARD_ONLY, PROFILE_NONE,
							dghtr_brd_valid);
	else if (!strncmp("SKU#01", sku_config, 6) &&
			(daughter_board_id == GP_DAUGHTER_BOARD))
		configure_evm_pin_mux(GP_DAUGHTER_BOARD, (1L << evm_profile),
							dghtr_brd_valid);
	else if (!strncmp("SKU#02", sku_config, 6) &&
			(daughter_board_id == IA_DAUGHTER_BOARD))
		configure_evm_pin_mux(IA_DAUGHTER_BOARD, (1L << evm_profile),
							dghtr_brd_valid);
	else if (!strncmp("SKU#03", sku_config, 6) &&
			(daughter_board_id == IPP_DAUGHTER_BOARD))
		configure_evm_pin_mux(IPP_DAUGHTER_BOARD, PROFILE_NONE,
							dghtr_brd_valid);
#ifdef CONFIG_AM335X_MIN_CONFIG
	board_min_init();
#else
	board_evm_init();
#endif

	gpmc_init();

	return 0;
}

#ifdef BOARD_LATE_INIT
/*
 * SPI bus number is switched to in case Industrial Automation
 * motor control EVM.
 */
static void set_spi_bus_on_board_detect(void){
	if (daughter_board_id == IA_DAUGHTER_BOARD)
		setenv("spi_bus_no", "1");
}

int board_late_init(void){
		set_spi_bus_on_board_detect();
		return 0;
}
#endif

/* Display the board info */
int checkboard(void)
{
	if (daughter_board_id == GP_DAUGHTER_BOARD)
		printf("board: General purpose daughter card connected\n");
	else if (daughter_board_id == IA_DAUGHTER_BOARD)
		printf("board: IA motor control daughter card connected\n");
	else if (daughter_board_id == IPP_DAUGHTER_BOARD)
		printf("board: IPP daughter card connected\n");
	else
		printf("board: No daughter card connected\n");

#ifdef CONFIG_AM335X_MIN_CONFIG
#ifdef CONFIG_NAND
	if ((daughter_board_id == GP_DAUGHTER_BOARD) &&
		(((1L << evm_profile)  == PROFILE_2) ||
			((1L << evm_profile) == PROFILE_3)))
		printf("NAND boot: Profile setting is wrong!!\n");
#endif
#ifdef CONFIG_NOR
	if ((daughter_board_id != GP_DAUGHTER_BOARD) ||
		((1L << evm_profile) != PROFILE_3))
		printf("NOR boot: Profile setting is wrong!!\n");
#endif
#endif
	return 0;
}

/*
 * Reset the CPU
 */
void reset_cpu(ulong addr)
{
	addr = __raw_readl(PRM_DEVICE_RSTCTRL);
	addr &= ~BIT(1);
	addr |= BIT(1);
	__raw_writel(addr, PRM_DEVICE_RSTCTRL);
}

#ifdef CONFIG_DRIVER_TI_CPSW
/* TODO : Check for the board specific PHY */
static void phy_init(char *name, int addr)
{
	unsigned short val;
	unsigned int   cntr = 0;
	unsigned short phyid1, phyid2;

	/* This is done as a workaround to support TLK110 rev1.0 phy */
	/* Reading phy identification register 1 */
	if (miiphy_read(name, addr, PHY_PHYIDR1, &phyid1) != 0) {
		printf("failed to read phyid1\n");
		return;
	}

	/* Reading phy identification register 2 */
	if (miiphy_read(name, addr, PHY_PHYIDR2, &phyid2) != 0) {
		printf("failed to read phyid2\n");
		return;
	}
	if ((phyid1 == TLK110_PHYIDR1) && (phyid2 == TLK110_PHYIDR2)) {
		miiphy_read(name, addr, TLK110_COARSEGAIN_REG, &val);
		val |= TLK110_COARSEGAIN_VAL;
		miiphy_write(name, addr, TLK110_COARSEGAIN_REG, val);

		miiphy_read(name, addr, TLK110_LPFHPF_REG, &val);
		val |= TLK110_LPFHPF_VAL;
		miiphy_write(name, addr, TLK110_LPFHPF_REG, val);

		miiphy_read(name, addr, TLK110_SPAREANALOG_REG, &val);
		val |= TLK110_SPAREANALOG_VAL;
		miiphy_write(name, addr, TLK110_SPAREANALOG_REG, val);

		miiphy_read(name, addr, TLK110_VRCR_REG, &val);
		val |= TLK110_VRCR_VAL;
		miiphy_write(name, addr, TLK110_VRCR_REG, val);

		miiphy_read(name, addr, TLK110_SETFFE_REG, &val);
		val |= TLK110_SETFFE_VAL;
		miiphy_write(name, addr, TLK110_SETFFE_REG, val);

		miiphy_read(name, addr, TLK110_FTSP_REG, &val);
		val |= TLK110_FTSP_VAL;
		miiphy_write(name, addr, TLK110_FTSP_REG, val);

		miiphy_read(name, addr, TLK110_ALFATPIDL_REG, &val);
		val |= TLK110_ALFATPIDL_VAL;
		miiphy_write(name, addr, TLK110_ALFATPIDL_REG, val);

		miiphy_read(name, addr, TLK110_PSCOEF21_REG, &val);
		val |= TLK110_PSCOEF21_VAL;
		miiphy_write(name, addr, TLK110_PSCOEF21_REG, val);

		miiphy_read(name, addr, TLK110_PSCOEF3_REG, &val);
		val |= TLK110_PSCOEF3_VAL;
		miiphy_write(name, addr, TLK110_PSCOEF3_REG, val);

		miiphy_read(name, addr, TLK110_ALFAFACTOR1_REG, &val);
		val |= TLK110_ALFAFACTOR1_VAL;
		miiphy_write(name, addr, TLK110_ALFAFACTOR1_REG, val);

		miiphy_read(name, addr, TLK110_ALFAFACTOR2_REG, &val);
		val |= TLK110_ALFAFACTOR2_VAL;
		miiphy_write(name, addr, TLK110_ALFAFACTOR2_REG, val);

		miiphy_read(name, addr, TLK110_CFGPS_REG, &val);
		val |= TLK110_CFGPS_VAL;
		miiphy_write(name, addr, TLK110_CFGPS_REG, val);

		miiphy_read(name, addr, TLK110_FTSPTXGAIN_REG, &val);
		val |= TLK110_FTSPTXGAIN_VAL;
		miiphy_write(name, addr, TLK110_FTSPTXGAIN_REG, val);

		miiphy_read(name, addr, TLK110_SWSCR3_REG, &val);
		val |= TLK110_SWSCR3_VAL;
		miiphy_write(name, addr, TLK110_SWSCR3_REG, val);

		miiphy_read(name, addr, TLK110_SCFALLBACK_REG, &val);
		val |= TLK110_SCFALLBACK_VAL;
		miiphy_write(name, addr, TLK110_SCFALLBACK_REG, val);

		miiphy_read(name, addr, TLK110_PHYRCR_REG, &val);
		val |= TLK110_PHYRCR_VAL;
		miiphy_write(name, addr, TLK110_PHYRCR_REG, val);
	}

	/* Enable Autonegotiation */
	if (miiphy_read(name, addr, PHY_BMCR, &val) != 0) {
		printf("failed to read bmcr\n");
		return;
	}
	val |= PHY_BMCR_DPLX | PHY_BMCR_AUTON | PHY_BMCR_100_MBPS;
	if (miiphy_write(name, addr, PHY_BMCR, val) != 0) {
		printf("failed to write bmcr\n");
		return;
	}
	miiphy_read(name, addr, PHY_BMCR, &val);

	/* Setup GIG advertisement only if it is not IA board */
	if (daughter_board_id != IA_DAUGHTER_BOARD) {
		miiphy_read(name, addr, PHY_1000BTCR, &val);
		val |= PHY_1000BTCR_1000FD;
		val &= ~PHY_1000BTCR_1000HD;
		miiphy_write(name, addr, PHY_1000BTCR, val);
		miiphy_read(name, addr, PHY_1000BTCR, &val);
	}

	/* Setup general advertisement */
	if (miiphy_read(name, addr, PHY_ANAR, &val) != 0) {
		printf("failed to read anar\n");
		return;
	}
	val |= (PHY_ANLPAR_10 | PHY_ANLPAR_10FD | PHY_ANLPAR_TX |
		PHY_ANLPAR_TXFD);
	if (miiphy_write(name, addr, PHY_ANAR, val) != 0) {
		printf("failed to write anar\n");
		return;
	}
	miiphy_read(name, addr, PHY_ANAR, &val);

	/* Restart auto negotiation*/
	miiphy_read(name, addr, PHY_BMCR, &val);
	val |= PHY_BMCR_RST_NEG;
	miiphy_write(name, addr, PHY_BMCR, val);

	/*check AutoNegotiate complete - it can take upto 3 secs*/
	do {
		udelay(40000);
		cntr++;
		if (!miiphy_read(name, addr, PHY_BMSR, &val)) {
			if (val & PHY_BMSR_AUTN_COMP)
				break;
		}
	} while (cntr < 250);

	if (cntr >= 250)
		printf("Auto negotitation failed\n");

	return;
}

static void cpsw_control(int enabled)
{
	/* nothing for now */
	/* TODO : VTP was here before */
	return;
}

static struct cpsw_slave_data cpsw_slaves[] = {
	{
		.slave_reg_ofs  = 0x208,
		.sliver_reg_ofs = 0xd80,
		.phy_id		= 0,
	},
	{
		.slave_reg_ofs  = 0x308,
		.sliver_reg_ofs = 0xdc0,
		.phy_id         = 1,
	},
};

static struct cpsw_platform_data cpsw_data = {
	.mdio_base	= AM335X_CPSW_MDIO_BASE,
	.cpsw_base	= AM335X_CPSW_BASE,
	.mdio_div	= 0xff,
	.channels	= 8,
	.cpdma_reg_ofs	= 0x800,
	.slaves		= 1,
	.slave_data	= cpsw_slaves,
	.ale_reg_ofs	= 0xd00,
	.ale_entries	= 1024,
	.host_port_reg_ofs	= 0x108,
	.hw_stats_reg_ofs	= 0x900,
	.mac_control		= (1 << 5) /* MIIEN      */,
	.control		= cpsw_control,
	.phy_init		= phy_init,
	.host_port_num		= 0,
	.version		= CPSW_CTRL_VERSION_2,
};

int board_eth_init(bd_t *bis)
{
	u_int8_t mac_addr[6];
	u_int32_t mac_hi, mac_lo;
	u_int32_t i;

	if (!eth_getenv_enetaddr("ethaddr", mac_addr)) {
		printf("<ethaddr> not set. Reading from E-fuse\n");
		/* try reading mac address from efuse */
		mac_lo = __raw_readl(MAC_ID0_LO);
		mac_hi = __raw_readl(MAC_ID0_HI);
		mac_addr[0] = mac_hi & 0xFF;
		mac_addr[1] = (mac_hi & 0xFF00) >> 8;
		mac_addr[2] = (mac_hi & 0xFF0000) >> 16;
		mac_addr[3] = (mac_hi & 0xFF000000) >> 24;
		mac_addr[4] = mac_lo & 0xFF;
		mac_addr[5] = (mac_lo & 0xFF00) >> 8;
		/* set the ethaddr variable with MACID detected */
		setenv("ethaddr", (char *)mac_addr);
	}

	if (!is_valid_ether_addr(mac_addr)) {
		/* Read MACID from eeprom if MACID in eFuse is not valid */
		for (i = 0; i < ETH_ALEN; i++)
			mac_addr[i] = brd_id_hdr.mac_addr[0][i];
		/* set the ethaddr variable with MACID detected */
		setenv("ethaddr", (char *)mac_addr);
	}

	if (is_valid_ether_addr(mac_addr)) {
		printf("Detected MACID:%x:%x:%x:%x:%x:%x\n", mac_addr[0],
			mac_addr[1], mac_addr[2], mac_addr[3],
			mac_addr[4], mac_addr[5]);
		cpsw_eth_set_mac_addr(mac_addr);
	} else {
		printf("Caution:using static MACID!! Set <ethaddr> variable\n");
	}

	/* set mii mode to rgmii in in device configure register */
	if (daughter_board_id != IA_DAUGHTER_BOARD)
		__raw_writel(RGMII_MODE_ENABLE, MAC_MII_SEL);

	if (daughter_board_id == IA_DAUGHTER_BOARD) {
		cpsw_slaves[0].phy_id = 1;
		cpsw_slaves[1].phy_id = 0;
	}

	return cpsw_register(&cpsw_data);
}
#endif

#ifdef CONFIG_NAND_TI81XX
/******************************************************************************
 * Command to switch between NAND HW and SW ecc
 *****************************************************************************/
extern void ti81xx_nand_switch_ecc(nand_ecc_modes_t hardware, int32_t mode);
static int do_switch_ecc(cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	int type = 0;
	if (argc < 2)
		goto usage;

	if (strncmp(argv[1], "hw", 2) == 0) {
		if (argc == 3)
			type = simple_strtoul(argv[2], NULL, 10);
		ti81xx_nand_switch_ecc(NAND_ECC_HW, type);
	}
	else if (strncmp(argv[1], "sw", 2) == 0)
		ti81xx_nand_switch_ecc(NAND_ECC_SOFT, 0);
	else
		goto usage;

	return 0;

usage:
	printf("Usage: nandecc %s\n", cmdtp->usage);
	return 1;
}

U_BOOT_CMD(
	nandecc, 3, 1,	do_switch_ecc,
	"Switch NAND ECC calculation algorithm b/w hardware and software",
	"[sw|hw <hw_type>] \n"
	"   [sw|hw]- Switch b/w hardware(hw) & software(sw) ecc algorithm\n"
	"   hw_type- 0 for Hamming code\n"
	"            1 for bch4\n"
	"            2 for bch8\n"
	"            3 for bch16\n"
);

#endif /* CONFIG_NAND_TI81XX */
