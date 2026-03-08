#pragma once

#include <stdexcept>
#include <algorithm>
#include <unistd.h>

#ifdef __linux__
  #include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
  #include <sys/event.h>
  #include <sys/time.h>
#else
  #error "Unsupported platform: need Linux (epoll) or macOS/BSD (kqueue)"
#endif

// ─────────────────────────────────────────────
// PollEvent — unified event notification
// ─────────────────────────────────────────────
struct PollEvent {
    int  fd;
    bool readable;
    bool writable;
};

// ─────────────────────────────────────────────
// Poller — thin platform abstraction over epoll (Linux) / kqueue (macOS)
// ─────────────────────────────────────────────
class Poller {
public:
    Poller();
    ~Poller();

    void add(int fd, bool want_read, bool want_write);
    void modify(int fd, bool want_read, bool want_write);
    void remove(int fd);

    // Fills events[0..n-1]; returns number of ready events.
    // timeout_ms < 0 = wait forever.
    int wait(PollEvent* events, int max_events, int timeout_ms);

private:
#ifdef __linux__
    int epfd_ = -1;
#else
    int kq_   = -1;
#endif
};

// ─────────────────────────────────────────────
// Inline implementations (header-only)
// ─────────────────────────────────────────────

#ifdef __linux__

inline Poller::Poller() {
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0) throw std::runtime_error("epoll_create1 failed");
}

inline Poller::~Poller() {
    if (epfd_ >= 0) close(epfd_);
}

inline void Poller::add(int fd, bool want_read, bool want_write) {
    epoll_event ev{};
    ev.data.fd = fd;
    if (want_read)  ev.events |= EPOLLIN;
    if (want_write) ev.events |= EPOLLOUT;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);
}

inline void Poller::modify(int fd, bool want_read, bool want_write) {
    epoll_event ev{};
    ev.data.fd = fd;
    if (want_read)  ev.events |= EPOLLIN;
    if (want_write) ev.events |= EPOLLOUT;
    epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
}

inline void Poller::remove(int fd) {
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
}

inline int Poller::wait(PollEvent* out, int max_events, int timeout_ms) {
    epoll_event raw[256];
    int cap = std::min(max_events, 256);
    int n = epoll_wait(epfd_, raw, cap, timeout_ms);
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) {
        out[i].fd       = raw[i].data.fd;
        out[i].readable = (raw[i].events & (EPOLLIN  | EPOLLHUP | EPOLLERR)) != 0;
        out[i].writable = (raw[i].events & EPOLLOUT) != 0;
    }
    return n;
}

#else  // kqueue (macOS / BSD)

inline Poller::Poller() {
    kq_ = kqueue();
    if (kq_ < 0) throw std::runtime_error("kqueue() failed");
}

inline Poller::~Poller() {
    if (kq_ >= 0) close(kq_);
}

inline void Poller::add(int fd, bool want_read, bool want_write) {
    struct kevent changes[2];
    int n = 0;
    if (want_read)
        EV_SET(&changes[n++], fd, EVFILT_READ,  EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (want_write)
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (n) kevent(kq_, changes, n, nullptr, 0, nullptr);
}

inline void Poller::modify(int fd, bool want_read, bool want_write) {
    struct kevent changes[4];
    int n = 0;
    if (want_read) {
        EV_SET(&changes[n++], fd, EVFILT_READ,  EV_ADD | EV_ENABLE, 0, 0, nullptr);
    } else {
        EV_SET(&changes[n++], fd, EVFILT_READ,  EV_DELETE, 0, 0, nullptr);
    }
    if (want_write) {
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    } else {
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }
    // Errors for missing filters (EV_DELETE on unregistered) are silently discarded
    // because nevents=0 (no output buffer for error events).
    kevent(kq_, changes, n, nullptr, 0, nullptr);
}

inline void Poller::remove(int fd) {
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ,  EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kq_, changes, 2, nullptr, 0, nullptr);
}

inline int Poller::wait(PollEvent* out, int max_events, int timeout_ms) {
    struct kevent raw[256];
    int cap = std::min(max_events, 256);

    struct timespec ts{ timeout_ms / 1000, (long)(timeout_ms % 1000) * 1'000'000L };
    const struct timespec* pts = (timeout_ms < 0) ? nullptr : &ts;

    int n = kevent(kq_, nullptr, 0, raw, cap, pts);
    if (n <= 0) return 0;

    for (int i = 0; i < n; i++) {
        out[i].fd       = (int)raw[i].ident;
        out[i].readable = (raw[i].filter == EVFILT_READ);
        out[i].writable = (raw[i].filter == EVFILT_WRITE);
    }
    return n;
}

#endif
