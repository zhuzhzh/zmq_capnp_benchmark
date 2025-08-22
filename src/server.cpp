#include <zmq.hpp>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <chrono>
#include <csignal> // For signal handling

#include "src/tlm_payload.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/array.h>

#include "tlm_payload.h" // For the C++ struct
#include "stats.h"

// --- Global stats object and signal handler ---
Stats deserialization_stats;
volatile bool running = true;

void signal_handler(int signum) {
    if (signum == SIGINT) {
        running = false;
    }
}
// ---

void handle_direct_message(const zmq::message_t& request, Stats& stats) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Simulate a real-world scenario where data must be copied out of the ZMQ buffer
    // and then accessed in a structured way.
    char* local_copy = new char[request.size()];
    memcpy(local_copy, request.data(), request.size());

    // 1. Cast the copied buffer to the struct type.
    ssln::hybrid::TlmPayload* header = reinterpret_cast<ssln::hybrid::TlmPayload*>(local_copy);

    // 2. Reconstruct the data pointer.
    header->data = reinterpret_cast<uint8_t*>(local_copy + sizeof(ssln::hybrid::TlmPayload));

    // 3. Read a field to prevent optimization. A volatile variable helps ensure this.
    volatile uint64_t id = header->id;
    (void)id; // Suppress unused variable warning

    // In a real app, you'd store/use the header pointer. Here we delete it to avoid leaks.
    delete[] local_copy;

    auto end = std::chrono::high_resolution_clock::now();
    double duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    stats.add(duration_us);
}

void handle_capnp_packed_message(const zmq::message_t& request, Stats& stats) {
    auto start = std::chrono::high_resolution_clock::now();

    auto bytes = kj::ArrayPtr<const kj::byte>(
        request.data<const kj::byte>(), 
        request.size()
    );
    kj::ArrayInputStream inputStream(bytes);
    ::capnp::PackedMessageReader reader(inputStream);
    (void)reader.getRoot<TlmPayload>();
    
    auto end = std::chrono::high_resolution_clock::now();
    double duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    stats.add(duration_us);
}

void handle_capnp_flat_message(const zmq::message_t& request, Stats& stats) {
    auto start = std::chrono::high_resolution_clock::now();

    size_t word_count = request.size() / sizeof(capnp::word);
    kj::Array<capnp::word> aligned_buffer = kj::heapArray<capnp::word>(word_count);
    memcpy(aligned_buffer.begin(), request.data(), aligned_buffer.asBytes().size());

    ::capnp::FlatArrayMessageReader reader(aligned_buffer);
    (void)reader.getRoot<TlmPayload>();

    auto end = std::chrono::high_resolution_clock::now();
    double duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    stats.add(duration_us);
}

int main (int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode> [print_interval]" << std::endl;
        std::cerr << "  mode: capnp-packed, capnp-flat, or direct" << std::endl;
        return 1;
    }
    std::string mode = argv[1];
    int print_interval = (argc > 2) ? std::stoi(argv[2]) : 1000;

    if (mode != "capnp-packed" && mode != "capnp-flat" && mode != "direct") {
        std::cerr << "Invalid mode specified." << std::endl;
        return 1;
    }

    //  Prepare our context and socket
    zmq::context_t context (1);
    zmq::socket_t socket (context, ZMQ_REP);
    socket.bind ("tcp://*:5555");

    std::cout << "Server started in " << mode << " mode. Printing stats every " << print_interval << " requests." << std::endl;
    std::cout << "(Press Ctrl+C to stop the server)" << std::endl;

    Stats stats;
    int request_count = 0;

    while (true) {
        zmq::message_t request;
        (void)socket.recv (request, zmq::recv_flags::none);

        if (mode == "direct") {
            handle_direct_message(request, stats);
        } else if (mode == "capnp-packed") {
            handle_capnp_packed_message(request, stats);
        } else { // capnp-flat
            handle_capnp_flat_message(request, stats);
        }

        socket.send (request, zmq::send_flags::none);

        request_count++;
        if (request_count == print_interval) {
            std::cout << "\n--- Server Deserialization Stats (last " << print_interval << " requests) ---" << std::endl;
            stats.calculate();
            // Reset for next batch
            stats = Stats();
            request_count = 0;
        }
    }

    return 0;
}