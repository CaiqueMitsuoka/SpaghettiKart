#pragma once

// Streams rendered frames out over a local TCP socket so an external process
// (e.g. a Phoenix/Elixir app) can pick them up for LiveView streaming.
// See docs/liveview-integration.md for the wire format and consumer contract.
//
// Called once per rendered frame from Interpreter::EndFrame() (libultraship,
// see the forward declaration there) with the current framebuffer dimensions.
// No-op until a consumer connects; does nothing if no viewer is attached.
// extern "C": the caller forward-declares this itself (can't reach this
// header from the libultraship submodule) and needs C linkage so namespace
// nesting on that side can't cause a link mismatch.
extern "C" void FrameStreamer_CaptureFrame(int width, int height);
