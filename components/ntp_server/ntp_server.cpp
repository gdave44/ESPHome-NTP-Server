#include "ntp_server.hpp"
#include "esphome/core/log.h"
#include "../gps_pps/gps_pps.h"
#include <sys/time.h>
#include <cerrno>
#include <cstring>

#ifdef USE_ESP_IDF
#include <fcntl.h>
#include <unistd.h>
#endif

namespace esphome {
namespace ntp_server {

static const char *const TAG = "ntp_server";
static const uint32_t NTP_UNIX_OFFSET = 2208988800UL;
static const int NTP_PACKET_SIZE = 48;

// ---- Platform-specific setup / loop ----

#ifdef USE_ESP_IDF

void NTP_Server::setup() {
  ESP_LOGI(TAG, "Setting up NTP server on port %u (ESP-IDF)...", this->port_);

  this->socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (this->socket_fd_ < 0) {
    ESP_LOGE(TAG, "Failed to create UDP socket: %d", errno);
    this->mark_failed();
    return;
  }

  int flags = fcntl(this->socket_fd_, F_GETFL, 0);
  fcntl(this->socket_fd_, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(this->port_);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(this->socket_fd_, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind port %u: %d", this->port_, errno);
    close(this->socket_fd_);
    this->socket_fd_ = -1;
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "NTP server listening on port %u", this->port_);
}

void NTP_Server::loop() {
  if (this->socket_fd_ < 0)
    return;

  uint8_t buf[NTP_PACKET_SIZE];
  struct sockaddr_in client{};
  socklen_t client_len = sizeof(client);

  int n = recvfrom(this->socket_fd_, buf, sizeof(buf), 0,
                   (struct sockaddr *) &client, &client_len);
  if (n < NTP_PACKET_SIZE)
    return;

  if (!this->is_time_synchronized_()) {
    ESP_LOGD(TAG, "NTP request dropped — time not synchronized");
    return;
  }

  uint8_t response[NTP_PACKET_SIZE];
  this->build_ntp_response_(buf, response);
  sendto(this->socket_fd_, response, NTP_PACKET_SIZE, 0,
         (struct sockaddr *) &client, client_len);

  ESP_LOGD(TAG, "NTP response sent");
}

#else  // Arduino

void NTP_Server::setup() {
  ESP_LOGI(TAG, "Setting up NTP server on port %u (Arduino)...", this->port_);
  if (!this->udp_.begin(this->port_)) {
    ESP_LOGE(TAG, "Failed to bind UDP to port %u", this->port_);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "NTP server listening on port %u", this->port_);
}

void NTP_Server::loop() {
  if (this->udp_.parsePacket() < NTP_PACKET_SIZE)
    return;

  uint8_t buf[NTP_PACKET_SIZE];
  this->udp_.read(buf, NTP_PACKET_SIZE);

  uint8_t response[NTP_PACKET_SIZE];
  this->build_ntp_response_(buf, response);

  this->udp_.beginPacket(this->udp_.remoteIP(), this->udp_.remotePort());
  this->udp_.write(response, NTP_PACKET_SIZE);
  this->udp_.endPacket();

  ESP_LOGD(TAG, "NTP response sent");
}

#endif  // USE_ESP_IDF

// ---- Shared NTP logic ----

NTPTimestamp NTP_Server::get_ntp_timestamp_() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  NTPTimestamp ts;
  ts.seconds = static_cast<uint32_t>(tv.tv_sec) + NTP_UNIX_OFFSET;
  // Convert µs to NTP 32-bit fraction: usec * 2^32 / 1,000,000
  ts.fraction = static_cast<uint32_t>(
      (static_cast<uint64_t>(tv.tv_usec) << 32) / 1000000ULL);
  return ts;
}

void NTP_Server::build_ntp_response_(const uint8_t *request, uint8_t *response) {
  // Capture receive timestamp immediately
  NTPTimestamp recv_ts = this->get_ntp_timestamp_();

  memset(response, 0, NTP_PACKET_SIZE);

  bool synced = this->is_time_synchronized_();

  // Byte 0: LI | VN | Mode
  //   LI=0 (no warning) or LI=3 (unsynchronized), VN=4, Mode=4 (server)
  response[0] = synced ? 0x24 : 0xE4;

  // Stratum: 1 = primary reference (GPS), 16 = unsynchronized
  response[1] = synced ? 1 : 16;

  // Poll: echo client's value
  response[2] = request[2];

  // Precision: 2^-20 ≈ 1 µs (matches gettimeofday resolution on ESP32)
  response[3] = static_cast<uint8_t>(-20 & 0xFF);

  // Root delay and root dispersion: negligible for a GPS primary reference
  // (4 bytes each, big-endian fixed-point, left as zero)

  // Reference ID: "GPS\0" for a GPS primary source
  response[12] = 'G';
  response[13] = 'P';
  response[14] = 'S';
  response[15] = '\0';

  // Reference timestamp (when the clock was last set)
  response[16] = (recv_ts.seconds >> 24) & 0xFF;
  response[17] = (recv_ts.seconds >> 16) & 0xFF;
  response[18] = (recv_ts.seconds >>  8) & 0xFF;
  response[19] =  recv_ts.seconds        & 0xFF;
  response[20] = (recv_ts.fraction >> 24) & 0xFF;
  response[21] = (recv_ts.fraction >> 16) & 0xFF;
  response[22] = (recv_ts.fraction >>  8) & 0xFF;
  response[23] =  recv_ts.fraction        & 0xFF;

  // Originate timestamp: copy client's transmit timestamp verbatim
  memcpy(&response[24], &request[40], 8);

  // Receive timestamp
  response[32] = (recv_ts.seconds >> 24) & 0xFF;
  response[33] = (recv_ts.seconds >> 16) & 0xFF;
  response[34] = (recv_ts.seconds >>  8) & 0xFF;
  response[35] =  recv_ts.seconds        & 0xFF;
  response[36] = (recv_ts.fraction >> 24) & 0xFF;
  response[37] = (recv_ts.fraction >> 16) & 0xFF;
  response[38] = (recv_ts.fraction >>  8) & 0xFF;
  response[39] =  recv_ts.fraction        & 0xFF;

  // Transmit timestamp: captured as late as possible to minimise asymmetry
  NTPTimestamp xmit_ts = this->get_ntp_timestamp_();
  response[40] = (xmit_ts.seconds >> 24) & 0xFF;
  response[41] = (xmit_ts.seconds >> 16) & 0xFF;
  response[42] = (xmit_ts.seconds >>  8) & 0xFF;
  response[43] =  xmit_ts.seconds        & 0xFF;
  response[44] = (xmit_ts.fraction >> 24) & 0xFF;
  response[45] = (xmit_ts.fraction >> 16) & 0xFF;
  response[46] = (xmit_ts.fraction >>  8) & 0xFF;
  response[47] =  xmit_ts.fraction        & 0xFF;
}

bool NTP_Server::is_time_synchronized_() {
  if (this->time_source_ == nullptr)
    return true;
  return this->time_source_->is_synchronized();
}

void NTP_Server::dump_config() {
  ESP_LOGCONFIG(TAG, "NTP Server:");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Time source: %s",
                this->time_source_ != nullptr ? "gps_pps" : "system (no source set)");
}

}  // namespace ntp_server
}  // namespace esphome
