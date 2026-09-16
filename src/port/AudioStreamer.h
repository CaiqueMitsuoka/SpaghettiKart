#pragma once

#include <cstddef>
#include <cstdint>

// Streams mixed audio out over a local TCP socket, same design as
// FrameStreamer (video). See docs/membrane-integration.md for the wire
// format and consumer contract.
//
// Called once per audio frame from AudioPlayer::Play() (libultraship, see
// the forward declaration there) with the interleaved PCM buffer as the
// game already has it. No-op until a consumer connects.
//
// `buf` is interleaved signed 16-bit PCM, native (host) byte order,
// `channels` channels, `sampleRate` Hz. Skips capture (silently) when the
// buffer isn't plain interleaved PCM in a channel count this can describe
// correctly - see the implementation for the one excluded case (5.1 matrix
// decode, which happens after this hook fires).
extern "C" void AudioStreamer_CaptureAudio(const uint8_t* buf, size_t len, int sampleRate, int channels);
