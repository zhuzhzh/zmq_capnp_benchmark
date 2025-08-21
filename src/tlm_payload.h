#ifndef SSLN_HYBRID_TLM_PAYLOAD_H
#define SSLN_HYBRID_TLM_PAYLOAD_H
#include <iostream>
#include <cstdint>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <string>

namespace ssln {
namespace hybrid {

// TLM payload structure for IPC communication
struct TlmPayload {
    uint64_t id;
    uint8_t command;              // Command type
    uint64_t address;             // Target address
    uint32_t data_length;         // Length of data
    uint32_t byte_enable_length;  // Length of byte enable
    uint32_t axuser_length;       // Length of axuser
    uint32_t xuser_length;        // Length of xuser
    uint32_t streaming_width;     // Streaming width
    int8_t response;              // Response status
    uint8_t *data;                // Variable length data followed by byte enable

    std::string format(bool is_req) {
        std::stringstream ss;
        ss << "------------------>> time stamp@ ";
        // 获取当前时间点
        auto now = std::chrono::system_clock::now();
        
        // 转换为时间_t 类型（即自1970年1月1日以来的秒数）
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        
        // 转换为tm结构体以便输出日期和时间
        std::tm now_tm = *std::localtime(&now_c);
        
        // 获取当前时间的微秒部分
        auto duration = now.time_since_epoch();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000000;
        
        // 输出日期和时间（年月日 时分秒 微秒）
        ss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(6) << micros << "\n";

        ss << "Id: " << id << "\n";
        if (is_req) {  // true:
            ss << "Op: " << (command == 0 ? "read request" : "write request") << "\n";
        } else {
            ss << "Op: " << (command == 0 ? "read response" : "write response") << "\n";
        }
        ss << "Addr: 0x" << std::hex << address << "\n";
        ss << "Data length: 0x" << std::hex << data_length << "\n";
        ss << "Streaming width: 0x" << streaming_width << "\n";
        ss << "Byte enable length: 0x" << byte_enable_length << "\n";
        ss << "Axuser length: 0x" << axuser_length << "\n";
        ss << "Xuser length: 0x" << xuser_length << "\n";
        ss << "------------------>>";
        return ss.str();
    }
};

}  // namespace hybrid
}  // namespace ssln

#endif  // SSLN_HYBRID_TLM_PAYLOAD_H
