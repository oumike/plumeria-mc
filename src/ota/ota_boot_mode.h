#pragma once

namespace plumeria {
namespace ota {

// Persist a one-shot request to run OTA in minimal boot mode.
bool requestBootMode();

// Returns true once per stored request, and clears it.
bool consumeBootModeRequest();

}  // namespace ota
}  // namespace plumeria
