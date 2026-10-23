// BF-018A Rev.5
// serial output with a copy kept for the web UI
//
#pragma once
#include <Arduino.h>

// Prints to Serial and keeps the latest output in a ring buffer the web UI reads.
// Safe to use from the Ticker task and from loop().
class LogTee : public Print {
 public:
  static const size_t m_size = 4096;  // bytes kept

  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Copy the kept output from position `from` (bytes written since boot) into `buffer`.
  // If `from` is no longer kept, or is ahead (e.g. the page saw an earlier boot), copying starts at the oldest byte kept.
  // `first` receives the position actually copied from. Returns the number of bytes copied.
  size_t Read(uint32_t from, char *buffer, size_t buffer_size, uint32_t *first);

 private:
  char     m_buffer[m_size];
  uint32_t m_total = 0;  // bytes written since boot
};

extern LogTee Log;
