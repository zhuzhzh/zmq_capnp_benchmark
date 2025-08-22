#include <zmq.hpp>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <chrono>
#include <csignal> // For signal handling
#include <sys/socket.h>
#include <sys/un.h>
#include <cerrno>

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

void handle_direct_message_raw(const void* data, size_t size, Stats& stats) {
    auto start = std::chrono::high_resolution_clock::now();

    // Simulate a real-world scenario where data must be copied
    // and then accessed in a structured way.
    char* local_copy = new char[size];
    memcpy(local_copy, data, size);

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

void handle_direct_message(const zmq::message_t& request, Stats& stats) {
    handle_direct_message_raw(request.data(), request.size(), stats);
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

// --- Unix Socket Helpers ---
bool read_all(int fd, void* buf, size_t size) {
    char* p = static_cast<char*>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t bytes_read = read(fd, p, remaining);
        if (bytes_read <= 0) {
            return false; // Error or client disconnected
        }
        p += bytes_read;
        remaining -= bytes_read;
    }
    return true;
}

bool write_all(int fd, const void* buf, size_t size) {
    const char* p = static_cast<const char*>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t bytes_written = write(fd, p, remaining);
        if (bytes_written < 0) {
            return false; // Error
        }
        p += bytes_written;
        remaining -= bytes_written;
    }
    return true;
}
// ---

int main (int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode> [print_interval]" << std::endl;
        std::cerr << "  mode: capnp-packed, capnp-flat, direct, or direct-unix" << std::endl;
        return 1;
    }
    std::string mode = argv[1];
    int print_interval = (argc > 2) ? std::stoi(argv[2]) : 1000;

    if (mode != "capnp-packed" && mode != "capnp-flat" && mode != "direct" && mode != "direct-unix") {
        std::cerr << "Invalid mode specified." << std::endl;
        return 1;
    }

    Stats stats;
    int request_count = 0;
    const char* socket_path = "/tmp/capnproto-test.sock";

    std::cout << "Server starting in " << mode << " mode. Printing stats every " << print_interval << " requests." << std::endl;
    std::cout << "(Press Ctrl+C to stop the server)" << std::endl;

    if (mode == "direct-unix") {
        int server_fd, client_fd;
        struct sockaddr_un address;
        
        // Create socket
        if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == 0) {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }

        // Bind socket
        address.sun_family = AF_UNIX;
        strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);
        unlink(socket_path); // Remove previous socket file
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("bind failed");
            exit(EXIT_FAILURE);
        }

        // Listen for connections
        if (listen(server_fd, 5) < 0) {
            perror("listen failed");
            exit(EXIT_FAILURE);
        }

        std::cout << "Unix socket server listening on " << socket_path << std::endl;

        if ((client_fd = accept(server_fd, NULL, NULL)) < 0) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }

        // Set larger socket buffer sizes
        int buffer_size = 8 * 1024 * 1024; // 8MB
        if (setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) < 0) {
            perror("setsockopt SO_RCVBUF failed");
        }
        if (setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) < 0) {
            perror("setsockopt SO_SNDBUF failed");
        }

        std::vector<char> buffer(8 * 1024 * 1024); // 8MB buffer, should be large enough
        
        while(true) {
            uint32_t msg_size;
            if (!read_all(client_fd, &msg_size, sizeof(msg_size))) {
                std::cout << "Client disconnected while reading size." << std::endl;
                break;
            }

            if (msg_size > buffer.size()) {
                std::cerr << "Error: Message size " << msg_size << " is larger than buffer " << buffer.size() << std::endl;
                break;
            }

            if (!read_all(client_fd, buffer.data(), msg_size)) {
                std::cout << "Client disconnected while reading payload." << std::endl;
                break;
            }
            
            handle_direct_message_raw(buffer.data(), msg_size, stats);

            // Echo back with framing
            if (!write_all(client_fd, &msg_size, sizeof(msg_size)) || !write_all(client_fd, buffer.data(), msg_size)) {
                std::cerr << "Error writing response to client." << std::endl;
                break;
            }

            request_count++;
            if (request_count == print_interval) {
                std::cout << "\n--- Server Deserialization Stats (last " << print_interval << " requests) ---" << std::endl;
                stats.calculate();
                stats = Stats();
                request_count = 0;
            }
        }
        close(client_fd);
        close(server_fd);
        unlink(socket_path);
        return 0;

    }

    // ZMQ modes
    zmq::context_t context (1);
    zmq::socket_t socket (context, ZMQ_REP);
    socket.bind ("tcp://*:5555");

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