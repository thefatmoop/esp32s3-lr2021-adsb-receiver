#include "wifi_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 uint64_t start=123456789000ULL,deadline=start+600000;
 assert(wifi_seconds_left(start,deadline)==600);
 assert(wifi_seconds_left(deadline-1,deadline)==1);
 assert(wifi_seconds_left(deadline,deadline)==0);
 assert(wifi_seconds_left(deadline+10000,deadline)==0);
 assert(!wifi_setup_expired(true,false,deadline-1,deadline));
 assert(wifi_setup_expired(true,false,deadline,deadline));
 assert(!wifi_setup_expired(true,true,deadline+1,deadline));
 assert(!wifi_setup_expired(false,false,deadline+1,deadline));
 assert(!wifi_setup_expired(true,false,deadline+1,0));
 puts("PASS 10-minute deadline, round-up countdown, long uptime, connected/AP guards");
}
