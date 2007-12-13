/*
 * Maemo USB helper utility to wedge drivers in and out of host mode, needed
 * for suidroot capability
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]) {
	FILE *controlf;

	if (argc < 2) {
		fprintf(stderr, "Failed to parse mode request\n");
		exit(1);
	}

	if (strcasecmp(argv[1], "host") == 0) {
		if ((controlf = fopen("/sys/devices/platform/musb_hdrc/mode", "w")) != NULL) {
			fprintf(controlf, "peripheral\n");
			fflush(controlf);
			usleep(10000);
			fprintf(controlf, "host\n");
			fflush(controlf);
			fclose(controlf);
			exit(0);
		} else {
			fprintf(stderr, "Failed to open musb_hdrc/mode: %s\n", strerror(errno));
			exit(0);
		}
	} 

	if (strcasecmp(argv[1], "periph") == 0) {
		if ((controlf = fopen("/sys/devices/platform/musb_hdrc/mode", "w")) != NULL) {
			fprintf(controlf, "peripheral\n");
			fflush(controlf);
			usleep(10000);
			fprintf(controlf, "otg\n");
			fflush(controlf);
			fclose(controlf);
			exit(0);
		} else {
			fprintf(stderr, "Failed to open musb_hdrc/mode: %s\n", strerror(errno));
			exit(0);
		}
	} 

	fprintf(stderr, "Failed to parse mode request\n");

	exit(1);
}
