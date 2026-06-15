#include "mesh/mesh_adapter.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/ESP32Board.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/radiolib/CustomSX1262.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#if defined(ENV_INCLUDE_GPS) && (ENV_INCLUDE_GPS == 1)
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>
#endif

namespace {

constexpr char kDefaultNodeName[] = "Plumeria";
constexpr char kPublicChannelName[] = "Public";
constexpr char kPublicChannelPsk[] = "izOH6cXN6mrJ5e26oRXNcg==";

constexpr uint32_t kSendTimeoutBaseMs = 500;
constexpr float kFloodSendTimeoutFactor = 16.0f;
constexpr float kDirectSendPerHopFactor = 6.0f;
constexpr uint32_t kDirectSendPerHopExtraMs = 250;

constexpr uint16_t kMinAutoAdvertIntervalMinutes = 60;
constexpr uint32_t kMinuteMs = 60000UL;
constexpr uint32_t kPersistFlushMs = 1000;

constexpr char kMeshPrefsNs[] = "mesh_store";
constexpr char kIdentityBlobKey[] = "identity_blob";
constexpr char kContactsCountKey[] = "contacts_cnt";
constexpr char kContactsBlobKey[] = "contacts_blob";
constexpr char kChannelsCountKey[] = "channels_cnt";
constexpr char kChannelsBlobKey[] = "channels_blob";

struct PersistedContact {
  uint8_t pub_key[32];
  char name[32];
  uint8_t type;
  uint8_t flags;
  uint8_t out_path_len;
  uint8_t out_path[MAX_PATH_SIZE];
  uint32_t last_advert_timestamp;
  uint32_t lastmod;
  int32_t gps_lat;
  int32_t gps_lon;
};

struct PersistedChannel {
  char name[32];
  uint8_t secret[32];
};

PersistedChannel g_channels_nvs_buf[MAX_GROUP_CHANNELS]{};
PersistedContact g_contacts_nvs_buf[MAX_CONTACTS + MAX_ANON_CONTACTS]{};
ChannelDetails g_channel_rollback_snapshot[MAX_GROUP_CHANNELS]{};

size_t encodeBase64(const uint8_t* src, size_t src_len, char* out, size_t out_size) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (!src || !out) {
    return 0;
  }

  const size_t required = ((src_len + 2) / 3) * 4 + 1;
  if (out_size < required) {
    return 0;
  }

  size_t i = 0;
  size_t o = 0;
  while (i + 2 < src_len) {
    const uint32_t n = (static_cast<uint32_t>(src[i]) << 16) |
                       (static_cast<uint32_t>(src[i + 1]) << 8) |
                       static_cast<uint32_t>(src[i + 2]);
    out[o++] = kAlphabet[(n >> 18) & 0x3F];
    out[o++] = kAlphabet[(n >> 12) & 0x3F];
    out[o++] = kAlphabet[(n >> 6) & 0x3F];
    out[o++] = kAlphabet[n & 0x3F];
    i += 3;
  }

  if (i < src_len) {
    const uint8_t a = src[i++];
    const bool has_b = i < src_len;
    const uint8_t b = has_b ? src[i++] : 0;

    out[o++] = kAlphabet[(a >> 2) & 0x3F];
    out[o++] = kAlphabet[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
    if (has_b) {
      out[o++] = kAlphabet[(b & 0x0F) << 2];
      out[o++] = '=';
    } else {
      out[o++] = '=';
      out[o++] = '=';
    }
  }

  out[o] = '\0';
  return o;
}

bool bytesToHex(const uint8_t* src, size_t src_len, char* out, size_t out_size) {
  if (!src || !out || out_size == 0) {
    return false;
  }

  const size_t needed = src_len * 2 + 1;
  if (out_size < needed) {
    out[0] = '\0';
    return false;
  }

  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < src_len; i++) {
    out[i * 2] = kHex[(src[i] >> 4) & 0x0F];
    out[i * 2 + 1] = kHex[src[i] & 0x0F];
  }
  out[src_len * 2] = '\0';
  return true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

bool hexToBytes(const char* hex, uint8_t* out, size_t out_len) {
  if (!hex || !out || out_len == 0) {
    return false;
  }

  const size_t expected = out_len * 2;
  if (strlen(hex) != expected) {
    return false;
  }

  for (size_t i = 0; i < out_len; i++) {
    const int hi = hexNibble(hex[i * 2]);
    const int lo = hexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }

  return true;
}

int loadChannelsSnapshotFromNvs(PersistedChannel* out_channels, int max_channels) {
  if (!out_channels || max_channels <= 0) {
    return 0;
  }

  Preferences prefs;
  if (!prefs.begin(kMeshPrefsNs, true)) {
    if (false) Serial.println("[PERSIST][CH] NVS open failed for read");
    return 0;
  }

  uint16_t saved_count = prefs.getUShort(kChannelsCountKey, 0);

  // Prefer compact blob format when present.
  const size_t blob_len = prefs.getBytesLength(kChannelsBlobKey);
  if (blob_len >= sizeof(PersistedChannel)) {
    const int blob_count = static_cast<int>(blob_len / sizeof(PersistedChannel));
    if (saved_count > 0 && blob_count != static_cast<int>(saved_count)) {
      if (false) Serial.printf("[PERSIST][CH] blob/count mismatch blob=%d count=%u, fallback keyset\n", blob_count,
                    static_cast<unsigned>(saved_count));
    } else {
    const int capped_blob_count = blob_count > max_channels ? max_channels : blob_count;
    const size_t to_read = static_cast<size_t>(capped_blob_count) * sizeof(PersistedChannel);

    memset(out_channels, 0, static_cast<size_t>(max_channels) * sizeof(PersistedChannel));
    const size_t got_blob = prefs.getBytes(kChannelsBlobKey, out_channels, to_read);
    if (got_blob >= sizeof(PersistedChannel)) {
      int loaded = 0;
      for (int i = 0; i < capped_blob_count && loaded < max_channels; i++) {
        if (out_channels[i].name[0] == '\0') {
          continue;
        }
        if (i != loaded) {
          out_channels[loaded] = out_channels[i];
        }
        loaded++;
      }
      prefs.end();
      if (false) Serial.printf("[PERSIST][CH] NVS blob loaded=%d raw=%d\n", loaded, capped_blob_count);
      return loaded;
    }
    }
  }

  if (saved_count == 0) {
    prefs.end();
    if (false) Serial.println("[PERSIST][CH] NVS empty (count=0)");
    return 0;
  }

  if (saved_count > static_cast<uint16_t>(max_channels)) {
    saved_count = static_cast<uint16_t>(max_channels);
  }

  int loaded = 0;
  for (uint16_t i = 0; i < saved_count && loaded < max_channels; i++) {
    char key_name[12] = {};
    char key_secret[12] = {};
    snprintf(key_name, sizeof(key_name), "ch_name_%u", static_cast<unsigned>(i));
    snprintf(key_secret, sizeof(key_secret), "ch_sec_%u", static_cast<unsigned>(i));

    String name = prefs.getString(key_name, "");
    if (name.length() == 0) {
      continue;
    }

    PersistedChannel entry{};
    strncpy(entry.name, name.c_str(), sizeof(entry.name) - 1);
    entry.name[sizeof(entry.name) - 1] = '\0';

    const size_t got_secret = prefs.getBytes(key_secret, entry.secret, sizeof(entry.secret));
    if (got_secret != 16 && got_secret != sizeof(entry.secret)) {
      continue;
    }

    if (got_secret == 16) {
      memset(&entry.secret[16], 0, sizeof(entry.secret) - 16);
    }

    out_channels[loaded++] = entry;
  }

  prefs.end();
  if (false) Serial.printf("[PERSIST][CH] NVS keyset loaded=%d declared=%u\n", loaded,
                static_cast<unsigned>(saved_count));
  return loaded;
}

int loadContactsSnapshotFromNvs(PersistedContact* out_contacts, int max_contacts) {
  if (!out_contacts || max_contacts <= 0) {
    return 0;
  }

  Preferences prefs;
  if (!prefs.begin(kMeshPrefsNs, true)) {
    if (false) Serial.println("[PERSIST][CT] NVS open failed for read");
    return 0;
  }

  if (!prefs.isKey(kContactsBlobKey)) {
    prefs.end();
    return 0;
  }

  const size_t blob_len = prefs.getBytesLength(kContactsBlobKey);
  if (blob_len < sizeof(PersistedContact)) {
    prefs.end();
    return 0;
  }

  int count = static_cast<int>(blob_len / sizeof(PersistedContact));
  const uint16_t declared = prefs.getUShort(kContactsCountKey, static_cast<uint16_t>(count));
  if (declared > 0 && declared < static_cast<uint16_t>(count)) {
    count = declared;
  }
  if (count > max_contacts) {
    count = max_contacts;
  }

  const size_t to_read = static_cast<size_t>(count) * sizeof(PersistedContact);
  memset(out_contacts, 0, static_cast<size_t>(max_contacts) * sizeof(PersistedContact));
  const size_t got = prefs.getBytes(kContactsBlobKey, out_contacts, to_read);
  prefs.end();

  const int loaded = static_cast<int>(got / sizeof(PersistedContact));
  if (false) Serial.printf("[PERSIST][CT] NVS loaded=%d declared=%u\n", loaded, static_cast<unsigned>(declared));
  return loaded;
}

bool loadIdentityFromNvs(::mesh::LocalIdentity* identity) {
  if (!identity) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kMeshPrefsNs, true)) {
    if (false) Serial.println("[PERSIST][ID] NVS open failed for read");
    return false;
  }

  const size_t blob_len = prefs.getBytesLength(kIdentityBlobKey);
  if (blob_len != PRV_KEY_SIZE && blob_len != (PRV_KEY_SIZE + PUB_KEY_SIZE)) {
    prefs.end();
    return false;
  }

  uint8_t blob[PRV_KEY_SIZE + PUB_KEY_SIZE] = {};
  const size_t got = prefs.getBytes(kIdentityBlobKey, blob, blob_len);
  prefs.end();
  if (got != blob_len) {
    return false;
  }

  identity->readFrom(blob, got);
  if (identity->pub_key[0] == 0x00 || identity->pub_key[0] == 0xFF) {
    return false;
  }

  if (false) Serial.println("[PERSIST][ID] loaded from NVS");
  return true;
}

bool saveIdentityToNvs(::mesh::LocalIdentity* identity) {
  if (!identity) {
    return false;
  }

  uint8_t blob[PRV_KEY_SIZE + PUB_KEY_SIZE] = {};
  const size_t blob_len = identity->writeTo(blob, sizeof(blob));
  if (blob_len != PRV_KEY_SIZE && blob_len != (PRV_KEY_SIZE + PUB_KEY_SIZE)) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kMeshPrefsNs, false)) {
    if (false) Serial.println("[PERSIST][ID] NVS open failed for write");
    return false;
  }

  const size_t wrote = prefs.putBytes(kIdentityBlobKey, blob, blob_len);
  prefs.end();
  if (wrote != blob_len) {
    if (false) Serial.println("[PERSIST][ID] NVS write failed");
    return false;
  }

  if (false) Serial.println("[PERSIST][ID] saved to NVS");
  return true;
}

int applyChannelSnapshot(BaseChatMesh* mesh, const PersistedChannel* channels, int channel_count) {
  if (!mesh || !channels || channel_count <= 0) {
    return 0;
  }

  ChannelDetails empty{};
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    mesh->setChannel(i, empty);
  }

  int loaded = 0;
  for (int i = 0; i < channel_count && loaded < MAX_GROUP_CHANNELS; i++) {
    if (channels[i].name[0] == '\0') {
      continue;
    }

    size_t secret_len = 32;
    bool upper_zero = true;
    for (size_t j = 16; j < 32; j++) {
      if (channels[i].secret[j] != 0) {
        upper_zero = false;
        break;
      }
    }
    if (upper_zero) {
      secret_len = 16;
    }

    char psk_b64[49] = {};
    if (encodeBase64(channels[i].secret, secret_len, psk_b64, sizeof(psk_b64)) == 0) {
      continue;
    }

    if (mesh->addChannel(channels[i].name, psk_b64) != nullptr) {
      loaded++;
      continue;
    }

    ChannelDetails channel{};
    strncpy(channel.name, channels[i].name, sizeof(channel.name) - 1);
    channel.name[sizeof(channel.name) - 1] = '\0';
    memcpy(channel.channel.secret, channels[i].secret, sizeof(channel.channel.secret));
    if (mesh->setChannel(loaded, channel)) {
      loaded++;
    }
  }

  return loaded;
}

}  // namespace

namespace plumeria {
namespace mesh {

class StandaloneChatMesh : public BaseChatMesh {
 public:
  StandaloneChatMesh(::mesh::Radio& radio,
                     ::mesh::MillisecondClock& millis_clock,
                     ::mesh::RNG& rng,
                     ::mesh::RTCClock& rtc_clock,
                     ::mesh::PacketManager& packet_manager,
                     ::mesh::MeshTables& mesh_tables,
                     MeshAdapter* adapter)
      : BaseChatMesh(radio, millis_clock, rng, rtc_clock, packet_manager, mesh_tables),
        adapter_(adapter),
        advert_location_enabled_(false),
        advert_lat_(0.0),
        advert_lon_(0.0) {
    strncpy(node_name_, kDefaultNodeName, sizeof(node_name_) - 1);
    node_name_[sizeof(node_name_) - 1] = '\0';
  }

  void beginStandalone() {
    BaseChatMesh::begin();
  }

  bool ensurePublicChannel() {
    if (hasChannelNamed(kPublicChannelName)) {
      return false;
    }
    return addChannel(kPublicChannelName, kPublicChannelPsk) != nullptr;
  }

  bool ensureHashtagChannel(const char* name) {
    if (!name || name[0] == '\0') {
      return false;
    }
    if (hasChannelNamed(name)) {
      return false;
    }
    return addHashtagChannel(name);
  }

  bool upsertChannel(const char* channel_name, const char* psk_base64) {
    if (!channel_name || channel_name[0] == '\0') {
      return false;
    }

    if (channel_name[0] == '#') {
      return addHashtagChannel(channel_name);
    }

    if (hasChannelNamed(channel_name)) {
      return true;
    }

    if (!psk_base64 || psk_base64[0] == '\0') {
      return false;
    }

    return addChannel(channel_name, psk_base64) != nullptr;
  }

  bool removeChannelByName(const char* channel_name) {
    if (!channel_name || channel_name[0] == '\0') {
      return false;
    }

    int remove_idx = -1;
    ChannelDetails channel{};
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (!getChannel(i, channel) || channel.name[0] == '\0') {
        continue;
      }
      if (strcmp(channel.name, channel_name) == 0) {
        remove_idx = i;
        break;
      }
    }

    if (remove_idx < 0) {
      return false;
    }

    for (int i = remove_idx; i < MAX_GROUP_CHANNELS - 1; i++) {
      ChannelDetails next{};
      if (getChannel(i + 1, next) && next.name[0] != '\0') {
        setChannel(i, next);
      } else {
        ChannelDetails empty{};
        setChannel(i, empty);
        for (int j = i + 1; j < MAX_GROUP_CHANNELS; j++) {
          setChannel(j, empty);
        }
        return true;
      }
    }

    ChannelDetails empty{};
    setChannel(MAX_GROUP_CHANNELS - 1, empty);
    return true;
  }

  bool sendDirectByName(const char* destination, const char* text) {
    if (!destination || !text || destination[0] == '\0' || text[0] == '\0') {
      return false;
    }

    ContactInfo* recipient = searchContactsByPrefix(destination);
    if (!recipient) {
      return false;
    }

    uint32_t expected_ack = 0;
    uint32_t est_timeout = 0;
    uint32_t ts = getRTCClock()->getCurrentTimeUnique();
    int send_result = sendMessage(*recipient, ts, 0, text, expected_ack, est_timeout);
    (void)expected_ack;
    (void)est_timeout;
    return send_result != MSG_SEND_FAILED;
  }

  bool sendDirectByPublicKey(const uint8_t* public_key, const char* text) {
    if (!public_key || !text || text[0] == '\0') {
      return false;
    }

    ContactInfo* recipient = lookupContactByPubKey(public_key, PUB_KEY_SIZE);
    if (!recipient) {
      return false;
    }

    uint32_t expected_ack = 0;
    uint32_t est_timeout = 0;
    uint32_t ts = getRTCClock()->getCurrentTimeUnique();
    int send_result = sendMessage(*recipient, ts, 0, text, expected_ack, est_timeout);
    (void)expected_ack;
    (void)est_timeout;
    return send_result != MSG_SEND_FAILED;
  }

  bool sendChannelByName(const char* channel_name, const char* text) {
    if (!channel_name || !text || channel_name[0] == '\0' || text[0] == '\0') {
      return false;
    }

    ChannelDetails channel{};
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (!getChannel(i, channel)) {
        continue;
      }
      if (strcmp(channel.name, channel_name) == 0) {
        return sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), channel.channel, node_name_, text,
                                static_cast<int>(strlen(text)));
      }
    }
    return false;
  }

  void broadcastSelfAdvert() {
    ::mesh::Packet* packet = nullptr;
    if (advert_location_enabled_ && advert_lat_ >= -90.0 && advert_lat_ <= 90.0 &&
        advert_lon_ >= -180.0 && advert_lon_ <= 180.0) {
      packet = createSelfAdvert(node_name_, advert_lat_, advert_lon_);
    }
    if (!packet) {
      packet = createSelfAdvert(node_name_);
    }
    if (packet) {
      sendZeroHop(packet);
    }
  }

  void broadcastSelfAdvertFlood() {
    ::mesh::Packet* packet = nullptr;
    if (advert_location_enabled_ && advert_lat_ >= -90.0 && advert_lat_ <= 90.0 &&
        advert_lon_ >= -180.0 && advert_lon_ <= 180.0) {
      packet = createSelfAdvert(node_name_, advert_lat_, advert_lon_);
    }
    if (!packet) {
      packet = createSelfAdvert(node_name_);
    }
    if (packet) {
      sendFlood(packet);
    }
  }

  void setAdvertLocation(bool enabled, double latitude, double longitude) {
    advert_location_enabled_ = enabled;
    advert_lat_ = latitude;
    advert_lon_ = longitude;
  }

  void getAdvertLocation(bool* enabled, double* latitude, double* longitude) const {
    if (enabled) {
      *enabled = advert_location_enabled_;
    }
    if (latitude) {
      *latitude = advert_lat_;
    }
    if (longitude) {
      *longitude = advert_lon_;
    }
  }

  void setNodeName(const char* name) {
    if (!name || name[0] == '\0') {
      return;
    }
    strncpy(node_name_, name, sizeof(node_name_) - 1);
    node_name_[sizeof(node_name_) - 1] = '\0';
  }

  const char* getNodeName() const {
    return node_name_;
  }

 protected:
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override {
    (void)path;
    char info[96];
    snprintf(info, sizeof(info), "%s contact %s (%u hops)", is_new ? "New" : "Seen", contact.name,
             static_cast<unsigned>(path_len & 63));
    adapter_->queueInfo(info);
    adapter_->markContactsDirty();
  }

  void onContactPathUpdated(const ContactInfo& contact) override {
    char info[96];
    snprintf(info, sizeof(info), "Path update for %s", contact.name);
    adapter_->queueInfo(info);
    adapter_->markContactsDirty();
  }

  ContactInfo* processAck(const uint8_t* data) override {
    (void)data;
    return nullptr;
  }

  void onMessageRecv(const ContactInfo& contact, ::mesh::Packet* packet, uint32_t sender_timestamp,
                     const char* text) override {
    (void)packet;
    (void)sender_timestamp;
    char contact_name[32] = {};
    if (contact.name[0] != '\0') {
      strncpy(contact_name, contact.name, sizeof(contact_name) - 1);
      contact_name[sizeof(contact_name) - 1] = '\0';
    } else {
      strncpy(contact_name, "(unnamed)", sizeof(contact_name) - 1);
      contact_name[sizeof(contact_name) - 1] = '\0';
    }
    char contact_key[65] = {};
    bytesToHex(contact.id.pub_key, PUB_KEY_SIZE, contact_key, sizeof(contact_key));
    if (false) Serial.printf("[RX][DM] from=%s msg=%.80s\n", contact.name, text ? text : "");
    adapter_->queueDirectMessage(contact_name, contact_key, text ? text : "");
  }

  void onCommandDataRecv(const ContactInfo& contact, ::mesh::Packet* packet, uint32_t sender_timestamp,
                         const char* text) override {
    (void)contact;
    (void)packet;
    (void)sender_timestamp;
    (void)text;
  }

  void onSignedMessageRecv(const ContactInfo& contact, ::mesh::Packet* packet, uint32_t sender_timestamp,
                           const uint8_t* sender_prefix, const char* text) override {
    (void)contact;
    (void)packet;
    (void)sender_timestamp;
    (void)sender_prefix;
    (void)text;
  }

  void onGroupDataRecv(::mesh::Packet* packet, uint8_t type, const ::mesh::GroupChannel& channel,
                       uint8_t* data, size_t len) override {
    if (type != PAYLOAD_TYPE_GRP_TXT) {
      BaseChatMesh::onGroupDataRecv(packet, type, channel, data, len);
      return;
    }

    if (len < 5 || !data) {
      return;
    }

    uint32_t timestamp = 0;
    memcpy(&timestamp, data, 4);
    const uint8_t txt_type = data[4];

    char text_buf[96] = {};
    size_t text_len = len - 5;
    if (text_len > sizeof(text_buf) - 1) {
      text_len = sizeof(text_buf) - 1;
    }
    if (text_len > 0) {
      memcpy(text_buf, &data[5], text_len);
      text_buf[text_len] = '\0';
    }

    if ((txt_type >> 2) != 0) {
      if (false) Serial.printf("[MESH][GRP] accepting txt_type=%u len=%u\n", static_cast<unsigned>(txt_type),
                    static_cast<unsigned>(len));
    }

    onChannelMessageRecv(channel, packet, timestamp, text_buf);
  }

  void onChannelMessageRecv(const ::mesh::GroupChannel& channel, ::mesh::Packet* packet, uint32_t timestamp,
                            const char* text) override {
    (void)packet;
    (void)timestamp;
    char resolved_name[32] = {};
    resolveChannelName(channel, resolved_name, sizeof(resolved_name));

    if (resolved_name[0] == '\0') {
      adapter_->queueInfo("RX channel hash unresolved; message dropped");
      return;
    }

    if (false) Serial.printf("[RX][CH] channel=%s msg=%.80s\n", resolved_name, text ? text : "");
    adapter_->queueChannelMessage(resolved_name, text ? text : "");
  }

  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data,
                           uint8_t len, uint8_t* reply) override {
    (void)contact;
    (void)sender_timestamp;
    (void)data;
    (void)len;
    (void)reply;
    return 0;
  }

  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override {
    (void)contact;
    (void)data;
    (void)len;
  }

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
    return kSendTimeoutBaseMs + static_cast<uint32_t>(kFloodSendTimeoutFactor * pkt_airtime_millis);
  }

  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override {
    uint8_t hops = path_len & 63;
    return kSendTimeoutBaseMs +
           static_cast<uint32_t>((pkt_airtime_millis * kDirectSendPerHopFactor + kDirectSendPerHopExtraMs) *
                                 (hops + 1));
  }

  void onSendTimeout() override {
    adapter_->queueInfo("TX timeout waiting for ACK");
  }

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override {
    if (!raw || len <= 0) {
      return;
    }

    adapter_->noteRxRaw();

    const uint8_t header = raw[0];
    const uint8_t route = header & PH_ROUTE_MASK;
    const uint8_t type = (header >> PH_TYPE_SHIFT) & PH_TYPE_MASK;
    if (false) Serial.printf("[RADIO][RAW] len=%d type=%u route=%u snr=%.1f rssi=%.1f hdr=0x%02X\n", len,
                  static_cast<unsigned>(type), static_cast<unsigned>(route), snr, rssi,
                  static_cast<unsigned>(header));
  }

  void logRx(::mesh::Packet* packet, int len, float score) override {
    if (!packet) {
      return;
    }

    adapter_->noteRxPacket();

    if (false) Serial.printf("[RADIO][PKT] len=%d type=%u route=%u score=%.3f snr=%.1f path_n=%u path_sz=%u\n", len,
                  static_cast<unsigned>(packet->getPayloadType()), static_cast<unsigned>(packet->getRouteType()),
                  score, packet->getSNR(), static_cast<unsigned>(packet->getPathHashCount()),
                  static_cast<unsigned>(packet->getPathHashSize()));
  }

 private:
  void resolveChannelName(const ::mesh::GroupChannel& group_channel, char* out_name, size_t out_name_size) {
    if (!out_name || out_name_size == 0) {
      return;
    }

    out_name[0] = '\0';
    ChannelDetails channel{};
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (!getChannel(i, channel) || channel.name[0] == '\0') {
        continue;
      }
      if (memcmp(channel.channel.hash, group_channel.hash, sizeof(group_channel.hash)) == 0) {
        strncpy(out_name, channel.name, out_name_size - 1);
        out_name[out_name_size - 1] = '\0';
        return;
      }
    }
  }

  bool hasChannelNamed(const char* name) {
    if (!name || name[0] == '\0') {
      return false;
    }

    ChannelDetails channel{};
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (!getChannel(i, channel)) {
        continue;
      }
      if (channel.name[0] != '\0' && strcmp(channel.name, name) == 0) {
        return true;
      }
    }
    return false;
  }

  int firstEmptyChannelSlot() {
    ChannelDetails channel{};
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (!getChannel(i, channel) || channel.name[0] == '\0') {
        return i;
      }
    }
    return -1;
  }

  bool addHashtagChannel(const char* name) {
    if (!name || name[0] == '\0') {
      return false;
    }

    // Normalize to #name and derive secret via SHA-256(name).
    char normalized[32] = {};
    size_t src = 0;
    while (name[src] == ' ' || name[src] == '\t') {
      src++;
    }

    size_t out = 0;
    if (name[src] != '#') {
      normalized[out++] = '#';
    }
    while (name[src] && name[src] != ' ' && name[src] != '\t' && name[src] != '\r' && name[src] != '\n' &&
           out < sizeof(normalized) - 1) {
      normalized[out++] = name[src++];
    }
    normalized[out] = '\0';
    if (out <= 1) {
      return false;
    }

    if (hasChannelNamed(normalized)) {
      return true;
    }

    int slot = firstEmptyChannelSlot();
    if (slot < 0 || slot >= MAX_GROUP_CHANNELS) {
      return false;
    }

    ChannelDetails channel{};
    ::mesh::Utils::sha256(channel.channel.secret, CIPHER_KEY_SIZE,
                          reinterpret_cast<const uint8_t*>(normalized), strlen(normalized));
    strncpy(channel.name, normalized, sizeof(channel.name) - 1);
    channel.name[sizeof(channel.name) - 1] = '\0';
    return setChannel(slot, channel);
  }

  MeshAdapter* adapter_;
  char node_name_[32];
  bool advert_location_enabled_;
  double advert_lat_;
  double advert_lon_;
};

struct MeshRuntime {
  MeshRuntime()
      : lora_spi(FSPI),
        radio_module(nullptr),
        radio_chip(nullptr),
        radio_driver(nullptr),
        packet_manager(16),
  mesh(nullptr)
#if defined(ENV_INCLUDE_GPS) && (ENV_INCLUDE_GPS == 1)
  , gps(Serial1, &rtc_clock),
  sensors(gps)
#endif
  {}

  ESP32Board board;
  SPIClass lora_spi;
  Module* radio_module;
  CustomSX1262* radio_chip;
  CustomSX1262Wrapper* radio_driver;
  StdRNG fast_rng;
  SimpleMeshTables tables;
  ArduinoMillis millis_clock;
  ESP32RTCClock rtc_clock;
  StaticPoolPacketManager packet_manager;
  StandaloneChatMesh* mesh;
#if defined(ENV_INCLUDE_GPS) && (ENV_INCLUDE_GPS == 1)
  MicroNMEALocationProvider gps;
  EnvironmentSensorManager sensors;
#endif
};

bool MeshAdapter::begin(const hal::RadioConfig& radio_config) {
  if (ready_) {
    return true;
  }

  runtime_ = new MeshRuntime();
  if (!runtime_) {
    queueInfo("Mesh runtime allocation failed");
    return false;
  }

  runtime_->board.begin();
  runtime_->rtc_clock.begin();

  runtime_->lora_spi.begin(radio_config.spi_sck, radio_config.spi_miso, radio_config.spi_mosi);
  runtime_->radio_module = new Module(radio_config.radio_cs, radio_config.radio_dio1, radio_config.radio_rst,
                                      radio_config.radio_busy, runtime_->lora_spi);
  if (!runtime_->radio_module) {
    queueInfo("SX1262 module allocation failed");
    return false;
  }

  runtime_->radio_chip = new CustomSX1262(runtime_->radio_module);
  if (!runtime_->radio_chip) {
    queueInfo("SX1262 radio allocation failed");
    return false;
  }

  if (!runtime_->radio_chip->std_init(&runtime_->lora_spi)) {
    queueInfo("SX1262 init failed");
    return false;
  }

  runtime_->radio_chip->setFrequency(radio_config.frequency_mhz);
  runtime_->radio_chip->setBandwidth(radio_config.bandwidth_khz);
  runtime_->radio_chip->setSpreadingFactor(radio_config.spreading_factor);
  runtime_->radio_chip->setCodingRate(radio_config.coding_rate);
  runtime_->radio_chip->setOutputPower(radio_config.tx_power_dbm);

  runtime_->radio_driver = new CustomSX1262Wrapper(*runtime_->radio_chip, runtime_->board);
  if (!runtime_->radio_driver) {
    queueInfo("Radio driver allocation failed");
    return false;
  }
  runtime_->radio_driver->setRxBoostedGainMode(radio_config.rx_boosted_gain);

  runtime_->fast_rng.begin(runtime_->radio_chip->random(0x7FFFFFFF));
  runtime_->mesh = new StandaloneChatMesh(*runtime_->radio_driver, runtime_->millis_clock, runtime_->fast_rng,
                                          runtime_->rtc_clock, runtime_->packet_manager, runtime_->tables, this);
  if (!runtime_->mesh) {
    queueInfo("Mesh object allocation failed");
    return false;
  }

  runtime_->mesh->setNodeName(kDefaultNodeName);

  if (!loadIdentityFromNvs(&runtime_->mesh->self_id)) {
    runtime_->mesh->self_id = ::mesh::LocalIdentity(&runtime_->fast_rng);
    int attempts = 0;
    while (attempts < 10 &&
           (runtime_->mesh->self_id.pub_key[0] == 0x00 || runtime_->mesh->self_id.pub_key[0] == 0xFF)) {
      runtime_->mesh->self_id = ::mesh::LocalIdentity(&runtime_->fast_rng);
      attempts++;
    }
    if (saveIdentityToNvs(&runtime_->mesh->self_id)) {
      queueInfo("Generated new mesh identity");
    } else {
      queueInfo("Generated new mesh identity (NVS save failed)");
    }
  } else {
    queueInfo("Loaded mesh identity");
  }

  runtime_->mesh->beginStandalone();

#if defined(ENV_INCLUDE_GPS) && (ENV_INCLUDE_GPS == 1)
  runtime_->sensors.begin();
#endif

  bool loaded_channels = loadChannelsFromFs();
  if (!loaded_channels) {
    bool added_public = runtime_->mesh->ensurePublicChannel();
    if (added_public) {
      markChannelsDirty();
      queueInfo("Seeded default channel: Public");
    }
  } else {
    bool added_public = runtime_->mesh->ensurePublicChannel();
    if (added_public) {
      markChannelsDirty();
      queueInfo("Added missing default channel: Public");
    }
  }

  if (channels_dirty_) {
    if (saveChannelsToFs()) {
      channels_dirty_ = false;
    } else {
      queueInfo("Failed to persist boot channel migration");
    }
  }

  loadContactsFromFs();

  runtime_->mesh->broadcastSelfAdvert();

  char info[96];
  snprintf(info, sizeof(info), "Mesh init %.3fMHz SF%u BW%.1fkHz CR4/%u", radio_config.frequency_mhz,
           radio_config.spreading_factor, radio_config.bandwidth_khz, radio_config.coding_rate);
  queueInfo(info);

  if (false) Serial.println("[MESH] MeshCore standalone runtime initialized");
  last_advert_ms_ = millis();
  last_persist_flush_ms_ = millis();
  ready_ = true;
  return true;
}

void MeshAdapter::loop() {
  if (!ready_) {
    return;
  }

  runtime_->mesh->loop();
#if defined(ENV_INCLUDE_GPS) && (ENV_INCLUDE_GPS == 1)
  runtime_->sensors.loop();
#endif
  runtime_->rtc_clock.tick();

  const uint32_t now = millis();
  if (now - last_advert_ms_ >= auto_advert_interval_ms_) {
    runtime_->mesh->broadcastSelfAdvert();
    runtime_->mesh->broadcastSelfAdvertFlood();
    last_advert_ms_ = now;
    queueInfo("Broadcast self advert (zero+flood)");
  }

  if ((contacts_dirty_ || channels_dirty_) && now - last_persist_flush_ms_ >= kPersistFlushMs) {
    if (contacts_dirty_) {
      saveContactsToFs();
      contacts_dirty_ = false;
    }
    if (channels_dirty_) {
      saveChannelsToFs();
      channels_dirty_ = false;
    }
    last_persist_flush_ms_ = now;
  }
}

bool MeshAdapter::sendDirectMessage(const char* destination, const char* text) {
  if (!ready_ || !destination || !text || destination[0] == '\0' || text[0] == '\0') {
    return false;
  }

  bool sent = runtime_->mesh->sendDirectByName(destination, text);
  if (!sent) {
    uint8_t pub_key[PUB_KEY_SIZE] = {};
    if (hexToBytes(destination, pub_key, sizeof(pub_key))) {
      sent = runtime_->mesh->sendDirectByPublicKey(pub_key, text);
    }
  }

  if (!sent) {
    queueInfo("Direct send failed: contact not found/path unavailable");
    return false;
  }

  char info[96];
  snprintf(info, sizeof(info), "TX DM -> %s", destination);
  queueInfo(info);
  return true;
}

bool MeshAdapter::sendChannelMessage(const char* channel_name, const char* text) {
  if (!ready_ || !channel_name || !text || channel_name[0] == '\0' || text[0] == '\0') {
    return false;
  }

  if (!runtime_->mesh->sendChannelByName(channel_name, text)) {
    queueInfo("Channel send failed: channel not configured");
    return false;
  }

  queueChannelMessage(channel_name, text);
  return true;
}

bool MeshAdapter::setNodeName(const char* node_name) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !node_name || node_name[0] == '\0') {
    return false;
  }

  runtime_->mesh->setNodeName(node_name);
  return true;
}

void MeshAdapter::getNodeName(char* out_name, size_t out_size) const {
  if (!out_name || out_size == 0) {
    return;
  }

  out_name[0] = '\0';
  if (!ready_ || !runtime_ || !runtime_->mesh) {
    return;
  }

  const char* name = runtime_->mesh->getNodeName();
  if (!name || name[0] == '\0') {
    name = kDefaultNodeName;
  }
  strncpy(out_name, name, out_size - 1);
  out_name[out_size - 1] = '\0';
}

bool MeshAdapter::getPublicKeyHex(char* out_hex, size_t out_size) const {
  if (!out_hex || out_size == 0) {
    return false;
  }

  out_hex[0] = '\0';
  if (!ready_ || !runtime_ || !runtime_->mesh) {
    return false;
  }

  return bytesToHex(runtime_->mesh->self_id.pub_key, PUB_KEY_SIZE, out_hex, out_size);
}

bool MeshAdapter::getIdentityKeysHex(char* out_public_hex, size_t public_hex_size,
                                     char* out_private_hex, size_t private_hex_size) const {
  if (!out_public_hex || public_hex_size == 0 || !out_private_hex || private_hex_size == 0) {
    return false;
  }

  out_public_hex[0] = '\0';
  out_private_hex[0] = '\0';
  if (!ready_ || !runtime_ || !runtime_->mesh) {
    return false;
  }

  static uint8_t identity_blob[PRV_KEY_SIZE + PUB_KEY_SIZE] = {};
  memset(identity_blob, 0, sizeof(identity_blob));
  const size_t blob_len = runtime_->mesh->self_id.writeTo(identity_blob, sizeof(identity_blob));
  if (blob_len != PRV_KEY_SIZE && blob_len != (PRV_KEY_SIZE + PUB_KEY_SIZE)) {
    return false;
  }

  if (!bytesToHex(runtime_->mesh->self_id.pub_key, PUB_KEY_SIZE, out_public_hex, public_hex_size)) {
    return false;
  }

  if (!bytesToHex(identity_blob, PRV_KEY_SIZE, out_private_hex, private_hex_size)) {
    out_public_hex[0] = '\0';
    return false;
  }

  return true;
}

bool MeshAdapter::importIdentityKeysHex(const char* public_hex, const char* private_hex) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !private_hex) {
    return false;
  }

  uint8_t identity_blob[PRV_KEY_SIZE + PUB_KEY_SIZE] = {};
  if (!hexToBytes(private_hex, identity_blob, PRV_KEY_SIZE)) {
    return false;
  }

  size_t blob_len = PRV_KEY_SIZE;
  if (public_hex && public_hex[0] != '\0') {
    if (!hexToBytes(public_hex, identity_blob + PRV_KEY_SIZE, PUB_KEY_SIZE)) {
      return false;
    }
    blob_len = PRV_KEY_SIZE + PUB_KEY_SIZE;
  }

  ::mesh::LocalIdentity imported_identity(&runtime_->fast_rng);
  imported_identity.readFrom(identity_blob, blob_len);
  if (imported_identity.pub_key[0] == 0x00 || imported_identity.pub_key[0] == 0xFF) {
    return false;
  }

  if (public_hex && public_hex[0] != '\0') {
    if (memcmp(imported_identity.pub_key, identity_blob + PRV_KEY_SIZE, PUB_KEY_SIZE) != 0) {
      return false;
    }
  }

  runtime_->mesh->self_id = imported_identity;
  return saveIdentityToNvs(&runtime_->mesh->self_id);
}

bool MeshAdapter::setGpsEnabled(bool enabled) {
#if defined(ENV_INCLUDE_GPS) && (ENV_INCLUDE_GPS == 1)
  if (!ready_ || !runtime_) {
    return false;
  }
  return runtime_->sensors.setSettingValue("gps", enabled ? "1" : "0");
#else
  (void)enabled;
  return false;
#endif
}

int MeshAdapter::getGpsSatelliteCount() const {
#if defined(ENV_INCLUDE_GPS) && (ENV_INCLUDE_GPS == 1)
  if (!ready_ || !runtime_) {
    return -1;
  }

  LocationProvider* location = runtime_->sensors.getLocationProvider();
  if (!location) {
    return -1;
  }

  long satellites = location->satellitesCount();
  if (satellites < 0) {
    return 0;
  }
  if (satellites > 255) {
    satellites = 255;
  }
  return static_cast<int>(satellites);
#else
  return -1;
#endif
}

bool MeshAdapter::setAdvertLocation(bool enabled, double latitude, double longitude) {
  if (!ready_ || !runtime_ || !runtime_->mesh) {
    return false;
  }

  if (enabled) {
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
      return false;
    }
  }

  runtime_->mesh->setAdvertLocation(enabled, latitude, longitude);
  return true;
}

bool MeshAdapter::setAutoAdvertIntervalMinutes(uint16_t minutes) {
  if (minutes < kMinAutoAdvertIntervalMinutes) {
    minutes = kMinAutoAdvertIntervalMinutes;
  }

  auto_advert_interval_minutes_ = minutes;
  auto_advert_interval_ms_ = static_cast<uint32_t>(minutes) * kMinuteMs;
  return true;
}

uint16_t MeshAdapter::getAutoAdvertIntervalMinutes() const {
  return auto_advert_interval_minutes_;
}

void MeshAdapter::getAdvertLocation(bool* enabled, double* latitude, double* longitude) const {
  if (enabled) {
    *enabled = false;
  }
  if (latitude) {
    *latitude = 0.0;
  }
  if (longitude) {
    *longitude = 0.0;
  }

  if (!ready_ || !runtime_ || !runtime_->mesh) {
    return;
  }

  runtime_->mesh->getAdvertLocation(enabled, latitude, longitude);
}

bool MeshAdapter::broadcastSelfAdvertNow() {
  if (!ready_ || !runtime_ || !runtime_->mesh) {
    return false;
  }

  runtime_->mesh->broadcastSelfAdvert();
  last_advert_ms_ = millis();
  return true;
}

bool MeshAdapter::broadcastSelfAdvertFloodNow() {
  if (!ready_ || !runtime_ || !runtime_->mesh) {
    return false;
  }

  runtime_->mesh->broadcastSelfAdvertFlood();
  last_advert_ms_ = millis();
  return true;
}

int MeshAdapter::exportChannels(char names[][32], int max_names) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !names || max_names <= 0) {
    return 0;
  }

  int exported = 0;
  ChannelDetails channel{};
  for (int i = 0; i < MAX_GROUP_CHANNELS && exported < max_names; i++) {
    if (!runtime_->mesh->getChannel(i, channel)) {
      continue;
    }
    if (channel.name[0] == '\0') {
      continue;
    }

    strncpy(names[exported], channel.name, 31);
    names[exported][31] = '\0';
    exported++;
  }

  return exported;
}

int MeshAdapter::exportChannelConfigs(MeshChannelConfig configs[], int max_configs) const {
  if (!ready_ || !runtime_ || !runtime_->mesh || !configs || max_configs <= 0) {
    return 0;
  }

  int exported = 0;
  ChannelDetails channel{};
  for (int i = 0; i < MAX_GROUP_CHANNELS && exported < max_configs; i++) {
    if (!runtime_->mesh->getChannel(i, channel)) {
      continue;
    }
    if (channel.name[0] == '\0') {
      continue;
    }

    MeshChannelConfig cfg{};
    strncpy(cfg.name, channel.name, sizeof(cfg.name) - 1);
    cfg.name[sizeof(cfg.name) - 1] = '\0';

    size_t secret_len = sizeof(channel.channel.secret);
    bool upper_zero = true;
    for (size_t j = 16; j < sizeof(channel.channel.secret); j++) {
      if (channel.channel.secret[j] != 0) {
        upper_zero = false;
        break;
      }
    }
    if (upper_zero) {
      secret_len = 16;
    }

    if (encodeBase64(channel.channel.secret, secret_len, cfg.psk_base64, sizeof(cfg.psk_base64)) == 0) {
      continue;
    }

    configs[exported++] = cfg;
  }

  return exported;
}

int MeshAdapter::exportContacts(MeshContactSummary contacts[], int max_contacts) const {
  if (!ready_ || !runtime_ || !runtime_->mesh || !contacts || max_contacts <= 0) {
    return 0;
  }

  int exported = 0;
  ContactInfo contact{};
  for (int i = 0; i < runtime_->mesh->getNumContacts() && exported < max_contacts; i++) {
    if (!runtime_->mesh->getContactByIdx(static_cast<uint32_t>(i), contact)) {
      continue;
    }

    MeshContactSummary summary{};
    if (contact.name[0] != '\0') {
      strncpy(summary.name, contact.name, sizeof(summary.name) - 1);
      summary.name[sizeof(summary.name) - 1] = '\0';
    } else {
      strncpy(summary.name, "(unnamed)", sizeof(summary.name) - 1);
      summary.name[sizeof(summary.name) - 1] = '\0';
    }

    if (!bytesToHex(contact.id.pub_key, PUB_KEY_SIZE, summary.public_key_hex, sizeof(summary.public_key_hex))) {
      continue;
    }

    summary.favorite = (contact.flags & 0x01U) != 0;
    summary.type = contact.type;
    summary.lastmod = contact.lastmod;
    summary.gps_lat_i = contact.gps_lat;
    summary.gps_lon_i = contact.gps_lon;
    contacts[exported++] = summary;
  }

  return exported;
}

bool MeshAdapter::setContactFavoriteByPublicKeyHex(const char* public_key_hex, bool favorite) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !public_key_hex) {
    return false;
  }

  uint8_t pub_key[PUB_KEY_SIZE] = {};
  if (!hexToBytes(public_key_hex, pub_key, sizeof(pub_key))) {
    return false;
  }

  ContactInfo* contact = runtime_->mesh->lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!contact) {
    return false;
  }

  if (favorite) {
    contact->flags |= 0x01U;
  } else {
    contact->flags &= static_cast<uint8_t>(~0x01U);
  }
  contact->lastmod = runtime_->rtc_clock.getCurrentTimeUnique();

  markContactsDirty();
  if (!saveContactsToFs()) {
    return false;
  }
  contacts_dirty_ = false;
  return true;
}

bool MeshAdapter::removeContactByPublicKeyHex(const char* public_key_hex) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !public_key_hex) {
    return false;
  }

  uint8_t pub_key[PUB_KEY_SIZE] = {};
  if (!hexToBytes(public_key_hex, pub_key, sizeof(pub_key))) {
    return false;
  }

  ContactInfo* contact = runtime_->mesh->lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!contact) {
    return false;
  }

  if (!runtime_->mesh->removeContact(*contact)) {
    return false;
  }

  markContactsDirty();
  if (!saveContactsToFs()) {
    return false;
  }
  contacts_dirty_ = false;
  return true;
}

bool MeshAdapter::addChannel(const char* channel_name, const char* psk_base64) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !channel_name || channel_name[0] == '\0') {
    return false;
  }

  if (false) Serial.printf("[PERSIST][CH] add request name=%s\n", channel_name);

  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    runtime_->mesh->getChannel(i, g_channel_rollback_snapshot[i]);
  }

  if (!runtime_->mesh->upsertChannel(channel_name, psk_base64)) {
    return false;
  }

  markChannelsDirty();
  if (!saveChannelsToFs()) {
    if (false) Serial.printf("[PERSIST][CH] add persist failed name=%s\n", channel_name);
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      runtime_->mesh->setChannel(i, g_channel_rollback_snapshot[i]);
    }
    channels_dirty_ = false;
    return false;
  }
  if (false) Serial.printf("[PERSIST][CH] add persist ok name=%s\n", channel_name);
  channels_dirty_ = false;
  return true;
}

bool MeshAdapter::removeChannel(const char* channel_name) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !channel_name || channel_name[0] == '\0') {
    return false;
  }

  if (false) Serial.printf("[PERSIST][CH] remove request name=%s\n", channel_name);

  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    runtime_->mesh->getChannel(i, g_channel_rollback_snapshot[i]);
  }

  if (!runtime_->mesh->removeChannelByName(channel_name)) {
    return false;
  }

  markChannelsDirty();
  if (!saveChannelsToFs()) {
    if (false) Serial.printf("[PERSIST][CH] remove persist failed name=%s\n", channel_name);
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      runtime_->mesh->setChannel(i, g_channel_rollback_snapshot[i]);
    }
    channels_dirty_ = false;
    return false;
  }
  if (false) Serial.printf("[PERSIST][CH] remove persist ok name=%s\n", channel_name);
  channels_dirty_ = false;
  return true;
}

void MeshAdapter::getRadioStats(MeshRadioStats* out_stats) const {
  if (!out_stats) {
    return;
  }

  out_stats->rx_raw_count = rx_raw_count_;
  out_stats->rx_packet_count = rx_packet_count_;
  out_stats->last_rx_ms = last_rx_ms_;
}

size_t MeshAdapter::drainEvents(MeshEvent* out_events, size_t max_events) {
  if (!out_events || max_events == 0) {
    return 0;
  }

  size_t copied = 0;
  while (copied < max_events && event_count_ > 0) {
    out_events[copied++] = event_queue_[event_tail_];
    event_tail_ = (event_tail_ + 1) % kMaxQueuedEvents;
    event_count_--;
  }
  return copied;
}

void MeshAdapter::queueInfo(const char* text) {
  if (!text || text[0] == '\0') {
    return;
  }

  if (event_count_ == kMaxQueuedEvents) {
    // Do not evict DM/channel events for informational chatter.
    return;
  }

  MeshEvent& evt = event_queue_[event_head_];
  evt.type = MeshEventType::Info;
  evt.channel_name[0] = '\0';
  evt.peer_key[0] = '\0';
  strncpy(evt.text, text, sizeof(evt.text) - 1);
  evt.text[sizeof(evt.text) - 1] = '\0';

  event_head_ = (event_head_ + 1) % kMaxQueuedEvents;
  event_count_++;
}

void MeshAdapter::queueChannelMessage(const char* channel_name, const char* text) {
  if (!channel_name || channel_name[0] == '\0' || !text || text[0] == '\0') {
    return;
  }

  if (event_count_ == kMaxQueuedEvents) {
    event_tail_ = (event_tail_ + 1) % kMaxQueuedEvents;
    event_count_--;
  }

  MeshEvent& evt = event_queue_[event_head_];
  evt.type = MeshEventType::ChannelMessage;
  strncpy(evt.channel_name, channel_name, sizeof(evt.channel_name) - 1);
  evt.channel_name[sizeof(evt.channel_name) - 1] = '\0';
  evt.peer_key[0] = '\0';
  strncpy(evt.text, text, sizeof(evt.text) - 1);
  evt.text[sizeof(evt.text) - 1] = '\0';

  event_head_ = (event_head_ + 1) % kMaxQueuedEvents;
  event_count_++;
}

void MeshAdapter::queueDirectMessage(const char* contact_name, const char* contact_key, const char* text) {
  if (!text || text[0] == '\0') {
    return;
  }

  const char* safe_name = (contact_name && contact_name[0] != '\0') ? contact_name : "(unnamed)";

  if (event_count_ == kMaxQueuedEvents) {
    event_tail_ = (event_tail_ + 1) % kMaxQueuedEvents;
    event_count_--;
  }

  MeshEvent& evt = event_queue_[event_head_];
  evt.type = MeshEventType::DirectMessage;
  strncpy(evt.channel_name, safe_name, sizeof(evt.channel_name) - 1);
  evt.channel_name[sizeof(evt.channel_name) - 1] = '\0';
  if (contact_key && contact_key[0] != '\0') {
    strncpy(evt.peer_key, contact_key, sizeof(evt.peer_key) - 1);
    evt.peer_key[sizeof(evt.peer_key) - 1] = '\0';
  } else {
    evt.peer_key[0] = '\0';
  }
  strncpy(evt.text, text, sizeof(evt.text) - 1);
  evt.text[sizeof(evt.text) - 1] = '\0';

  event_head_ = (event_head_ + 1) % kMaxQueuedEvents;
  event_count_++;
}

void MeshAdapter::noteRxRaw() {
  rx_raw_count_++;
  last_rx_ms_ = millis();
}

void MeshAdapter::noteRxPacket() {
  rx_packet_count_++;
  last_rx_ms_ = millis();
}

void MeshAdapter::markContactsDirty() {
  contacts_dirty_ = true;
}

void MeshAdapter::markChannelsDirty() {
  channels_dirty_ = true;
}

bool MeshAdapter::loadContactsFromFs() {
  const int available = loadContactsSnapshotFromNvs(g_contacts_nvs_buf, MAX_CONTACTS + MAX_ANON_CONTACTS);
  if (available <= 0) {
    return false;
  }

  int loaded = 0;
  for (int i = 0; i < available; i++) {
    const PersistedContact& record = g_contacts_nvs_buf[i];
    ContactInfo contact{};
    contact.id = ::mesh::Identity(record.pub_key);
    strncpy(contact.name, record.name, sizeof(contact.name) - 1);
    contact.name[sizeof(contact.name) - 1] = '\0';
    contact.type = record.type;
    contact.flags = record.flags;
    contact.out_path_len = record.out_path_len;
    memcpy(contact.out_path, record.out_path, sizeof(contact.out_path));
    contact.last_advert_timestamp = record.last_advert_timestamp;
    contact.lastmod = record.lastmod;
    contact.gps_lat = record.gps_lat;
    contact.gps_lon = record.gps_lon;
    contact.sync_since = 0;
    contact.shared_secret_valid = false;

    if (runtime_->mesh->addContact(contact)) {
      loaded++;
    }
  }

  if (loaded > 0) {
    char info[96];
    snprintf(info, sizeof(info), "Loaded %d contacts", loaded);
    queueInfo(info);
  }
  return loaded > 0;
}

bool MeshAdapter::saveContactsToFs() {
  int persisted = 0;
  memset(g_contacts_nvs_buf, 0, sizeof(g_contacts_nvs_buf));

  ContactInfo contact{};
  for (int i = 0; i < runtime_->mesh->getNumContacts(); i++) {
    if (persisted >= (MAX_CONTACTS + MAX_ANON_CONTACTS)) {
      break;
    }

    if (!runtime_->mesh->getContactByIdx(static_cast<uint32_t>(i), contact)) {
      continue;
    }

    PersistedContact& record = g_contacts_nvs_buf[persisted];
    memcpy(record.pub_key, contact.id.pub_key, sizeof(record.pub_key));
    strncpy(record.name, contact.name, sizeof(record.name) - 1);
    record.type = contact.type;
    record.flags = contact.flags;
    record.out_path_len = contact.out_path_len;
    memcpy(record.out_path, contact.out_path, sizeof(record.out_path));
    record.last_advert_timestamp = contact.last_advert_timestamp;
    record.lastmod = contact.lastmod;
    record.gps_lat = contact.gps_lat;
    record.gps_lon = contact.gps_lon;
    persisted++;
  }

  Preferences prefs;
  if (!prefs.begin(kMeshPrefsNs, false)) {
    queueInfo("Failed to open NVS for contacts");
    if (false) Serial.println("[PERSIST][CT] NVS open failed for write");
    return false;
  }

  bool ok = true;
  const size_t blob_len = static_cast<size_t>(persisted) * sizeof(PersistedContact);
  if (persisted > 0) {
    const size_t wrote = prefs.putBytes(kContactsBlobKey, g_contacts_nvs_buf, blob_len);
    if (wrote != blob_len) {
      ok = false;
    }
  } else if (prefs.isKey(kContactsBlobKey)) {
    if (!prefs.remove(kContactsBlobKey)) {
      ok = false;
    }
  }

  prefs.putUShort(kContactsCountKey, static_cast<uint16_t>(persisted));
  prefs.end();

  char info[96];
  snprintf(info, sizeof(info), "Saved %d contacts", persisted);
  queueInfo(info);
  if (!ok) {
    if (false) Serial.println("[PERSIST][CT] NVS write failed");
  } else {
    if (false) Serial.printf("[PERSIST][CT] NVS saved=%d\n", persisted);
  }
  return ok;
}

bool MeshAdapter::loadChannelsFromFs() {
  const int nvs_count = loadChannelsSnapshotFromNvs(g_channels_nvs_buf, MAX_GROUP_CHANNELS);
  if (false) Serial.printf("[PERSIST][CH] snapshot nvs=%d\n", nvs_count);

  if (nvs_count <= 0) {
    if (false) Serial.println("[PERSIST][CH] no persisted channels found");
    return false;
  }

  const int loaded = applyChannelSnapshot(runtime_ ? runtime_->mesh : nullptr, g_channels_nvs_buf, nvs_count);
  if (loaded <= 0) {
    if (false) Serial.printf("[PERSIST][CH] apply snapshot failed count=%d\n", nvs_count);
    return false;
  }

  char info[96];
  snprintf(info, sizeof(info), "Loaded %d channels (NVS)", loaded);
  queueInfo(info);
  if (false) Serial.printf("[PERSIST][CH] loaded=%d source=NVS\n", loaded);

  return true;
}

bool MeshAdapter::saveChannelsToFs() {
  int expected = 0;
  memset(g_channels_nvs_buf, 0, sizeof(g_channels_nvs_buf));
  int nvs_count = 0;
  ChannelDetails channel{};
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!runtime_->mesh->getChannel(i, channel)) {
      continue;
    }
    if (channel.name[0] == '\0') {
      continue;
    }

    expected++;

    PersistedChannel record{};
    strncpy(record.name, channel.name, sizeof(record.name) - 1);
    memcpy(record.secret, channel.channel.secret, sizeof(record.secret));

    if (nvs_count < MAX_GROUP_CHANNELS) {
      g_channels_nvs_buf[nvs_count++] = record;
    }
  }

  char info[96];
  snprintf(info, sizeof(info), "Saved %d/%d channels", nvs_count, expected);
  queueInfo(info);
  if (false) Serial.printf("[PERSIST][CH] save nvs=%d expected=%d\n", nvs_count, expected);

  Preferences prefs;
  if (prefs.begin(kMeshPrefsNs, false)) {
    bool nvs_ok = true;

    if (nvs_count > 0) {
      const size_t blob_len = static_cast<size_t>(nvs_count) * sizeof(PersistedChannel);
      const size_t wrote_blob = prefs.putBytes(kChannelsBlobKey, g_channels_nvs_buf, blob_len);
      if (wrote_blob != blob_len) {
        nvs_ok = false;
        if (false) Serial.printf("[PERSIST][CH] NVS blob write failed wrote=%u expected=%u\n",
                      static_cast<unsigned>(wrote_blob), static_cast<unsigned>(blob_len));
      }
    } else if (prefs.isKey(kChannelsBlobKey)) {
      if (!prefs.remove(kChannelsBlobKey)) {
        nvs_ok = false;
        if (false) Serial.println("[PERSIST][CH] NVS blob remove failed");
      }
    }

    const size_t wrote_count = prefs.putUShort(kChannelsCountKey, static_cast<uint16_t>(nvs_count));
    if (wrote_count != sizeof(uint16_t)) {
      nvs_ok = false;
      if (false) Serial.println("[PERSIST][CH] NVS count write failed");
    }

    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      char key_name[12] = {};
      char key_secret[12] = {};
      snprintf(key_name, sizeof(key_name), "ch_name_%d", i);
      snprintf(key_secret, sizeof(key_secret), "ch_sec_%d", i);

      if (i < nvs_count) {
        if (prefs.putString(key_name, g_channels_nvs_buf[i].name) == 0) {
          nvs_ok = false;
        }
        const size_t wrote = prefs.putBytes(key_secret, g_channels_nvs_buf[i].secret,
                                            sizeof(g_channels_nvs_buf[i].secret));
        if (wrote != sizeof(g_channels_nvs_buf[i].secret)) {
          nvs_ok = false;
        }
      } else {
        if (prefs.isKey(key_name)) {
          if (!prefs.remove(key_name)) {
            nvs_ok = false;
          }
        }
        if (prefs.isKey(key_secret)) {
          if (!prefs.remove(key_secret)) {
            nvs_ok = false;
          }
        }
      }
    }

    prefs.end();
    if (!nvs_ok) {
      queueInfo("Warning: NVS channel backup had write errors");
      if (false) Serial.println("[PERSIST][CH] NVS backup write errors");
      return false;
    } else {
      if (false) Serial.printf("[PERSIST][CH] NVS backup saved=%d\n", nvs_count);
      return true;
    }
  } else {
    queueInfo("Warning: failed to open NVS channel backup");
    if (false) Serial.println("[PERSIST][CH] NVS open failed for write");
    return false;
  }
}

}  // namespace mesh
}  // namespace plumeria
