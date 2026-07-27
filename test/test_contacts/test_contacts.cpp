// Host-side tests for the contact capacity policy and CSV field escaping.
// Run with: ~/.platformio/penv/bin/pio test -e native
#include <string.h>
#include <unity.h>

#include "mesh/contact_capacity.h"
#include "web/csv_field.h"

using plumeria::mesh::ContactSlot;
using plumeria::mesh::selectFifoEvictionIndex;
using plumeria::web::csvEscapeField;

void test_evicts_oldest_non_favorite(void) {
  const ContactSlot slots[] = {
      {false, 300},
      {false, 100},
      {false, 200},
  };
  TEST_ASSERT_EQUAL_INT(1, selectFifoEvictionIndex(slots, 3));
}

void test_favorites_are_never_evicted(void) {
  // The oldest two contacts are favorites, so the newest non-favorite goes.
  const ContactSlot slots[] = {
      {true, 10},
      {true, 20},
      {false, 900},
  };
  TEST_ASSERT_EQUAL_INT(2, selectFifoEvictionIndex(slots, 3));
}

void test_all_favorites_reports_no_candidate(void) {
  const ContactSlot slots[] = {
      {true, 10},
      {true, 20},
  };
  TEST_ASSERT_EQUAL_INT(-1, selectFifoEvictionIndex(slots, 2));
}

void test_ties_resolve_to_insert_order(void) {
  // Unknown first-seen (0) sorts oldest; the lower index wins a tie.
  const ContactSlot slots[] = {
      {false, 0},
      {false, 0},
      {false, 5},
  };
  TEST_ASSERT_EQUAL_INT(0, selectFifoEvictionIndex(slots, 3));
}

void test_empty_table_has_no_candidate(void) {
  const ContactSlot slots[] = {{false, 1}};
  TEST_ASSERT_EQUAL_INT(-1, selectFifoEvictionIndex(slots, 0));
  TEST_ASSERT_EQUAL_INT(-1, selectFifoEvictionIndex(nullptr, 3));
  (void)slots;
}

// Models the runtime insert path: MeshCore appends new contacts and compacts
// the array on removal, and MeshAdapter::ensureContactCapacityForInsert() runs
// the FIFO policy in a loop until a slot is free.
struct ContactTable {
  ContactSlot slots[256];
  int count = 0;
  int limit = 8;
  int evicted = 0;
  int rejected = 0;

  bool insert(bool favorite, uint32_t first_seen) {
    while (count >= limit) {
      const int victim = plumeria::mesh::selectFifoEvictionIndexBy(count, [this](int i) { return slots[i]; });
      if (victim < 0) {
        rejected++;
        return false;
      }
      for (int i = victim; i < count - 1; i++) {
        slots[i] = slots[i + 1];
      }
      count--;
      evicted++;
    }
    slots[count].favorite = favorite;
    slots[count].first_seen = first_seen;
    count++;
    return true;
  }

  int favoriteCount() const {
    int n = 0;
    for (int i = 0; i < count; i++) {
      if (slots[i].favorite) {
        n++;
      }
    }
    return n;
  }

  bool holds(uint32_t first_seen) const {
    for (int i = 0; i < count; i++) {
      if (slots[i].first_seen == first_seen) {
        return true;
      }
    }
    return false;
  }
};

void test_count_never_exceeds_limit_under_pressure(void) {
  ContactTable table;
  for (uint32_t i = 1; i <= 200; i++) {
    TEST_ASSERT_TRUE(table.insert(false, i));
    TEST_ASSERT_TRUE(table.count <= table.limit);
  }
  TEST_ASSERT_EQUAL_INT(table.limit, table.count);
  // Only the newest `limit` contacts survive; the oldest went first.
  TEST_ASSERT_FALSE(table.holds(1));
  TEST_ASSERT_TRUE(table.holds(200));
}

void test_favorites_survive_sustained_pressure(void) {
  ContactTable table;
  table.insert(true, 1);   // oldest contact in the table, but favorited
  table.insert(true, 2);
  table.insert(false, 3);
  for (uint32_t i = 100; i < 300; i++) {
    TEST_ASSERT_TRUE(table.insert(false, i));
    TEST_ASSERT_EQUAL_INT(2, table.favoriteCount());
  }
  TEST_ASSERT_TRUE(table.holds(1));
  TEST_ASSERT_TRUE(table.holds(2));
  TEST_ASSERT_FALSE(table.holds(3));  // the non-favorite was evicted
  TEST_ASSERT_EQUAL_INT(table.limit, table.count);
}

void test_full_of_favorites_rejects_instead_of_evicting(void) {
  ContactTable table;
  for (uint32_t i = 1; i <= 8; i++) {
    TEST_ASSERT_TRUE(table.insert(true, i));
  }
  TEST_ASSERT_EQUAL_INT(table.limit, table.count);

  TEST_ASSERT_FALSE(table.insert(false, 99));
  TEST_ASSERT_FALSE(table.insert(true, 99));
  TEST_ASSERT_EQUAL_INT(table.limit, table.count);
  TEST_ASSERT_EQUAL_INT(8, table.favoriteCount());
  TEST_ASSERT_EQUAL_INT(0, table.evicted);
  TEST_ASSERT_EQUAL_INT(2, table.rejected);
  for (uint32_t i = 1; i <= 8; i++) {
    TEST_ASSERT_TRUE(table.holds(i));
  }
}

void test_unfavoriting_frees_a_slot_again(void) {
  ContactTable table;
  for (uint32_t i = 1; i <= 8; i++) {
    table.insert(true, i);
  }
  TEST_ASSERT_FALSE(table.insert(false, 50));

  table.slots[3].favorite = false;  // user unfavorites one contact
  TEST_ASSERT_TRUE(table.insert(false, 50));
  TEST_ASSERT_FALSE(table.holds(4));  // the freshly unfavorited entry went
  TEST_ASSERT_EQUAL_INT(7, table.favoriteCount());
}

void test_csv_plain_field_is_unquoted(void) {
  char out[32] = {};
  TEST_ASSERT_EQUAL_UINT(5, csvEscapeField("Alice", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Alice", out);
}

void test_csv_quotes_commas(void) {
  char out[32] = {};
  csvEscapeField("Smith, Bob", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("\"Smith, Bob\"", out);
}

void test_csv_doubles_embedded_quotes(void) {
  char out[32] = {};
  csvEscapeField("say \"hi\"", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("\"say \"\"hi\"\"\"", out);
}

void test_csv_quotes_newlines(void) {
  char out[32] = {};
  csvEscapeField("line1\r\nline2", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("\"line1\r\nline2\"", out);
}

void test_csv_overflow_yields_empty_field(void) {
  char out[4] = {"xxx"};
  TEST_ASSERT_EQUAL_UINT(0, csvEscapeField("too long for buffer", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_csv_null_and_empty_inputs(void) {
  char out[8] = {"junk"};
  TEST_ASSERT_EQUAL_UINT(0, csvEscapeField(nullptr, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL_UINT(0, csvEscapeField("", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_evicts_oldest_non_favorite);
  RUN_TEST(test_favorites_are_never_evicted);
  RUN_TEST(test_all_favorites_reports_no_candidate);
  RUN_TEST(test_ties_resolve_to_insert_order);
  RUN_TEST(test_empty_table_has_no_candidate);
  RUN_TEST(test_count_never_exceeds_limit_under_pressure);
  RUN_TEST(test_favorites_survive_sustained_pressure);
  RUN_TEST(test_full_of_favorites_rejects_instead_of_evicting);
  RUN_TEST(test_unfavoriting_frees_a_slot_again);
  RUN_TEST(test_csv_plain_field_is_unquoted);
  RUN_TEST(test_csv_quotes_commas);
  RUN_TEST(test_csv_doubles_embedded_quotes);
  RUN_TEST(test_csv_quotes_newlines);
  RUN_TEST(test_csv_overflow_yields_empty_field);
  RUN_TEST(test_csv_null_and_empty_inputs);
  return UNITY_END();
}
