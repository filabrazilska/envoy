#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/optional.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/logger.h"
#include "common/event/libevent.h"
#include "common/network/filter_manager_impl.h"

namespace Envoy {
namespace Network {

/**
 * Utility functions for the connection implementation.
 */
class ConnectionImplUtility {
public:
  /**
   * Update the buffer stats for a connection.
   * @param delta supplies the data read/written.
   * @param new_total supplies the final total buffer size.
   * @param previous_total supplies the previous final total buffer size. previous_total will be
   *        updated to new_total when the call is complete.
   * @param stat_total supplies the counter to increment with the delta.
   * @param stat_current supplies the guage that should be updated with the delta of previous_total
   *        and new_total.
   */
  static void updateBufferStats(uint64_t delta, uint64_t new_total, uint64_t& previous_total,
                                Stats::Counter& stat_total, Stats::Gauge& stat_current);
};

/**
 * Implementation of Network::Connection.
 */
class ConnectionImpl : public virtual Connection,
                       public BufferSource,
                       public TransportSocketCallbacks,
                       protected Logger::Loggable<Logger::Id::connection> {
public:
  // TODO(lizan): Remove the old style constructor when factory is ready.
  ConnectionImpl(Event::Dispatcher& dispatcher, int fd,
                 Address::InstanceConstSharedPtr remote_address,
                 Address::InstanceConstSharedPtr local_address,
                 Address::InstanceConstSharedPtr bind_to_address, bool using_original_dst,
                 bool connected);

  ConnectionImpl(Event::Dispatcher& dispatcher, int fd,
                 Address::InstanceConstSharedPtr remote_address,
                 Address::InstanceConstSharedPtr local_address,
                 Address::InstanceConstSharedPtr bind_to_address,
                 TransportSocketPtr&& transport_socket, bool using_original_dst, bool connected);

  ~ConnectionImpl();

  // Network::FilterManager
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void addReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
  void addBytesSentCallback(BytesSentCb cb) override;
  void close(ConnectionCloseType type) override;
  Event::Dispatcher& dispatcher() override;
  uint64_t id() const override;
  std::string nextProtocol() const override { return transport_socket_->protocol(); }
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  void detectEarlyCloseWhenReadDisabled(bool value) override { detect_early_close_ = value; }
  bool readEnabled() const override;
  const Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }
  const Address::InstanceConstSharedPtr& localAddress() const override { return local_address_; }
  void setConnectionStats(const ConnectionStats& stats) override;
  Ssl::Connection* ssl() override { return nullptr; }
  const Ssl::Connection* ssl() const override { return nullptr; }
  State state() const override;
  void write(Buffer::Instance& data) override;
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override { return read_buffer_limit_; }
  bool usingOriginalDst() const override { return using_original_dst_; }
  bool aboveHighWatermark() const override { return above_high_watermark_; }

  // Network::BufferSource
  Buffer::Instance& getReadBuffer() override { return read_buffer_; }
  Buffer::Instance& getWriteBuffer() override { return *current_write_buffer_; }

  // Network::TransportSocketCallbacks
  int fd() override { return fd_; }
  Connection& connection() override { return *this; }
  void raiseEvent(ConnectionEvent event) override;
  // Should the read buffer be drained?
  bool shouldDrainReadBuffer() override {
    return read_buffer_limit_ > 0 && read_buffer_.length() >= read_buffer_limit_;
  }
  // Mark read buffer ready to read in the event loop. This is used when yielding following
  // shouldDrainReadBuffer().
  // TODO(htuch): While this is the basis for also yielding to other connections to provide some
  // fair sharing of CPU resources, the underlying event loop does not make any fairness guarantees.
  // Reconsider how to make fairness happen.
  void setReadBufferReady() override { file_event_->activate(Event::FileReadyType::Read); }

protected:
  void closeSocket(ConnectionEvent close_type);
  void doConnect();

  void onLowWatermark();
  void onHighWatermark();

  FilterManagerImpl filter_manager_;
  Address::InstanceConstSharedPtr remote_address_;
  Address::InstanceConstSharedPtr local_address_;
  Buffer::OwnedImpl read_buffer_;
  // This must be a WatermarkBuffer, but as it is created by a factory the ConnectionImpl only has
  // a generic pointer.
  Buffer::InstancePtr write_buffer_;
  uint32_t read_buffer_limit_ = 0;
  TransportSocketPtr transport_socket_;

private:
  void onFileEvent(uint32_t events);
  void onRead(uint64_t read_buffer_size);
  void onReadReady();
  void onWriteReady();
  void updateReadBufferStats(uint64_t num_read, uint64_t new_size);
  void updateWriteBufferStats(uint64_t num_written, uint64_t new_size);

  static std::atomic<uint64_t> next_global_id_;

  Event::Dispatcher& dispatcher_;
  int fd_{-1};
  Event::FileEventPtr file_event_;
  const uint64_t id_;
  std::list<ConnectionCallbacks*> callbacks_;
  std::list<BytesSentCb> bytes_sent_callbacks_;
  bool read_enabled_{true};
  bool connecting_{false};
  bool close_with_flush_{false};
  bool immediate_connection_error_{false};
  bool bind_error_{false};
  const bool using_original_dst_;
  bool above_high_watermark_{false};
  bool detect_early_close_{true};
  Buffer::Instance* current_write_buffer_{};
  uint64_t last_read_buffer_size_{};
  uint64_t last_write_buffer_size_{};
  std::unique_ptr<ConnectionStats> connection_stats_;
  // Tracks the number of times reads have been disabled. If N different components call
  // readDisabled(true) this allows the connection to only resume reads when readDisabled(false)
  // has been called N times.
  uint32_t read_disable_count_{0};
};

/**
 * libevent implementation of Network::ClientConnection.
 */
class ClientConnectionImpl : public ConnectionImpl, virtual public ClientConnection {
public:
  ClientConnectionImpl(Event::Dispatcher& dispatcher,
                       const Address::InstanceConstSharedPtr& remote_address,
                       const Address::InstanceConstSharedPtr& source_address);

  // Network::ClientConnection
  void connect() override { doConnect(); }
};

} // namespace Network
} // namespace Envoy
