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
  AckReceived,
  LoginSuccess,
  LoginFail,
  LoginTimeout,
  AdminCommandResponse,
};

struct MeshEvent {
  MeshEventType type;
  char channel_name[32];
  char peer_key[65];
  char text[96];
  uint8_t ack_count;
  // Populated for LoginSuccess events; zero for others.
  uint8_t login_perm;       // permission byte (admin bit etc.) from server
  uint8_t login_acl_perm;   // ACL permission byte (v7+)
  uint8_t login_fw_ver;     // server firmware ver level (v7+)
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
  bool ignored;
  uint8_t type;
  // Packed path_len from MeshCore (upper 2 bits hash size-1, lower 6 bits hop count).
  uint8_t out_path_len;
  // Raw route path bytes (hash-count * hash-size bytes, max 64).
  uint8_t out_path[64];
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
  bool setPathHashMode(uint8_t mode);
  uint8_t getPathHashMode() const;
  bool setMultiAck(bool enabled);
  bool setRepeaterMode(bool enabled);
  bool getRepeaterMode() const;
  bool setMeshRegion(const char* region_name);
  void getMeshRegion(char* out_name, size_t out_size) const;
  bool setAutoAdvertIntervalMinutes(uint16_t minutes);
  uint16_t getAutoAdvertIntervalMinutes() const;
  bool broadcastSelfAdvertNow();
  bool broadcastSelfAdvertFloodNow();
  int exportChannels(char names[][32], int max_names);
  int exportChannelConfigs(MeshChannelConfig configs[], int max_configs) const;
  int exportContacts(MeshContactSummary contacts[], int max_contacts) const;
  bool importContactByPublicKeyHex(const char* public_key_hex, const char* contact_name,
                                   uint8_t contact_type = 0);
  bool importFavoriteContactByPublicKeyHex(const char* public_key_hex);
  bool setContactFavoriteByPublicKeyHex(const char* public_key_hex, bool favorite);
  bool removeContactByPublicKeyHex(const char* public_key_hex);
  // Ignore list: suppress DMs/chats from a contact. Persisted + exported/imported.
  bool setContactIgnoredByPublicKeyHex(const char* public_key_hex, const char* name, bool ignored);
  bool isContactIgnoredByPublicKeyHex(const char* public_key_hex) const;
  bool isContactIgnoredByName(const char* name) const;
  int ignoredCount() const;
  bool getIgnoredEntry(int index, char* key_out, size_t key_size, char* name_out, size_t name_size) const;
  bool addIgnoredContact(const char* public_key_hex, const char* name);
  void clearIgnoredContacts();
  bool persistIgnoredContacts();
  bool sendLogin(const char* public_key_hex, const char* password);
  bool sendAdminCommand(const char* public_key_hex, const char* command);
  bool requestContactTelemetryByPublicKeyHex(const char* public_key_hex);
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
  void queueAckReceived(const char* contact_name, const char* contact_key, const char* sent_text, uint8_t ack_count);
  void queueLoginEvent(MeshEventType type, const uint8_t* pub_key, const char* contact_name,
                       uint8_t perm, uint8_t acl_perm, uint8_t fw_ver, const char* text);
  void queueAdminCommandResponse(const uint8_t* pub_key, const char* contact_name, const char* text);
  void handleLoginResponse(const void* contact, const uint8_t* data, uint8_t len);
  void handleContactTelemetryResponse(const void* contact, const uint8_t* data, uint8_t len);
  void handleCommandData(const void* contact, const char* text);
  void checkLoginTimeout(uint32_t now_ms);
  void noteRxRaw();
  void noteRxPacket();
  void markContactsDirty();
  void markChannelsDirty();
  void noteContactAdvertTelemetry(const uint8_t* pub_key, uint8_t advert_type, uint16_t feat1, uint16_t feat2);
  bool loadContactTelemetry(const uint8_t* pub_key, MeshContactSummary* out_summary) const;
  bool loadContactTelemetryFromFs();
  bool saveContactTelemetryToFs();
  bool loadContactsFromFs();
  bool saveContactsToFs();
  bool loadChannelsFromFs();
  bool saveChannelsToFs();
  bool loadIgnoredFromNvs();
  bool saveIgnoredToNvs() const;
  int findIgnoredSlotByKey(const char* public_key_hex) const;

  static constexpr size_t kMaxIgnored = 32;
  struct IgnoredContact {
    char key[65];
    char name[32];
    bool used;
  };
  // Heap-allocated at begin() so the array lives in the heap rather than the
  // tight static DRAM segment; null if allocation ever fails (feature disabled).
  IgnoredContact* ignored_ = nullptr;
  bool ensureIgnoredAllocated();

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
  uint8_t path_hash_mode_ = 0;
  bool multi_ack_enabled_ = false;
  bool repeater_enabled_ = false;
  char mesh_region_[32] = {};
  uint32_t rx_raw_count_ = 0;
  uint32_t rx_packet_count_ = 0;
  uint32_t last_rx_ms_ = 0;
  ContactTelemetrySnapshot contact_telemetry_[kMaxContactTelemetry]{};
  bool identity_loaded_from_nvs_ = false;
  bool adverts_unlocked_for_boot_ = false;
  bool contacts_dirty_ = false;
  bool channels_dirty_ = false;
  bool telemetry_dirty_ = false;
  bool ready_ = false;

  // Pending login tracking (single-flight).
  bool pending_login_active_ = false;
  uint8_t pending_login_pubkey_[kPubKeySize] = {};
  uint32_t pending_login_deadline_ms_ = 0;
  char pending_login_name_[32] = {};
};

}  // namespace mesh
}  // namespace plumeria
