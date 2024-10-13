/*
 * TrafficHelper.h
 * Copyright (C) 2018-2021 Linar Yusupov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRAFFICHELPER_H
#define TRAFFICHELPER_H

#include "system/SoC.h"

/* for DISTANCE method: traffic beyond ALARM_ZONE_NONE is invisible */
#define ALARM_ZONE_NONE       15000 /* zone range is 1500m <-> 15000m */
#define ALARM_ZONE_CLOSE      1500  /* zone range is 1000m <->  1500m */
#define ALARM_ZONE_LOW        1000  /* zone range is  700m <->  1000m */
#define ALARM_ZONE_IMPORTANT  700   /* zone range is  400m <->   700m */
#define ALARM_ZONE_URGENT     400   /* zone range is    0m <->   400m */
#define ALARM_ZONE_EXTREME    250

/* for VECTOR method: */
#define ALARM_VECTOR_ANGLE    10
#define ALARM_VECTOR_SPEED   2.0    // was 0.3 (m/s) which may have caused alarms on tow
#define ALARM_TIME_CLOSE      30
#define ALARM_TIME_LOW        19
#define ALARM_TIME_IMPORTANT  13
#define ALARM_TIME_URGENT     9
#define ALARM_TIME_EXTREME    6

#define VERTICAL_SLOPE                5  /* slope effect for alerts */
#define VERTICAL_SLACK               30  /* meters  - allow for GPS alt error */
#define VERTICAL_SEPARATION          80  /* meters adj_alt_diff - after SLACK removed - was 300 */
#define VERTICAL_VISIBILITY_RANGE   900  /* this value higher than FLARM specs */

/* stealth mode visibility range */
#define STEALTH_DISTANCE 2000
#define STEALTH_VERTICAL  300

#define TRAFFIC_VECTOR_UPDATE_INTERVAL 2 /* seconds */
#define TRAFFIC_UPDATE_INTERVAL_MS (TRAFFIC_VECTOR_UPDATE_INTERVAL * 1000)
#define isTimeToUpdateTraffic() (millis() - UpdateTrafficTimeMarker > \
                                  TRAFFIC_UPDATE_INTERVAL_MS)

typedef struct traffic_by_dist_struct {
  ufo_t *fop;
  float distance;
} traffic_by_dist_t;

enum
{
	TRAFFIC_ALARM_NONE,
	TRAFFIC_ALARM_DISTANCE,
	TRAFFIC_ALARM_VECTOR,
	TRAFFIC_ALARM_LEGACY
};

#define TRAFFIC_ALERT_SOUND   1

bool air_relay(ufo_t *fop);
void AddTraffic(ufo_t *fop);
void ParseData(void);
void Traffic_setup(void);
void Traffic_loop(void);
void ClearExpired(void);
void Traffic_Update(ufo_t *fop);
int  Traffic_Count(void);
void logCloseTraffic(void);

int  traffic_cmp_by_distance(const void *, const void *);
float Adj_alt_diff(ufo_t *, ufo_t *);
void generate_random_id(void);

extern ufo_t fo, Container[MAX_TRACKING_OBJECTS], EmptyFO;
extern uint8_t fo_raw[34];
extern traffic_by_dist_t traffic_by_dist[MAX_TRACKING_OBJECTS];
extern int max_alarm_level;
extern bool alarm_ahead;
extern bool relay_waiting;
extern float average_baro_alt_diff;

#if defined(ESP32)
extern File AlarmLog;
extern bool AlarmLogOpen;
#endif

#endif /* TRAFFICHELPER_H */
