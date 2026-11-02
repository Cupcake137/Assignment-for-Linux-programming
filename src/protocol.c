#include "protocol.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

static const char *skip_space(const char *p)
{
    while (isspace((unsigned char)*p)) ++p;
    return p;
}

static int read_id(const char **cursor, unsigned *id)
{
    const char *p = skip_space(*cursor);
    if (!isdigit((unsigned char)*p)) return -1;
    char *end;
    errno = 0;
    unsigned long value = strtoul(p, &end, 10);
    if (errno || value == 0 || value > UINT16_MAX) return -1;
    if (*end != '\0' && !isspace((unsigned char)*end)) return -1;
    *id = (unsigned)value;
    *cursor = end;
    return 0;
}

int parse_sensor_line(const char *line, sensor_data_t *out)
{
    unsigned id;
    const char *p = line;
    if (read_id(&p, &id) < 0 || *p == '\0') return -1;
    p = skip_space(p);
    char *end;
    errno = 0;
    double value = strtod(p, &end);
    if (errno || end == p || !isfinite(value) ||
        !isspace((unsigned char)*end)) return -1;
    p = skip_space(end);
    if (!isdigit((unsigned char)*p)) return -1;
    errno = 0;
    long long timestamp = strtoll(p, &end, 10);
    if (errno || end == p || timestamp < 0 || timestamp > INT64_MAX ||
        *skip_space(end) != '\0') return -1;
    out->sensor_id = (uint16_t)id;
    out->value = value;
    out->ts = (int64_t)timestamp;
    return 0;
}

int parse_map_line(const char *line, unsigned *room_id, unsigned *sensor_id)
{
    const char *p = line;
    if (read_id(&p, room_id) < 0 || *p == '\0') return -1;
    if (read_id(&p, sensor_id) < 0 || *skip_space(p) != '\0') return -1;
    return 0;
}
