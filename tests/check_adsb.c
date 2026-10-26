#include "../main/adsb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
int main(void){assert(adsb_selftest());puts("PASS CRC, every single-bit corruption, pulse pairs, callsign, altitude, velocity and global CPR");return 0;}
