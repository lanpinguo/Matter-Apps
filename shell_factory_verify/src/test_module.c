/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef CONFIG_ARCH_POSIX
#include <unistd.h>
#else
#include <zephyr/posix/unistd.h>
#endif

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <hal/nrf_gpio.h>

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <zephyr/drivers/flash.h>

LOG_MODULE_REGISTER(app_test);

#define SLEEP_TIME_MS 500


const struct gpio_dt_spec mIOConfig[3] = {
    [0] = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
    [1] = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
    [2] = GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios)
};

void io_config_init(void) 
{
	int err;

    int mode = 0;

    for(int i = 0; i < 3; i++){
        if (!device_is_ready(mIOConfig[i].port)) {
            return;
        }

        err = gpio_pin_configure_dt(&mIOConfig[i], GPIO_INPUT);
        if (err < 0) {
            return;
        }        
    }	

    k_sleep(K_MSEC(100));

    mode |= gpio_pin_get_dt(&mIOConfig[1]) > 0 ? 0x1 : 0; 
     
    mode |= gpio_pin_get_dt(&mIOConfig[2]) > 0 ? 0x2 : 0; 

    printk("io-config mode: %u\n", mode);

}

static int io_config_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "io_config_init test:");

    io_config_init();

	return 0;
}

SHELL_SUBCMD_ADD((section_cmd), ioconfig, NULL, "help for ioconfig", io_config_handler, 1, 0);



static const struct gpio_dt_spec lock0 = GPIO_DT_SPEC_GET(DT_ALIAS(lock0), gpios);


void lock_init(void) {
  if (!device_is_ready(lock0.port)) {
    printk("Didn't find lock device referred by the lock0\n");
    return;
  }

  gpio_pin_configure_dt(&lock0, GPIO_OUTPUT);
  gpio_pin_set_dt(&lock0, 1);
}





#define BTN_COUNT 1

struct otc_btn_work_info {
	struct k_work_delayable work;
	uint32_t pins;
} otc_btn_work;

static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0});
static struct gpio_callback button_cb_data;
static const struct gpio_dt_spec btns[BTN_COUNT] = {button0};


static void otc_btn_work_fn(struct k_work *work)
{
	int err;
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct otc_btn_work_info *btn_work = CONTAINER_OF(dwork, struct otc_btn_work_info, work);

	if (btn_work->pins == BIT(button0.pin)) {

		printk("button0 pressed, triggle lock\n");
		// gpio_pin_toggle_dt(&lock0);
		if (gpio_pin_set_dt(&lock0, 1) != 0) {
			printk("lock0 set error\n");
		}

		k_msleep(SLEEP_TIME_MS);

		if (gpio_pin_set_dt(&lock0, 0) != 0) {
			printk("lock0 set error\n");
		}

	} 
}


static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	otc_btn_work.pins = pins;
	k_work_schedule(&otc_btn_work.work, K_MSEC(100));
}

static void configure_button_irq(const struct gpio_dt_spec btn)
{
	int ret;

	if (!device_is_ready(btn.port)) {
		printk("Error: button device %s is not ready\n", btn.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&btn, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n", ret, btn.port->name, btn.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);

	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n", ret,
		       btn.port->name, btn.pin);
		return;
	}

	button_cb_data.pin_mask |= BIT(btn.pin);
	gpio_add_callback(btn.port, &button_cb_data);

	printk("Set up button at %s pin %d\n", btn.port->name, btn.pin);
}

static void configure_buttons(void)
{
	gpio_init_callback(&button_cb_data, button_pressed, 0);

	for (int idx = 0; idx < BTN_COUNT; idx++) {
		configure_button_irq(btns[idx]);
	}
}

void buttons_init(void)
{
	k_work_init_delayable(&otc_btn_work.work, otc_btn_work_fn);

	configure_buttons();

}


const struct device *qspi_flash = DEVICE_DT_GET(DT_NODELABEL(w25q128));
const struct flash_driver_api *qspi_nor_api;
char write_buf[1024] = {0};
char read_buf[1024] = {0};

int qspi_init()
{
	int ret = 0;
	int err = 0;
	printk("qspi_init start\n");

	if (!device_is_ready(qspi_flash)) {
		printk("QSPI FLASH not ready\n");
		return -ENODEV;
	}

	memset(write_buf, 0x55, 1024);
	memset(read_buf, 0, 1024);

	qspi_nor_api = qspi_flash->api;

	ret = qspi_nor_api->erase(qspi_flash, 0, 4*1024);

	ret = qspi_nor_api->write(qspi_flash, 0, write_buf,1024);

	ret = qspi_nor_api->read(qspi_flash, 0, read_buf, 1024);

	for(int i = 0; i < 1024; i++){
		if(write_buf[i] != read_buf[i]){
			err = 1;
			break;
		}
	}

	if(err){
		printk("QSPI FLAH check failed\n");
	}
	else{
		printk("QSPI FLAH check passed\n");
	}


	return ret;

}




static const struct device *GIPO_P0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *GIPO_P1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));


static const struct i2c_dt_spec dev_io_expander[8] = {
	[0] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_0)),
	[1] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_1)),
	[2] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_2)),
	[3] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_3)),
	[4] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_4)),
	[5] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_5)),
	[6] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_6)),
	[7] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_7)),
};


int port0_pins[32] = {9, 10, 31, 29, 2, 20, 17, 15, 12, 8, 6, 22, 24, -1};
int port1_pins[32] = {4, 6, 2, 9, 0, 1, -1};

static int gpio_init(void)
{
	int err;

	printk("gpio_init start\n");

	if (!device_is_ready(GIPO_P0)) {
		printk("GPIO controller not ready\n");
		return -ENODEV;
	}

	for(int i = 0 ; i < 32; i++){
		if(port0_pins[i] < 0){
			break;
		}
        err = gpio_pin_configure(GIPO_P0, port0_pins[i],
                    GPIO_OUTPUT | GPIO_ACTIVE_LOW);
        if (err) {
            printk("GPIO_0 config error: %d\n", err);
            return err;
        }
	}


	for(int i = 0 ; i < 32; i++){
		if(port1_pins[i] < 0){
			break;
		}
        err = gpio_pin_configure(GIPO_P1, port1_pins[i],
                    GPIO_OUTPUT | GPIO_ACTIVE_LOW);
        if (err) {
            printk("GPIO_0 config error: %d\n", err);
            return err;
        }
	}


	return 0;
}

int gpio_test(bool set)
{


	for(int i = 0 ; i < 32 ; i++){

		if(port0_pins[i] < 0){
			break;
		}

		printk("GPIO_0 set pin %d \n", port0_pins[i]);
		if (gpio_pin_set(GIPO_P0,  port0_pins[i], set) != 0) {
			printk("GPIO_0 set error\n");
		}

	}

	for(int i = 0 ; i < 32 ; i++){

		if(port1_pins[i] < 0){
			break;
		}

		printk("GPIO_1 set pin %d \n", port1_pins[i]);
		if (gpio_pin_set(GIPO_P1,  port1_pins[i], set) != 0) {
			printk("GPIO_1 set error\n");
		}


	}

	return 0;
}

int io_expander_init()
{


	return 0;
}

int io_expander_test()
{


	return 0;
}

int relay_set(int slot, int chl, int value)
{
	int ret;
	uint8_t buf[2] = {0};


	if (!device_is_ready(dev_io_expander[slot].bus)) {
		printk("I2C bus %s is not ready!\n\r",dev_io_expander[slot].bus->name);
		return -1;
	}

	/* Keep the i2c pin with right state, recover bus first */
	i2c_recover_bus(dev_io_expander[slot].bus);

	ret = i2c_read_dt(&dev_io_expander[slot], buf, 1);
	if(ret != 0){
		printk("Failed to read from I2C device address %x at Reg. %x \n", dev_io_expander[slot].addr, buf[0]);
		return -1;
	}	

	buf[0] = buf[0] & (~(0x3<<(2*chl)));
	if(value == 0){
		buf[0] |= (0x02 << (2*chl));
	}
	else{
		buf[0] |= (0x01 << (2*chl));
	}
	ret = i2c_write_dt(&dev_io_expander[slot], buf, 1);
	if(ret != 0){
		printk("Failed to write to I2C device address %x at reg. %x \n", dev_io_expander[slot].addr, buf[0]);
		return -1;
	}

	k_sleep(K_MSEC(10));

	buf[0] = buf[0] & 0xFC;
	ret = i2c_write_dt(&dev_io_expander[slot], buf, 1);
	if(ret != 0){
		printk("Failed to write to I2C device address %x at reg. %x \n", dev_io_expander[slot].addr, buf[0]);
		return -1;
	}

	return 0;
}


int test_module_init()
{
	qspi_init();
	buttons_init();
	lock_init();

	return 0;
}



/* Commands below are added using memory section approach which allows to build
 * a set of subcommands from multiple files.
 */
static int relay_handler(const struct shell *sh, size_t argc, char **argv)
{
	int relay_chl = 0;
	int relay_slot = 0;
	int relay_val = 0;
	int c;
	char *ptr = NULL;

	getopt_init();

	while ((c = getopt(argc, argv, "c:s:of")) != -1) {
		switch (c) {

		case 's':
			relay_slot = strtol(optarg, &ptr, 10);
			break;
		case 'c':
			relay_chl = strtol(optarg, &ptr, 10);
			break;
		case 'o':
			relay_val = 1;
			break;
		case 'f':
			relay_val = 0;
			break;
		case 'h':
			/* When getopt is active shell is not parsing
			 * command handler to print help message. It must
			 * be done explicitly.
			 */
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		case '?':
			if (optarg == 'c') {
				shell_print(sh,
					"Option -%c requires an argument.",
					optarg);
			} else if (isprint(optarg) != 0) {
				shell_print(sh,
					"Unknown option `-%c'.",
					optarg);
			} else {
				shell_print(sh,
					"Unknown option character `\\x%x'.",
					optarg);
			}
			return 1;
		default:
			break;
		}
	}

	shell_print(sh, "option `slot - %d, chl - %d, val -%d'\n", relay_slot, relay_chl, relay_val);
	relay_set(relay_slot, relay_chl, relay_val);

	return 0;
}

SHELL_SUBCMD_ADD((section_cmd), relay, NULL, "help for relay", relay_handler, 1, 5);


/* Commands below are added using memory section approach which allows to build
 * a set of subcommands from multiple files.
 */
static int init_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "init executed");

	test_module_init();

	return 0;
}

SHELL_SUBCMD_ADD((section_cmd), init, NULL, "help for init", init_handler, 1, 0);



static int gpio_set_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "gpio set executed");

	gpio_init();

	gpio_test(true);

	return 0;
}

SHELL_SUBCMD_ADD((section_cmd), gpioset, NULL, "help for gpio", gpio_set_handler, 1, 0);

static int gpio_reset_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "gpio reset executed");

	gpio_init();

	gpio_test(false);

	return 0;
}

SHELL_SUBCMD_ADD((section_cmd), gpioreset, NULL, "help for gpio", gpio_reset_handler, 1, 0);



static int i2c_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "i2c test executed");

	io_expander_test();

	return 0;
}

SHELL_SUBCMD_ADD((section_cmd), i2c, NULL, "help for i2c", i2c_handler, 1, 0);



void foo(void)
{
	LOG_INF("info message");
	LOG_WRN("warning message");
	LOG_ERR("err message");
}

static int sub_cmd1_handler(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "sub cmd1 executed");

	return 0;
}

SHELL_SUBCMD_COND_ADD(1, (section_cmd, cmd1), sub_cmd1, NULL, "help for cmd2",
			sub_cmd1_handler, 1, 0);
