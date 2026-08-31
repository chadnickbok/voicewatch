#define main original_test_main
#include "tests/host/test_wire.c"
#undef main
int main(void) {
    esp_moq_client_t client;
    mock_transport_t mock = {0};
    esp_moq_transport_t transport = { .vtable = &mock_transport_vtable, .context = &mock };
    esp_moq_lite05_setup_t setup = {0};
    mock.maximum_write = sizeof(mock.bytes);
    mock.block_open_once = true;
    CHECK_OK(esp_moq_client_init(&client, &transport, &setup));
    CHECK_OK(esp_moq_client_start(&client));
    CHECK(esp_moq_client_transport_connected(&client) == ESP_MOQ_ERR_WOULD_BLOCK);
    CHECK(client.tx.active);
    CHECK(esp_moq_client_start(&client) == ESP_MOQ_ERR_INVALID_STATE);
    CHECK(!client.tx.active);
    CHECK_OK(esp_moq_client_flush(&client));
    CHECK(!client.session.local_setup_sent && !mock.finished);
    printf("REPRODUCED: rejected duplicate client_start discards queued SETUP, session remains NEGOTIATING\n");
    esp_moq_lite05_subscribe_request_t req = {0}, decoded;
    uint8_t wire[128]; size_t written, consumed;
    req.broadcast = bytes_slice("watch.hang"); req.track = bytes_slice("0.opus");
    req.has_start_group = true; req.start_group = UINT64_MAX;
    CHECK_OK(esp_moq_lite05_subscribe_request_encode(&req, wire, sizeof(wire), &written));
    CHECK_OK(esp_moq_lite05_subscribe_request_decode(wire, written, &decoded, &consumed));
    CHECK(!decoded.has_start_group);
    printf("REPRODUCED: has_start_group=true, start_group=UINT64_MAX encodes successfully as absent\n");
    printf("sizeof client=%zu track_rx=%zu subscribe_rx=%zu group_rx=%zu session=%zu\n", sizeof(client), sizeof(esp_moq_lite05_track_rx_t), sizeof(esp_moq_lite05_subscribe_rx_t), sizeof(esp_moq_lite05_group_rx_t), sizeof(esp_moq_lite05_session_t));
    return 0;
}
