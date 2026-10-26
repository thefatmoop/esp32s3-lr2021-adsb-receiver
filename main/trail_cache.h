#pragma once
#include "adsb.h"
#define TRAIL_POINTS 24
#define TRAIL_AIRCRAFT 48
typedef struct {int32_t lat,lon,alt;uint32_t at;} trail_point;
typedef struct {uint32_t icao;uint8_t count,head;bool nonicao;trail_point points[TRAIL_POINTS];} trail_record;
void trail_cache_record(const aircraft *a);
void trail_cache_snapshot(uint32_t icao,bool nonicao,trail_record *out);
