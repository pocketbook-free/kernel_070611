/*
  Author: Brian Xu
  Date: 2009/09/14
  File: adxl345_sysfs.c

  Modify: Aimar Liu
  Date: 2009/12/30
  File: adxl345_sysfs.c
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/signal.h>
#include <linux/slab.h> //mem malloc
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <asm/gpio.h>
#include <plat/regs-gpio.h>
#include <plat/irqs.h>


#include <mach/map.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <asm/mach/irq.h>
#include <plat/gpio-cfg.h>

#include "adxl345.h"

#if 1
	#define ADXL_DEBUG
#else
	#ifdef ADXL_DEBUG
	#undef ADXL_DEBUG
	#endif
#endif

#ifdef ADXL_DEBUG
	#define dprintk(x, args...)	do { printk(x, ##args); } while(0)
#else
	#define dprintk(x, args...)	do { } while(0)
#endif

/* ADXL345/6 Register Map */
#define DEVID		    0x00	/* R   Device ID */
#define THRESH_TAP	0x1D	/* R/W Tap threshold */
#define OFSX		    0x1E	/* R/W X-axis offset */
#define OFSY		    0x1F	/* R/W Y-axis offset */
#define OFSZ		    0x20	/* R/W Z-axis offset */
#define DUR		      0x21	/* R/W Tap duration */
#define LATENT		  0x22	/* R/W Tap latency */
#define WINDOW		  0x23	/* R/W Tap window */
#define THRESH_ACT	  0x24	/* R/W Activity threshold */
#define THRESH_INACT	0x25	/* R/W Inactivity threshold */
#define TIME_INACT   	0x26	/* R/W Inactivity time */
#define ACT_INACT_CTL	0x27	/* R/W Axis enable control for activity and inactivity detection */
#define THRESH_FF	      0x28	/* R/W Free-fall threshold */
#define TIME_FF		      0x29	/* R/W Free-fall time */
#define TAP_AXES	      0x2A	/* R/W Axis control for tap/double tap */
#define ACT_TAP_STATUS	0x2B	/* R   Source of tap/double tap */
#define BW_RATE		  0x2C	/* R/W Data rate and power mode control */
#define POWER_CTL	  0x2D	/* R/W Power saving features control */
#define INT_ENABLE	0x2E	/* R/W Interrupt enable control */
#define INT_MAP		  0x2F	/* R/W Interrupt mapping control */
#define INT_SOURCE	0x30	/* R   Source of interrupts */
#define DATA_FORMAT	0x31	/* R/W Data format control */
#define DATAX0		  0x32	/* R   X-Axis Data 0 */
#define DATAX1		  0x33	/* R   X-Axis Data 1 */
#define DATAY0		  0x34	/* R   Y-Axis Data 0 */
#define DATAY1		  0x35	/* R   Y-Axis Data 1 */
#define DATAZ0		  0x36  /* R   Z-Axis Data 0 */
#define DATAZ1		  0x37	/* R   Z-Axis Data 1 */
#define FIFO_CTL	  0x38	/* R/W FIFO control  */
#define FIFO_STATUS	0x39	/* R   FIFO status   */

/* DEVIDs */
#define ID_ADXL345	0xE5

/* INT_ENABLE/INT_MAP/INT_SOURCE Bits */
#define DATA_READY	(1 << 7)
#define SINGLE_TAP	(1 << 6)
#define DOUBLE_TAP	(1 << 5)
#define ACTIVITY	  (1 << 4)
#define INACTIVITY	(1 << 3)
#define FREE_FALL	  (1 << 2)
#define WATERMARK	  (1 << 1)
#define OVERRUN		  (1 << 0)

/* ACT_INACT_CONTROL Bits */
#define ACT_ACDC	  (1 << 7)
#define ACT_X_EN	  (1 << 6)
#define ACT_Y_EN	  (1 << 5)
#define ACT_Z_EN	  (1 << 4)
#define INACT_ACDC	(1 << 3)
#define INACT_X_EN	(1 << 2)
#define INACT_Y_EN	(1 << 1)
#define INACT_Z_EN	(1 << 0)

/* TAP_AXES Bits */
#define SUPPRESS	(1 << 3)
#define TAP_X_EN	(1 << 2)
#define TAP_Y_EN	(1 << 1)
#define TAP_Z_EN	(1 << 0)

/* ACT_TAP_STATUS Bits */
#define ACT_X_SRC	(1 << 6)
#define ACT_Y_SRC	(1 << 5)
#define ACT_Z_SRC	(1 << 4)
#define ASLEEP		(1 << 3)
#define TAP_X_SRC	(1 << 2)
#define TAP_Y_SRC	(1 << 1)
#define TAP_Z_SRC	(1 << 0)

/* BW_RATE Bits */
#define LOW_POWER	(1 << 4)
#define RATE(x)		((x) & 0xF)

/* POWER_CTL Bits */
#define PCTL_LINK	      (1 << 5)
#define PCTL_AUTO_SLEEP (1 << 4)
#define PCTL_MEASURE	  (1 << 3)
#define PCTL_SLEEP	    (1 << 2)
#define PCTL_WAKEUP(x)	((x) & 0x3)

/* DATA_FORMAT Bits */
#define SELF_TEST	  (1 << 7)
#define SPI		      (1 << 6)
#define INT_INVERT	(1 << 5)
#define FULL_RES	  (1 << 3)
#define JUSTIFY		  (1 << 2)
#define RANGE(x)	  ((x) & 0x3)
#define RANGE_PM_2g	  0
#define RANGE_PM_4g	  1
#define RANGE_PM_8g	  2
#define RANGE_PM_16g	3

/*
 * Maximum value our axis may get in full res mode for the input device
 * (signed 13 bits)
 */
#define ADXL_FULLRES_MAX_VAL 4096

/*
 * Maximum value our axis may get in fixed res mode for the input device
 * (signed 10 bits)
 */
#define ADXL_FIXEDRES_MAX_VAL 512

/* FIFO_CTL Bits */
#define FIFO_MODE(x)	(((x) & 0x3) << 6)
#define FIFO_BYPASS	  0
#define FIFO_FIFO	    1
#define FIFO_STREAM	  2
#define FIFO_TRIGGER	3
#define TRIGGER		(1 << 5)
#define SAMPLES(x)	((x) & 0x1F)

/* FIFO_STATUS Bits */
#define FIFO_TRIG	(1 << 7)
#define ENTRIES(x)	((x) & 0x3F)

#define AC_READ(ac, reg, buf)	  ( (ac)->read ( (ac)->client, reg, buf ) )
#define AC_WRITE(ac, reg, val)	( (ac)->write ( (ac)->client, reg, val ) )

#define USE_FIFO_MODE
#define REGISTER_INPUT_DEVICE


static const struct adxl345_platform_data adxl345_default_init = {
  	.tap_threshold = 64, //62.5mg LSB, reg 0x1d
  	.x_axis_offset = 0,  //15.6mg LSB, reg 0x1e
	.y_axis_offset = 0,  //15.6mg LSB, reg 0x1f
	.z_axis_offset = 0,  //15.6mg LSB, reg 0x20
	.tap_duration  = 0x10, //0.625ms LSB, reg 0x21,should not be less than 5.
	.tap_latency   = 0x60, //1.25ms LSB, reg 0x22
	.tap_window    = 0x30, //1.25ms LSB, reg 0x23 
	.activity_threshold   = 6, //62.5mg LSB, reg 0x24
	.inactivity_threshold = 4, //62.5mg LSB, reg 0x25
	.inactivity_time = 2,      //1s LSB, reg 0x26
	.act_axis_control = 0xFF,  //ACT_ACDC,ACT_X_EN,ACT_Y_EN,ACT_Z_EN, reg 0x27
	.free_fall_threshold = 8,  //62.5mg LSB, reg 0x28, 300~600mg recommended
	.free_fall_time = 0x20,      //5ms LSB, reg 0x29, 100~350ms recommended 
	.tap_axis_control = TAP_X_EN | TAP_Y_EN | TAP_Z_EN, // | SUPPRESS,//reg 0x2a
	.data_rate = 7, //3200HZ/(2^(15-x)), reg 0x2c, data rate bigger, more sensitive.
	.data_format = FULL_RES, //reg 0x31

	.ev_type = EV_ABS,  // EV_ABS=0x03
	.ev_code_x = ABS_X,	/* EV_ABS ABS_X=0x01*/
	.ev_code_y = ABS_Y,	/* EV_ABS ABS_Y=0x02*/
	.ev_code_z = ABS_Z,	/* EV_ABS ABS_Z=0x03*/
   
  	//default type EV_KEY=0x01 
	.ev_code_tap_x = KEY_X,	/* EV_KEY KEY_X=45*/
	.ev_code_tap_y = KEY_Y,	/* EV_KEY KEY_Y=21*/
	.ev_code_tap_z = KEY_Z,	/* EV_KEY KEY_Z=44*/
	.ev_code_ff    = KEY_F, /* EV_KEY KEY_F=33*/
	.ev_code_activity   = KEY_A,   /* EV_KEY KEY_A=30 */
	.ev_code_inactivity = KEY_I,   /* EV_KEY KEY_I=23 */
	
	.low_power_mode = 0,
	.power_mode = ADXL_AUTO_SLEEP | ADXL_LINK,  //reg 0x2d
#ifdef USE_FIFO_MODE
	.fifo_mode = /*FIFO_BYPASS,*/FIFO_STREAM,  //reg 0x38  FIFO_BYPASS FIFO_STREAM
	.watermark = 1,  //reg 0x2e, INT_ENABLE
#else
	.fifo_mode = FIFO_BYPASS,  //reg 0x38  FIFO_BYPASS FIFO_STREAM
	.watermark = 0,  //reg 0x2e, INT_ENABLE
#endif
};

static int adxl34x_i2c_read(struct i2c_client *client, unsigned char reg, unsigned char * buf);
static int adxl34x_i2c_write(struct i2c_client *client, unsigned char reg, unsigned char val);
static void adxl345_enable(struct adxl345 *ac);
static void adxl345_disable(struct adxl345 *ac);
static void adxl345_get_triple(struct adxl345 *ac, struct axis_triple *axis);


#ifndef REGISTER_INPUT_DEVICE

static bool bStopPool;
static int gsDelayTime=200;

void adxl34x_get_axis_triple(struct adxl345 *ac, struct axis_triple *current_axis)
{
	unsigned int   Buffer[3] = {0,0,0};	//Three Axis Buffer Value
	signed   int   Decimal[3] = {0,0,0}; //Three Axis Deciaml Value
	unsigned char  X_DIR = 0, Y_DIR = 1, Z_DIR = 2;  // Three Directions
	unsigned char	X_Low, X_High, Y_Low, Y_High, Z_Low, Z_High;

	mutex_lock(&ac->mutex);
	//Get high byte and low byte data of X, Y, Z axis
	/*X_Low	= */adxl34x_i2c_read(ac->client, DATAX0, &X_Low);
	/*X_High	= */adxl34x_i2c_read(ac->client, DATAX1, &X_High);
	/*Y_Low	= */adxl34x_i2c_read(ac->client, DATAY0, &Y_Low);
	/*Y_High	= */adxl34x_i2c_read(ac->client, DATAY1, &Y_High);
	/*Z_Low	= */adxl34x_i2c_read(ac->client, DATAZ0, &Z_Low);
	/*Z_High	= */adxl34x_i2c_read(ac->client, DATAZ1, &Z_High);

	//filter to 11bit
	X_High &= 0x07;
	Y_High &= 0x07;
	Z_High &= 0x07;

	Buffer[X_DIR] = (X_High<<8) | X_Low;
	Buffer[Y_DIR] = (Y_High<<8) | Y_Low;
	Buffer[Z_DIR] = (Z_High<<8) | Z_Low;

	mutex_unlock(&ac->mutex);
	
	// print 3-axis value in Hex
	// printk("ADXL345/346 axis triple: X axis = 0x%04x, Y axis = 0x%04x, Z axis = 0x%04x \n" , Buffer[0], Buffer[1],  Buffer[2]);

	// Translate the twos complement to true binary code for +/-
	if (Buffer[X_DIR] > 0x400)
		Decimal[X_DIR] = Buffer[X_DIR] - 0x800;
	else
		Decimal[X_DIR] = Buffer[X_DIR];

	if (Buffer[Y_DIR] > 0x400)
		Decimal[Y_DIR] = Buffer[Y_DIR] - 0x800;
	else
		Decimal[Y_DIR] = Buffer[Y_DIR];
	
	if (Buffer[Z_DIR] > 0x400)
		Decimal[Z_DIR] = Buffer[Z_DIR] - 0x800;
	else
		Decimal[Z_DIR] = Buffer[Z_DIR];
//&*&*&*SW1_20091119 because android's x y is diff with EVB G-sensor locate so need change  x to y and y to -x
	current_axis->x = Decimal[Y_DIR];
	current_axis->y = Decimal[X_DIR]* -1;
//&*&*&*SW2_20091119 because android's x y is diff with EVB G-sensor locate so need change  x to y and y to -x
	current_axis->z = Decimal[Z_DIR];

}

static ssize_t attr_read(struct device *dev, struct device_attribute *devattr,		char *buf)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	int data_len=0;	
	struct axis_triple axis;

	adxl34x_get_axis_triple(ac, &axis);
	if(bStopPool)
	{
		data_len = sprintf(buf, "wake");
		bStopPool = false;
	}
	else
	{
		data_len = sprintf(buf, "acceleration:%d:%d:%d:%d", axis.x, axis.y, axis.z, gsDelayTime);
	}
	return data_len;
}

static ssize_t attr_control(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{	
	struct adxl345 *ac = dev_get_drvdata(dev);
	int ms=0;
	printk(KERN_INFO "gsensor: attr_control buf=%s\n",buf);

	mutex_lock(&ac->mutex);	
	if(!strcmp((const char*)buf, "wake"))
	{
		bStopPool	= true;
	}
	else if(sscanf(buf, "set-delay:%d", &ms) == 1)
	{
		gsDelayTime = ms;
	}
	mutex_unlock(&ac->mutex);
	return count;
}
static DEVICE_ATTR(adxl34x_attr, 0666, attr_read, attr_control);
#endif

#ifdef REGISTER_INPUT_DEVICE
static void adxl345_service_ev_fifo(struct adxl345 *ac)
{
	struct adxl345_platform_data *pdata = &ac->pdata;
	struct axis_triple axis;
	adxl345_get_triple(ac, &axis);
	//dprintk(KERN_ALERT "%s: fifo service input_event, axis->x is %d, axis->y is %d, axis->z is %d\n",__func__, axis.x, axis.y, axis.z);
	input_event(ac->input, ac->pdata.ev_type, pdata->ev_code_x, axis.x);
	input_sync(ac->input);
	input_event(ac->input, ac->pdata.ev_type, pdata->ev_code_y, axis.y);
	input_sync(ac->input);
	input_event(ac->input, ac->pdata.ev_type, pdata->ev_code_z, axis.z);
	input_sync(ac->input);
}

static void adxl345_report_key_single(struct input_dev *input, int code)
{
	input_report_key(input, code, 1);
	input_sync(input);
	input_report_key(input, code, 0);
	input_sync(input);
}

static void adxl345_report_key_double(struct input_dev *input, int code)
{
  	input_report_key(input, code, 1);
	input_sync(input);
	input_report_key(input, code, 0);
	input_sync(input);
	input_report_key(input, code, 1);
	input_sync(input);
	input_report_key(input, code, 0);
	input_sync(input);
}
#endif

static void adxl345_get_triple(struct adxl345 *ac, struct axis_triple *axis)
{
	//unsigned short buf[3];
	unsigned char buf[6];
	short int temp;
	ac->read_block(ac->client, DATAX0, 6, (unsigned char *)buf);
	
	mutex_lock(&ac->mutex);
	//filter to 10bit
	buf[1] &= 0x07;
	buf[3] &= 0x07;
	buf[5] &= 0x07;

	axis->x = (buf[1]<<8) + buf[0];
	axis->y = (buf[3]<<8) + buf[2];
	axis->z = (buf[5]<<8) + buf[4];
	
	// Translate the twos complement to true binary code for +/-

	if (axis->x > 0x400)
		axis->x -= 0x800;
	//else
		//axis->x = axis->x;

	if (axis->y > 0x400)
		axis->y -= 0x800;
	//else
		//axis->y = axis->y;
	
	if (axis->z > 0x400)
		axis->z -= 0x800;
	//else
		//axis->z = axis->z;

	temp = axis->x;
	axis->x = axis->y;
	axis->y = temp * -1;
	
	mutex_unlock(&ac->mutex);
}

static int act_count = 0;
static int inact_count = 0;
static int ff_count = 0;
static int tap_count = 0;

void adxl345_work(struct work_struct *work)
{
	struct adxl345 *ac = container_of(work, struct adxl345, work);
	struct adxl345_platform_data *pdata = &ac->pdata;
	unsigned char int_stat, tap_stat;
#ifdef USE_FIFO_MODE
	unsigned char fifo_state;
	int samples;
#endif
	
	/*ACT_TAP_STATUS should be read before clearing the interrupt
	 * Avoid reading ACT_TAP_STATUS in case TAP detection is disabled*/
	if (pdata->tap_axis_control & (TAP_X_EN | TAP_Y_EN | TAP_Z_EN))  // | SUPPRESS))
		AC_READ(ac, ACT_TAP_STATUS, &tap_stat);
	else
		tap_stat = 0;
		
  	//read interrpt source
	AC_READ(ac, INT_SOURCE, &int_stat);
	
  	//if free fall
	if (int_stat & FREE_FALL)
	{
		dprintk(KERN_ALERT "\n%s: Free Fall %d\n",__func__,++ff_count);
#ifdef REGISTER_INPUT_DEVICE
		adxl345_report_key_single(ac->input, pdata->ev_code_ff);
#endif
	}
#ifdef USE_FIFO_MODE
  	//if overrun
	if (int_stat & OVERRUN)
	{
		dprintk(KERN_ALERT "%s OVERRUN\n",__func__);
	}
#endif
 	 //if single tap
	if (int_stat & SINGLE_TAP) 
	{	
		if (tap_stat & TAP_X_SRC)
		{
			dprintk(KERN_ALERT "\n%s: Single TapX  %d\n",__func__,++tap_count);
#ifdef REGISTER_INPUT_DEVICE
			adxl345_report_key_single(ac->input,pdata->ev_code_tap_x);
#endif
		}
		if (tap_stat & TAP_Y_SRC)
		{
			dprintk(KERN_ALERT "\n%s: Single TapY  %d\n",__func__,++tap_count);
#ifdef REGISTER_INPUT_DEVICE
			adxl345_report_key_single(ac->input,pdata->ev_code_tap_y);
#endif
		}
		if (tap_stat & TAP_Z_SRC)
		{
			dprintk(KERN_ALERT "\n%s: Single TapZ  %d\n",__func__,++tap_count);
#ifdef REGISTER_INPUT_DEVICE
			adxl345_report_key_single(ac->input,pdata->ev_code_tap_z);
#endif
		}
	}
  	//if double tap
	if (int_stat & DOUBLE_TAP) 
	{
		if (tap_stat & TAP_X_SRC)
		{
			dprintk(KERN_ALERT "\n%s: Double TapX  %d\n",__func__,++tap_count);
#ifdef REGISTER_INPUT_DEVICE
			adxl345_report_key_double(ac->input,pdata->ev_code_tap_x);
#endif
		}
		if (tap_stat & TAP_Y_SRC)
		{
			dprintk(KERN_ALERT "\n%s: Double TapY  %d\n",__func__,++tap_count);
#ifdef REGISTER_INPUT_DEVICE
			adxl345_report_key_double(ac->input,pdata->ev_code_tap_y);
#endif
		}
		if (tap_stat & TAP_Z_SRC)
		{
			dprintk(KERN_ALERT "\n%s: Double TapZ  %d\n",__func__,++tap_count);
#ifdef REGISTER_INPUT_DEVICE
			adxl345_report_key_double(ac->input,pdata->ev_code_tap_z);
#endif
		}
	}
  	//if activity or inactivity
	if (pdata->ev_code_activity || pdata->ev_code_inactivity) 
	{	
		if (int_stat & ACTIVITY)
		{
			if(tap_stat & ACT_X_SRC)
			{
				dprintk(KERN_ALERT "\n%s: Activity ActX  %d\n",__func__,++act_count);
			}
			if(tap_stat & ACT_Y_SRC)
			{
				dprintk(KERN_ALERT "\n%s: Activity ActY  %d\n",__func__,++act_count);
			}
			if(tap_stat & ACT_Z_SRC)
			{
				dprintk(KERN_ALERT "\n%s: Activity ActZ  %d\n",__func__,++act_count);
			}
#ifdef REGISTER_INPUT_DEVICE
			adxl345_report_key_single(ac->input, pdata->ev_code_activity);
#endif
		}
		if (int_stat & INACTIVITY)
		{
			dprintk(KERN_ALERT "\n%s: Inactivity %d\n",__func__, ++inact_count);
#ifdef REGISTER_INPUT_DEVICE
			adxl345_report_key_single(ac->input, pdata->ev_code_inactivity);
#endif
		}
	}
#ifdef USE_FIFO_MODE
	//if data ready | watermark
	if (int_stat & (DATA_READY | WATERMARK)) 
	{
		//dprintk(KERN_ALERT "%s: DataReady or WaterMark\n",__func__);
		if (pdata->fifo_mode)
		{
			AC_READ(ac, FIFO_STATUS, &fifo_state);
			samples = ENTRIES(fifo_state) + 1;
		}
		else
			samples = 1;

		for (; samples>0; samples--) 
		{
#ifdef REGISTER_INPUT_DEVICE
			adxl345_service_ev_fifo(ac);
#endif
			/* To ensure that the FIFO has
			 * completely popped, there must be at least 5 us between
			 * the end of reading the data registers, signified by the
			 * transition to register 0x38 from 0x37 or the CS pin
			 * going high, and the start of new reads of the FIFO or
			 * reading the FIFO_STATUS register. For SPI operation at
			 * 1.5 MHz or lower, the register addressing portion of the
			 * transmission is sufficient delay to ensure the FIFO has
			 * completely popped. It is necessary for SPI operation
			 * greater than 1.5 MHz to de-assert the CS pin to ensure a
			 * total of 5 us, which is at most 3.4 us at 5 MHz
			 * operation.*/
			if (ac->fifo_delay && (samples>1))
				udelay(3);
		}
	}
#endif
	enable_irq(ac->client->irq);
}

static ssize_t adxl345_disable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", ac->disabled);
}

static ssize_t adxl345_disable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	unsigned long val;
	int error;

	error = strict_strtoul(buf, 10, &val);
	if (error)
		return error;
		
	if (val)
		adxl345_disable(ac);
	else
		adxl345_enable(ac);

	return count;
}
static DEVICE_ATTR(disable, 0666, adxl345_disable_show, adxl345_disable_store);

static ssize_t adxl345_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	unsigned char data[10];
	short int data_x,data_y,data_z;
	ac->read_block(ac->client, DATAX0, 6, data);
  	data_x = (data[1]<<8) + data[0];
  	data_y = (data[3]<<8) + data[2];
  	data_z = (data[5]<<8) + data[4];
	return sprintf(buf, "%d,%d,%d\n", data_x,data_y,data_z);
}
static DEVICE_ATTR(data, 0664, adxl345_data_show, NULL);

static ssize_t adxl345_ff_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	//struct adxl345 *ac = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", ff_count);
}
static DEVICE_ATTR(ff_count, 0664, adxl345_ff_count_show, NULL);


static ssize_t adxl345_calibrate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	ssize_t count;
	mutex_lock(&ac->mutex);
	count = sprintf(buf, "%d,%d,%d\n", ac->hwcal.x, ac->hwcal.y, ac->hwcal.z);
	mutex_unlock(&ac->mutex);
	return count;
}

static ssize_t adxl345_calibrate_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	/* Hardware offset calibration has a resolution of 15.6 mg/LSB.
	We use HW calibration and handle the remaining bits in SW. (4mg/LSB)*/
	int i,j;
  	char bufx[5];
	char bufy[5];
	char bufz[5];
	long x,y,z;
	memset(bufx,0,5);
	memset(bufy,0,5);
	memset(bufz,0,5);
	
	i = 0;
	j = 0;
	while ((i<count) && (buf[i]!=','))
	{
	    bufx[j++] = buf[i++];	
	}
	i++;
	j = 0;
	while ((i<count) && (buf[i]!=','))
	{
	    bufy[j++] = buf[i++];	
	}
	i++;
	j = 0;
	while (i<count)
	{
	    bufz[j++] = buf[i++];	
	}
	dprintk(KERN_ALERT "%s: bufx=%s,bufy=%s,bufz=%s\n",__func__,bufx,bufy,bufz);
  
  	strict_strtol(bufx, 10, &x);
  	strict_strtol(bufy, 10, &y);
  	strict_strtol(bufz, 10, &z);
  
  	ac->hwcal.x = (s8) x;
  	ac->hwcal.y = (s8) y;
  	ac->hwcal.z = (s8) z;
  	
  	mutex_lock(&ac->mutex);
	AC_WRITE(ac, OFSX, ac->hwcal.x);
	AC_WRITE(ac, OFSY, ac->hwcal.y);
	AC_WRITE(ac, OFSZ, ac->hwcal.z);
	mutex_unlock(&ac->mutex);
	
	return count;
}
static DEVICE_ATTR(calibrate, 0664, adxl345_calibrate_show, adxl345_calibrate_store);

static ssize_t adxl345_rate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	ssize_t count;
	mutex_lock(&ac->mutex);
	count = sprintf(buf, "%u\n", RATE(ac->pdata.data_rate));
	mutex_unlock(&ac->mutex);
	return count;
}

static ssize_t adxl345_rate_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	unsigned long val;
	int error;
	mutex_lock(&ac->mutex);
	error = strict_strtoul(buf, 10, &val);
	if (error)
		return error;
	ac->pdata.data_rate = RATE(val);
	AC_WRITE(ac, BW_RATE, ac->pdata.data_rate |
		 (ac->pdata.low_power_mode ? LOW_POWER : 0));
	mutex_unlock(&ac->mutex);
	return count;
}
static DEVICE_ATTR(rate, 0664, adxl345_rate_show, adxl345_rate_store);

static ssize_t adxl345_autosleep_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	ssize_t count;
	mutex_lock(&ac->mutex);
	count = sprintf(buf, "%u\n", ac->pdata.power_mode & (PCTL_AUTO_SLEEP | PCTL_LINK) ? 1 : 0);
	mutex_unlock(&ac->mutex);
	return count;
}

static ssize_t adxl345_autosleep_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	unsigned long val;
	int error;
	mutex_lock(&ac->mutex);
	error = strict_strtoul(buf, 10, &val); //Enable(1) or disable(0)
	if (error)
		return error;
	if (val)
		ac->pdata.power_mode |= (PCTL_AUTO_SLEEP | PCTL_LINK);
	else
		ac->pdata.power_mode &= ~(PCTL_AUTO_SLEEP | PCTL_LINK);
	if (!ac->disabled && ac->opened)
		AC_WRITE(ac, POWER_CTL, ac->pdata.power_mode | PCTL_MEASURE);
	mutex_unlock(&ac->mutex);
	return count;
}
static DEVICE_ATTR(autosleep, 0664, adxl345_autosleep_show, adxl345_autosleep_store);

#ifdef ADXL_DEBUG
static ssize_t adxl345_write_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct adxl345 *ac = dev_get_drvdata(dev);
	unsigned long val;
	int error;
	/* This allows basic ADXL register write access for debug purposes.*/
	mutex_lock(&ac->mutex);
	error = strict_strtoul(buf, 16, &val);
	if (error)
		return error;
	AC_WRITE(ac, val >> 8, val & 0xFF); //high 8bit is reg, low 8bit is value
	mutex_unlock(&ac->mutex);
	return count;
}
static DEVICE_ATTR(write, 0664, NULL, adxl345_write_store);
#endif

static struct attribute *adxl345_attributes[] = {
	&dev_attr_disable.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_rate.attr,
	&dev_attr_autosleep.attr,
	&dev_attr_data.attr,
	&dev_attr_ff_count.attr,
#ifdef ADXL_DEBUG
	&dev_attr_write.attr,
#endif
#ifndef REGISTER_INPUT_DEVICE
	&dev_attr_adxl34x_attr.attr,
#endif
	NULL
};

static const struct attribute_group adxl345_attr_group = {
	.attrs = adxl345_attributes,
};

#ifdef REGISTER_INPUT_DEVICE
static int adxl345_input_open(struct input_dev *input)
{
	struct adxl345 *ac = input_get_drvdata(input);
	mutex_lock(&ac->mutex);
	ac->opened = 1;
	mutex_unlock(&ac->mutex);
	adxl345_enable(ac);
	return 0;
}
static void adxl345_input_close(struct input_dev *input)
{
	struct adxl345 *ac = input_get_drvdata(input);
	adxl345_disable(ac);
	mutex_lock(&ac->mutex);
	ac->opened = 0;
	mutex_unlock(&ac->mutex);
}
#endif

static irqreturn_t adxl345_irq(int irq, void *handle)
{
	struct adxl345 *ac = handle;
	disable_irq_nosync(irq);
	schedule_work(&ac->work);
	return IRQ_HANDLED;
}

static void adxl345_read_and_show_regs(struct adxl345 *ac)
{ 
  //unsigned char reg_data;
  unsigned char buf[30];
  short int data;
  
  ac->read_block(ac->client, THRESH_TAP, 11, (unsigned char *)buf);
  dprintk(KERN_ALERT "THRESH_TAP is %d.\n",buf[0]);
  dprintk(KERN_ALERT "OFSX is %d.\n",buf[1]);
  dprintk(KERN_ALERT "OFSY is %d.\n",buf[2]);
  dprintk(KERN_ALERT "OFSZ is %d.\n",buf[3]);
  dprintk(KERN_ALERT "DUR is %d.\n",buf[4]);
  dprintk(KERN_ALERT "LATENT is %d.\n",buf[5]);
  dprintk(KERN_ALERT "WINDOW is %d.\n",buf[6]);
  dprintk(KERN_ALERT "THRESH_ACT  is %d.\n",buf[7]);
  dprintk(KERN_ALERT "THRESH_INACT is %d.\n",buf[8]);
  dprintk(KERN_ALERT "TIME_INACT  is %d.\n",buf[9]);
  dprintk(KERN_ALERT "ACT_INACT_CTL is 0x%x.\n",buf[10]);
  
  ac->read_block(ac->client, THRESH_FF, 10, (unsigned char *)buf);
  dprintk(KERN_ALERT "THRESH_FF   is 0x%x.\n",buf[0]);
  dprintk(KERN_ALERT "TIME_FF     is 0x%x.\n",buf[1]);
  dprintk(KERN_ALERT "TAP_AXES    is 0x%x.\n",buf[2]);
  dprintk(KERN_ALERT "ACT_TAP_STATUS is 0x%x.\n",buf[3]);
  dprintk(KERN_ALERT "BW_RATE     is 0x%x.\n",buf[4]);
  dprintk(KERN_ALERT "POWER_CTL   is 0x%x.\n",buf[5]);
  dprintk(KERN_ALERT "INT_ENABLE  is 0x%x.\n",buf[6]);
  dprintk(KERN_ALERT "INT_MAP     is 0x%x.\n",buf[7]);
  dprintk(KERN_ALERT "INT_SOURCE  is 0x%x.\n",buf[8]);
  dprintk(KERN_ALERT "DATA_FORMAT is 0x%x.\n",buf[9]);
  
  ac->read_block(ac->client, DATAX0, 8, (unsigned char *)buf);
  data = (buf[1]<<8) + buf[0];
  dprintk(KERN_ALERT "DATAX is %d.\n", data);
  data = (buf[3]<<8) + buf[2];
  dprintk(KERN_ALERT "DATAY is %d.\n", data);
  data = (buf[5]<<8) + buf[4];
  dprintk(KERN_ALERT "DATAZ is %d.\n", data);
  dprintk(KERN_ALERT "FIFO_CTL    is 0x%x.\n",buf[6]);
  dprintk(KERN_ALERT "FIFO_STATUS is 0x%x.\n",buf[7]);
}

static int __devinit adxl345_initialize(struct i2c_client *client, struct adxl345 *ac)
{
#ifdef REGISTER_INPUT_DEVICE
	struct input_dev *input_dev;
    int range;
#endif
	struct adxl345_platform_data *devpd = client->dev.platform_data;
	struct adxl345_platform_data *pdata;
	int err;
	unsigned char revid;
 	
 	dprintk(KERN_ALERT "Enter %s \n", __FUNCTION__);
	
	if (!client->irq) 
	{
		dprintk(KERN_ALERT "%s no IRQ?\n",__func__);
		return -ENODEV;
	}

	if (!devpd) 
	{
		dprintk(KERN_ALERT "%s No platfrom data: Using default initialization\n",__func__);
		devpd = (struct adxl345_platform_data *)&adxl345_default_init;
	}

	memcpy(&ac->pdata, devpd, sizeof(ac->pdata));
	pdata = &ac->pdata;

#ifdef REGISTER_INPUT_DEVICE
	input_dev = input_allocate_device();
	if (!input_dev)
	{
		return -ENOMEM;
	}
	ac->input = input_dev;
#endif
	ac->disabled = 1;
	ac->opened = 1;

	INIT_WORK(&ac->work, adxl345_work);
	mutex_init(&ac->mutex);

	AC_READ(ac, DEVID, &revid);

	switch (revid) 
	{    
		case ID_ADXL345:
				ac->model = 0x345;
				dprintk(KERN_ALERT "************************************************************\n");
				dprintk(KERN_ALERT "%s: ADXL345 ID is 0x%x.\n",__func__,ac->model);
				dprintk(KERN_ALERT "************************************************************\n");
				break;
		default:
				dprintk(KERN_ALERT "%s Failed to probe, no find sensor device.\n", __func__);
				err = -ENODEV;
				goto err_free_mem;
	}
#ifdef REGISTER_INPUT_DEVICE
	snprintf(ac->phys, sizeof(ac->phys), "%s/input0", dev_name(&client->dev));

	input_dev->name = "ADXL345 accelerometer";
	input_dev->phys = ac->phys;
	input_dev->dev.parent = &client->dev;
	input_dev->id.product = ac->model;
	input_dev->id.bustype = BUS_I2C;
	//input_dev->open = adxl345_input_open;
	//input_dev->close = adxl345_input_close;
	input_set_drvdata(input_dev, ac);//input_dev->dev.driver_data = ac;

	__set_bit(ac->pdata.ev_type, input_dev->evbit);
	if (ac->pdata.ev_type == EV_REL) 
	{
		__set_bit(REL_X, input_dev->relbit);
		__set_bit(REL_Y, input_dev->relbit);
		__set_bit(REL_Z, input_dev->relbit);
	} 
	else /* EV_ABS */
	{
		__set_bit(ABS_X, input_dev->absbit);
		__set_bit(ABS_Y, input_dev->absbit);
		__set_bit(ABS_Z, input_dev->absbit);

		if (pdata->data_format & FULL_RES)
			range = ADXL_FULLRES_MAX_VAL;	  /* Signed 13-bit */
		else
			range = ADXL_FIXEDRES_MAX_VAL;	/* Signed 10-bit */

		input_set_abs_params(input_dev, ABS_X, -range, range, 3, 3);
		input_set_abs_params(input_dev, ABS_Y, -range, range, 3, 3);
		input_set_abs_params(input_dev, ABS_Z, -range, range, 3, 3);
	}
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(pdata->ev_code_tap_x, input_dev->keybit);
	__set_bit(pdata->ev_code_tap_y, input_dev->keybit);
	__set_bit(pdata->ev_code_tap_z, input_dev->keybit);

	if (pdata->ev_code_ff) 
	{
		ac->int_mask = FREE_FALL;
		__set_bit(pdata->ev_code_ff, input_dev->keybit);
	}

	if (pdata->ev_code_activity)
	{
		__set_bit(pdata->ev_code_activity, input_dev->keybit);
	}
	
	if (pdata->ev_code_inactivity)
	{
		__set_bit(pdata->ev_code_inactivity, input_dev->keybit);
	}
#endif
	ac->int_mask = FREE_FALL;
	ac->int_mask |= ACTIVITY | INACTIVITY;
	//if have watermark, it must use fifo.
	if (pdata->watermark) 
	{
		ac->int_mask |= WATERMARK;
		if (!FIFO_MODE(pdata->fifo_mode))
			pdata->fifo_mode |= FIFO_STREAM;
	} 
	else 
	{
		//ac->int_mask |= DATA_READY;
	}

	if (pdata->tap_axis_control & (TAP_X_EN | TAP_Y_EN | TAP_Z_EN))// | SUPPRESS))
		ac->int_mask |= SINGLE_TAP | DOUBLE_TAP;

	if (FIFO_MODE(pdata->fifo_mode) == FIFO_BYPASS)
		ac->fifo_delay = 0;

	AC_WRITE(ac, POWER_CTL, 0);
  
	err = request_irq(client->irq, adxl345_irq, IRQF_TRIGGER_FALLING, client->dev.driver->name, ac);
	if (err) 
	{
		dev_err(&client->dev, "irq %d busy?\n", client->irq);
		goto err_free_mem;
	}

	err = sysfs_create_group(&client->dev.kobj, &adxl345_attr_group);
	if (err)
		goto err_free_irq;
	
#ifdef REGISTER_INPUT_DEVICE
	err = input_register_device(input_dev);
	if (err)
		goto err_remove_attr;
#endif
	//***** Setting g-sensor via I2C *****
	AC_WRITE(ac, THRESH_TAP, pdata->tap_threshold);
	AC_WRITE(ac, OFSX, pdata->x_axis_offset);
	AC_WRITE(ac, OFSY, pdata->y_axis_offset);
	AC_WRITE(ac, OFSZ, pdata->z_axis_offset);
	ac->hwcal.x = pdata->x_axis_offset;
	ac->hwcal.y = pdata->y_axis_offset;
	ac->hwcal.z = pdata->z_axis_offset;
	AC_WRITE(ac, THRESH_TAP, pdata->tap_threshold);
	AC_WRITE(ac, DUR, pdata->tap_duration);
	AC_WRITE(ac, LATENT, pdata->tap_latency);
	AC_WRITE(ac, WINDOW, pdata->tap_window);
	AC_WRITE(ac, THRESH_ACT, pdata->activity_threshold);
	AC_WRITE(ac, THRESH_INACT, pdata->inactivity_threshold);
	AC_WRITE(ac, TIME_INACT, pdata->inactivity_time);
	AC_WRITE(ac, THRESH_FF, pdata->free_fall_threshold);
	AC_WRITE(ac, TIME_FF, pdata->free_fall_time);
	AC_WRITE(ac, TAP_AXES, pdata->tap_axis_control);
	AC_WRITE(ac, ACT_INACT_CTL, pdata->act_axis_control);
	AC_WRITE(ac, BW_RATE, RATE(ac->pdata.data_rate) | (pdata->low_power_mode ? LOW_POWER : 0));
	AC_WRITE(ac, DATA_FORMAT, pdata->data_format | INT_INVERT );
	AC_WRITE(ac, FIFO_CTL, FIFO_MODE(pdata->fifo_mode) | SAMPLES(pdata->watermark));
	AC_WRITE(ac, INT_MAP, 0x00);	/* Map all INTs to INT1 */
	AC_WRITE(ac, INT_ENABLE, ac->int_mask); 

	pdata->power_mode &= (PCTL_AUTO_SLEEP | PCTL_LINK);

	return 0;

 err_remove_attr:
	 sysfs_remove_group(&client->dev.kobj, &adxl345_attr_group);
 err_free_irq:
	 free_irq(client->irq, ac);
 err_free_mem:
 #ifdef REGISTER_INPUT_DEVICE
	 input_free_device(input_dev);
 #endif
	return err;
}

static void adxl345_disable(struct adxl345 *ac)
{
	mutex_lock(&ac->mutex);
	if (!ac->disabled && ac->opened) 
	{
		ac->disabled = 1;
		cancel_work_sync(&ac->work);
		/*A '0' places the ADXL345 into standby mode with minimum power consumption.*/
		AC_WRITE(ac, POWER_CTL, 0);
	}
	mutex_unlock(&ac->mutex);
}

static void adxl345_enable(struct adxl345 *ac)
{
	mutex_lock(&ac->mutex);
	if (ac->disabled && ac->opened) 
	{
		AC_WRITE(ac, POWER_CTL, ac->pdata.power_mode | PCTL_MEASURE);
		ac->disabled = 0;
	}
	mutex_unlock(&ac->mutex);
}

#ifdef CONFIG_PM
static int adxl345_suspend(struct i2c_client *client, pm_message_t message)
{
	struct adxl345 *ac = dev_get_drvdata(&client->dev);
	adxl345_disable(ac);
	return 0;
}

static int adxl345_resume(struct i2c_client *client)
{
	struct adxl345 *ac = dev_get_drvdata(&client->dev);
	adxl345_enable(ac);
	return 0;
}
#else
#define adxl345_suspend NULL
#define adxl345_resume  NULL
#endif


static int adxl34x_i2c_read(struct i2c_client *client, unsigned char reg, unsigned char * buf)
{
  int ret;
  struct i2c_msg send_msg = { client->addr, 0, 1, &reg };
  struct i2c_msg recv_msg = { client->addr, I2C_M_RD, 1, buf };
  
  ret = i2c_transfer(client->adapter, &send_msg, 1);
  if (ret < 0) 
  {
    dprintk(KERN_ALERT "%s transfer sendmsg Error.\n",__func__);
    return -EIO;
  }
  ret = i2c_transfer(client->adapter, &recv_msg, 1);
  if (ret < 0)
  {
    dprintk(KERN_ALERT "%s receive reg_data error.\n",__func__);	
    return -EIO;
  }
  return 0;     	
}


static int adxl34x_i2c_write(struct i2c_client *client, unsigned char reg, unsigned char val)
{
  int ret;
  unsigned char buf[2];
  struct i2c_msg msg = { client->addr, 0, 2, buf };
	
  buf[0] = reg;
  buf[1] = val; 

  ret = i2c_transfer(client->adapter, &msg, 1);
  if (ret < 0)
  {
  	dprintk(KERN_ALERT "%s write reg Error.\n",__func__);
  	return -EIO;
  }
  return 0;
}

static int adxl345_i2c_read_block_data(struct i2c_client *client, unsigned char reg, int count,unsigned char *buf)
{
	int ret;
  struct i2c_msg send_msg = { client->addr, 0, 1, &reg };
  struct i2c_msg recv_msg = { client->addr, I2C_M_RD, count, buf };
  
  ret = i2c_transfer(client->adapter, &send_msg, 1);
  if (ret < 0) 
  {
    dprintk(KERN_ALERT "%s transfer sendmsg Error.\n",__func__);
    return -EIO;
  }
  ret = i2c_transfer(client->adapter, &recv_msg, 1);
  if (ret < 0)
  {
    dprintk(KERN_ALERT "%s receive reg_data error.\n",__func__);	
    return -EIO;
  }
  return 0;     	
}

static int __devinit adxl345_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct adxl345 *ac;
	int error;
	
	dprintk("Enter %s \n", __FUNCTION__);
#ifdef CONFIG_EX3_HARDWARE_DVT

	//setup GPL13 GPL14 for INT1 and INT2
  s3c_gpio_cfgpin(S3C64XX_GPL(14), S3C_GPIO_SFN(3)); 
	s3c_gpio_setpull(S3C64XX_GPL(14), S3C_GPIO_PULL_UP);
	set_irq_type(IRQ_EINT(22), IRQ_TYPE_EDGE_FALLING);
  
  s3c_gpio_cfgpin(S3C64XX_GPL(13), S3C_GPIO_SFN(3));
	s3c_gpio_setpull(S3C64XX_GPL(13), S3C_GPIO_PULL_UP);
	set_irq_type(IRQ_EINT(21), IRQ_TYPE_EDGE_FALLING);
#else
	//setup GPM3 GPM4 for INT1 and INT2
  s3c_gpio_cfgpin(S3C64XX_GPM(4), S3C_GPIO_SFN(3)); 
	s3c_gpio_setpull(S3C64XX_GPM(4), S3C_GPIO_PULL_UP);
	set_irq_type(IRQ_EINT(27), IRQ_TYPE_EDGE_FALLING);

	s3c_gpio_cfgpin(S3C64XX_GPM(3), S3C_GPIO_SFN(3));
	s3c_gpio_setpull(S3C64XX_GPM(3), S3C_GPIO_PULL_UP);
	set_irq_type(IRQ_EINT(26), IRQ_TYPE_EDGE_FALLING);
#endif
	ac = kzalloc(sizeof(struct adxl345), GFP_KERNEL);
	if (!ac)
		return -ENOMEM;

	i2c_set_clientdata(client, ac);//client->dev.driver_data = ac;

	ac->client = client;
	ac->read_block = adxl345_i2c_read_block_data;
	ac->read = adxl34x_i2c_read;
	ac->write = adxl34x_i2c_write;

	error = adxl345_initialize(client, ac);
	if (error) 
	{
		i2c_set_clientdata(client, NULL);
		kfree(ac);
	}


	return 0;
}

static int __devexit adxl345_cleanup(struct i2c_client *client, struct adxl345 *ac)
{
	adxl345_disable(ac);
	sysfs_remove_group(&ac->client->dev.kobj, &adxl345_attr_group);
	free_irq(ac->client->irq, ac);
	input_unregister_device(ac->input);
	dprintk(KERN_ALERT "%s unregistered accelerometer\n",__func__);
	return 0;
}

static int __devexit adxl345_i2c_remove(struct i2c_client *client)
{
	struct adxl345 *ac = dev_get_drvdata(&client->dev);
	adxl345_cleanup(client, ac);
	i2c_set_clientdata(client, NULL);
	kfree(ac);
	return 0;
}

static const struct i2c_device_id adxl345_id[] = {
	{ "adxl345", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, adxl345_id);

static struct i2c_driver adxl345_driver = {
	.driver = {
		.name = "adxl345",
		.owner = THIS_MODULE,
	},
	.probe    = adxl345_i2c_probe,
	.remove   = __devexit_p(adxl345_i2c_remove),
	.suspend  = adxl345_suspend,
	.resume   = adxl345_resume,
	.id_table = adxl345_id,
};

static int __init adxl345_i2c_init(void)
{
	return i2c_add_driver(&adxl345_driver);
}
module_init(adxl345_i2c_init);

static void __exit adxl345_i2c_exit(void)
{
	i2c_del_driver(&adxl345_driver);
}
module_exit(adxl345_i2c_exit);

MODULE_AUTHOR("Brian Xu <xuhuanting929@yahoo.com.cn>");
MODULE_DESCRIPTION("ADXL345 Three-Axis Digital Accelerometer Driver");
MODULE_LICENSE("GPL");









