#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "c-runtime.h"


typedef enum {
    RMT_RESULT_OK = 0,
    RMT_RESULT_FAIL = 1,
    RMT_RESULT_TIMEOUT = 2,
    RMT_RESULT_INVALID_ARG = 3,
    RMT_RESULT_NO_MEMORY = 4,
    RMT_RESULT_NOT_FOUND = 5,
    RMT_RESULT_NOT_SUPPORTED = 5,
    RMT_RESULT_INVALID_STATE = 6,
} RMT_RESULT;

RMT_RESULT esp_err_to_rmt_result(esp_err_t err) {
    switch (err) {
        case ESP_OK:
            return RMT_RESULT_OK;
        case ESP_ERR_TIMEOUT:
            return RMT_RESULT_TIMEOUT; 
        case ESP_ERR_INVALID_ARG:
            return RMT_RESULT_INVALID_ARG;
        case ESP_ERR_NO_MEM:
            return RMT_RESULT_NO_MEMORY;
        case ESP_ERR_NOT_FOUND:
            return RMT_RESULT_NOT_FOUND;
        case ESP_ERR_NOT_SUPPORTED:
            return RMT_RESULT_NOT_SUPPORTED;
        case ESP_ERR_INVALID_STATE:
            return RMT_RESULT_INVALID_STATE;
        default:
            return RMT_RESULT_FAIL;
    }
}

typedef struct {
    rmt_encoder_t base;
    value_t bs_encode;
    value_t bs_is_done;
    value_t bs_reset;
    value_t bs_deinit;
} custom_encoder_t;

size_t custome_encoder_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state) {
    custom_encoder_t *custom_encoder = __containerof(encoder, custom_encoder_t, base);
    int32_t encoded_symbols = ((int32_t (*)(value_t, value_t))gc_function_object_ptr(custom_encoder->bs_encode, 0))(custom_encoder->bs_encode, (value_t)primary_data);
    int32_t is_done = ((int32_t (*)(value_t))gc_function_object_ptr(custom_encoder->bs_is_done, 0))(custom_encoder->bs_is_done);
    *ret_state = is_done == 1 ? RMT_ENCODING_COMPLETE : RMT_ENCODING_RESET;
    return (size_t) encoded_symbols;
}

esp_err_t custome_encoder_reset(rmt_encoder_t *encoder) {
    custom_encoder_t *custom_encoder = __containerof(encoder, custom_encoder_t, base);
    ((void (*)(value_t))gc_function_object_ptr(custom_encoder->bs_reset, 0))(custom_encoder->bs_reset);
    return ESP_OK;
}

esp_err_t custome_encoder_del(rmt_encoder_t *encoder) {
    custom_encoder_t *custom_encoder = __containerof(encoder, custom_encoder_t, base);
    ((void (*)(value_t))gc_function_object_ptr(custom_encoder->bs_deinit, 0))(custom_encoder->bs_deinit);
    free(custom_encoder);
    return ESP_OK;
}

RMT_RESULT custome_encoder_new(value_t bs_encode, value_t bs_is_done, value_t bs_reset, value_t bs_deinit, int32_t *ret_encoder) {
    custom_encoder_t *encoder = rmt_alloc_encoder_mem(sizeof(custom_encoder_t));
    if (encoder == NULL) {
        return RMT_RESULT_NO_MEMORY;
    }
    encoder->base.encode = custome_encoder_encode;
    encoder->base.reset = custome_encoder_reset;
    encoder->base.del = custome_encoder_del;
    encoder->bs_encode = bs_encode;
    encoder->bs_is_done = bs_is_done;
    encoder->bs_reset = bs_reset;
    encoder->bs_deinit = bs_deinit;
    *ret_encoder = (int32_t)&encoder->base;
    return RMT_RESULT_OK;
}

RMT_RESULT custome_encoder_close(int32_t encoder_handle) {
    esp_err_t err = rmt_del_encoder((rmt_encoder_handle_t)encoder_handle);
    return esp_err_to_rmt_result(err);
}

RMT_RESULT copy_encoder_new(int32_t *ret_handle) {
    rmt_copy_encoder_config_t config = {};
    rmt_encoder_handle_t handle = NULL;
    esp_err_t err = rmt_new_copy_encoder(&config, &handle);
    *ret_handle = (int32_t)handle;
    return esp_err_to_rmt_result(err);
}

RMT_RESULT bytes_encoder_new(rmt_symbol_word_t bit0, rmt_symbol_word_t bit1, int32_t msb_first, int32_t *ret_handle) {
    rmt_bytes_encoder_config_t config = {
        .bit0 = bit0,
        .bit1 = bit1,
        .flags.msb_first = msb_first
    };
    rmt_encoder_handle_t handle = NULL;
    esp_err_t err = rmt_new_bytes_encoder(&config, &handle);
    *ret_handle = (int32_t)handle;
    return esp_err_to_rmt_result(err);
}

int32_t builtin_encoder_encode(int32_t builtin_encoder_handle, int32_t channel_handle, void *data, int32_t data_size, int32_t *ret_state) {
    rmt_encoder_handle_t encoder_handle = (rmt_encoder_handle_t)builtin_encoder_handle;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    size_t encoded_symbols = encoder_handle->encode(encoder_handle, (rmt_channel_handle_t)channel_handle, data, data_size,  &session_state);
    if (session_state & RMT_ENCODING_COMPLETE) {
        *ret_state = 1; // done
    } else {
        *ret_state = 0; // not done
    }
    return (int32_t) encoded_symbols;
}

RMT_RESULT builtin_encoder_reset(int32_t builtin_encoder_handle) {
    esp_err_t err = rmt_encoder_reset((rmt_encoder_handle_t)builtin_encoder_handle);
    return esp_err_to_rmt_result(err);
}

RMT_RESULT builtin_encoder_deinit(int32_t builtin_encoder_handle) {
    esp_err_t err = rmt_del_encoder((rmt_encoder_handle_t)builtin_encoder_handle);
    return esp_err_to_rmt_result(err);
}

RMT_RESULT transmit_async(int32_t channel_handle, int32_t encoder_handle, int32_t loop_count, value_t data, int32_t data_size) {
    rmt_transmit_config_t tx_config = {
        .loop_count = loop_count,
    };
    esp_err_t err = rmt_transmit(
        (rmt_channel_handle_t)channel_handle, 
        (rmt_encoder_handle_t)encoder_handle, 
        (void *)data, data_size,
        &tx_config 
    );
    return esp_err_to_rmt_result(err);
}

RMT_RESULT wait_transmit_completed(int32_t channel_handle, int32_t timeout_ms) {
    esp_err_t err = rmt_tx_wait_all_done((rmt_channel_handle_t)channel_handle, timeout_ms);
    return esp_err_to_rmt_result(err);
}

RMT_RESULT tx_channel_new(int32_t pin, int32_t resolution_hz, int32_t *ret_handle) {
    rmt_channel_handle_t channel_handle = NULL;
    rmt_tx_channel_config_t chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = pin,
        .mem_block_symbols = 64,
        .resolution_hz = resolution_hz,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };
    esp_err_t err = rmt_new_tx_channel(&chan_config, &channel_handle);
    *ret_handle = (int32_t)channel_handle;
    return esp_err_to_rmt_result(err);
}

RMT_RESULT tx_channel_delete(int32_t channel_handle) {
    esp_err_t err = rmt_del_channel((rmt_channel_handle_t)channel_handle);
    return esp_err_to_rmt_result(err);
}

RMT_RESULT tx_channel_enable(int32_t channel_handle) {
    esp_err_t err = rmt_enable((rmt_channel_handle_t)channel_handle);
    return esp_err_to_rmt_result(err);
}

RMT_RESULT tx_channel_disable(int32_t channel_handle) {
    esp_err_t err = rmt_disable((rmt_channel_handle_t)channel_handle);
    return esp_err_to_rmt_result(err);
}

void* get_native_ptr(value_t obj) {
    return &(value_to_ptr(obj))->body[2];
}