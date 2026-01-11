#ifndef COMMUNICATION_H
#define COMMUNICATION_H

// NOTE (legacy/internal):
// This header is a historical compatibility entrypoint kept for platform-internal
// code and a small set of regression tests.
//
// New business/service code MUST use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}.
// See docs/ref/推荐用法-P0-通信抽象.md.

// Preserve the same hardening knobs at this legacy include path.
#if defined(WXZ_FORBID_LEGACY_COMMUNICATION)
#error "communication/communication.h is legacy. Use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. See docs/ref/推荐用法-P0-通信抽象.md"
#endif

#if defined(WXZ_ENFORCE_LEGACY_COMMUNICATION_INTERNAL_ONLY) && !defined(WXZ_LEGACY_COMMUNICATION_ALLOWED)
#error "communication/communication.h is legacy and internal-only in this build. Use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. If you are platform internal code/tests, add WXZ_LEGACY_COMMUNICATION_ALLOWED=1 for that target."
#endif

// Actual legacy definitions live under internal headers to make the boundary explicit.
#include "internal/legacy_communicator.h"

#endif // COMMUNICATION_H
