/* linux/drivers/char/watchdog/s3c_wdt.c
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C Watchdog Timer Support
 *
 * Based on, softdog.c by Alan Cox,
 *     (c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <mach/map.h>

#undef S3C_VA_WATCHDOG
#define S3C_VA_WATCHDOG (0)

#include <plat/regs-watchdog.h>

#define PFX "s3c-wdt: "

#define CONFIG_S3C_WATCHDOG_ATBOOT		(0)
#define CONFIG_S3C_WATCHDOG_DEFAULT_TIME	(15)

static int nowayout	= WATCHDOG_NOWAYOUT;
static int tmr_margin	= CONFIG_S3C_WATCHDOG_DEFAULT_TIME;
static int tmr_atboot	= CONFIG_S3C_WATCHDOG_ATBOOT;
static int soft_noboot;
static int debug;

module_param(tmr_margin,  int, 0);
module_param(tmr_atboot,  int, 0);
module_param(nowayout,    int, 0);
module_param(soft_noboot, int, 0);
module_param(debug,	  int, 0);

MODULE_PARM_DESC(tmr_margin, "Watchdog tmr_margin in seconds. default="
		__MODULE_STRING(CONFIG_S3C_WATCHDOG_DEFAULT_TIME) ")");
MODULE_PARM_DESC(tmr_atboot,
		"Watchdog is started at boot time if set to 1, default="
			__MODULE_STRING(CONFIG_S3C_WATCHDOG_ATBOOT));
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
MODULE_PARM_DESC(soft_noboot, "Watchdog action, set to 1 to ignore reboots, 0 to reboot (default depends on ONLY_TESTING)");
MODULE_PARM_DESC(debug, "Watchdog debug, set to >1 for debug, (default 0)");


typedef enum close_state {
	CLOSE_STATE_NOT,
	CLOSE_STATE_ALLOW = 0x4021
} close_state_t;

static unsigned long open_lock;
static struct device    *wdt_dev;	/* platform device attached to */
static struct resource	*wdt_mem;
static struct resource	*wdt_irq;
static struct clk	*wdt_clock;
static void __iomem	*wdt_base;
static unsigned int	 wdt_count;
static close_state_t	 allow_close;
static DEFINE_SPINLOCK(wdt_lock);

/* watchdog control routines */

#define DBG(msg...) do { \
	if (debug) \
		printk(KERN_INFO msg); \
	} while (0)

/* functions */

static void s3c_wdt_keepalive(void)
{
	spin_lock(&wdt_lock);
	writel(wdt_count, wdt_base + S3C_WTCNT);
	spin_unlock(&wdt_lock);
}

static void __s3c_wdt_stop(void)
{
	unsigned long wtcon;

	wtcon = readl(wdt_base + S3C_WTCON);
	wtcon &= ~(S3C_WTCON_ENABLE | S3C_WTCON_RSTEN);
	writel(wtcon, wdt_base + S3C_WTCON);
}

static void s3c_wdt_stop(void)
{
	spin_lock(&wdt_lock);
	__s3c_wdt_stop();
	spin_unlock(&wdt_lock);
}

static void s3c_wdt_start(void)
{
	unsigned long wtcon;

	spin_lock(&wdt_lock);

	__s3c_wdt_stop();

	wtcon = readl(wdt_base + S3C_WTCON);
	wtcon |= S3C_WTCON_ENABLE | S3C_WTCON_DIV128;

	if (soft_noboot) {
		wtcon |= S3C_WTCON_INTEN;
		wtcon &= ~S3C_WTCON_RSTEN;
	} else {
		wtcon &= ~S3C_WTCON_INTEN;
		wtcon |= S3C_WTCON_RSTEN;
	}

	DBG("%s: wdt_count=0x%08x, wtcon=%08lx\n",
	    __func__, wdt_count, wtcon);

	writel(wdt_count, wdt_base + S3C_WTDAT);
	writel(wdt_count, wdt_base + S3C_WTCNT);
	writel(wtcon, wdt_base + S3C_WTCON);
	spin_unlock(&wdt_lock);
}

static int s3c_wdt_set_heartbeat(int timeout)
{
	unsigned int freq = clk_get_rate(wdt_clock);
	unsigned int count;
	unsigned int divisor = 1;
	unsigned long wtcon;

	if (timeout < 1)
		return -EINVAL;

	freq /= 128;
	count = timeout * freq;

	DBG("%s: count=%d, timeout=%d, freq=%d\n",
	    __func__, count, timeout, freq);

	/* if the count is bigger than the watchdog register,
	   then work out what we need to do (and if) we can
	   actually make this value
	*/

	if (count >= 0x10000) {
		for (divisor = 1; divisor <= 0x100; divisor++) {
			if ((count / divisor) < 0x10000)
				break;
		}

		if ((count / divisor) >= 0x10000) {
			dev_err(wdt_dev, "timeout %d too big\n", timeout);
			return -EINVAL;
		}
	}

	tmr_margin = timeout;

	DBG("%s: timeout=%d, divisor=%d, count=%d (%08x)\n",
	    __func__, timeout, divisor, count, count/divisor);

	count /= divisor;
	wdt_count = count;

	/* update the pre-scaler */
	wtcon = readl(wdt_base + S3C_WTCON);
	wtcon &= ~S3C_WTCON_PRESCALE_MASK;
	wtcon |= S3C_WTCON_PRESCALE(divisor-1);

	writel(count, wdt_base + S3C_WTDAT);
	writel(wtcon, wdt_base + S3C_WTCON);

	return 0;
}

/*
 *	/dev/watchdog handling
 */

static int s3c_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &open_lock))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	allow_close = CLOSE_STATE_NOT;

	/* start the timer */
	s3c_wdt_start();
	return nonseekable_open(inode, file);
}

static int s3c_wdt_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 * 	Lock it in if it's a module and we set nowayout
	 */

	if (allow_close == CLOSE_STATE_ALLOW)
		s3c_wdt_stop();
	else {
		dev_err(wdt_dev, "Unexpected close, not stopping watchdog\n");
		s3c_wdt_keepalive();
	}
	allow_close = CLOSE_STATE_NOT;
	clear_bit(0, &open_lock);
	return 0;
}

static ssize_t s3c_wdt_write(struct file *file, const char __user *data,
				size_t len, loff_t *ppos)
{
	/*
	 *	Refresh the timer.
	 */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			allow_close = CLOSE_STATE_NOT;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					allow_close = CLOSE_STATE_ALLOW;
			}
		}
		s3c_wdt_keepalive();
	}
	return len;
}

#define OPTIONS WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE

static const struct watchdog_info s3c_wdt_ident = {
	.options          =     OPTIONS,
	.firmware_version =	0,
	.identity         =	"S3C Watchdog",
};


static long s3c_wdt_ioctl(struct file *file,	unsigned int cmd,
							unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_margin;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &s3c_wdt_ident,
			sizeof(s3c_wdt_ident)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_KEEPALIVE:
		s3c_wdt_keepalive();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, p))
			return -EFAULT;
		if (s3c_wdt_set_heartbeat(new_margin))
			return -EINVAL;
		s3c_wdt_keepalive();
		return put_user(tmr_margin, p);
	case WDIOC_GETTIMEOUT:
		return put_user(tmr_margin, p);
	default:
		return -ENOTTY;
	}
}

/* kernel interface */

static const struct file_operations s3c_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= s3c_wdt_write,
	.unlocked_ioctl	= s3c_wdt_ioctl,
	.open		= s3c_wdt_open,
	.release	= s3c_wdt_release,
};

static struct miscdevice s3c_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &s3c_wdt_fops,
};

/* interrupt handler code */

static irqreturn_t s3c_wdt_irq(int irqno, void *param)
{
	dev_info(wdt_dev, "watchdog timer expired (irq)\n");

	s3c_wdt_keepalive();
	return IRQ_HANDLED;
}
/* device interface */

static int s3c_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev;
	unsigned int wtcon;
	int started = 0;
	int ret;
	int size;

	DBG("%s: probe=%p\n", __func__, pdev);

	dev = &pdev->dev;
	wdt_dev = &pdev->dev;

	/* get the memory region for the watchdog timer */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "no memory resource specified\n");
		return -ENOENT;
	}

	size = (res->end - res->start) + 1;
	wdt_mem = request_mem_region(res->start, size, pdev->name);
	if (wdt_mem == NULL) {
		dev_err(dev, "failed to get memory region\n");
		ret = -ENOENT;
		goto err_req;
	}

	wdt_base = ioremap(res->start, size);
	if (wdt_base == NULL) {
		dev_err(dev, "failed to ioremap() region\n");
		ret = -EINVAL;
		goto err_req;
	}

	DBG("probe: mapped wdt_base=%p\n", wdt_base);

	wdt_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (wdt_irq == NULL) {
		dev_err(dev, "no irq resource specified\n");
		ret = -ENOENT;
		goto err_map;
	}

	ret = request_irq(wdt_irq->start, s3c_wdt_irq, 0, pdev->name, pdev);
	if (ret != 0) {
		dev_err(dev, "failed to install irq (%d)\n", ret);
		goto err_map;
	}

	wdt_clock = clk_get(&pdev->dev, "watchdog");
	if (IS_ERR(wdt_clock)) {
		dev_err(dev, "failed to find watchdog clock source\n");
		ret = PTR_ERR(wdt_clock);
		goto err_irq;
	}

	clk_enable(wdt_clock);

	/* see if we can actually set the requested timer margin, and if
	 * not, try the default value */

	if (s3c_wdt_set_heartbeat(tmr_margin)) {
		started = s3c_wdt_set_heartbeat(
					CONFIG_S3C_WATCHDOG_DEFAULT_TIME);

		if (started == 0)
			dev_info(dev,
			   "tmr_margin value out of range, default %d used\n",
			       CONFIG_S3C_WATCHDOG_DEFAULT_TIME);
		else
			dev_info(dev, "default timer value is out of range, cannot start\n");
	}

	ret = misc_register(&s3c_wdt_miscdev);
	if (ret) {
		dev_err(dev, "cannot register miscdev on minor=%d (%d)\n",
			WATCHDOG_MINOR, ret);
		goto err_clk;
	}

	if (tmr_atboot && started == 0) {
		dev_info(dev, "starting watchdog timer\n");
		s3c_wdt_start();
	} else if (!tmr_atboot) {
		/* if we're not enabling the watchdog, then ensure it is
		 * disabled if it has been left running from the bootloader
		 * or other source */

		s3c_wdt_stop();
	}

	/* print out a statement of readiness */

	wtcon = readl(wdt_base + S3C_WTCON);

	dev_info(dev, "watchdog %sactive, reset %sabled, irq %sabled\n",
		 (wtcon & S3C_WTCON_ENABLE) ?  "" : "in",
		 (wtcon & S3C_WTCON_RSTEN) ? "" : "dis",
		 (wtcon & S3C_WTCON_INTEN) ? "" : "en");

	return 0;

 err_clk:
	clk_disable(wdt_clock);
	clk_put(wdt_clock);

 err_irq:
	free_irq(wdt_irq->start, pdev);

 err_map:
	iounmap(wdt_base);

 err_req:
	release_resource(wdt_mem);
	kfree(wdt_mem);

	return ret;
}

static int s3c_wdt_remove(struct platform_device *dev)
{
	release_resource(wdt_mem);
	kfree(wdt_mem);
	wdt_mem = NULL;

	free_irq(wdt_irq->start, dev);
	wdt_irq = NULL;

	clk_disable(wdt_clock);
	clk_put(wdt_clock);
	wdt_clock = NULL;

	iounmap(wdt_base);
	misc_deregister(&s3c_wdt_miscdev);
	return 0;
}

static void s3c_wdt_shutdown(struct platform_device *dev)
{
	s3c_wdt_stop();
}

#ifdef CONFIG_PM

static unsigned long wtcon_save;
static unsigned long wtdat_save;

static int s3c_wdt_suspend(struct platform_device *dev, pm_message_t state)
{
	/* Save watchdog state, and turn it off. */

	wtcon_save = readl(wdt_base + S3C_WTCON);
	wtdat_save = readl(wdt_base + S3C_WTDAT);
    printk("Andy in the s3c_wdt suspend function\n");
	/* Note that WTCNT doesn't need to be saved. */
	s3c_wdt_stop();

	return 0;
}

static int s3c_wdt_resume(struct platform_device *dev)
{
	/* Restore watchdog state. */

	writel(wtdat_save, wdt_base + S3C_WTDAT);
	writel(wtdat_save, wdt_base + S3C_WTCNT); /* Reset count */

	 wtcon_save&=~(S3C_WTCON_ENABLE|S3C_WTCON_RSTEN);
	 writel(wtcon_save, wdt_base + S3C_WTCON);
     
	printk(KERN_INFO PFX "watchdog %sabled\n",
	       (wtcon_save & S3C_WTCON_ENABLE) ? "en" : "dis");

	return 0;
}

#else
#define s3c_wdt_suspend NULL
#define s3c_wdt_resume  NULL
#endif /* CONFIG_PM */


static struct platform_driver s3c_wdt_driver = {
	.probe		= s3c_wdt_probe,
	.remove		= s3c_wdt_remove,
	.shutdown	= s3c_wdt_shutdown,
	.suspend	= s3c_wdt_suspend,
	.resume		= s3c_wdt_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "s3c-wdt",
	},
};


static char banner[] __initdata =
	KERN_INFO "S3C Watchdog Timer, (c) 2004 Simtec Electronics\n";

static int __init watchdog_init(void)
{
	printk(banner);
	return platform_driver_register(&s3c_wdt_driver);
}

static void __exit watchdog_exit(void)
{
	platform_driver_unregister(&s3c_wdt_driver);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>, "
	      "Dimitry Andric <dimitry.andric@tomtom.com>");
MODULE_DESCRIPTION("S3C Watchdog Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:s3c-wdt");
