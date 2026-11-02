#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "sbuffer.h"
/* Text record: SENSOR_ID TEMPERATURE UNIX_TIMESTAMP followed by newline. */
int parse_sensor_line(const char *line, sensor_data_t *out);
/* Map record: ROOM_ID SENSOR_ID; IDs must be 1..65535. */
int parse_map_line(const char *line, unsigned *room_id, unsigned *sensor_id);

#endif
