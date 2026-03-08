#include "reactor.h"

#include <iostream>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

// ─────────────────────────────────────────────
// Constructor — creates the notify pipe and adds server + pipe to poller
// ─────────────────────────────────────────────
Reactor::Reactor(int server_fd, ClientManager& clients, DatabaseManager& db)
    : server_fd_(server_fd), clients_(clients), db_(db)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        throw std::runtime_error("Reactor: pipe() failed");
    }
    // Make both ends non-blocking so post_result() never blocks
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    notify_read_fd_  = pipefd[0];
    notify_write_fd_ = pipefd[1];

    poller_.add(server_fd_,     true, false);
    poller_.add(notify_read_fd_, true, false);
}

Reactor::~Reactor() {
    if (notify_read_fd_  >= 0) close(notify_read_fd_);
    if (notify_write_fd_ >= 0) close(notify_write_fd_);
}

// ─────────────────────────────────────────────
// Handler registration — forwarded to the internal HttpRouter
// ─────────────────────────────────────────────
void Reactor::register_ws_handler(WebSocketHandler* handler) {
    router_.register_ws_handler(handler);
}

void Reactor::register_http_handler(HttpHandler* handler) {
    router_.register_http_handler(handler);
}

// ─────────────────────────────────────────────
// run — the event loop; blocks until process exits
// ─────────────────────────────────────────────
void Reactor::run() {
    const int MAX_EVENTS = 64;
    PollEvent events[MAX_EVENTS];

    std::cout << "[Reactor] Event loop started." << std::endl;

    while (true) {
        int n = poller_.wait(events, MAX_EVENTS, -1);

        for (int i = 0; i < n; i++) {
            int  fd       = events[i].fd;
            bool readable = events[i].readable;
            bool writable = events[i].writable;

            if (fd == server_fd_) {
                on_accept();
            } else if (fd == notify_read_fd_) {
                on_db_notify();
            } else {
                // Check conn exists before each call (previous handler may close it)
                if (readable && conns_.count(fd)) {
                    on_readable(fd);
                }
                if (writable && conns_.count(fd)) {
                    on_writable(fd);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────
// on_accept — accept a new TCP connection
// ─────────────────────────────────────────────
void Reactor::on_accept() {
    struct sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);
    int fd = accept(server_fd_, (struct sockaddr*)&addr, &addrlen);
    if (fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) perror("accept");
        return;
    }

    // Non-blocking so write() returns EAGAIN instead of blocking
    fcntl(fd, F_SETFL, O_NONBLOCK);

    poller_.add(fd, true, false);

    ConnectionState cs;
    cs.fd = fd;
    conns_[fd] = std::move(cs);
}

// ─────────────────────────────────────────────
// on_readable — data is available on fd
// ─────────────────────────────────────────────
void Reactor::on_readable(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    ConnectionState& conn = it->second;

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        // Connection closed or error
        if (conn.ws_handler && conn.state == ParseState::WS_CONNECTED) {
            conn.ws_handler->on_close(conn);
        }
        close_conn(fd);
        return;
    }

    conn.read_buf.append(buf, n);

    if (conn.state == ParseState::READING_HTTP_HEADERS) {
        size_t header_end = conn.read_buf.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            std::string headers_raw = conn.read_buf.substr(0, header_end + 4);
            conn.read_buf.erase(0, header_end + 4); // keep any body bytes
            dispatch_http(conn, headers_raw);
        }
        // else: incomplete headers — wait for more data

    } else if (conn.state == ParseState::WS_CONNECTED) {
        // Parse as many complete WS frames as possible
        while (conn.read_buf.size() >= 2) {
            auto& rb = conn.read_buf;

            uint8_t b0 = (uint8_t)rb[0];
            uint8_t b1 = (uint8_t)rb[1];

            uint8_t opcode     = b0 & 0x0F;
            bool    masked     = (b1 & 0x80) != 0;
            size_t  payload_len = b1 & 0x7F;
            size_t  header_len = 2;

            if (payload_len == 126) {
                if (rb.size() < 4) break; // need 2 more length bytes
                payload_len = ((uint8_t)rb[2] << 8) | (uint8_t)rb[3];
                header_len  = 4;
            } else if (payload_len == 127) {
                if (rb.size() < 10) break; // need 8 more length bytes
                payload_len = 0;
                for (int i = 0; i < 8; i++)
                    payload_len = (payload_len << 8) | (uint8_t)rb[2 + i];
                header_len = 10;
            }

            if (masked) header_len += 4;

            if (rb.size() < header_len + payload_len) break; // frame incomplete

            // Extract + XOR-unmask payload
            std::string payload(payload_len, '\0');
            if (masked) {
                const char* mask_ptr = rb.data() + (header_len - 4);
                for (size_t i = 0; i < payload_len; i++)
                    payload[i] = rb[header_len + i] ^ mask_ptr[i % 4];
            } else {
                payload.assign(rb.data() + header_len, payload_len);
            }

            rb.erase(0, header_len + payload_len);

            if (opcode == 0x08) { // Close frame
                if (conn.ws_handler) conn.ws_handler->on_close(conn);
                close_conn(fd);
                return;
            }
            if (opcode == 0x01 || opcode == 0x02) { // Text or Binary
                dispatch_ws_frame(conn, payload);
                // dispatch_ws_frame might close the connection
                if (!conns_.count(fd)) return;
            }
            // Ping (0x09) / Pong (0x0A): silently ignored
        }
    }
}

// ─────────────────────────────────────────────
// on_writable — send buffer has room; drain the write queue
// ─────────────────────────────────────────────
void Reactor::on_writable(int fd) {
    try_flush(fd);
}

// ─────────────────────────────────────────────
// on_db_notify — drain the pipe and deliver pending DB results
// ─────────────────────────────────────────────
void Reactor::on_db_notify() {
    // Drain the pipe (may have multiple bytes if multiple results were posted)
    uint8_t discard[64];
    while (read(notify_read_fd_, discard, sizeof(discard)) > 0) {}

    // Swap under lock to minimise contention
    std::queue<DbResult> local;
    {
        std::lock_guard<std::mutex> lk(result_mutex_);
        std::swap(local, pending_results_);
    }

    while (!local.empty()) {
        auto& r = local.front();
        if (conns_.count(r.target_fd)) {
            queue_write(r.target_fd, r.frame);
        }
        local.pop();
    }
}

// ─────────────────────────────────────────────
// close_conn — remove from poller + map, close fd
// ─────────────────────────────────────────────
void Reactor::close_conn(int fd) {
    poller_.remove(fd);
    conns_.erase(fd);
    close(fd);
}

// ─────────────────────────────────────────────
// queue_write — append data to write queue, try immediate flush
// ─────────────────────────────────────────────
void Reactor::queue_write(int fd, const std::string& data) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;

    ConnectionState& conn = it->second;
    bool was_empty = conn.write_queue.empty();
    conn.write_queue.push_back(data);

    if (was_empty) {
        try_flush(fd);
    }
    // If was_empty==false, EPOLLOUT is already registered and try_flush
    // will be called from on_writable() when the kernel is ready.
}

// ─────────────────────────────────────────────
// try_flush — drain write queue as far as the kernel allows
// ─────────────────────────────────────────────
void Reactor::try_flush(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    ConnectionState& conn = it->second;

    while (!conn.write_queue.empty()) {
        const std::string& data = conn.write_queue.front();
        ssize_t n = write(fd, data.data(), data.size());

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Send buffer full — register EPOLLOUT/EVFILT_WRITE
                poller_.modify(fd, true, true);
                return;
            }
            // Real error — close the connection
            if (conn.ws_handler && conn.state == ParseState::WS_CONNECTED) {
                conn.ws_handler->on_close(conn);
            }
            close_conn(fd);
            return;
        }

        if ((size_t)n < data.size()) {
            // Partial write — keep the remainder
            conn.write_queue.front() = data.substr(n);
            poller_.modify(fd, true, true);
            return;
        }

        conn.write_queue.pop_front();
    }

    // Queue drained — no longer need EPOLLOUT
    poller_.modify(fd, true, false);
}

// ─────────────────────────────────────────────
// dispatch_http — called when full HTTP headers are in the buffer
// ─────────────────────────────────────────────
void Reactor::dispatch_http(ConnectionState& conn,
                             const std::string& headers_raw) {
    bool is_ws = router_.route_buffered(conn, headers_raw);

    if (is_ws) {
        conn.state = ParseState::WS_CONNECTED;
        // O_NONBLOCK was set on accept; WS connection is already non-blocking.
    } else {
        // HTTP response was written directly by the handler; close the fd.
        close_conn(conn.fd);
    }
}

// ─────────────────────────────────────────────
// dispatch_ws_frame — forward a decoded payload to the WS handler
// ─────────────────────────────────────────────
void Reactor::dispatch_ws_frame(ConnectionState& conn,
                                const std::string& payload) {
    if (conn.ws_handler) {
        conn.ws_handler->on_message(conn, payload);
    }
}

// ─────────────────────────────────────────────
// post_result — thread-safe; called by DB worker threads
// ─────────────────────────────────────────────
void Reactor::post_result(int target_fd, std::string frame) {
    {
        std::lock_guard<std::mutex> lk(result_mutex_);
        pending_results_.push({target_fd, std::move(frame)});
    }
    // Wake the event loop
    uint8_t b = 1;
    write(notify_write_fd_, &b, sizeof(b));
}
