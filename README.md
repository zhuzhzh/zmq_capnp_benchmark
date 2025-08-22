# Cap'n Proto vs Direct Memory Performance Test

This project measures and compares the latency of sending data over a ZMQ REQ/REP socket using two different methods:
1.  **capnp**: Serializing the data with Cap'n Proto in different formats (`packed`, `flat`).
2.  **direct**: Sending the raw C++ struct and payload data directly via `memcpy`.

## Dependencies

The following libraries are required:
- ZeroMQ (`libzmq`)
- CppZMQ (C++ wrapper for ZeroMQ)
- Cap'n Proto

## Environment Variables

The `Makefile` will use the following environment variables to find the dependencies. If they are not set, it will fall back to default paths under `/home/public/`.

- `ZEROMQ_HOME`: Path to ZeroMQ installation.
- `CPPZMQ_HOME`: Path to CppZMQ installation.
- `CAPNPROTO_HOME`: Path to Cap'n Proto installation.

Example:
```bash
export ZEROMQ_HOME=/path/to/your/zeromq
```

## Build

To build the server and client executables, run `make`:

```bash
make
```
This will create `server` and `client` inside the `build/` directory.

To clean the build artifacts:
```bash
make clean
```

## Running the Test

为了获得稳定和可复现的性能数据，强烈建议使用 `taskset` 工具将 Server 和 Client 绑定到不同的 CPU 核心上，以避免操作系统调度带来的延迟抖动。

1.  **Start the Server**: 打开一个终端，将 Server 绑定到 CPU 核心 0 并运行。Server 会持续运行，并在每收到指定数量的请求后打印一次统计信息。

    - `direct`: Raw C++ struct transfer.
    - `capnp-packed`: Cap'n Proto with Packed serialization.
    - `capnp-flat`: Cap'n Proto with standard (non-packed) serialization.
    - `[print_interval]` (optional): 每收到 N 个请求后打印一次统计信息，默认为 1000。

    ```bash
    # Example for packed capnp mode on CPU 0, printing stats every 1000 requests
    taskset -c 0 ./build/server capnp-packed 1000
    ```

2.  **Run the Client**: 打开另一个终端，将 Client 绑定到 CPU 核心 1 并运行。

    -   `<mode>`: `direct`, `capnp-packed`, or `capnp-flat` (must match the server)
    -   `<size_kb>`: `4` (for 4KB) or `4096` (for 4MB)
    -   `[num_requests]` (optional): Number of messages to send, defaults to 1000. **Must match the server.**

    **Example 1: Test Direct Memory with 4KB payload on CPU 1**
    ```bash
    taskset -c 1 ./build/client direct 4
    ```

    **Example 2: Test Cap'n Proto (Packed) with 4MB payload for 5000 requests**
    ```bash
    # First start the server, printing stats every 5000 requests
    taskset -c 0 ./build/server capnp-packed 5000
    # Then run the client
    taskset -c 1 ./build/client capnp-packed 4096 5000
    ```

The client will run the test and print its latency statistics. The server will print its deserialization statistics every `[print_interval]` requests.

## Performance Results & Conclusion

我们通过一系列详尽的性能实验，最终得到了一个贯穿所有测试场景的、清晰可靠的结论。

### 最终性能数据 (4MB 负载, 随机50%压缩率)

| 模式 | 客户端开销 (序列化) | 服务端开销 (反序列化) | 网络 RTT | **端到端总延迟 (估算)** |
| :--- | :--- | :--- | :--- | :--- |
| `direct` | **~416 us** | **~330 us** | **~2740 us** | **~3486 us (最佳)** |
| `capnp-flat` | ~1750 us | ~354 us | ~2738 us | ~4842 us |
| `capnp-packed`| ~6583 us | ~3500 us | ~5442 us | ~15525 us |

*注：客户端和服务端数据来自不同轮次的测试，但量级和趋势具有代表性。RTT为客户端测得的总往返时间。*

### 核心结论

对于**大部分是二进制数据、在低延迟网络（本地或局域网）上传输**的特定应用场景，**直接内存操作 (`direct` 模式) 是无可争议的性能冠军**。

其根本原因在于，在这种场景下，**CPU 的计算开销是压倒性的性能瓶颈**。

1.  **`direct` 模式**: 性能最佳。它在客户端和服务端的 CPU 开销都极低（仅为 `memcpy` 的成本），这使得它的综合性能表现最好。
2.  **`capnp-flat` 模式**: 性能居中。它在服务端的开销与 `direct` 模式相当（都需要一次内存拷贝），但其在客户端的**编码计算**引入了显著的 CPU 开销，导致其慢于 `direct` 模式。
3.  **`capnp-packed` 模式**: 性能最差。它在客户端有高昂的**编码**开销，在服务端有更巨大的**解压缩**开销。虽然它能有效压缩数据，但在网络不成瓶颈的环境下，这些 CPU 成本远大于其节省的网络时间。

### 何时选择 Cap'n Proto?

尽管在本次测试中 `direct` 模式胜出，Cap'n Proto 依然在许多其他场景下是更优的选择：

-   **网络是瓶颈时**: 在广域网（WAN）或低带宽网络下，`capnp-packed` 带来的网络传输优势将变得至关重要。
-   **数据结构复杂时**: 当数据不再是单个二进制块，而是包含大量字段、字符串和嵌套结构时，Cap'n Proto 在开发效率、代码可维护性和协议安全性上的优势将远超手写 `memcpy`。
-   **需要随机访问时**: 如果接收端只需要读取消息中的一两个字段，Cap'n Proto 的“原地读取”特性可以避免解包整个消息，开销极小。

### 附录: IPC 传输方式对比 (ZMQ vs Raw Unix Socket)

在 `direct` 模式的基础上，我们还对比了 ZMQ (TCP loopback) 和原生 Unix Domain Socket 作为底层 IPC 传输方式的性能差异。

-   **初步测试**: 在一个简单的实现中，ZMQ 的性能出人意料地超过了原生 Socket。在 4MB 负载的测试下，ZMQ 的平均 RTT 约为 **2559 µs**，而原生 Socket 实现则高达 **5523 µs**。
-   **问题定位**: 经过分析，性能瓶颈在于原生 Socket 实现采用了系统默认的内核收发缓冲区（`SO_SNDBUF`, `SO_RCVBUF`）。对于大尺寸消息，过小的默认缓冲区导致了大量的 `read()`/`write()` 系统调用，从而产生了巨大的延迟开销。
-   **优化与最终结论**: 通过使用 `setsockopt` 将客户端和服务器的 Socket 缓冲区大小手动增加到 8MB（足以容纳整个消息），原生 Unix Socket 的性能得到了极大提升。优化后，其平均 RTT 降低至 **2453 µs**，最终略微优于 ZMQ。

这证明了 ZMQ 作为一个高度优化的库，其默认配置在多种场景下表现优异。然而，对于特定的高性能场景，**直接使用原生 Socket 并进行仔细的参数调优（尤其是缓冲区大小），可以获得极致的性能**。

-   **小包性能 (4KB)**: 对于小尺寸的消息，原生 Socket 的优势更加明显。测试显示其 RTT 约为 **~140 µs**，显著优于 ZMQ 的 **~185 µs**。这说明在延迟敏感的小包场景下，绕过 ZMQ 的抽象层能带来更显著的性能提升。