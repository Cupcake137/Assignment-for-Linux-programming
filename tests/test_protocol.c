#include "protocol.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    sensor_data_t data;
    assert(parse_sensor_line("20 21.5 1000\r", &data) == 0);
    assert(data.sensor_id == 20 && data.value == 21.5 && data.ts == 1000);
    const char *bad[] = {
        "", "20 21", "-1 21 1", "65536 21 1", "0 21 1", "20 nan 1",
        "20 inf 1", "20 21 -1", "20 21 1 extra", "20x 21 1", "20 21x 1",
        "20 21 9999999999999999999999999", "20 1e999 1"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(parse_sensor_line(bad[i], &data) == -1);
    }
    unsigned room, sensor;
    assert(parse_map_line("602 20\n", &room, &sensor) == 0);
    assert(room == 602 && sensor == 20);
    assert(parse_map_line("-602 20", &room, &sensor) == -1);
    assert(parse_map_line("602 20 garbage", &room, &sensor) == -1);
    puts("PASS protocol: valid lines, bounds, malformed and non-finite input");
    return 0;
}
