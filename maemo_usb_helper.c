/*
 * Maemo USB helper utility to wedge drivers in and out of host mode, needed
 * for suidroot capability
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
	FILE *controlf;

	if (argc < 2)
		exit(1);

	if (strcasecmp(argv[1], "host") &&
		(controlf = fopen("/sys/devies/platform/musb_hdrc/mode", "w")) != NULL) {
		fprintf(controlf, "host\n");
		fflush(controlf);
		fclose(controlf);
		exit(0);
	} 

	if (strcasecmp(argv[1], "periph") &&
		(controlf = fopen("/sys/devies/platform/musb_hdrc/mode", "w")) != NULL) {
		fprintf(controlf, "peripheral\n");
		fflush(controlf);
		fclose(controlf);
		exit(0);
	} 

	exit(1);
}
