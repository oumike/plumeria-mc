#pragma once

#include <stddef.h>

namespace plumeria {
namespace web {

// Writes `value` into `out` as one RFC 4180 CSV field: fields containing a
// comma, double quote, CR or LF are wrapped in double quotes and embedded
// quotes are doubled. Returns the character count written (excluding the NUL);
// on overflow `out` is set to an empty string and 0 is returned.
inline size_t csvEscapeField(const char* value, char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return 0;
  }
  out[0] = '\0';
  if (!value) {
    return 0;
  }

  bool needs_quotes = false;
  size_t needed = 0;
  for (const char* p = value; *p != '\0'; p++) {
    if (*p == ',' || *p == '"' || *p == '\r' || *p == '\n') {
      needs_quotes = true;
    }
    needed += (*p == '"') ? 2 : 1;
  }
  if (needs_quotes) {
    needed += 2;
  }
  if (needed + 1 > out_size) {
    return 0;
  }

  size_t o = 0;
  if (needs_quotes) {
    out[o++] = '"';
  }
  for (const char* p = value; *p != '\0'; p++) {
    if (*p == '"' && needs_quotes) {
      out[o++] = '"';
    }
    out[o++] = *p;
  }
  if (needs_quotes) {
    out[o++] = '"';
  }
  out[o] = '\0';
  return o;
}

}  // namespace web
}  // namespace plumeria
