#
# Makefile for the touchscreen drivers.
#

# Each configuration option enables a list of files.

wm97xx-ts-y := wm97xx-core.o

obj-$(CONFIG_TOUCHSCREEN_ADS7846)	+= ads7846.o
obj-$(CONFIG_TOUCHSCREEN_ATMEL_TSADCC)	+= atmel_tsadcc.o
obj-$(CONFIG_TOUCHSCREEN_BITSY)		+= h3600_ts_input.o
obj-$(CONFIG_TOUCHSCREEN_CORGI)		+= corgi_ts.o
obj-$(CONFIG_TOUCHSCREEN_GUNZE)		+= gunze.o
obj-$(CONFIG_TOUCHSCREEN_ELO)		+= elo.o
obj-$(CONFIG_TOUCHSCREEN_FUJITSU)	+= fujitsu_ts.o
obj-$(CONFIG_TOUCHSCREEN_HANWANG)	+= hanwang_ts.o
#obj-$(CONFIG_TOUCHSCREEN_HANWANG) += touch_power.o
obj-$(CONFIG_TOUCHSCREEN_INEXIO)	+= inexio.o
obj-$(CONFIG_TOUCHSCREEN_MIGOR)		+= migor_ts.o
obj-$(CONFIG_TOUCHSCREEN_MTOUCH)	+= mtouch.o
obj-$(CONFIG_TOUCHSCREEN_MK712)		+= mk712.o
obj-$(CONFIG_TOUCHSCREEN_HP600)		+= hp680_ts_input.o
obj-$(CONFIG_TOUCHSCREEN_HP7XX)		+= jornada720_ts.o
obj-$(CONFIG_TOUCHSCREEN_HTCPEN)	+= htcpen.o
obj-$(CONFIG_TOUCHSCREEN_USB_COMPOSITE)	+= usbtouchscreen.o
obj-$(CONFIG_TOUCHSCREEN_PENMOUNT)	+= penmount.o
obj-$(CONFIG_TOUCHSCREEN_SYNAPTICS_I2C_RMI)	+= synaptics_i2c_rmi.o
obj-$(CONFIG_TOUCHSCREEN_TOUCHIT213)	+= touchit213.o
obj-$(CONFIG_TOUCHSCREEN_TOUCHRIGHT)	+= touchright.o
obj-$(CONFIG_TOUCHSCREEN_TOUCHWIN)	+= touchwin.o
obj-$(CONFIG_TOUCHSCREEN_TSC2007)	+= tsc2007.o
obj-$(CONFIG_TOUCHSCREEN_UCB1400)	+= ucb1400_ts.o
obj-$(CONFIG_TOUCHSCREEN_WACOM_W8001)	+= wacom_w8001.o
obj-$(CONFIG_TOUCHSCREEN_WM97XX)	+= wm97xx-ts.o
obj-$(CONFIG_TOUCHSCREEN_DA9034)	+= da9034-ts.o
wm97xx-ts-$(CONFIG_TOUCHSCREEN_WM9705)	+= wm9705.o
wm97xx-ts-$(CONFIG_TOUCHSCREEN_WM9712)	+= wm9712.o
wm97xx-ts-$(CONFIG_TOUCHSCREEN_WM9713)	+= wm9713.o
obj-$(CONFIG_TOUCHSCREEN_WM97XX_MAINSTONE)	+= mainstone-wm97xx.o
obj-$(CONFIG_TOUCHSCREEN_S3C)		+= s3c-ts.o
