# Cap'n Proto vs Direct Memory Performance Test

This project measures and compares the latency of sending data over a ZMQ REQ/REP socket using two different methods:
1.  **capnp**: Serializing the data with Cap'n Proto.
2.  **direct**: Sending the raw C++ struct and payload data directly.

## Dependencies

The following libraries are required:
- ZeroMQ (`libzmq`)
- CppZMQ (C++ wrapper for ZeroMQ)
- Cap'n Proto
- Quill (for logging, though only headers are used)

## Environment Variables

The `Makefile` will use the following environment variables to find the dependencies. If they are not set, it will fall back to default paths under `/home/public/`.

- `ZEROMQ_HOME`: Path to ZeroMQ installation.
- `CPPZMQ_HOME`: Path to CppZMQ installation.
- `QUILL_HOME`: Path to Quill installation.
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

The client will run the test and print its latency statistics upon completion. The server will print its deserialization statistics every `[print_interval]` requests.