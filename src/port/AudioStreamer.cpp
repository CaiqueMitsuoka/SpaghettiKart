#include "AudioStreamer.h"
#include "StreamSocket.h"

#include <arpa/inet.h>
#include <cstring>
#include <vector>

// Wire format is documented in docs/membrane-integration.md - keep both in sync.
namespace {

constexpr int kPort = 5600;
constexpr uint8_t kFormatPcmS16 = 2;

StreamSocket gSocket(kPort);
uint32_t gAudioSeq = 0;
std::vector<uint8_t> gSendBuf;

} // namespace

void AudioStreamer_CaptureAudio(const uint8_t* buf, size_t len, int sampleRate, int channels) {
    gSocket.PollAccept();
    if (!gSocket.HasClient()) {
        return; // No consumer attached - skip entirely.
    }
    if (buf == nullptr || len == 0 || channels <= 0 || channels > 255) {
        return;
    }

    // Payload after the 4-byte length prefix: format u8, sampleRate u32 BE,
    // channels u8, seq u32 BE, timestamp_us u64 BE, then raw interleaved
    // S16 samples in the host's native byte order (this process and its
    // consumers are all little-endian in practice, so no per-sample
    // byteswap - only the header fields are normalized to big-endian, for
    // consistency with FrameStreamer's wire format).
    constexpr size_t kHeaderSize = 1 + 4 + 1 + 4 + 8;
    const size_t payloadSize = kHeaderSize + len;
    gSendBuf.resize(4 + payloadSize);

    uint32_t netLen = htonl((uint32_t)payloadSize);
    memcpy(gSendBuf.data(), &netLen, 4);

    uint8_t* p = gSendBuf.data() + 4;
    *p++ = kFormatPcmS16;
    uint32_t rate = htonl((uint32_t)sampleRate);
    memcpy(p, &rate, 4);
    p += 4;
    *p++ = (uint8_t)channels;
    uint32_t seq = htonl(gAudioSeq++);
    memcpy(p, &seq, 4);
    p += 4;
    uint64_t timestampUs = StreamMonotonicMicros();
    for (int i = 7; i >= 0; i--) {
        *p++ = (uint8_t)(timestampUs >> (i * 8));
    }
    memcpy(p, buf, len);

    gSocket.Send(gSendBuf.data(), gSendBuf.size());
}
