#include "pn532_cxx/transaction.hpp"
#include "pn532_cxx/transport.hpp"

namespace pn532 {

Transaction::Transaction(Transport &transport, bool valid)
    : _transport(&transport), _valid(valid) {}

Transaction::Transaction(Transaction &&other) noexcept
    : _transport(other._transport), _valid(other._valid),
      _in_read_mode(other._in_read_mode) {
  other._transport = nullptr;
  other._valid = false;
  other._in_read_mode = false;
}

Transaction &Transaction::operator=(Transaction &&other) noexcept {
  if (this != &other) {
    if (_valid && _transport) {
      _transport->endTransaction();
    }
    _transport = other._transport;
    _valid = other._valid;
    _in_read_mode = other._in_read_mode;
    other._transport = nullptr;
    other._valid = false;
    other._in_read_mode = false;
  }
  return *this;
}

Transaction::~Transaction() {
  if (_valid && _transport) {
    _transport->endTransaction();
  }
}

Status Transaction::write(span<const uint8_t> data) {
  if (!_valid || !_transport) {
    return TRANSPORT_ERROR;
  }
  return _transport->writeChunk(data);
}

Status Transaction::waitForAck(uint32_t timeout_ms) {
  if (!_valid || !_transport) {
    return TRANSPORT_ERROR;
  }

  _transport->endTransaction();

  if (!_transport->waitReady(timeout_ms)) {
    _valid = false;
    return TIMEOUT;
  }

  static constexpr uint8_t EXPECTED_ACK[6] = {0x00, 0x00, 0xFF,
                                               0x00, 0xFF, 0x00};
  uint8_t ack_buf[6];

  Status st = _transport->prepareRead();
  if (st != SUCCESS) {
    _valid = false;
    return st;
  }

  st = _transport->readChunk({ack_buf, 6});
  _transport->endTransaction();

  if (st != SUCCESS) {
    _valid = false;
    return st;
  }

  for (size_t i = 0; i < 6; ++i) {
    if (ack_buf[i] != EXPECTED_ACK[i]) {
      _valid = false;
      return INVALID_FRAME;
    }
  }

  return SUCCESS;
}

Status Transaction::waitForResponse(uint32_t timeout_ms) {
  if (!_valid || !_transport) {
    return TRANSPORT_ERROR;
  }

  if (!_transport->waitReady(timeout_ms)) {
    _valid = false;
    return TIMEOUT;
  }

  Status st = _transport->prepareRead();
  if (st != SUCCESS) {
    _valid = false;
    return st;
  }

  _in_read_mode = true;
  return SUCCESS;
}

Status Transaction::read(span<uint8_t> buffer) {
  if (!_valid || !_transport) {
    return TRANSPORT_ERROR;
  }
  if (!_in_read_mode) {
    return TRANSPORT_ERROR;
  }
  return _transport->readChunk(buffer);
}

} // namespace pn532
