// BF-018A Rev.5
// serial output with a copy kept for the web UI
//
#include "BF_Log.h"

LogTee Log;

static portMUX_TYPE log_mux = portMUX_INITIALIZER_UNLOCKED;

size_t LogTee::write(uint8_t c)
{
  return write(&c, 1);
}

size_t LogTee::write(const uint8_t *buffer, size_t size)
{
  Serial.write(buffer, size);

  portENTER_CRITICAL(&log_mux);
  for (size_t i = 0; i < size; ++i) {
    m_buffer[m_total++ % m_size] = buffer[i];
  }
  portEXIT_CRITICAL(&log_mux);
  return size;
}

size_t LogTee::Read(uint32_t from, char *buffer, size_t buffer_size, uint32_t *first)
{
  portENTER_CRITICAL(&log_mux);
  uint32_t oldest = m_total > m_size ? m_total - m_size : 0;
  if (from < oldest || from > m_total) {
    from = oldest;
  }
  size_t length = m_total - from;
  if (length > buffer_size) {
    length = buffer_size;
  }
  size_t start = from % m_size;
  size_t part  = m_size - start;  // bytes up to the end of the ring
  if (part > length) {
    part = length;
  }
  memcpy(buffer, m_buffer + start, part);
  memcpy(buffer + part, m_buffer, length - part);
  portEXIT_CRITICAL(&log_mux);

  *first = from;
  return length;
}
