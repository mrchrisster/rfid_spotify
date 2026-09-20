#pragma once
#include <algorithm>
#include <stdint.h>
#include <vector>

// Candidate state is committed atomically only after discovery/validation succeeds.
class AlbumOrder {
  std::vector<uint16_t> values;
  size_t position = 0;
public:
  bool empty() const { return values.empty(); }
  bool exhausted() const { return position >= values.size(); }
  bool current(uint16_t& value) const {
    if (exhausted()) return false;
    value = values[position]; return true;
  }
  void commit(std::vector<uint16_t>& candidate) { values.swap(candidate); position = 0; }
  template<class Random> void reshuffle(Random random) {
    for (size_t i = values.size(); i > 1; --i) std::swap(values[i - 1], values[random() % i]);
    position = 0;
  }
  void played() { if (!exhausted()) ++position; }
  void clear() { values.clear(); position = 0; }
};
inline uint32_t remainingDelay(uint32_t now, uint32_t start, uint32_t duration) {
  uint32_t elapsed = now - start;
  return elapsed < duration ? duration - elapsed : 0;
}
