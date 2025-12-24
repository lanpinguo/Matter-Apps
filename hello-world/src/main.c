/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <stdio.h>
 #include <zephyr/kernel.h>
 
 int main(void)
 {
	 int i = 0;
	 while (1) {
		 printf("Hello World! %d %s\n", i, CONFIG_BOARD_TARGET);
		 i++;
		 k_sleep(K_MSEC(1000));
	 }
 
	 return 0;
 }
 