#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal/device_board.h"

namespace plumeria {
namespace mesh {

enum class MeshEventType : uint8_t {
  Info,
  ChannelMessage,
  DirectMessage,
};

struct MeshEvent {
  MeshEventType type;
  char channel_name[32];
  char peer_key[65];
  char text[96];
};

struct MeshRadioStats {
  uint32_t rx_raw_count;
  uint32_t rx_packet_count;
  uint32_t last_rx_ms;
};

struct MeshChannelConfig {
  char name[32];
  char psk_base64[49];
};

struct MeshContactSummary {
  char name[32];
  char public_key_hex[65];
  bool favorite;
  uint8_t type;
  uint32_t lastmod;
  int32_t gps_lat_i;
  int32_t gps_lon_i;
  uint8_t telemetry_adv_type;
  uint16_t telemetry_feat1;
  uint16_t telemetry_feat2;
};

struct MeshRuntime;

class MeshAdapter {
 public:
  bool begin(const hal::RadioConfig& radio_config);
  void loop();

  bool sendDirectMessage(const char* destination, const char* text);
  bool sendChannelMessage(const char* channel_name, const char* text);
  bool setNodeName(const char* node_name);
  void getNodeName(char* out_name, size_t out_size) const;
  bool getPublicKeyHex(char* out_hex, size_t out_size) const;
  bool getIdentityKeysHex(char* out_public_hex, size_t public_hex_size,
                          char* out_private_hex, size_t private_hex_size) const;
  bool importIdentityKeysHex(const char* public_hex, const char* private_hex);
  bool identityLoadedFromNvs() const;
  bool setGpsEnabled(bool enabled);
  int getGpsSatelliteCount() const;
  bool setAdvertLocation(bool enabled, double latitude, double longitude);
  void getAdvertLocation(bool* enabled, double* latitude, double* longitude) const;
  bool setAutoAdvertIntervalMinutes(uint16_t minutes);
  uint16_t getAutoAdvertIntervalMinutes() const;
  bool broadcastSelfAdvertNow();
  bool broadcastSelfAdvertFloodNow();
  int exportChannels(char names[][32], int max_names);
  int exportChannelConfigs(MeshChannelConfig configs[], int max_configs) const;
  int exportContacts(MeshContactSummary contacts[], int max_contacts) const;
  bool setContactFavoriteByPublicKeyHex(const char* public_key_hex, bool favorite);
  bool removeContactByPublicKeyHex(const char* public_key_hex);
  bool addChannel(const char* channel_name, const char* psk_base64 = nullptr);
  bool removeChannel(const char* channel_name);
  void getRadioStats(MeshRadioStats* out_stats) const;

  size_t drainEvents(MeshEvent* out_events, size_t max_events);

 private:
  friend class StandaloneChatMesh;

  static constexpr size_t kPubKeySize = 32;
  static constexpr size_t kMaxContactTelemetry = 48;

  struct ContactTelemetrySnapshot {
    uint8_t pub_key[kPubKeySize];
    uint8_t advert_type;
    uint16_t feat1;
    uint16_t feat2;
    uint32_t last_update_ms;
    bool used;
  };

  void queueInfo(const char* text);
  void queueChannelMessage(const char* channel_name, const char* text);
  void queueDirectMessage(const char* contact_name, const char* contact_key, const char* text);
  void noteRxRaw();
  void noteRxPacket();
  void markContactsDirty();
  void markChannelsDirty();
  void noteContactAdvertTelemetry(const uint8_t* pub_key, uint8_t advert_type, uint16_t feat1, uint16_t feat2);
  bool loadContactTelemetry(const uint8_t* pub_key, MeshContactSummary* out_summary) const;
  bool loadContactsFromFs();
  bool saveContactsToFs();
  bool loadChannelsFromFs();
  bool saveChannelsToFs();

  static constexpr size_t kMaxQueuedEvents = 32;
  MeshEvent event_queue_[kMaxQueuedEvents]{};
  size_t event_head_ = 0;
  size_t event_tail_ = 0;
  size_t event_count_ = 0;

  MeshRuntime* runtime_ = nullptr;
  uint32_t last_advert_ms_ = 0;
  uint32_t auto_advert_interval_ms_ = 21600000UL;
  uint16_t auto_advert_interval_minutes_ = 360;
  uint32_t last_persist_flush_ms_ = 0;
  uint32_t rx_raw_count_ = 0;
  uint32_t rx_packet_count_ = 0;
  uint32_t last_rx_ms_ = 0;
  ContactTelemetrySnapshot contact_telemetry_[kMaxContactTelemetry]{};
  bool identity_loaded_from_nvs_ = false;
  bool adverts_unlocked_for_boot_ = false;
  bool contacts_dirty_ = false;
  bool channels_dirty_ = false;
  bool ready_ = false;
};

}  // namespace mesh
}  // namespace plumeria
