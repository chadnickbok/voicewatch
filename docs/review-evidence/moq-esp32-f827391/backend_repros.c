/* Exact adapter functions extracted from f827391; ngtcp2/socket stubs isolate adapter behavior. */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_moq/ngtcp2_transport.h"
#define ESP_MOQ_NGTCP2_TX_STREAMS 16U
#define ESP_MOQ_NGTCP2_TX_BLOCK_BYTES 256U
#define ESP_MOQ_NGTCP2_TX_BLOCKS 64U
#define ESP_MOQ_NGTCP2_PUMP_BUDGET 16U
#define ESP_MOQ_NGTCP2_NO_INDEX UINT16_MAX
#define NGTCP2_ERR_STREAM_DATA_BLOCKED -1
#define NGTCP2_ERR_STREAM_NOT_FOUND -2
#define NGTCP2_ERR_STREAM_SHUT_WR -3
#define NGTCP2_ERR_CALLBACK_FAILURE -4
#define NGTCP2_WRITE_STREAM_FLAG_NONE 0
#define NGTCP2_WRITE_STREAM_FLAG_FIN 1
#define NGTCP2_STREAM_DATA_FLAG_FIN 1

typedef int ngtcp2_conn;
typedef long ngtcp2_ssize;
typedef uint64_t ngtcp2_tstamp;
typedef struct { int dummy; } ngtcp2_path;
typedef struct { ngtcp2_path path; } ngtcp2_path_storage;
typedef struct { int dummy; } ngtcp2_pkt_info;
typedef struct { uint8_t *base; size_t len; } ngtcp2_vec;
typedef struct {
    uint16_t next;
    uint16_t length;
    uint16_t submitted;
    uint64_t offset;
    uint8_t data[ESP_MOQ_NGTCP2_TX_BLOCK_BYTES];
} tx_block_t;

typedef struct {
    bool used;
    bool fin_requested;
    bool fin_submitted;
    int64_t id;
    uint16_t head;
    uint16_t tail;
    uint64_t next_offset;
    uint64_t acked_offset;
} tx_stream_t;


struct esp_moq_ngtcp2 {
    esp_moq_ngtcp2_config_t config;
    esp_moq_ngtcp2_state_t state;
    ngtcp2_conn *connection;
    tx_block_t tx_blocks[64];
    tx_stream_t tx_streams[16];
    uint16_t free_block;
    uint8_t tx_packet[1350];
    size_t pending_packet_length;
    bool connected_callback_pending, deferred_rx_assigned, deferred_rx_fin;
    int64_t deferred_rx_stream_id;
    size_t deferred_rx_length;
    uint8_t deferred_rx[4352];
};
static unsigned attempts; static int64_t attempted_ids[16];
static ngtcp2_tstamp now_timestamp(void) { return 0; }
static esp_moq_result_t flush_pending_packet(esp_moq_ngtcp2_t *a) { a->pending_packet_length=0; return ESP_MOQ_OK; }
static esp_moq_result_t fail_connection(esp_moq_ngtcp2_t *a,int e) {(void)a;(void)e;return ESP_MOQ_ERR_TRANSPORT;}
static void ngtcp2_path_storage_zero(ngtcp2_path_storage *p) {memset(p,0,sizeof(*p));}
static void ngtcp2_conn_update_pkt_tx_time(ngtcp2_conn *c,ngtcp2_tstamp t) {(void)c;(void)t;}
static int ngtcp2_conn_extend_max_stream_offset(ngtcp2_conn *c,int64_t i,size_t n) {(void)c;(void)i;(void)n;return 0;}
static void ngtcp2_conn_extend_max_offset(ngtcp2_conn *c,size_t n) {(void)c;(void)n;}
static ngtcp2_ssize ngtcp2_conn_writev_stream(ngtcp2_conn *c,ngtcp2_path *p,ngtcp2_pkt_info *pi,uint8_t *buf,size_t cap,ngtcp2_ssize *written,uint32_t flags,int64_t id,const ngtcp2_vec *v,size_t nv,ngtcp2_tstamp ts) {
    (void)c;(void)p;(void)pi;(void)buf;(void)cap;(void)flags;(void)v;(void)nv;(void)ts;
    attempted_ids[attempts++]=id; *written=-1;
    if(id==2) return NGTCP2_ERR_STREAM_DATA_BLOCKED;
    *written=1; return 1;
}
static tx_stream_t *find_tx_stream(esp_moq_ngtcp2_t *adapter, int64_t id)
{
    size_t i;

    for (i = 0; i < ESP_MOQ_NGTCP2_TX_STREAMS; ++i) {
        if (adapter->tx_streams[i].used && adapter->tx_streams[i].id == id) {
            return &adapter->tx_streams[i];
        }
    }
    return NULL;
}

static tx_stream_t *next_sendable_stream(esp_moq_ngtcp2_t *adapter)
{
    size_t i;

    for (i = 0; i < ESP_MOQ_NGTCP2_TX_STREAMS; ++i) {
        tx_stream_t *stream = &adapter->tx_streams[i];
        uint16_t index;

        if (!stream->used) {
            continue;
        }
        for (index = stream->head; index != ESP_MOQ_NGTCP2_NO_INDEX;
             index = adapter->tx_blocks[index].next) {
            tx_block_t *block = &adapter->tx_blocks[index];
            if (block->submitted < block->length) {
                return stream;
            }
        }
        if (stream->fin_requested && !stream->fin_submitted) {
            return stream;
        }
    }
    return NULL;
}

static tx_block_t *next_unsent_block(
    esp_moq_ngtcp2_t *adapter,
    tx_stream_t *stream)
{
    uint16_t index;

    for (index = stream->head; index != ESP_MOQ_NGTCP2_NO_INDEX;
         index = adapter->tx_blocks[index].next) {
        tx_block_t *block = &adapter->tx_blocks[index];
        if (block->submitted < block->length) {
            return block;
        }
    }
    return NULL;
}

static esp_moq_result_t pump_output(esp_moq_ngtcp2_t *adapter)
{
    unsigned int packet_count;
    esp_moq_result_t flush_result;

    flush_result = flush_pending_packet(adapter);
    if (flush_result == ESP_MOQ_ERR_WOULD_BLOCK) {
        return ESP_MOQ_OK;
    }
    if (flush_result != ESP_MOQ_OK) {
        return flush_result;
    }
    for (packet_count = 0; packet_count < ESP_MOQ_NGTCP2_PUMP_BUDGET;
         ++packet_count) {
        ngtcp2_path_storage packet_path;
        ngtcp2_pkt_info packet_info = {0};
        ngtcp2_ssize data_written = -1;
        ngtcp2_ssize packet_length;
        tx_stream_t *stream = next_sendable_stream(adapter);
        tx_block_t *block = stream == NULL ? NULL : next_unsent_block(adapter, stream);
        ngtcp2_vec vector;
        const ngtcp2_vec *vectors = NULL;
        size_t vector_count = 0;
        int64_t stream_id = -1;
        uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_NONE;
        ngtcp2_tstamp timestamp = now_timestamp();

        if (stream != NULL) {
            stream_id = stream->id;
            if (block != NULL) {
                vector.base = block->data + block->submitted;
                vector.len = block->length - block->submitted;
                vectors = &vector;
                vector_count = 1;
                if (stream->fin_requested && block->next == ESP_MOQ_NGTCP2_NO_INDEX) {
                    flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
                }
            } else {
                vector.base = NULL;
                vector.len = 0;
                vectors = &vector;
                vector_count = 1;
                flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
            }
        }

        ngtcp2_path_storage_zero(&packet_path);
        packet_length = ngtcp2_conn_writev_stream(
            adapter->connection,
            &packet_path.path,
            &packet_info,
            adapter->tx_packet,
            sizeof(adapter->tx_packet),
            &data_written,
            flags,
            stream_id,
            vectors,
            vector_count,
            timestamp);
        if (packet_length < 0) {
            if (packet_length == NGTCP2_ERR_STREAM_DATA_BLOCKED ||
                packet_length == NGTCP2_ERR_STREAM_NOT_FOUND ||
                packet_length == NGTCP2_ERR_STREAM_SHUT_WR) {
                return ESP_MOQ_OK;
            }
            return fail_connection(adapter, (int)packet_length);
        }
        ngtcp2_conn_update_pkt_tx_time(adapter->connection, timestamp);
        if (data_written >= 0 && stream != NULL) {
            if (block != NULL) {
                block->submitted += (uint16_t)data_written;
                if ((flags & NGTCP2_WRITE_STREAM_FLAG_FIN) != 0 &&
                    block->submitted == block->length) {
                    stream->fin_submitted = true;
                }
            } else {
                stream->fin_submitted = true;
            }
        }
        if (packet_length == 0) {
            return ESP_MOQ_OK;
        }
        adapter->pending_packet_length = (size_t)packet_length;
        flush_result = flush_pending_packet(adapter);
        if (flush_result == ESP_MOQ_ERR_WOULD_BLOCK) {
            return ESP_MOQ_OK;
        }
        if (flush_result != ESP_MOQ_OK) {
            return flush_result;
        }
    }
    return ESP_MOQ_OK;
}

static esp_moq_result_t transport_write(
    void *context,
    esp_moq_stream_id_t stream_id,
    const uint8_t *data,
    size_t length,
    size_t *accepted)
{
    esp_moq_ngtcp2_t *adapter = context;
    tx_stream_t *stream;
    tx_block_t *block;
    uint16_t index;
    size_t copy_length;

    if (adapter == NULL || accepted == NULL || (data == NULL && length != 0)) {
        return ESP_MOQ_ERR_INVALID_ARGUMENT;
    }
    *accepted = 0;
    stream = find_tx_stream(adapter, (int64_t)stream_id);
    if (stream == NULL || stream->fin_requested) {
        return ESP_MOQ_ERR_INVALID_STATE;
    }
    if (length == 0) {
        return ESP_MOQ_OK;
    }
    if (adapter->free_block == ESP_MOQ_NGTCP2_NO_INDEX) {
        return ESP_MOQ_ERR_WOULD_BLOCK;
    }
    index = adapter->free_block;
    block = &adapter->tx_blocks[index];
    adapter->free_block = block->next;
    copy_length = length < sizeof(block->data) ? length : sizeof(block->data);
    block->next = ESP_MOQ_NGTCP2_NO_INDEX;
    block->length = (uint16_t)copy_length;
    block->submitted = 0;
    block->offset = stream->next_offset;
    memcpy(block->data, data, copy_length);
    stream->next_offset += copy_length;
    if (stream->tail == ESP_MOQ_NGTCP2_NO_INDEX) {
        stream->head = index;
    } else {
        adapter->tx_blocks[stream->tail].next = index;
    }
    stream->tail = index;
    *accepted = copy_length;
    return pump_output(adapter);
}

static int receive_stream_data_cb(
    ngtcp2_conn *connection,
    uint32_t flags,
    int64_t stream_id,
    uint64_t offset,
    const uint8_t *data,
    size_t length,
    void *user_data,
    void *stream_user_data)
{
    esp_moq_ngtcp2_t *adapter = user_data;
    esp_moq_result_t result = ESP_MOQ_OK;

    (void)offset;
    (void)stream_user_data;
    if (adapter->connected_callback_pending) {
        if ((adapter->deferred_rx_assigned &&
             adapter->deferred_rx_stream_id != stream_id) ||
            length > sizeof(adapter->deferred_rx) - adapter->deferred_rx_length) {
            return NGTCP2_ERR_CALLBACK_FAILURE;
        }
        adapter->deferred_rx_assigned = true;
        adapter->deferred_rx_stream_id = stream_id;
        if (length != 0) {
            memcpy(
                adapter->deferred_rx + adapter->deferred_rx_length,
                data,
                length);
            adapter->deferred_rx_length += length;
        }
        adapter->deferred_rx_fin =
            adapter->deferred_rx_fin ||
            (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0;
        if (length != 0) {
            if (ngtcp2_conn_extend_max_stream_offset(connection, stream_id, length) != 0) {
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            ngtcp2_conn_extend_max_offset(connection, length);
        }
        return 0;
    }
    if (adapter->config.callbacks.stream_data != NULL) {
        result = adapter->config.callbacks.stream_data(
            adapter->config.user_data,
            (esp_moq_stream_id_t)stream_id,
            data,
            length,
            (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0);
    }
    if (result != ESP_MOQ_OK) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    if (length != 0) {
        if (ngtcp2_conn_extend_max_stream_offset(connection, stream_id, length) != 0) {
            return NGTCP2_ERR_CALLBACK_FAILURE;
        }
        ngtcp2_conn_extend_max_offset(connection, length);
    }
    return 0;
}
int main(void) {
    esp_moq_ngtcp2_t a={0}; uint8_t data=42; size_t accepted=999;
    a.state=ESP_MOQ_NGTCP2_CONNECTED;
    assert(receive_stream_data_cb(NULL,0,1,0,&data,1,&a,NULL)==0);
    esp_moq_result_t r=transport_write(&a,1,&data,1,&accepted);
    assert(r==ESP_MOQ_ERR_INVALID_STATE && accepted==0);
    puts("REPRODUCED: receive peer bidirectional stream 1, then respond -> INVALID_STATE, accepted=0");
    memset(&a,0,sizeof(a));
    for(unsigned i=0;i<2;++i) {
        a.tx_streams[i].used=true;a.tx_streams[i].id=2+4*i;
        a.tx_streams[i].head=(uint16_t)i;a.tx_streams[i].tail=(uint16_t)i;
        a.tx_blocks[i].length=1;a.tx_blocks[i].next=UINT16_MAX;
    }
    for(unsigned i=0;i<4;++i) assert(pump_output(&a)==ESP_MOQ_OK);
    assert(attempts==4);
    for(unsigned i=0;i<attempts;++i) assert(attempted_ids[i]==2);
    puts("REPRODUCED: 4 pump ticks retry blocked stream 2; writable stream 6 attempted zero times");
    return 0;
}
