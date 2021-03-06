/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright © 2007 Atheros Communications, Inc.,  All Rights Reserved.
 */

/*
 * Manage the atheros ethernet PHY.
 *
 * All definitions in this file are operating system independent!
 */

//#include <linux/config.h>
#include <linux/types.h>
//#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>

#include "athrs16_phy.h"
#include "mdio.h"

/* PHY selections and access functions */
typedef enum {
    PHY_SRCPORT_INFO, 
    PHY_PORTINFO_SIZE,
} PHY_CAP_TYPE;

typedef enum {
    PHY_SRCPORT_NONE,
    PHY_SRCPORT_VLANTAG, 
    PHY_SRCPORT_TRAILER,
} PHY_SRCPORT_TYPE;

typedef enum {
    AG7100_PHY_SPEED_10T,
    AG7100_PHY_SPEED_100TX,
    AG7100_PHY_SPEED_1000T,
}ag7100_phy_speed_t;

#define DRV_LOG(DBG_SW, X0, X1, X2, X3, X4, X5, X6)
#define DRV_MSG(x,a,b,c,d,e,f)
#define DRV_PRINT(DBG_SW,X)

#define ATHR_LAN_PORT_VLAN          1
#define ATHR_WAN_PORT_VLAN          2

#define CONFIG_PORT0_AS_SWITCH 1
/*depend on connection between cpu mac and s16 mac*/
#if defined (CONFIG_PORT0_AS_SWITCH)
#define ENET_UNIT_LAN 0  
#define ENET_UNIT_WAN 1
#define CFG_BOARD_AP96 1
#else
#define ENET_UNIT_LAN 1  
#define ENET_UNIT_WAN 0
#define CFG_BOARD_PB45 1
#endif

#define TRUE    1
#define FALSE   0

#define ATHR_PHY0_ADDR   0x0
#define ATHR_PHY1_ADDR   0x1
#define ATHR_PHY2_ADDR   0x2
#define ATHR_PHY3_ADDR   0x3
#define ATHR_PHY4_ADDR   0x4
#define ATHR_IND_PHY 4

#define MODULE_NAME "ATHRS16"

/***STUFF***/
#define ETHERNET_JUMBO_FRAME 9600
#define SUPPORT_JUMBO_FRAME 1
#define SUPPORT_FLOW_CONTROL 1

#define ETHSWITCH_VIRTUAL_LAN_AND_WAN 1

#define	SWITCH_PORTS_STAT_CONFIG (25|(2<<5)|(13<<10))
#define ATHEROS_PORT_TO_PHY(p) (p-1)	// PHY ID = PORT ID -1

#define	ATHEROS_MIN_PORT_ID 0
#define	ATHEROS_MAX_PORT_ID 5
#define	ATHEROS_MIN_PHY_ID 1
#define	ATHEROS_MAX_PHY_ID ATHEROS_MAX_PORT_ID
#define	ATHEROS_CPU_PORT_ID ATHEROS_MIN_PORT_ID
#define	ATHEROS_PHY_PORT_MASK ((1<<(ATHEROS_MAX_PHY_ID+1)) - (1<<ATHEROS_MIN_PHY_ID))
#define	ATHEROS_CPU_PORT_MASK (1<<ATHEROS_CPU_PORT_ID)
#define	ATHEROS_ALL_PORT_MASK (ATHEROS_PHY_PORT_MASK | ATHEROS_CPU_PORT_MASK)

#if defined(ETHSWITCH_VIRTUAL_LAN_AND_WAN)
#define	ATHEROS_ETHERTYPE_VLAN 0x8100	// Arbitrary value here
#define	ETHSWITCH_AR8316_LAN_ID 0x2	// This value is arbitrary, but must be different than WAN ID (below)
#define	ETHSWITCH_AR8316_WAN_ID 0x1	// This value is arbitrary, but must be different than LAN ID (above)
#define	ATHEROS_WAN_PORT_ID ATHEROS_MAX_PORT_ID	// Indicate which PHY port is for WAN
#define	ATHEROS_LAN_PORT_MASK (~(1<<ATHEROS_WAN_PORT_ID) & ATHEROS_PHY_PORT_MASK)
#define	ATHEROS_WAN_PORT_MASK ( (1<<ATHEROS_WAN_PORT_ID) & ATHEROS_PHY_PORT_MASK)
#endif
/*
 * Track per-PHY port information.
 */
typedef struct {
    BOOL   isEnetPort;       /* normal enet port */
    BOOL   isPhyAlive;       /* last known state of link */
    int    ethUnit;          /* MAC associated with this phy port */
    uint32_t phyBase;
    uint32_t phyAddr;          /* PHY registers associated with this phy port */
    uint32_t VLANTableSetting; /* Value to be written to VLAN table */
} athrPhyInfo_t;

/*
 * Per-PHY information, indexed by PHY unit number.
 */
static athrPhyInfo_t athrPhyInfo[] = {
    {TRUE,   /* phy port 0 -- LAN port 0 */
     FALSE,
     ENET_UNIT_LAN,
     0,
     ATHR_PHY0_ADDR,
     ATHR_LAN_PORT_VLAN
    },

    {TRUE,   /* phy port 1 -- LAN port 1 */
     FALSE,
     ENET_UNIT_LAN,
     0,
     ATHR_PHY1_ADDR,
     ATHR_LAN_PORT_VLAN
    },

    {TRUE,   /* phy port 2 -- LAN port 2 */
     FALSE,
     ENET_UNIT_LAN,
     0,
     ATHR_PHY2_ADDR, 
     ATHR_LAN_PORT_VLAN
    },

    {TRUE,   /* phy port 3 -- LAN port 3 */
     FALSE,
     ENET_UNIT_LAN,
     0,
     ATHR_PHY3_ADDR, 
     ATHR_LAN_PORT_VLAN
    },

    {TRUE,   /* phy port 4 -- WAN port or LAN port 4 */
     FALSE,
     ENET_UNIT_WAN,
     0,
     ATHR_PHY4_ADDR, 
     ATHR_LAN_PORT_VLAN   /* Send to all ports */
    },
    
    {FALSE,  /* phy port 5 -- CPU port (no RJ45 connector) */
     TRUE,
     ENET_UNIT_LAN,
     0,
     0x00, 
     ATHR_LAN_PORT_VLAN    /* Send to all ports */
    },
};

static uint8_t athr16_init_flag = 0;

//#define ATHR_PHY_MAX (sizeof(ipPhyInfo) / sizeof(ipPhyInfo[0]))
#define ATHR_PHY_MAX 5

/* Range of valid PHY IDs is [MIN..MAX] */
#define ATHR_ID_MIN 0
#define ATHR_ID_MAX (ATHR_PHY_MAX-1)

/* Convenience macros to access myPhyInfo */
#define ATHR_IS_ENET_PORT(phyUnit) (athrPhyInfo[phyUnit].isEnetPort)
#define ATHR_IS_PHY_ALIVE(phyUnit) (athrPhyInfo[phyUnit].isPhyAlive)
#define ATHR_ETHUNIT(phyUnit) (athrPhyInfo[phyUnit].ethUnit)
#define ATHR_PHYBASE(phyUnit) (athrPhyInfo[phyUnit].phyBase)
#define ATHR_PHYADDR(phyUnit) (athrPhyInfo[phyUnit].phyAddr)
#define ATHR_VLAN_TABLE_SETTING(phyUnit) (athrPhyInfo[phyUnit].VLANTableSetting)


#define ATHR_IS_ETHUNIT(phyUnit, ethUnit) \
            (ATHR_IS_ENET_PORT(phyUnit) &&        \
            ATHR_ETHUNIT(phyUnit) == (ethUnit))

#define ATHR_IS_WAN_PORT(phyUnit) (!(ATHR_ETHUNIT(phyUnit)==ENET_UNIT_LAN))
            
/* Forward references */
static BOOL athrs16_phy_is_link_alive(int phyUnit);

void phy_mode_setup(void)
{
#ifdef S16_VER_1_0
    printk("phy_mode_setup\n");

    /*work around for phy4 rgmii mode*/
    phy_reg_write(ATHR_PHYBASE(ATHR_IND_PHY), ATHR_PHYADDR(ATHR_IND_PHY), 29, 18);     
    phy_reg_write(ATHR_PHYBASE(ATHR_IND_PHY), ATHR_PHYADDR(ATHR_IND_PHY), 30, 0x480c);    

    /*rx delay*/ 
    phy_reg_write(ATHR_PHYBASE(ATHR_IND_PHY), ATHR_PHYADDR(ATHR_IND_PHY), 29, 0);     
    phy_reg_write(ATHR_PHYBASE(ATHR_IND_PHY), ATHR_PHYADDR(ATHR_IND_PHY), 30, 0x824e);  

    /*tx delay*/ 
    phy_reg_write(ATHR_PHYBASE(ATHR_IND_PHY), ATHR_PHYADDR(ATHR_IND_PHY), 29, 5);     
    phy_reg_write(ATHR_PHYBASE(ATHR_IND_PHY), ATHR_PHYADDR(ATHR_IND_PHY), 30, 0x3d47);    
#endif
}

void athrs16_reg_init()
{
    /* if using header for register configuration, we have to     */
    /* configure s16 register after frame transmission is enabled */

	u32_t phy;

	if (athr16_init_flag)
		return;

	printk("********** athrs16_reg_init start. **********\n");
	printk("Atheros switch chip ID = 0x%hx%hx\n", phy_reg_read(0, 0, 2), phy_reg_read(0, 0, 3));

	athrs16_reg_write(0x0008, (athrs16_reg_read(0x0008) & 0xfcffffff) | (1 << 31) | (1 << 24));		// Disable SPI and set LED open drain
	// Enable RGMII for CPU port
	athrs16_reg_write(0x0008, (athrs16_reg_read(0x0008) & 0xff9fffe0) | (1 << 7) | (0x2 << 21) | 0x2);
	printk("Atheros power-on strapping = 0x%x\n", athrs16_reg_read(0x0008));
	BUG_ON((athrs16_reg_read(0x0008) & 0x1f) != 2);
#if !defined(ETHSWITCH_VIRTUAL_LAN_AND_WAN)
	athrs16_reg_write(0x0008, (athrs16_reg_read(0x0008) & 0xfb7fbfff) | (0x0 << 23) | (0 << 14) | 0x8);
	printk("Atheros power-on strapping = 0x%x\n", athrs16_reg_read(0x0008));

	/*
	 * MAC5/PHY4: Disable Atheros specific feature of disable MDC/MDIO access when power down.
	 */
	phy_reg_write(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1D, 0x03);
	u16_t debug_reg3 = phy_reg_read(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E);
	phy_reg_write(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E, debug_reg3 & ~(1 << 8));
	printk("\tSystem Mode PHY[%d:0x03]: %hx -> %hx\n", ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID),
		debug_reg3, phy_reg_read(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E));

	/*
	 * MAC5/PHY4: Set RGMII I/F to enabel TX clock delay after HW/SW reset.
	 */
	phy_reg_write(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1D, 0x05);
	u16_t debug_sysmode = phy_reg_read(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E);
	phy_reg_write(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E, debug_sysmode | (1 << 8));
	printk("\tSystem Mode PHY[%d:0x05]: %hx -> %hx\n", ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID),
		debug_sysmode, phy_reg_read(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E));

	phy_reg_write(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1D, 0x12);
	u16_t debug_rgmiimode = phy_reg_read(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E);
	phy_reg_write(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E, debug_rgmiimode | 8);
	printk("\tSystem Mode PHY[%d:0x12]: %hx -> %hx\n", ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID),
		debug_rgmiimode, phy_reg_read(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x1E));
#endif

	for (phy = ATHEROS_MIN_PHY_ID; phy <= ATHEROS_MAX_PHY_ID; phy++) {
		/* Eanble IGMP snooping (to CPU port - port 0) and allow VLAN tag */
		athrs16_reg_write(0x104 + (phy << 8),
			athrs16_reg_read(0x104 + (phy << 8)) | (3 << 20) | (1 << 13) | (1 << 10));
#if defined(ETHSWITCH_VIRTUAL_LAN_AND_WAN)
		if (phy == ATHEROS_WAN_PORT_ID) {
			athrs16_reg_write(0x104 + (phy << 8),
				(athrs16_reg_read(0x104 + (phy << 8)) & ~(3 << 8)) | (1 << 8));	// Strip tag off egress frames from WAN port
			athrs16_reg_write(0x108 + (phy << 8),
				(athrs16_reg_read(0x108 + (phy << 8)) & ~(ATHEROS_LAN_PORT_MASK << 16)) |
				(ATHEROS_CPU_PORT_MASK << 16) | (1 << 30) | (1 << 26) | (1 << 12));
			athrs16_reg_write(0x108 + (phy << 8),
				(athrs16_reg_read(0x108 + (phy << 8)) & ~0x0fff) | ETHSWITCH_AR8316_WAN_ID);
		} else {
			athrs16_reg_write(0x104 + (phy << 8),
				(athrs16_reg_read(0x104 + (phy << 8)) & ~(3 << 8)) | (1 << 8));	// Strip tag off egress frames from LAN ports
			athrs16_reg_write(0x108 + (phy << 8),
				(athrs16_reg_read(0x108 + (phy << 8)) & ~(ATHEROS_WAN_PORT_MASK << 16)) |
				(ATHEROS_CPU_PORT_MASK << 16) | (1 << 30) | (1 << 26) | (1 << 12));
			athrs16_reg_write(0x108 + (phy << 8),
				(athrs16_reg_read(0x108 + (phy << 8)) & ~0x0fff) | ETHSWITCH_AR8316_LAN_ID);
		}
#else
		if (phy == ATHEROS_MAX_PORT_ID) {
			athrs16_reg_write(0x104 + (phy << 8),
				athrs16_reg_read(0x104 + (phy << 8)) & ~0x7);
		}
#endif
		/*
		 * Reason: Enable smartspeed function but allow more re-try times to fix RJ45 will detect
		 *	   the wrong signal and link up to 10Mbps.
		 * Modified: John Huang
		 * Date: 2009.12.02
		 */
		phy_reg_write(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x14,
			phy_reg_read(0, ATHEROS_PORT_TO_PHY(ATHEROS_MAX_PORT_ID), 0x0014) | 0x1C); //set PHY 0x14[4:2]=111

		printk("Atheros switch port[%d]: control = 0x%x status = 0x%x port_vlan = 0x%x\n", phy,
			athrs16_reg_read(0x104 + (phy << 8)), athrs16_reg_read(0x100 + (phy << 8)), athrs16_reg_read(0x108 + (phy << 8)));
	}

	athrs16_reg_write(0x104 + (ATHEROS_CPU_PORT_ID << 8),
		athrs16_reg_read(0x104 + (ATHEROS_CPU_PORT_ID << 8)) | (1 << 11));	// Enable CPU header
#if defined(ETHSWITCH_VIRTUAL_LAN_AND_WAN)
	// CPU egress all frames with tag and hardware IGMP snooping
	athrs16_reg_write(0x104 + (ATHEROS_CPU_PORT_ID << 8),
		(athrs16_reg_read(0x104 + (ATHEROS_CPU_PORT_ID << 8)) & ~(3 << 8)) | (2 << 8) | (3 << 20));
	athrs16_reg_write(0x104 + (ATHEROS_CPU_PORT_ID << 8),
		athrs16_reg_read(0x104 + (ATHEROS_CPU_PORT_ID << 8)) | (0 << 15) | (1 << 13));	// Enable CPU port as tagged port
	athrs16_reg_write(0x108 + (ATHEROS_CPU_PORT_ID << 8),
		(athrs16_reg_read(0x108 + (ATHEROS_CPU_PORT_ID << 8)) | (1 << 30)) & ~(1 << 26) & ~(1 << 12));
	athrs16_reg_write(0x108 + (ATHEROS_CPU_PORT_ID << 8),
		(athrs16_reg_read(0x108 + (ATHEROS_CPU_PORT_ID << 8)) & ~0x0fff) | ETHSWITCH_AR8316_LAN_ID);
	athrs16_reg_write(0x0074, ATHEROS_ETHERTYPE_VLAN);

	/* Setup VLAN */
	while (athrs16_reg_read(0x0040) & (1 << 3)) {}
	athrs16_reg_write(0x0044, (1 << 11) | ATHEROS_LAN_PORT_MASK | ATHEROS_CPU_PORT_MASK);
	athrs16_reg_write(0x0040, (ETHSWITCH_AR8316_LAN_ID << 16) | (1 << 3) | 2);
	while (athrs16_reg_read(0x0040) & (1 << 3)) {}
	athrs16_reg_write(0x0044, (1 << 11) | ATHEROS_WAN_PORT_MASK | ATHEROS_CPU_PORT_MASK);
	athrs16_reg_write(0x0040, (ETHSWITCH_AR8316_WAN_ID << 16) | (1 << 3) | 2);
#ifdef DEBUG_PKG
	while (athrs16_reg_read(0x0040) & (1 << 3)) {}
	athrs16_reg_write(0x0040, (0 << 16) | (1 << 3) | 5);
	while (athrs16_reg_read(0x0040) & (1 << 3)) {}
	printk("Wan VLAN entry = 0x%x - 0x%x", athrs16_reg_read(0x0040), athrs16_reg_read(0x0044));
	athrs16_reg_write(0x0040, athrs16_reg_read(0x0040) | (1 << 3) | 5);
	while (athrs16_reg_read(0x0040) & (1 << 3)) {}
	printk("Lan VLAN entry = 0x%x - 0x%x", athrs16_reg_read(0x0040), athrs16_reg_read(0x0044));
#endif
	/* Setup ACL to enforce WAN traffic forwarding */
#if 0
	// Enable ACL
	athrs16_reg_write(0x003c, athrs16_reg_read(0x003c) | (1 << 21));	// Enable ACL

	athrs16_reg_write(0x58824, 1);					// ADDR0
	athrs16_reg_write(0x58828, 0);					// ADDR1
	athrs16_reg_write(0x5882c, 0);					// ADDR2
	athrs16_reg_write(0x58830, 0);					// ADDR3
	athrs16_reg_write(0x58820, 1);					// ADDR valid
	athrs16_reg_write(0x58838, 0);					// rule length

	athrs16_reg_write(0x58420, 0x00000000);				// rule byte[3:0]
	athrs16_reg_write(0x58424, 0x00000000);				// rule byte[7:4]
	athrs16_reg_write(0x58428, 0x00000000);				// rule byte[11:8]
	athrs16_reg_write(0x5842c, 0x00000000);				// rule byte[15:12]
	athrs16_reg_write(0x58430, ATHEROS_WAN_PORT_MASK);		// rule byte[16]

	athrs16_reg_write(0x58c20, 0x00000000);				// mask byte[3:0]
	athrs16_reg_write(0x58c24, 0x00000000);				// mask byte[7:4]
	athrs16_reg_write(0x58c28, 0x00000000);				// mask byte[11:8]
	athrs16_reg_write(0x58c2c, 0x00000000);				// mask byte[15:12]

	athrs16_reg_write(0x5883c, 0x00000001);				// rule select

	athrs16_reg_write(0x58020, (1 << 31) | ((ATHEROS_WAN_PORT_MASK|ATHEROS_CPU_PORT_MASK) << 20));// rule result
#endif
#endif//defined(ETHSWITCH_VIRTUAL_LAN_AND_WAN)

#if SUPPORT_JUMBO_FRAME
	BUG_ON((ETHERNET_JUMBO_FRAME >= (1 << 14)));
	athrs16_reg_write(0x0030, (athrs16_reg_read(0x0030) & 0xffff0000) | ETHERNET_JUMBO_FRAME);
#endif

	/* Enabel CPU port and forward broadcast/multicast/unicast to CPU */
	athrs16_reg_write(0x0078, athrs16_reg_read(0x0078) | (1 << 8));		// Enable CPU port
	printk("Atheros CPU control = 0x%x\n", athrs16_reg_read(0x0078));
	athrs16_reg_write(0x100 + (ATHEROS_CPU_PORT_ID << 8),
		/*(athrs16_reg_read(0x100 + (ATHEROS_CPU_PORT_ID << 8)) & ~(0 << 9)) |*/ 0x007e);
	printk("Atheros switch port[%d]: control = 0x%x status = 0x%x port_vlan = 0x%x\n", ATHEROS_CPU_PORT_ID,
		athrs16_reg_read(0x104 + (ATHEROS_CPU_PORT_ID << 8)), athrs16_reg_read(0x100 + (ATHEROS_CPU_PORT_ID << 8)), athrs16_reg_read(0x108 + (ATHEROS_CPU_PORT_ID << 8)));

	/* Eanble IGMP snooping (to CPU port - port 0) */
	athrs16_reg_write(0x003c, athrs16_reg_read(0x003c) | (1 << 22));
	// Flood broadcast to CPU port Flood multicast/unicast to all port
	athrs16_reg_write(0x002C, athrs16_reg_read(0x002C) | (1 << 26) | (0x3f << 16) | (0x3f << 0));
	// Flood IGMP to cpu port
	//athrs16_reg_write(0x002C, athrs16_reg_read(0x002C) | (1 << 8) );
	// Flood IGMP to cpu port ans wan port
	athrs16_reg_write(0x002C, athrs16_reg_read(0x002C) | (1 << 8) | (1<<13));

#if defined(SWITCH_PORTS_STATISTICS)
	athrs16_reg_write(0x0080, (1 << 30) | (1 << 24));			// Reset MIB counters
	athrs16_reg_write(0x0080, (1 << 30) | (0 << 24));			// Start MIB counters
#endif

//Print all register value
#if 0
	{
		for(phy = 0; phy < 0x79; phy++){
			printk("Atheros switch register[%x] = 0x%x\n",phy , athrs16_reg_read(phy));
		}	
	
		for (phy = 0; phy < 6; phy++) {
			int reg;
			printk("Atheros port[%x] ",phy);
			for(reg = 0x100; reg < 0x120; reg++){
				printk(" register[%x] = 0x%x\n", reg + (phy << 8) , athrs16_reg_read(reg + (phy << 8)));
			}			
		}		
	}
#endif

/*

#if CFG_BOARD_PB45
    athrs16_reg_write(0x208, 0x2fd0001);  
    athrs16_reg_write(0x108, 0x2be0001);  
#elif CFG_BOARD_AP96
    athrs16_reg_write(0x8, 0x012e1bea);
#endif
    
    athrs16_reg_write(0x100, 0x7e);
    athrs16_reg_write(0x200, 0x280);
    athrs16_reg_write(0x300, 0x280);
    athrs16_reg_write(0x400, 0x280);
    athrs16_reg_write(0x500, 0x280);
#if CFG_BOARD_PB45
     athrs16_reg_write(0x600, 0x280);
#elif CFG_BOARD_AP96
//    athrs16_reg_write(0x600, 0x0);
    athrs16_reg_write(0x600, 0x280);
#endif

    athrs16_reg_write(0x38, 0xc000050e);

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)        
#ifdef HEADER_EN        
    athrs16_reg_write(0x104, 0x6804);
#else
    athrs16_reg_write(0x104, 0x6004);
#endif

    athrs16_reg_write(0x204, 0x6004);
    athrs16_reg_write(0x304, 0x6004);
    athrs16_reg_write(0x404, 0x6004);
    athrs16_reg_write(0x504, 0x6004);    
    athrs16_reg_write(0x604, 0x6004);    
#else
#ifdef HEADER_EN        
    athrs16_reg_write(0x104, 0x4804);
#else
    athrs16_reg_write(0x104, 0x4004);
#endif
#endif

#ifdef FULL_FEATURE
    hsl_dev_init(0, 2);
#endif

#ifdef CFG_BOARD_AP96

//    phy_reg_write(ATHR_PHYBASE(4), ATHR_PHYADDR(4), 29, 18);     
//    phy_reg_write(ATHR_PHYBASE(4), ATHR_PHYADDR(4), 30, 0x4c0c); 
    

//    phy_reg_write(ATHR_PHYBASE(4), ATHR_PHYADDR(4), 29, 0);     
//    phy_reg_write(ATHR_PHYBASE(4), ATHR_PHYADDR(4), 30, 0x82ee); 
   

  //  phy_reg_write(ATHR_PHYBASE(4), ATHR_PHYADDR(4), 29, 5);     
  //  phy_reg_write(ATHR_PHYBASE(4), ATHR_PHYADDR(4), 30, 0x3d46);  
#endif
*/

    printk("********** athrs16_reg_init complete. **********\n");

    athr16_init_flag = 1;
}

/******************************************************************************
*
* athrs16_phy_is_link_alive - test to see if the specified link is alive
*
* RETURNS:
*    TRUE  --> link is alive
*    FALSE --> link is down
*/
static BOOL
athrs16_phy_is_link_alive(int phyUnit)
{
    uint16_t phyHwStatus;
    uint32_t phyBase;
    uint32_t phyAddr;

    phyBase = ATHR_PHYBASE(phyUnit);
    phyAddr = ATHR_PHYADDR(phyUnit);

    phyHwStatus = phy_reg_read(phyBase, phyAddr, ATHR_PHY_SPEC_STATUS);

    if (phyHwStatus & ATHR_STATUS_LINK_PASS)
        return TRUE;

    return FALSE;
}

/******************************************************************************
*
* athrs16_phy_setup - reset and setup the PHY associated with
* the specified MAC unit number.
*
* Resets the associated PHY port.
*
* RETURNS:
*    TRUE  --> associated PHY is alive
*    FALSE --> no LINKs on this ethernet unit
*/

BOOL
athrs16_phy_setup(int ethUnit)
{
    int       phyUnit;
    uint16_t  phyHwStatus;
    uint16_t  timeout;
    int       liveLinks = 0;
    uint32_t  phyBase = 0;
    BOOL      foundPhy = FALSE;
    uint32_t  phyAddr = 0;
    

    /* See if there's any configuration data for this enet */
    /* start auto negogiation on each phy */
    for (phyUnit=0; phyUnit < ATHR_PHY_MAX; phyUnit++) {
        if (!ATHR_IS_ETHUNIT(phyUnit, ethUnit)) {
            continue;
        }

        foundPhy = TRUE;
        phyBase = ATHR_PHYBASE(phyUnit);
        phyAddr = ATHR_PHYADDR(phyUnit);
        
        phy_reg_write(phyBase, phyAddr, ATHR_AUTONEG_ADVERT, ATHR_ADVERTISE_ALL);

        phy_reg_write(phyBase, phyAddr, ATHR_1000BASET_CONTROL, ATHR_ADVERTISE_1000FULL);

        /* Reset PHYs*/
        phy_reg_write(phyBase, phyAddr, ATHR_PHY_CONTROL, ATHR_CTRL_AUTONEGOTIATION_ENABLE | ATHR_CTRL_SOFTWARE_RESET);

    }

    if (!foundPhy) {
        return FALSE; /* No PHY's configured for this ethUnit */
    }

    /*
     * After the phy is reset, it takes a little while before
     * it can respond properly.
     */
    mdelay(1000);


    /*
     * Wait up to 3 seconds for ALL associated PHYs to finish
     * autonegotiation.  The only way we get out of here sooner is
     * if ALL PHYs are connected AND finish autonegotiation.
     */
    for (phyUnit=0; (phyUnit < ATHR_PHY_MAX) /*&& (timeout > 0) */; phyUnit++) {
        if (!ATHR_IS_ETHUNIT(phyUnit, ethUnit)) {
            continue;
        }

        timeout=20;
        for (;;) {
            phyHwStatus = phy_reg_read(phyBase, phyAddr, ATHR_PHY_CONTROL);

            if (ATHR_RESET_DONE(phyHwStatus)) {
                DRV_PRINT(DRV_DEBUG_PHYSETUP,
                          ("Port %d, Neg Success\n", phyUnit));
                break;
            }
            if (timeout == 0) {
                DRV_PRINT(DRV_DEBUG_PHYSETUP,
                          ("Port %d, Negogiation timeout\n", phyUnit));
                break;
            }
            if (--timeout == 0) {
                DRV_PRINT(DRV_DEBUG_PHYSETUP,
                          ("Port %d, Negogiation timeout\n", phyUnit));
                break;
            }

            mdelay(150);
        }
    }

    /*
     * All PHYs have had adequate time to autonegotiate.
     * Now initialize software status.
     *
     * It's possible that some ports may take a bit longer
     * to autonegotiate; but we can't wait forever.  They'll
     * get noticed by mv_phyCheckStatusChange during regular
     * polling activities.
     */
    for (phyUnit=0; phyUnit < ATHR_PHY_MAX; phyUnit++) {
        if (!ATHR_IS_ETHUNIT(phyUnit, ethUnit)) {
            continue;
        }

        if (athrs16_phy_is_link_alive(phyUnit)) {
            liveLinks++;
            ATHR_IS_PHY_ALIVE(phyUnit) = TRUE;
        } else {
            ATHR_IS_PHY_ALIVE(phyUnit) = FALSE;
        }

        DRV_PRINT(DRV_DEBUG_PHYSETUP, ("eth%d: Phy Specific Status=%4.4x\n", ethUnit, phy_reg_read(ATHR_PHYBASE(phyUnit), ATHR_PHYADDR(phyUnit), ATHR_PHY_SPEC_STATUS)));
    }
    
    return (liveLinks > 0);
}

/******************************************************************************
*
* athrs16_phy_is_fdx - Determines whether the phy ports associated with the
* specified device are FULL or HALF duplex.
*
* RETURNS:
*    1 --> FULL
*    0 --> HALF
*/
int
athrs16_phy_is_fdx(int ethUnit)
{
    int       phyUnit;
    uint32_t  phyBase;
    uint32_t  phyAddr;
    uint16_t  phyHwStatus;
    int       ii = 200;

    if (ethUnit == ENET_UNIT_LAN)
        return TRUE;

    for (phyUnit=0; phyUnit < ATHR_PHY_MAX; phyUnit++) {
        if (!ATHR_IS_ETHUNIT(phyUnit, ethUnit)) {
            continue;
        }

        if (athrs16_phy_is_link_alive(phyUnit)) {

            phyBase = ATHR_PHYBASE(phyUnit);
            phyAddr = ATHR_PHYADDR(phyUnit);

            do {
                phyHwStatus = phy_reg_read(phyBase, phyAddr, ATHR_PHY_SPEC_STATUS);
                mdelay(10);
            } while((!(phyHwStatus & ATHR_STATUS_RESOVLED)) && --ii);
            
            if (phyHwStatus & ATHER_STATUS_FULL_DEPLEX)
                return TRUE;
        }
    }

    return FALSE;
}


/******************************************************************************
*
* athrs16_phy_speed - Determines the speed of phy ports associated with the
* specified device.
*
* RETURNS:
*               AG7100_PHY_SPEED_10T, AG7100_PHY_SPEED_100TX;
*               AG7100_PHY_SPEED_1000T;
*/

int
athrs16_phy_speed(int ethUnit)
{
    int       phyUnit;
    uint16_t  phyHwStatus;
    uint32_t  phyBase;
    uint32_t  phyAddr;
    int       ii = 200;

    if (ethUnit == ENET_UNIT_LAN)
        return AG7100_PHY_SPEED_1000T;
        
    for (phyUnit=0; phyUnit < ATHR_PHY_MAX; phyUnit++) {
        if (!ATHR_IS_ETHUNIT(phyUnit, ethUnit)) {
            continue;
        }

        if (athrs16_phy_is_link_alive(phyUnit)) {

            phyBase = ATHR_PHYBASE(phyUnit);
            phyAddr = ATHR_PHYADDR(phyUnit);
            do {
		phyHwStatus = phy_reg_read(phyBase, phyAddr, ATHR_PHY_SPEC_STATUS);
                mdelay(10);
            }while((!(phyHwStatus & ATHR_STATUS_RESOVLED)) && --ii);
            
            phyHwStatus = ((phyHwStatus & ATHER_STATUS_LINK_MASK) >> ATHER_STATUS_LINK_SHIFT);

            switch(phyHwStatus) {
            case 0:
                return AG7100_PHY_SPEED_10T;
            case 1:
                return AG7100_PHY_SPEED_100TX;
            case 2:
                return AG7100_PHY_SPEED_1000T;
            default:
                printk("Unkown speed read!\n");
            }
        }
    }

    return AG7100_PHY_SPEED_10T;
}

/*****************************************************************************
*
* athr_phy_is_up -- checks for significant changes in PHY state.
*
* A "significant change" is:
*     dropped link (e.g. ethernet cable unplugged) OR
*     autonegotiation completed + link (e.g. ethernet cable plugged in)
*
* When a PHY is plugged in, phyLinkGained is called.
* When a PHY is unplugged, phyLinkLost is called.
*/

int
athrs16_phy_is_up(int ethUnit)
{
    int           phyUnit;
    uint16_t      phyHwStatus, phyHwControl;
    athrPhyInfo_t *lastStatus;
    int           linkCount   = 0;
    int           lostLinks   = 0;
    int           gainedLinks = 0;
    uint32_t      phyBase;
    uint32_t      phyAddr;

    for (phyUnit=0; phyUnit < ATHR_PHY_MAX; phyUnit++) {
        if (!ATHR_IS_ETHUNIT(phyUnit, ethUnit)) {
            continue;
        }

        phyBase = ATHR_PHYBASE(phyUnit);
        phyAddr = ATHR_PHYADDR(phyUnit);

        lastStatus = &athrPhyInfo[phyUnit];

        if (lastStatus->isPhyAlive) { /* last known link status was ALIVE */
            phyHwStatus = phy_reg_read(phyBase, phyAddr, ATHR_PHY_SPEC_STATUS);

            /* See if we've lost link */
            if (phyHwStatus & ATHR_STATUS_LINK_PASS) {
                linkCount++;
            } else {
                lostLinks++;
                DRV_PRINT(DRV_DEBUG_PHYCHANGE,("\nenet%d port%d down\n",
                                               ethUnit, phyUnit));
                lastStatus->isPhyAlive = FALSE;
            }
        } else { /* last known link status was DEAD */
            /* Check for reset complete */
            phyHwStatus = phy_reg_read(phyBase, phyAddr, ATHR_PHY_STATUS);
            if (!ATHR_RESET_DONE(phyHwStatus))
                continue;

            phyHwControl = phy_reg_read(phyBase, phyAddr, ATHR_PHY_CONTROL);
            /* Check for AutoNegotiation complete */            
            if ((!(phyHwControl & ATHR_CTRL_AUTONEGOTIATION_ENABLE)) 
                 || ATHR_AUTONEG_DONE(phyHwStatus)) {
                phyHwStatus = phy_reg_read(phyBase, phyAddr, ATHR_PHY_SPEC_STATUS);

                if (phyHwStatus & ATHR_STATUS_LINK_PASS) {
                gainedLinks++;
                linkCount++;
                DRV_PRINT(DRV_DEBUG_PHYCHANGE,("\nenet%d port%d up\n",
                                               ethUnit, phyUnit));
                lastStatus->isPhyAlive = TRUE;
                }
            }
        }
    }

    return (linkCount);

}

uint32_t
athrs16_reg_read(uint32_t reg_addr)
{
    uint32_t reg_word_addr;
    uint32_t phy_addr, tmp_val, reg_val;
    uint16_t phy_val;
    uint8_t phy_reg;

    /* change reg_addr to 16-bit word address, 32-bit aligned */
    reg_word_addr = (reg_addr & 0xfffffffc) >> 1;

    /* configure register high address */
    phy_addr = 0x18;
    phy_reg = 0x0;
    phy_val = (uint16_t) ((reg_word_addr >> 8) & 0x1ff);  /* bit16-8 of reg address */
    phy_reg_write(0, phy_addr, phy_reg, phy_val);

    /* For some registers such as MIBs, since it is read/clear, we should */
    /* read the lower 16-bit register then the higher one */

    /* read register in lower address */
    phy_addr = 0x10 | ((reg_word_addr >> 5) & 0x7); /* bit7-5 of reg address */
    phy_reg = (uint8_t) (reg_word_addr & 0x1f);   /* bit4-0 of reg address */
    reg_val = (uint32_t) phy_reg_read(0, phy_addr, phy_reg);

    /* read register in higher address */
    reg_word_addr++;
    phy_addr = 0x10 | ((reg_word_addr >> 5) & 0x7); /* bit7-5 of reg address */
    phy_reg = (uint8_t) (reg_word_addr & 0x1f);   /* bit4-0 of reg address */
    tmp_val = (uint32_t) phy_reg_read(0, phy_addr, phy_reg);
    reg_val |= (tmp_val << 16);

    return reg_val;   
}

void
athrs16_reg_write(uint32_t reg_addr, uint32_t reg_val)
{
    uint32_t reg_word_addr;
    uint32_t phy_addr;
    uint16_t phy_val;
    uint8_t phy_reg;

    /* change reg_addr to 16-bit word address, 32-bit aligned */
    reg_word_addr = (reg_addr & 0xfffffffc) >> 1;

    /* configure register high address */
    phy_addr = 0x18;
    phy_reg = 0x0;
    phy_val = (uint16_t) ((reg_word_addr >> 8) & 0x1ff);  /* bit16-8 of reg address */
    phy_reg_write(0, phy_addr, phy_reg, phy_val);

    /* For some registers such as ARL and VLAN, since they include BUSY bit */
    /* in lower address, we should write the higher 16-bit register then the */
    /* lower one */

    /* read register in higher address */
    reg_word_addr++;
    phy_addr = 0x10 | ((reg_word_addr >> 5) & 0x7); /* bit7-5 of reg address */
    phy_reg = (uint8_t) (reg_word_addr & 0x1f);   /* bit4-0 of reg address */
    phy_val = (uint16_t) ((reg_val >> 16) & 0xffff);
    phy_reg_write(0, phy_addr, phy_reg, phy_val);

    /* write register in lower address */
    reg_word_addr--;
    phy_addr = 0x10 | ((reg_word_addr >> 5) & 0x7); /* bit7-5 of reg address */
    phy_reg = (uint8_t) (reg_word_addr & 0x1f);   /* bit4-0 of reg address */
    phy_val = (uint16_t) (reg_val & 0xffff);
    phy_reg_write(0, phy_addr, phy_reg, phy_val); 
}

int
athr_ioctl(uint32_t *args, int cmd)
{
#ifdef FULL_FEATURE
    if (sw_ioctl(args, cmd))
        return -EOPNOTSUPP;

    return 0;
#else
    printk("EOPNOTSUPP\n");
    return -EOPNOTSUPP;
#endif
}
