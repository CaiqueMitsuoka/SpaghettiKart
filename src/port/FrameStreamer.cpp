#include "FrameStreamer.h"

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// Local-only TCP frame server. One viewer at a time, loopback only.
// Wire format is documented in docs/liveview-integration.md - keep both in sync.
namespace {

constexpr int kPort = 5599;
constexpr uint8_t kFormatRgba8 = 1;

int gListenFd = -1;
int gClientFd = -1;
uint32_t gFrameSeq = 0;
std::vector<uint8_t> gPixelBuf;
std::vector<uint8_t> gSendBuf;

bool EnsureListening() {
    if (gListenFd >= 0) {
        return true;
    }

    gListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (gListenFd < 0) {
        return false;
    }

    int one = 1;
    setsockopt(gListenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kPort);

    if (bind(gListenFd, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(gListenFd, 1) != 0) {
        close(gListenFd);
        gListenFd = -1;
        return false;
    }

    fcntl(gListenFd, F_SETFL, O_NONBLOCK);
    return true;
}

void AcceptClientIfPending() {
    if (gClientFd >= 0 || gListenFd < 0) {
        return;
    }
    int fd = accept(gListenFd, nullptr, nullptr);
    if (fd >= 0) {
        // gListenFd is non-blocking (below) so EnsureListening() can poll it
        // every frame without stalling the render loop. On macOS/Darwin,
        // accept() inherits that O_NONBLOCK flag onto the new connection, so
        // without this the blocking write loop in SendAll below fails on the
        // first EWOULDBLOCK (whenever a ~1MB+ frame doesn't fit in the
        // socket's send buffer in one syscall) instead of actually blocking,
        // dropping otherwise-healthy clients almost immediately. Force the
        // accepted socket back to blocking mode explicitly so this can't
        // happen regardless of platform inheritance behavior.
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1) {
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        }

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        gClientFd = fd;
        gFrameSeq = 0;
    }
}

void DropClient() {
    if (gClientFd >= 0) {
        close(gClientFd);
        gClientFd = -1;
    }
}

// Blocking write of the full buffer. Simple and correct for a single local
// consumer; a slow consumer will stall the render loop here. Fine for a demo
// over loopback - revisit (e.g. drop-oldest ring buffer) before anything
// remote or production-facing.
bool SendAll(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(gClientFd, data + sent, len - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

} // namespace

void FrameStreamer_CaptureFrame(int /*width*/, int /*height*/) {
    if (!EnsureListening()) {
        return;
    }

    AcceptClientIfPending();
    if (gClientFd < 0) {
        return; // No viewer attached - skip the readback entirely.
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Ignore the caller's width/height entirely - it's the window's logical
    // size (see GfxWindowBackendSDL2::GetDimensions on macOS), and the
    // current GL_VIEWPORT is unreliable too (it reflects whatever the last
    // draw call set, e.g. the game's internal low-res 3D render pass, not
    // necessarily the full default-framebuffer/window size after a final
    // upscale blit). The only ground truth for "how big is the actual
    // window backbuffer right now" is the window system itself.
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(SDL_GL_GetCurrentWindow(), &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    const size_t pixelBytes = (size_t)width * (size_t)height * 4;
    gPixelBuf.resize(pixelBytes);

    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, gPixelBuf.data());

    // Payload after the 4-byte length prefix (matches Erlang's
    // :gen_tcp packet: 4 framing): format u8, width/height/seq u32 BE, pixels.
    constexpr size_t kHeaderSize = 1 + 4 + 4 + 4;
    const size_t payloadSize = kHeaderSize + pixelBytes;
    gSendBuf.resize(4 + payloadSize);

    uint32_t netLen = htonl((uint32_t)payloadSize);
    memcpy(gSendBuf.data(), &netLen, 4);

    uint8_t* p = gSendBuf.data() + 4;
    *p++ = kFormatRgba8;
    uint32_t w = htonl((uint32_t)width);
    uint32_t h = htonl((uint32_t)height);
    uint32_t seq = htonl(gFrameSeq++);
    memcpy(p, &w, 4);
    p += 4;
    memcpy(p, &h, 4);
    p += 4;
    memcpy(p, &seq, 4);
    p += 4;

    // glReadPixels gives bottom-left origin rows; flip here so the consumer
    // always gets a normal top-left-origin image.
    const size_t rowBytes = (size_t)width * 4;
    for (int row = 0; row < height; row++) {
        const uint8_t* src = gPixelBuf.data() + (size_t)(height - 1 - row) * rowBytes;
        memcpy(p + (size_t)row * rowBytes, src, rowBytes);
    }

    if (!SendAll(gSendBuf.data(), gSendBuf.size())) {
        DropClient();
    }
}
