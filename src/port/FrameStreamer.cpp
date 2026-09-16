#include "FrameStreamer.h"
#include "StreamSocket.h"

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <arpa/inet.h>
#include <cstring>
#include <vector>

// Wire format is documented in docs/liveview-integration.md - keep both in sync.
namespace {

constexpr int kPort = 5599;
constexpr uint8_t kFormatRgba8 = 1;

StreamSocket gSocket(kPort);
uint32_t gFrameSeq = 0;
std::vector<uint8_t> gPixelBuf;
std::vector<uint8_t> gSendBuf;

} // namespace

void FrameStreamer_CaptureFrame(int /*width*/, int /*height*/) {
    gSocket.PollAccept();
    if (!gSocket.HasClient()) {
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
    // :gen_tcp packet: 4 framing): format u8, width/height/seq u32 BE,
    // timestamp_us u64 BE, pixels.
    constexpr size_t kHeaderSize = 1 + 4 + 4 + 4 + 8;
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
    // Explicit big-endian byte write, MSB first - no reliance on htonl's
    // return value being safe to shift/combine arithmetically.
    uint64_t timestampUs = StreamMonotonicMicros();
    for (int i = 7; i >= 0; i--) {
        *p++ = (uint8_t)(timestampUs >> (i * 8));
    }

    // glReadPixels gives bottom-left origin rows; flip here so the consumer
    // always gets a normal top-left-origin image.
    const size_t rowBytes = (size_t)width * 4;
    for (int row = 0; row < height; row++) {
        const uint8_t* src = gPixelBuf.data() + (size_t)(height - 1 - row) * rowBytes;
        memcpy(p + (size_t)row * rowBytes, src, rowBytes);
    }

    gSocket.Send(gSendBuf.data(), gSendBuf.size());
}
