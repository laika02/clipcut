#include "playback/transport.h"

#include <stdio.h>

int main(void) {
    TransportState transport;
    transport_init(&transport);

    transport_set_trim(&transport, 1.0, 3.5);
    transport_seek(&transport, 2.0);
    transport_play(&transport, 10.0);
    (void)transport_tick(&transport, 10.75);
    transport_pause(&transport);
    transport_seek(&transport, 3.4);
    transport_play(&transport, 20.0);
    (void)transport_tick(&transport, 20.5);

    printf("state=%d playhead=%0.3f trim=%0.3f-%0.3f\n",
        (int)transport.playback_state,
        transport.playhead_sec,
        transport.trim_start_sec,
        transport.trim_end_sec);

    transport_stop(&transport);
    printf("stopped=%d playhead=%0.3f\n",
        (int)transport.playback_state,
        transport.playhead_sec);

    return 0;
}
