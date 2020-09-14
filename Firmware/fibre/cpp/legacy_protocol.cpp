

#include "legacy_protocol.hpp"

#include <fibre/protocol.hpp>
#include <fibre/crc.hpp>
#include "logging.hpp"
#include "print_utils.hpp"
#include "async_stream.hpp"
#include <memory>
#include <stdlib.h>

DEFINE_LOG_TOPIC(LEGACY_PROTOCOL);
USE_LOG_TOPIC(LEGACY_PROTOCOL);

using namespace fibre;


/* PacketWrapper -------------------------------------------------------------*/

void PacketWrapper::start_write(cbufptr_t buffer, TransferHandle* handle, Completer<WriteResult>& completer) {
    if (handle) {
        *handle = reinterpret_cast<TransferHandle>(this);
    }

    if (state_ != kStateIdle) {
        completer.complete({kStreamError, buffer.begin()});
    }

    // TODO: support buffer size >= 128
    if (buffer.size() >= 128) {
        completer.complete({kStreamError, buffer.begin()});
    }

    completer_ = &completer;

    header_buf_[0] = CANONICAL_PREFIX;
    header_buf_[1] = static_cast<uint8_t>(buffer.size());
    header_buf_[2] = calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(CANONICAL_CRC8_INIT, header_buf_, 2);
    
    payload_buf_ = buffer;

    uint16_t crc16 = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(CANONICAL_CRC16_INIT, buffer.begin(), buffer.size());
    trailer_buf_[0] = (uint8_t)((crc16 >> 8) & 0xff),
    trailer_buf_[1] = (uint8_t)((crc16 >> 0) & 0xff);

    state_ = kStateSendingHeader;
    expected_tx_end_ = header_buf_ + 3;
    tx_channel_->start_write(header_buf_, &inner_transfer_handle_, *this);
}

void PacketWrapper::cancel_write(TransferHandle transfer_handle) {
    state_ = kStateCancelling;
    tx_channel_->cancel_write(inner_transfer_handle_);
}

void PacketWrapper::complete(WriteResult result) {
    if (state_ == kStateCancelling) {
        state_ = kStateIdle;
        safe_complete(completer_, {kStreamCancelled, payload_buf_.begin()});
        return;
    }

    if (result.status != kStreamOk) {
        state_ = kStateIdle;
        safe_complete(completer_, {result.status, payload_buf_.begin()});
        return;
    }

    if (result.end < expected_tx_end_) {
        tx_channel_->start_write({result.end, expected_tx_end_}, &inner_transfer_handle_, *this);
        return;
    }

    if (state_ == kStateSendingHeader) {
        state_ = kStateSendingPayload;
        expected_tx_end_ = payload_buf_.end();
        tx_channel_->start_write(payload_buf_, &inner_transfer_handle_, *this);

    } else if (state_ == kStateSendingPayload) {
        state_ = kStateSendingTrailer;
        expected_tx_end_ = trailer_buf_ + 2;
        tx_channel_->start_write(trailer_buf_, &inner_transfer_handle_, *this);

    } else if (state_ == kStateSendingTrailer) {
        state_ = kStateIdle;
        safe_complete(completer_, {kStreamOk, payload_buf_.end()});
    }
}


/* PacketUnwrapper -----------------------------------------------------------*/

void PacketUnwrapper::start_read(bufptr_t buffer, TransferHandle* handle, Completer<ReadResult>& completer) {
    if (handle) {
        *handle = reinterpret_cast<TransferHandle>(this);
    }

    if (state_ != kStateIdle) {
        completer.complete({kStreamError, buffer.begin()});
    }

    completer_ = &completer;
    payload_buf_ = buffer;

    state_ = kStateReceivingHeader;
    expected_rx_end_ = rx_buf_ + 3;
    rx_channel_->start_read({rx_buf_, expected_rx_end_}, &inner_transfer_handle_, *this);
}

void PacketUnwrapper::cancel_read(TransferHandle transfer_handle) {
    state_ = kStateCancelling;
    rx_channel_->cancel_read(inner_transfer_handle_);
}

void PacketUnwrapper::complete(ReadResult result) {
    // All code paths in this function must end with either of these two:
    //   - rx_channel_->start_read() to bounce back control to the underlying stream
    //   - safe_complete() to return control to the client

    if (state_ == kStateCancelling) {
        state_ = kStateIdle;
        safe_complete(completer_, {kStreamCancelled, payload_buf_.begin()});
        return;
    }

    if (result.status != kStreamOk) {
        state_ = kStateIdle;
        safe_complete(completer_, {result.status, payload_buf_.begin()});
        return;
    }

    if (result.end < expected_rx_end_) {
        rx_channel_->start_read({result.end, expected_rx_end_}, &inner_transfer_handle_, *this);
        return;
    }

    if (state_ == kStateReceivingHeader) {
        size_t n_discard;

        // Process header
        if (rx_buf_[0] != CANONICAL_PREFIX) {
            n_discard = 1;
        } else if ((rx_buf_[1] & 0x80)) {
            n_discard = 2; // TODO: support packets larger than 128 bytes
        } else if (calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(CANONICAL_CRC8_INIT, rx_buf_, 3)) {
            n_discard = 3;
        } else {
            state_ = kStateReceivingPayload;
            payload_length_ = std::min(payload_buf_.size(), (size_t)rx_buf_[1]);
            expected_rx_end_ = payload_buf_.begin() + payload_length_;
            rx_channel_->start_read(payload_buf_.take(payload_length_), &inner_transfer_handle_, *this);
            return;
        }

        // Header was bad: discard the bad header bytes and receive more
        memmove(rx_buf_, rx_buf_ + n_discard, sizeof(rx_buf_) - n_discard);
        rx_channel_->start_read(bufptr_t{rx_buf_}.skip(3 - n_discard), &inner_transfer_handle_, *this);

    } else if (state_ == kStateReceivingPayload) {
        expected_rx_end_ = rx_buf_ + 2;
        state_ = kStateReceivingTrailer;
        rx_channel_->start_read({rx_buf_, expected_rx_end_}, &inner_transfer_handle_, *this);

    } else if (state_ == kStateReceivingTrailer) {
        uint16_t crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(CANONICAL_CRC16_INIT, payload_buf_.begin(), payload_length_);
        crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc, rx_buf_, 2);

        if (!crc) {
            state_ = kStateIdle;
            safe_complete(completer_, {kStreamOk, payload_buf_.begin() + payload_length_});
        } else {
            state_ = kStateReceivingHeader;
            expected_rx_end_ = rx_buf_ + 3;
            rx_channel_->start_read({rx_buf_, expected_rx_end_}, &inner_transfer_handle_, *this);
        }
    }
}


/* LegacyProtocolPacketBased -------------------------------------------------*/

#ifdef FIBRE_ENABLE_CLIENT

/**
 * @brief Starts a remote endpoint operation.
 * 
 * @param endpoint_id: The endpoint ID to invoke the operation on.
 * @param tx_buf: The tx_buf to write to the endpoint. Must remain valid until
 *        the completer is invoked.
 * @param rx_length: The desired number of bytes to read from the endpoint. The
 *        actual returned buffer may be smaller.
 * @param completer: The completer that will be notified once the operation
 *        completes (whether successful or not).
 *        The buffer given to the completer is only valid if the status is
 *        kStreamOk and until the completer returns.
 * @param handle: The variable pointed to by this argument is set to a handle
 *        that can be passed to cancel_endpoint_operation() to cancel the
 *        ongoing operation. If the completer is invoked directly from within
 *        this function then the handle is not set later than invoking the
 *        completer.
 */
void LegacyProtocolPacketBased::start_endpoint_operation(uint16_t endpoint_id, cbufptr_t tx_buf, bufptr_t rx_buf, EndpointOperationHandle* handle, Completer<EndpointOperationResult>& completer) {
    if (tx_buf.size() + 8 >= tx_mtu_) {
        FIBRE_LOG(E) << "packet too large";
        completer.complete({kStreamError, rx_buf.begin()});
    }

    if (rx_buf.size() > 0xffff) {
        FIBRE_LOG(E) << "receive size larger than 65535 currently not supported";
        completer.complete({kStreamError, rx_buf.begin()});
    }

    outbound_seq_no_ = ((outbound_seq_no_ + 1) & 0x7fff);

    EndpointOperation op = {
        .seqno = (uint16_t)(outbound_seq_no_ | 0x0080), // FIXME: we hardwire one bit of the seq-no to 1 to avoid conflicts with the ODrive ASCII protocol
        .endpoint_id = endpoint_id,
        .tx_buf = tx_buf,
        .rx_buf = rx_buf,
        .completer = &completer
    };

    if (handle) {
        *handle = op.seqno | 0xffff0000;
    }

    if (tx_handle_) {
        FIBRE_LOG(D) << "Endpoint operation already in progress. Enqueuing this one.";

        // A TX operation is already in progress
        if (pending_operation_.completer) {
            // Previous endpoint operation was not yet sent. We don't support
            // enqueuing multiple endpoint operations while the first didn't send yet.
            FIBRE_LOG(E) << "previous endpoint operation still not sent";
            completer.complete({kStreamError, rx_buf.begin()});
        } else {
            // Control is returned to start_endpoint_operation once TX completes
            pending_operation_ = op;
        }
        return;
    }

    start_endpoint_operation(op);
}

void LegacyProtocolPacketBased::start_endpoint_operation(EndpointOperation op) {
    write_le<uint16_t>(op.seqno, tx_buf_);
    write_le<uint16_t>(op.endpoint_id | 0x8000, tx_buf_ + 2);
    write_le<uint16_t>(op.rx_buf.size(), tx_buf_ + 4);

    memcpy(tx_buf_ + 6, op.tx_buf.begin(), op.tx_buf.size());

    uint16_t trailer = (op.endpoint_id & 0x7fff) == 0 ?
                       PROTOCOL_VERSION : client_.json_crc_;

    write_le<uint16_t>(trailer, tx_buf_ + 6 + op.tx_buf.size());

    expected_acks_[op.seqno] = op;
    transmitting_op_ = op.seqno | 0xffff0000;
    tx_channel_->start_write(cbufptr_t{tx_buf_}.take(8 + op.tx_buf.size()), &tx_handle_, *static_cast<WriteCompleter*>(this));
}


void LegacyProtocolPacketBased::cancel_endpoint_operation(EndpointOperationHandle handle) {
    if (!handle) {
        return;
    }

    uint16_t seqno = static_cast<uint16_t>(handle & 0xffff);

    Completer<EndpointOperationResult>* completer;
    uint8_t* rx_end = nullptr;

    if (pending_operation_.seqno == seqno) {
        completer = pending_operation_.completer;
        rx_end = pending_operation_.rx_buf.begin();
        pending_operation_ = {};
    }

    auto it = expected_acks_.find(handle);

    if (it != expected_acks_.end()) {
        completer = it->second.completer;
        rx_end = it->second.rx_buf.begin();
        expected_acks_.erase(it);
    }

    if (transmitting_op_ == handle) {
        // Cancel the TX task because it belongs to the endpoint operation that
        // is being cancelled.
        tx_channel_->cancel_write(tx_handle_);
    } else {
        // Either we're waiting for an ack on this operation or it has not yet
        // been sent. In both cases we can just complete immediately.
        safe_complete(completer, {kStreamCancelled, rx_end});
    }
}

#endif

#ifdef FIBRE_ENABLE_SERVER

// Returns part of the JSON interface definition.
bool fibre::endpoint0_handler(fibre::cbufptr_t* input_buffer, fibre::bufptr_t* output_buffer) {
    // The request must contain a 32 bit integer to specify an offset
    std::optional<uint32_t> offset = read_le<uint32_t>(input_buffer);
    
    if (!offset.has_value()) {
        // Didn't receive any offset
        return false;
    } else if (*offset == 0xffffffff) {
        // If the offset is special value 0xFFFFFFFF, send back the JSON version ID instead
        return write_le<uint32_t>(json_version_id_, output_buffer);
    } else if (*offset >= embedded_json_length) {
        // Attempt to read beyond the buffer end - return empty response
        return true;
    } else {
        // Return part of the json file
        size_t n_copy = std::min(output_buffer->size(), embedded_json_length - (size_t)*offset);
        memcpy(output_buffer->begin(), embedded_json + *offset, n_copy);
        *output_buffer = output_buffer->skip(n_copy);
        return true;
    }
}

#endif

void LegacyProtocolPacketBased::on_write_finished(WriteResult result) {
    tx_handle_ = 0;

#if FIBRE_ENABLE_CLIENT
    if (transmitting_op_) {
        uint16_t seqno = transmitting_op_ & 0xffff;
        transmitting_op_ = 0;

        // If the TX task was a remote endpoint operation but didn't succeed
        // we terminate that operation
        if (result.status != kStreamOk) {
            auto it = expected_acks_.find(seqno);
            auto completer = it->second.completer;
            auto rx_end = it->second.rx_buf.begin();
            expected_acks_.erase(it);
            safe_complete(completer, {result.status, rx_end});
        }
    }
#endif

    // TODO: should we prioritize the server or client side here?

#if FIBRE_ENABLE_SERVER
    if (rx_end_) {
        // There is a write operation pending from the server side (i.e. an ack
        // for a local endpoint operation).
        uint8_t* rx_end = rx_end_;
        rx_end_ = nullptr;
        on_read_finished({kStreamOk, rx_end});
        return;
    }
#endif

#if FIBRE_ENABLE_CLIENT
    if (pending_operation_.completer) {
        // There is a write operation pending from the client side (i.e. an
        // outgoing remote endpoint operation).
        EndpointOperation op = pending_operation_;
        pending_operation_ = {};
        start_endpoint_operation(op);
        return;
    }
#endif
}

void LegacyProtocolPacketBased::on_read_finished(ReadResult result) {
    TransferHandle dummy;

    if (result.status == kStreamClosed) {
        FIBRE_LOG(D) << "RX stream closed.";
        on_closed(kStreamClosed);
        return;
    } else if (result.status == kStreamCancelled) {
        FIBRE_LOG(W) << "RX operation cancelled.";
        // TODO: close stream
        return;
    } else if (result.status != kStreamOk) {
        FIBRE_LOG(W) << "RX error. Not restarting.";
        // TODO: we should distinguish between permanent and temporary errors.
        // If we try to restart after a permanent error we might end up in a
        // busy loop.
        on_closed(kStreamError);
        return;
    }

    cbufptr_t rx_buf = cbufptr_t{rx_buf_, result.end};
    //FIBRE_LOG(D) << "got packet of length " << (result.end - rx_buf_) /*<< ": " << as_hex(rx_buf)*/;

    std::optional<uint16_t> seq_no = read_le<uint16_t>(&rx_buf);

    if (!seq_no.has_value()) {
        FIBRE_LOG(W) << "packet too short";

    } else if (*seq_no & 0x8000) {

#ifdef FIBRE_ENABLE_CLIENT
        
        auto it = expected_acks_.find(*seq_no & 0x7fff);

        if (it == expected_acks_.end()) {
            FIBRE_LOG(W) << "received unexpected ACK: " << (*seq_no & 0x7fff);
        } else {
            size_t n_copy = std::min((size_t)(result.end - rx_buf.begin()), it->second.rx_buf.size());
            memcpy(it->second.rx_buf.begin(), rx_buf.begin(), n_copy);
            uint8_t* rx_end = it->second.rx_buf.begin() + n_copy;
            auto completer = it->second.completer;
            expected_acks_.erase(it);
            safe_complete(completer, {kStreamOk, rx_end});
        }

#else
        FIBRE_LOG(W) << "received ack but client support is not compiled in";
#endif

    } else {

#ifdef FIBRE_ENABLE_SERVER
        if (rx_buf.size() < 6) {
            FIBRE_LOG(W) << "packet too short";
            rx_channel_->start_read(rx_buf_, &dummy, *static_cast<ReadCompleter*>(this));
            return;
        }

        // TODO: think about some kind of ordering guarantees
        // currently the seq_no is just used to associate a response with a request

        uint16_t endpoint_id = *read_le<uint16_t>(&rx_buf);
        bool expect_response = endpoint_id & 0x8000;
        endpoint_id &= 0x7fff;

        if (expect_response && tx_handle_) {
            // The operation expects a response but the output channel is still
            // busy. Stop receiving for now. This function will be invoked again
            // once the TX operation is finished.
            rx_end_ = result.end;
            return;
        }

        // Verify packet trailer. The expected trailer value depends on the selected endpoint.
        // For endpoint 0 this is just the protocol version, for all other endpoints it's a
        // CRC over the entire JSON descriptor tree (this may change in future versions).
        uint16_t expected_trailer = endpoint_id ? fibre::json_crc_ : PROTOCOL_VERSION;
        uint16_t actual_trailer = *(rx_buf.end() - 2) | (*(rx_buf.end() - 1) << 8);
        if (expected_trailer != actual_trailer) {
            FIBRE_LOG(D) << "trailer mismatch for endpoint " << endpoint_id << ": expected " << as_hex(expected_trailer) << ", got " << as_hex(actual_trailer);
            rx_channel_->start_read(rx_buf_, &dummy, *static_cast<ReadCompleter*>(this));
            return;
        }
        FIBRE_LOG(D) << "trailer ok for endpoint " << endpoint_id;

        // TODO: if more bytes than the MTU were requested, should we abort or just return as much as possible?

        uint16_t expected_response_length = *read_le<uint16_t>(&rx_buf);

        // Limit response length according to our local TX buffer size
        if (expected_response_length > tx_mtu_ - 2)
            expected_response_length = tx_mtu_ - 2;

        fibre::cbufptr_t input_buffer{rx_buf.begin(), rx_buf.end() - 2};
        fibre::bufptr_t output_buffer{tx_buf_ + 2, expected_response_length};
        fibre::endpoint_handler(endpoint_id, &input_buffer, &output_buffer);

        // Send response
        if (expect_response) {
            size_t actual_response_length = expected_response_length - output_buffer.size() + 2;
            write_le<uint16_t>(*seq_no | 0x8000, tx_buf_);

            FIBRE_LOG(D) << "send packet: " << as_hex(cbufptr_t{tx_buf_, actual_response_length});
            tx_channel_->start_write({tx_buf_, actual_response_length}, &tx_handle_, *static_cast<WriteCompleter*>(this));
        }
#else
        FIBRE_LOG(W) << "received request but server support is not compiled in";
#endif
    }

    rx_channel_->start_read(rx_buf_, &dummy, *static_cast<ReadCompleter*>(this));
}

void LegacyProtocolPacketBased::on_closed(StreamStatus status) {

#ifdef FIBRE_ENABLE_CLIENT
    // Cancel all ongoing endpoint operations
    for (auto& item: expected_acks_) {
        if (item.second.completer)
            (*item.second.completer).complete({status, item.second.rx_buf.begin()});
    }
    expected_acks_.clear();

    // Report that the root object was lost
    if (client_.on_lost_root_object_ && client_.root_obj_) {
        client_.root_obj_ = nullptr;
        client_.on_lost_root_object_->complete(&client_);
    }
#endif
    safe_complete(on_stopped_, this, status);
}

#if FIBRE_ENABLE_CLIENT
void LegacyProtocolPacketBased::start(Completer<LegacyObjectClient*, std::shared_ptr<LegacyObject>>& on_found_root_object, Completer<LegacyObjectClient*>& on_lost_root_object, Completer<LegacyProtocolPacketBased*, StreamStatus>& on_stopped) {
#else
void LegacyProtocolPacketBased::start(Completer<LegacyProtocolPacketBased*, StreamStatus>& on_stopped) {
#endif
    on_stopped_ = &on_stopped;
    TransferHandle dummy;
    rx_channel_->start_read(rx_buf_, &dummy, *static_cast<ReadCompleter*>(this));

#if FIBRE_ENABLE_CLIENT
    if (on_stopped_) {
        client_.start(on_found_root_object, on_lost_root_object);
    }
#endif
}
