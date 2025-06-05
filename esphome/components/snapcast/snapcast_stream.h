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

enum class StreamState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED_IDLE,      // Connected but waiting
  STREAMING,           // Receiving data
  ERROR,               // Fatal or recoverable error
  STOPPING             // Requested shutdown
};


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
    esp_err_t disconnect();
    esp_err_t start_with_notify(std::shared_ptr<esphome::TimedRingBuffer> ring_buffer, TaskHandle_t notification_task);
    esp_err_t stop_streaming();
    esp_err_t report_volume(uint8_t volume, bool muted);
    
    bool is_connected(){ return this->state_ == StreamState::STREAMING || this->state_ == StreamState::CONNECTED_IDLE; }
    bool is_running(){ return this->state_ == StreamState::STREAMING; }
    void set_on_status_update(std::function<void(StreamState state, uint8_t volume, bool muted)> cb) {
        this->on_status_update_ = std::move(cb);
    }
    
protected:
    void send_message_(SnapcastMessage &msg);
    void send_hello_();
    void send_time_sync_();
    
    esp_err_t read_next_data_chunk_(uint32_t timeout_ms);
    void on_server_settings_msg_(const ServerSettingsMessage &msg);
    void on_time_msg_(MessageHeader msg, tv_t time);
    tv_t to_local_time_(tv_t server_time) {
        return server_time - this->est_time_diff_ + tv_t::from_millis(this->server_buffer_size_);
    }

    void set_state_(StreamState new_state);
    
    std::string server_;
    uint32_t port_;
    StreamState state_{StreamState::DISCONNECTED};
    TaskHandle_t stream_task_handle_{nullptr};
    TaskHandle_t notification_target_{nullptr};
    
    StaticTask_t task_stack_;
    StackType_t *task_stack_buffer_{nullptr};

    std::shared_ptr<esphome::TimedRingBuffer> write_ring_buffer_;
    
    uint32_t last_time_sync_{0};
    esp_transport_handle_t transport_{nullptr};
    tv_t est_time_diff_{0, 0};
    TimeStats time_stats_;
    uint32_t server_buffer_size_{0};
    bool codec_header_sent_{false};
    bool is_running_{false};
    uint8_t volume_{0};
    bool muted_{false};
    
    std::function<void(StreamState state, uint8_t volume, bool muted)> on_status_update_;
private:
    void stream_task_();

    void connect_();
    void disconnect_();
    void start_streaming_();
    void stop_streaming_();
    void send_report_();
};




}
}