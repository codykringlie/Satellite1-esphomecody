#pragma once

#include <string>
#include "esp_transport.h"

#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/component.h"

#include "esphome/components/audio/timed_ring_buffer.h"

#include "messages.h"



namespace esphome {
namespace snapcast {

#define MAX_TIMES 100

class TimeStats {
public:
    void add(tv_t val) {
        times[next_insert] = val;
        next_insert = (next_insert + 1) % MAX_TIMES;
        if (count < MAX_TIMES)
            ++count;
    }

    tv_t get_median() const {
        std::array<tv_t, MAX_TIMES> sorted{};
        std::copy(times.begin(), times.begin() + count, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + count);
        return sorted[count / 2];
    }

private:
    std::array<tv_t, MAX_TIMES> times{};
    size_t count = 0;
    size_t next_insert = 0;
};

class SnapcastStream {
public:
    esp_err_t connect(std::string server, uint32_t port);
    esp_err_t init_streaming();
    esp_err_t disconnect();
    bool receive_next_message();
    esp_err_t read_next_data_chunk(uint8_t *data, size_t &size, uint32_t timeout_ms);
    esp_err_t read_next_data_chunk(std::shared_ptr<esphome::TimedRingBuffer> ring_buffer, uint32_t timeout_ms);
    bool is_connected() { return this->transport_ != nullptr; }
    bool is_running() { return this->is_running_; }
    void set_idle() { this->is_running_ = false; }
    void trigger_time_sync() {
        if (millis() - this->last_time_sync_ > 1000) { // every 1 seconds
            this->send_time_sync_();
        }
    }

protected:
    void send_message_(SnapcastMessage &msg);
    void send_queueed_messages_();
    void send_hello_();
    void send_time_sync_();
    tv_t to_local_time(tv_t server_time) {
        return server_time - this->est_time_diff_ + tv_t::from_millis(this->server_buffer_size_);
    }

    void on_server_settings_msg(const ServerSettingsMessage &msg);
    void on_time_msg(MessageHeader msg, tv_t time);

    uint32_t last_time_sync_{0};
    esp_transport_handle_t transport_{NULL};
    tv_t est_time_diff_{0, 0};
    TimeStats time_stats_;
    uint32_t server_buffer_size_{0};
    bool codec_header_sent_{false};
    bool is_running_{false};
};




}
}