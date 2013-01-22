/* drivers/misc/sweep2wake.c
 *
 * Copyright 2012 XpauM sweep2wake driver
 *
 */

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/earlysuspend.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/sweep2wake.h>
#include <asm/gpio.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#include <linux/delay.h>

#define TIMER_MAX 2000 // 2 sec

static DEFINE_MUTEX(sw_power_mutex);
bool sw_enabled = true; /* is sweep2wake enabled */
bool sw_suspended = false; /* is system suspended */
bool sw_unlockenable = true; /* unlock with sweep2wake left->right */
bool sw_lockenable = true; /* unlock with sweep2wake right->left */
bool count[4] = {false,false,false,false}; /* to count the buttons */
struct input_dev *input_dev= NULL;
void reset_count (void);
void push_pwr (void);

void timer_callback(unsigned long data);
static struct timer_list timer =
		TIMER_INITIALIZER(timer_callback, 0, 0);

void reset_count (){
	int i=0;	
	for(;i<4;i++)
		count[i]=false;
	
}

static void sw_early_suspend(struct early_suspend *h)
{
	sw_suspended = true;
}

static void sw_late_resume(struct early_suspend *h)
{
	sw_suspended = false;
}

static struct early_suspend sw_suspend_data = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = sw_early_suspend,
	.resume = sw_late_resume,
};

//Input dev
void setInputDev (struct input_dev *dev){
	input_dev=dev;
}

//is enabled?
bool sw_is_enabled (){
	return sw_enabled;
}

bool sw_is_enabled_unlock (){
	return sw_unlockenable;
}


/* enable disable */
static ssize_t sw_status_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", (sw_enabled ? 1 : 0));
}

static ssize_t sw_status_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size){
	if(sw_suspended == true){
		pr_info("%s: sweep2wake cant be changed with phone suspended\n", __FUNCTION__);
		return size;
	}
	unsigned int data;
	if(sscanf(buf, "%u\n", &data) == 1) {
		if (data == 1) {
			pr_info("%s: sweep2wake enabled\n", __FUNCTION__);
			sw_enabled = true;
		} else if (data == 0) {
			pr_info("%s: sweep2wake disabled\n", __FUNCTION__);
			sw_enabled = false;
		} else {
			pr_info("%s: invalid input range %u\n", __FUNCTION__,
					data);
		}
	} else {
		pr_info("%s: invalid input\n", __FUNCTION__);
	}

	return size;
}

static ssize_t sw_unlock_status_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%u\n", (sw_unlockenable ? 1 : 0));
}

static ssize_t sw_unlock_status_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size){
	if(sw_suspended == true){
		pr_info("%s: sweep2wake cant be changed with phone suspended\n", __FUNCTION__);
		return size;
	}
	unsigned int data;
	if(sscanf(buf, "%u\n", &data) == 1) {
		if (data == 1) {
			pr_info("%s: sweep unlock enabled\n", __FUNCTION__);
			sw_unlockenable = true;
		} else if (data == 0) {
			pr_info("%s: sweep unlock disabled\n", __FUNCTION__);
			sw_unlockenable = false;
		} else {
			pr_info("%s: invalid input range %u\n", __FUNCTION__,
					data);
		}
	} else {
		pr_info("%s: invalid input\n", __FUNCTION__);
	}

	return size;
}

static ssize_t sw_lock_status_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%u\n", (sw_lockenable ? 1 : 0));
}

static ssize_t sw_lock_status_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size){	
	if(sw_suspended == true){
		pr_info("%s: sweep2wake cant be changed with phone suspended\n", __FUNCTION__);
		return size;
	}
	unsigned int data;
	if(sscanf(buf, "%u\n", &data) == 1) {
		if (data == 1) {
			pr_info("%s: sweep lock enabled\n", __FUNCTION__);
			sw_lockenable = true;
		} else if (data == 0) {
			pr_info("%s: sweep lock disabled\n", __FUNCTION__);
			sw_lockenable = false;
		} else {
			pr_info("%s: invalid input range %u\n", __FUNCTION__,
					data);
		}
	} else {
		pr_info("%s: invalid input\n", __FUNCTION__);
	}

	return size;
}

/* attrs */
static DEVICE_ATTR(enabled, S_IRUGO | S_IWUGO,
		sw_status_read,
		sw_status_write);

static DEVICE_ATTR(lock, S_IRUGO | S_IWUGO,
		sw_lock_status_read,
		sw_lock_status_write);

static DEVICE_ATTR(unlock, S_IRUGO | S_IWUGO,
		sw_unlock_status_read,
		sw_unlock_status_write);

static struct attribute *sw_notification_attributes[] = {
	&dev_attr_enabled.attr,
	&dev_attr_lock.attr,
	&dev_attr_unlock.attr,
	NULL
};

static struct attribute_group sw_notification_group = {
	.attrs  = sw_notification_attributes,
};


// timer
void timer_callback(unsigned long data)
{
	reset_count();
	pr_info("%s: sweep2wake reset\n", __FUNCTION__);
}

//push the power button
void push_pwr (){
	if(input_dev != NULL){
		mutex_lock(&sw_power_mutex);
		input_event(input_dev, EV_KEY, KEY_POWER, 1);
		input_event(input_dev, EV_SYN, 0, 0);
		msleep(100);
		input_event(input_dev, EV_KEY, KEY_POWER, 0);
		input_event(input_dev, EV_SYN, 0, 0);
		msleep(100);
		mutex_unlock(&sw_power_mutex);
	}else{
		pr_info("%s: SWEEP2WAKE: input-dev not initialized\n", __FUNCTION__);
	}
}


void sw_unlock(u16 state){ //left-right 0x100->0x0->0x200->0x0->0x400
	bool success = false;
	
	if( sw_suspended == false || sw_unlockenable == false || sw_enabled == false)
		return;	

	if(count[0] == true || state == 0x400){
		if(count[1] == true || state == 0x0){
			if(count[2] == true || state == 0x200){
				if(count[3] == true || state == 0x0){
					if(state == 0x100){
						push_pwr();
						pr_info("%s: sweep2wake unlock\n", __FUNCTION__);

					}
					if(count[3] != true){
						count[3] = true;
						success = true;
					}
				}
				if(count[2] != true){
					count[2] = true;
					success = true;
				}
			}
			if(count[1] != true){
				count[1] = true;
				success = true;
			}		
		}
		if(count[0] != true){
			timer.expires = jiffies +
				msecs_to_jiffies(TIMER_MAX);
			add_timer(&timer);
			count[0] = true;
			success = true;
		}
	}

	if(!success && count[0] == true){
		reset_count();
		del_timer(&timer);
		pr_info("%s: sweep2wake reset\n", __FUNCTION__);
	}
}


void sw_lock (u16 state){ //right->left 0x400->0x0->0x200->0x0->0x100
	bool success = false;
	
	if( sw_suspended == true || sw_lockenable == false || sw_enabled == false)
		return;	

	if(count[0] == true || state == 0x100){
		if(count[1] == true || state == 0x0){
			if(count[2] == true || state == 0x200){
				if(count[3] == true || state == 0x0){
					if(state == 0x400){
						push_pwr();
						pr_info("%s: sweep2wake lock\n", __FUNCTION__);
						
					}
					if(count[3] != true){
						count[3] = true;
						success = true;
					}
				}
				if(count[2] != true){
					count[2] = true;
					success = true;
				}
			}
			if(count[1] != true){
				count[1] = true;
				success = true;
			}		
		}
		if(count[0] != true){
			timer.expires = jiffies +
				msecs_to_jiffies(TIMER_MAX);
			add_timer(&timer);
			count[0] = true;
			success = true;
		}
	}

	if(!success && count[0] == true){
		reset_count();
		del_timer(&timer);
		pr_info("%s: sweep2wake reset\n", __FUNCTION__);
	}
}

//init
static struct miscdevice sw_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sweep2wake",
};

static int __init sweep2wake_control_init(void)
{
	int ret;

	pr_info("%s misc_register(%s)\n", __FUNCTION__, sw_device.name);
	ret = misc_register(&sw_device);
	if (ret) {
		pr_err("%s misc_register(%s) fail\n", __FUNCTION__,
				sw_device.name);
		return 1;
	}

	/* add the sweep2wake attributes */
	if (sysfs_create_group(&sw_device.this_device->kobj,
				&sw_notification_group) < 0) {
		pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
		pr_err("Failed to create sysfs group for device (%s)!\n",
				sw_device.name);
	}

	register_early_suspend(&sw_suspend_data);

	return 0;
}

device_initcall(sweep2wake_control_init);
