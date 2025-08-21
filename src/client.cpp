#include <zmq.hpp>
#include <string>
#include <iostream>
#include <vector>
#include <algorithm> // For std::fill, std::shuffle
#include <random>    // For std::random_device, std::mt19937

#include <chrono>
#include "src/tlm_payload.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>

#include "tlm_payload.h" // For the C++ struct
#include "stats.h"

// --- Cap'n Proto Mode ---
void build_capnp_message(::capnp::MallocMessageBuilder& message, uint64_t id,
                         const std::vector<uint8_t>& payload_data, Stats& fill_stats) {
    auto fill_start = std::chrono::high_resolution_clock::now();

    TlmPayload::Builder tlmBuilder = message.initRoot<TlmPayload>();
    tlmBuilder.setId(id);
    tlmBuilder.setCommand(1); // Write
    tlmBuilder.setAddress(0x12345678);
    tlmBuilder.setStreamingWidth(4);

    tlmBuilder.setPayload(kj::ArrayPtr<const uint8_t>(payload_data.data(), payload_data.size()));

    tlmBuilder.setDataLength(payload_data.size());
    tlmBuilder.setByteEnableLength(0);
    tlmBuilder.setAxuserLength(0);
    tlmBuilder.setXuserLength(0);
    tlmBuilder.setResponse(-1); // Request

    auto fill_end = std::chrono::high_resolution_clock::now();
    double fill_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(fill_end - fill_start).count();
    fill_stats.add(fill_duration_us);
}

// --- Direct Memory Mode ---
zmq::message_t build_direct_message(uint64_t id, const std::vector<uint8_t>& payload_data) {
    ssln::hybrid::TlmPayload header;
    header.id = id;
    header.command = 1; // Write
    header.address = 0x12345678;
    header.streaming_width = 4;
    header.data_length = payload_data.size();
    header.byte_enable_length = 0;
    header.axuser_length = 0;
    header.xuser_length = 0;
    header.response = -1; // Request
    header.data = nullptr; // Pointer is not sent

    size_t total_size = sizeof(header) + payload_data.size();
    zmq::message_t message(total_size);

    // Copy header
    memcpy(message.data(), &header, sizeof(header));
    // Copy payload
    memcpy(static_cast<uint8_t*>(message.data()) + sizeof(header), payload_data.data(), payload_data.size());

    return message;
}


int main (int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <mode> <size_kb> [num_requests]" << std::endl;
        std::cerr << "  mode: capnp or direct" << std::endl;
        std::cerr << "  size_kb: 4 or 4096" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    size_t payload_size = std::stoul(argv[2]) * 1024;
    int num_requests = (argc > 3) ? std::stoi(argv[3]) : 1000;

    if ((mode != "capnp-packed" && mode != "capnp-flat" && mode != "direct") || (payload_size != 4096 && payload_size != 4096 * 1024)) {
        std::cerr << "Invalid arguments. Mode must be 'capnp-packed', 'capnp-flat', or 'direct'." << std::endl;
        return 1;
    }

    //  Prepare our context and socket
    zmq::context_t context (1);
    zmq::socket_t socket (context, ZMQ_REQ);

    socket.connect ("tcp://localhost:5555");

    // --- Pre-run to determine message sizes ---
    std::vector<uint8_t> pre_run_payload(payload_size);
    std::fill(pre_run_payload.begin(), pre_run_payload.begin() + payload_size / 2, 0);
    std::fill(pre_run_payload.begin() + payload_size / 2, pre_run_payload.end(), 0xAA);
    std::random_device rd_pre;
    std::mt19937 g_pre(rd_pre());
    std::shuffle(pre_run_payload.begin(), pre_run_payload.end(), g_pre);
    Stats dummy_stats; // For pre-run, we don't care about these stats
    
    zmq::message_t direct_msg = build_direct_message(0, pre_run_payload);
    
    ::capnp::MallocMessageBuilder capnp_builder_packed;
    build_capnp_message(capnp_builder_packed, 0, pre_run_payload, dummy_stats);
    kj::VectorOutputStream capnp_stream_packed;
    capnp::writePackedMessage(capnp_stream_packed, capnp_builder_packed);
    size_t capnp_packed_size = capnp_stream_packed.getArray().size();

    ::capnp::MallocMessageBuilder capnp_builder_flat;
    build_capnp_message(capnp_builder_flat, 0, pre_run_payload, dummy_stats);
    kj::Array<capnp::word> flat_words = capnp::messageToFlatArray(capnp_builder_flat);
    size_t capnp_flat_size = flat_words.asBytes().size();


    std::cout << "--- Message Size Report ---" << std::endl;
    std::cout << "Direct mode size: " << direct_msg.size() << " bytes" << std::endl;
    std::cout << "Cap'n Proto (Packed) size: " << capnp_packed_size << " bytes" << std::endl;
    std::cout << "Cap'n Proto (Flat) size:   " << capnp_flat_size << " bytes" << std::endl;
    std::cout << "---------------------------" << std::endl;

    Stats rtt_stats;
    Stats ser_total_stats;
    Stats ser_fill_stats;
    Stats ser_copy_stats;
    
    std::cout << "Running test: mode=" << mode << ", payload=" << payload_size / 1024 << "KB, requests=" << num_requests << std::endl;

    // Pre-allocate and shuffle payload data to avoid this cost in the loop
    std::vector<uint8_t> payload(payload_size);
    std::fill(payload.begin(), payload.begin() + payload.size() / 2, 0);
    std::fill(payload.begin() + payload.size() / 2, payload.end(), 0xAA);
    std::random_device rd_main;
    std::mt19937 g_main(rd_main());
    std::shuffle(payload.begin(), payload.end(), g_main);

    //  Do N requests, waiting each time for a response
    for (int request_nbr = 0; request_nbr != num_requests; request_nbr++) {
        zmq::message_t request;
        
        if (mode == "direct") {
            auto ser_start = std::chrono::high_resolution_clock::now();
            request = build_direct_message(request_nbr, payload);
            auto ser_end = std::chrono::high_resolution_clock::now();
            double ser_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(ser_end - ser_start).count();
            ser_total_stats.add(ser_duration_us);
            ser_fill_stats.add(ser_duration_us); // For direct, fill is the total
            ser_copy_stats.add(0); // No separate copy step
        } else { // capnp modes
            auto total_start = std::chrono::high_resolution_clock::now();
            ::capnp::MallocMessageBuilder message;
            build_capnp_message(message, request_nbr, payload, ser_fill_stats);
            
            kj::ArrayPtr<const kj::byte> buffer;
            kj::VectorOutputStream outputStream; // For packed
            kj::Array<capnp::word> words; // For flat

            if (mode == "capnp-packed") {
                capnp::writePackedMessage(outputStream, message);
                buffer = outputStream.getArray();
            } else { // capnp-flat
                words = capnp::messageToFlatArray(message);
                buffer = words.asBytes();
            }

            auto copy_start = std::chrono::high_resolution_clock::now();
            request.rebuild(buffer.size());
            memcpy(request.data(), buffer.begin(), buffer.size());
            auto copy_end = std::chrono::high_resolution_clock::now();
            double copy_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(copy_end - copy_start).count();
            ser_copy_stats.add(copy_duration_us);

            auto total_end = std::chrono::high_resolution_clock::now();
            double total_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count();
            ser_total_stats.add(total_duration_us);
        }
        
        auto rtt_start = std::chrono::high_resolution_clock::now();

        socket.send (request, zmq::send_flags::none);

        //  Get the reply.
        zmq::message_t reply;
        (void)socket.recv (reply, zmq::recv_flags::none);

        auto rtt_end = std::chrono::high_resolution_clock::now();
        double rtt_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(rtt_end - rtt_start).count();
        rtt_stats.add(rtt_duration_us);
    }

    std::cout << "\n--- Total Serialization Stats (includes all steps below) ---" << std::endl;
    ser_total_stats.calculate();

    std::cout << "\n--- Step 1: Field Filling Stats ---" << std::endl;
    ser_fill_stats.calculate();

    std::cout << "\n--- Step 2: Copy to ZMQ Buffer Stats ---" << std::endl;
    ser_copy_stats.calculate();
    
    std::cout << "\n--- Network RTT + Deserialization Stats ---" << std::endl;
    rtt_stats.calculate();

    return 0;
}