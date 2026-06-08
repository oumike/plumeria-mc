#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal/tlora_pager_board.h"

namespace plumeria {
namespace mesh {

enum class MeshEventType : uint8_t {
  Info,
  ChannelMessage,
};

struct MeshEvent {
  MeshEventType type;
  char channel_name[32];
  char text[96];
};

struct MeshRadioStats {
  uint32_t rx_raw_count;
  uint32_t rx_packet_count;
  uint32_t last_rx_ms;
};

struct MeshRuntime;

class MeshAdapter {
 public:
  bool begin(const hal::TloraPagerRadioConfig& radio_config);
  void loop();

  bool sendDirectMessage(const char* destination, const char* text);
  bool sendChannelMessage(const char* channel_name, const char* text);
  int exportChannels(char names[][32], int max_names);
  bool addChannel(const char* channel_name, const char* psk_base64 = nullptr);
  bool removeChannel(const char* channel_name);
  void getRadioStats(MeshRadioStats* out_stats) const;

  size_t drainEvents(MeshEvent* out_events, size_t max_events);

 private:
  friend class StandaloneChatMesh;

  void queueInfo(const char* text);
  void queueChannelMessage(const char* channel_name, const char* text);
  void noteRxRaw();
  void noteRxPacket();
  void markContactsDirty();
  void markChannelsDirty();
  bool loadContactsFromFs();
  bool saveContactsToFs();
  bool loadChannelsFromFs();
  bool saveChannelsToFs();

  static constexpr size_t kMaxQueuedEvents = 8;
  MeshEvent event_queue_[kMaxQueuedEvents]{};
  size_t event_head_ = 0;
  size_t event_tail_ = 0;
  size_t event_count_ = 0;

  MeshRuntime* runtime_ = nullptr;
  uint32_t last_advert_ms_ = 0;
  uint32_t last_persist_flush_ms_ = 0;
  uint32_t rx_raw_count_ = 0;
  uint32_t rx_packet_count_ = 0;
  uint32_t last_rx_ms_ = 0;
  bool contacts_dirty_ = false;
  bool channels_dirty_ = false;
  bool ready_ = false;
};

}  // namespace mesh
}  // namespace plumeria
