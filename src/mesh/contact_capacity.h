#pragma once

#include <stdint.h>

// Upper bound on contacts kept in memory (and therefore in NVS). When the mesh
// is at capacity a new contact FIFO-evicts the oldest non-favorite one, so
// memory stays predictable on constrained targets. Override per environment
// with -DPLUMERIA_MAX_CONTACTS=<n>; the adapter additionally clamps this to
// MeshCore's compile-time MAX_CONTACTS.
#ifndef PLUMERIA_MAX_CONTACTS
#define PLUMERIA_MAX_CONTACTS 128
#endif

namespace plumeria {
namespace mesh {

constexpr int kContactLimitDefault = PLUMERIA_MAX_CONTACTS;

// One contact as the eviction policy sees it.
struct ContactSlot {
  bool favorite;
  uint32_t first_seen;  // first-seen timestamp; 0 when unknown (treated as oldest)
};

// Returns the index of the oldest non-favorite contact (lowest first_seen), or
// -1 when every contact is favorited and nothing may be purged. Ties resolve to
// the lower index, which is insert order in the MeshCore contacts array.
// `slot_at(i)` yields contact i, so callers can read the live contacts table
// without materializing a scratch copy of it.
template <typename SlotAccessor>
inline int selectFifoEvictionIndexBy(int count, SlotAccessor slot_at) {
  int oldest = -1;
  uint32_t oldest_first_seen = 0;
  for (int i = 0; i < count; i++) {
    const ContactSlot slot = slot_at(i);
    if (slot.favorite) {
      continue;
    }
    if (oldest < 0 || slot.first_seen < oldest_first_seen) {
      oldest = i;
      oldest_first_seen = slot.first_seen;
    }
  }
  return oldest;
}

inline int selectFifoEvictionIndex(const ContactSlot* slots, int count) {
  if (!slots) {
    return -1;
  }
  return selectFifoEvictionIndexBy(count, [slots](int i) { return slots[i]; });
}

}  // namespace mesh
}  // namespace plumeria
