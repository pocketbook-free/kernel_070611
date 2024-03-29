/* linux/arch/arm/plat-s3c/dev-hsmmc2.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C series device definition for hsmmc device 1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>

#include <mach/map.h>
#include <plat/sdhci.h>
#include <plat/devs.h>
#include <plat/cpu.h>

static struct resource s3c_hsmmc2_resource[] = {
	[0] = {
		.start = S3C_PA_HSMMC2,
		.end   = S3C_PA_HSMMC2 + S3C_SZ_HSMMC - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_HSMMC2,
		.end   = IRQ_HSMMC2,
		.flags = IORESOURCE_IRQ,
	}
};

static u64 s3c_device_hsmmc2_dmamask = 0xffffffffUL;

struct s3c_sdhci_platdata s3c_hsmmc2_def_platdata = {
	.max_width	= 4,
//&*&*&*EH1_20100113, enable BCM4319 Wi-Fi
	.host_caps	= (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED |	MMC_CAP_SD_HIGHSPEED),
//	.host_caps	= (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED |	MMC_CAP_SD_HIGHSPEED | MMC_CAP_ON_BOARD),
//&*&*&*EH2_20100113, enable BCM4319 Wi-Fi
};

struct platform_device s3c_device_hsmmc2 = {
	.name		= "s3c-sdhci",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(s3c_hsmmc2_resource),
	.resource	= s3c_hsmmc2_resource,
	.dev		= {
		.dma_mask		= &s3c_device_hsmmc2_dmamask,
		.coherent_dma_mask	= 0xffffffffUL,
		.platform_data		= &s3c_hsmmc2_def_platdata,
	},
};
EXPORT_SYMBOL(s3c_device_hsmmc2);

void s3c_sdhci2_set_platdata(struct s3c_sdhci_platdata *pd)
{
	struct s3c_sdhci_platdata *set = &s3c_hsmmc2_def_platdata;

	set->max_width = pd->max_width;

//&*&*&*EH1_20100113, enable BCM4319 Wi-Fi
/*
	if (pd->host_caps)
		set->host_caps = pd->host_caps;
*/
//&*&*&*EH2_20100113, enable BCM4319 Wi-Fi

	if (pd->cfg_gpio)
		set->cfg_gpio = pd->cfg_gpio;

	if (pd->cfg_card)
		set->cfg_card = pd->cfg_card;
//&*&*&*EH1_20100113, enable BCM4319 Wi-Fi
/*
	if (pd->cfg_ext_cd)
		set->cfg_ext_cd = pd->cfg_ext_cd;
	if (pd->detect_ext_cd)
		set->detect_ext_cd = pd->detect_ext_cd;
	if (pd->ext_cd)
		set->ext_cd = pd->ext_cd;
*/
//&*&*&*EH2_20100113, enable BCM4319 Wi-Fi
}

