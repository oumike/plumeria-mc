#include "mesh/mesh_adapter.h"

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <stdio.h>
#include <string.h>

#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/ESP32Board.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/radiolib/CustomSX1262.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>

namespace {

constexpr char kDefaultNodeName[] = "Plumeria";
constexpr char kPublicChannelName[] = "Public";
constexpr char kPublicChannelPsk[] = "izOH6cXN6mrJ5e26oRXNcg==";
constexpr char kTestHashtagChannelName[] = "#rhino";

constexpr uint32_t kSendTimeoutBaseMs = 500;
constexpr float kFloodSendTimeoutFactor = 16.0f;
constexpr float kDirectSendPerHopFactor = 6.0f;
constexpr uint32_t kDirectSendPerHopExtraMs = 250;

constexpr uint32_t kAutoAdvertMs = 120000;
constexpr uint32_t kPersistFlushMs = 5000;

constexpr char kContactsPath[] = "/mesh_contacts.bin";
constexpr char kChannelsPath[] = "/mesh_channels.bin";

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
      : BaseChatMesh(radio, millis_clock, rng, rtc_clock, packet_manager, mesh_tables), adapter_(adapter) {
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
    ::mesh::Packet* packet = createSelfAdvert(node_name_);
    if (packet) {
      sendZeroHop(packet);
    }
  }

  void setNodeName(const char* name) {
    if (!name || name[0] == '\0') {
      return;
    }
    strncpy(node_name_, name, sizeof(node_name_) - 1);
    node_name_[sizeof(node_name_) - 1] = '\0';
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
    char info[96];
    snprintf(info, sizeof(info), "DM %s: %.64s", contact.name, text ? text : "");
    adapter_->queueInfo(info);
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

  void onChannelMessageRecv(const ::mesh::GroupChannel& channel, ::mesh::Packet* packet, uint32_t timestamp,
                            const char* text) override {
    (void)packet;
    (void)timestamp;
    char resolved_name[32] = {};
    resolveChannelName(channel, resolved_name, sizeof(resolved_name));
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
    Serial.printf("[RADIO][RAW] len=%d type=%u route=%u snr=%.1f rssi=%.1f hdr=0x%02X\n", len,
                  static_cast<unsigned>(type), static_cast<unsigned>(route), snr, rssi,
                  static_cast<unsigned>(header));
  }

  void logRx(::mesh::Packet* packet, int len, float score) override {
    if (!packet) {
      return;
    }

    adapter_->noteRxPacket();

    Serial.printf("[RADIO][PKT] len=%d type=%u route=%u score=%.3f snr=%.1f path_n=%u path_sz=%u\n", len,
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

    // Mirror Sigurd/MeshCore behavior: normalize to #name and derive secret via SHA-256(name).
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
    ::mesh::Utils::sha256(channel.channel.secret, sizeof(channel.channel.secret),
                          reinterpret_cast<const uint8_t*>(normalized), strlen(normalized));
    strncpy(channel.name, normalized, sizeof(channel.name) - 1);
    channel.name[sizeof(channel.name) - 1] = '\0';
    return setChannel(slot, channel);
  }

  MeshAdapter* adapter_;
  char node_name_[32];
};

struct MeshRuntime {
  MeshRuntime()
      : lora_spi(FSPI),
        radio_module(nullptr),
        radio_chip(nullptr),
        radio_driver(nullptr),
        packet_manager(16),
        mesh(nullptr) {}

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
};

bool MeshAdapter::begin(const hal::TloraPagerRadioConfig& radio_config) {
  if (ready_) {
    return true;
  }

  if (!SPIFFS.begin(true)) {
    queueInfo("SPIFFS mount failed");
    return false;
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

  IdentityStore store(SPIFFS, "/identity");
  store.begin();
  if (!store.load("_main", runtime_->mesh->self_id)) {
    runtime_->mesh->self_id = ::mesh::LocalIdentity(&runtime_->fast_rng);
    int attempts = 0;
    while (attempts < 10 &&
           (runtime_->mesh->self_id.pub_key[0] == 0x00 || runtime_->mesh->self_id.pub_key[0] == 0xFF)) {
      runtime_->mesh->self_id = ::mesh::LocalIdentity(&runtime_->fast_rng);
      attempts++;
    }
    store.save("_main", runtime_->mesh->self_id);
    queueInfo("Generated new mesh identity");
  } else {
    queueInfo("Loaded mesh identity");
  }

  runtime_->mesh->beginStandalone();

  bool loaded_channels = loadChannelsFromFs();
  if (!loaded_channels) {
    bool added_public = runtime_->mesh->ensurePublicChannel();
    bool added_rhino = runtime_->mesh->ensureHashtagChannel(kTestHashtagChannelName);
    if (added_public || added_rhino) {
      markChannelsDirty();
      queueInfo("Seeded default channels: Public, #rhino");
    }
  } else {
    bool added_public = runtime_->mesh->ensurePublicChannel();
    bool added_rhino = runtime_->mesh->ensureHashtagChannel(kTestHashtagChannelName);
    if (added_public || added_rhino) {
      markChannelsDirty();
      queueInfo("Added missing default channels");
    }
  }

  loadContactsFromFs();

  runtime_->mesh->broadcastSelfAdvert();

  char info[96];
  snprintf(info, sizeof(info), "Mesh init %.3fMHz SF%u BW%.1fkHz CR4/%u", radio_config.frequency_mhz,
           radio_config.spreading_factor, radio_config.bandwidth_khz, radio_config.coding_rate);
  queueInfo(info);

  Serial.println("[MESH] MeshCore standalone runtime initialized");
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
  runtime_->rtc_clock.tick();

  const uint32_t now = millis();
  if (now - last_advert_ms_ >= kAutoAdvertMs) {
    runtime_->mesh->broadcastSelfAdvert();
    last_advert_ms_ = now;
    queueInfo("Broadcast self advert");
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

  if (!runtime_->mesh->sendDirectByName(destination, text)) {
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

bool MeshAdapter::addChannel(const char* channel_name, const char* psk_base64) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !channel_name || channel_name[0] == '\0') {
    return false;
  }

  if (!runtime_->mesh->upsertChannel(channel_name, psk_base64)) {
    return false;
  }

  markChannelsDirty();
  return true;
}

bool MeshAdapter::removeChannel(const char* channel_name) {
  if (!ready_ || !runtime_ || !runtime_->mesh || !channel_name || channel_name[0] == '\0') {
    return false;
  }

  if (!runtime_->mesh->removeChannelByName(channel_name)) {
    return false;
  }

  markChannelsDirty();
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
    event_tail_ = (event_tail_ + 1) % kMaxQueuedEvents;
    event_count_--;
  }

  MeshEvent& evt = event_queue_[event_head_];
  evt.type = MeshEventType::Info;
  evt.channel_name[0] = '\0';
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
  File file = SPIFFS.open(kContactsPath, "r");
  if (!file) {
    return false;
  }

  int loaded = 0;
  PersistedContact record{};
  while (file.available() >= static_cast<int>(sizeof(record))) {
    if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record)) {
      break;
    }

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

  file.close();

  if (loaded > 0) {
    char info[96];
    snprintf(info, sizeof(info), "Loaded %d contacts", loaded);
    queueInfo(info);
  }
  return loaded > 0;
}

bool MeshAdapter::saveContactsToFs() {
  File file = SPIFFS.open(kContactsPath, "w", true);
  if (!file) {
    queueInfo("Failed to save contacts");
    return false;
  }

  int saved = 0;
  ContactInfo contact{};
  for (int i = 0; i < runtime_->mesh->getNumContacts(); i++) {
    if (!runtime_->mesh->getContactByIdx(static_cast<uint32_t>(i), contact)) {
      continue;
    }

    PersistedContact record{};
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

    if (file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record)) {
      saved++;
    }
  }

  file.close();

  char info[96];
  snprintf(info, sizeof(info), "Saved %d contacts", saved);
  queueInfo(info);
  return true;
}

bool MeshAdapter::loadChannelsFromFs() {
  File file = SPIFFS.open(kChannelsPath, "r");
  if (!file) {
    return false;
  }

  int loaded = 0;
  int slot = 0;
  PersistedChannel record{};
  while (slot < MAX_GROUP_CHANNELS && file.available() >= static_cast<int>(sizeof(record))) {
    if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record)) {
      break;
    }

    if (record.name[0] == '\0') {
      continue;
    }

    ChannelDetails channel{};
    strncpy(channel.name, record.name, sizeof(channel.name) - 1);
    channel.name[sizeof(channel.name) - 1] = '\0';
    memcpy(channel.channel.secret, record.secret, sizeof(channel.channel.secret));

    if (runtime_->mesh->setChannel(slot, channel)) {
      loaded++;
      slot++;
    }
  }

  file.close();

  if (loaded > 0) {
    char info[96];
    snprintf(info, sizeof(info), "Loaded %d channels", loaded);
    queueInfo(info);
  }
  return loaded > 0;
}

bool MeshAdapter::saveChannelsToFs() {
  File file = SPIFFS.open(kChannelsPath, "w", true);
  if (!file) {
    queueInfo("Failed to save channels");
    return false;
  }

  int saved = 0;
  ChannelDetails channel{};
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!runtime_->mesh->getChannel(i, channel)) {
      continue;
    }
    if (channel.name[0] == '\0') {
      continue;
    }

    PersistedChannel record{};
    strncpy(record.name, channel.name, sizeof(record.name) - 1);
    memcpy(record.secret, channel.channel.secret, sizeof(record.secret));

    if (file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record)) {
      saved++;
    }
  }

  file.close();

  char info[96];
  snprintf(info, sizeof(info), "Saved %d channels", saved);
  queueInfo(info);
  return true;
}

}  // namespace mesh
}  // namespace plumeria
