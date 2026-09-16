#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

// Minimal local-only (loopback) TCP server: accepts one client at a time,
// skips all work when nothing is attached. Shared plumbing for
// FrameStreamer (video) and AudioStreamer (audio) - see
// docs/liveview-integration.md and docs/membrane-integration.md for the
// wire formats built on top of this.
class StreamSocket {
  public:
    explicit StreamSocket(int port) : mPort(port) {
    }
    ~StreamSocket();

    StreamSocket(const StreamSocket&) = delete;
    StreamSocket& operator=(const StreamSocket&) = delete;

    // Accepts a pending connection if one is waiting and none is attached
    // yet. Cheap no-op otherwise - safe to call every frame/tick.
    void PollAccept();

    bool HasClient() const {
        return mClientFd >= 0;
    }

    // Blocking send of the full buffer (caller owns any length-prefix
    // framing). A slow consumer stalls the caller here rather than dropping
    // frames - fine for a single local demo viewer, not for anything remote
    // or with untrusted/many consumers. Drops the client and returns false
    // on any send failure so the next PollAccept() can pick up a new one.
    bool Send(const uint8_t* data, size_t len);

  private:
    int mPort;
    int mListenFd = -1;
    int mClientFd = -1;

    bool EnsureListening();
    void DropClient();
};

// Microseconds since first call, monotonic. Shared across every
// StreamSocket-based producer in this process (video, audio, ...) so their
// timestamps land in the same clock domain and a downstream consumer (e.g.
// a Membrane pipeline) can line them up without any cross-process clock
// sync.
inline uint64_t StreamMonotonicMicros() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return (uint64_t)duration_cast<microseconds>(steady_clock::now() - start).count();
}
