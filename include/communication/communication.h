#ifndef COMMUNICATION_H
#define COMMUNICATION_H

// 注意（legacy/internal）：
// 该头文件是历史兼容入口，仅保留给平台内部代码和少量回归测试使用。
//
// 新业务/服务代码必须使用 wxz::core::{FastddsChannel,InprocChannel,ShmChannel}。
// 详见 docs/ref/推荐用法-P0-通信抽象.md。

// 在这个 legacy include 路径下保留相同的加固开关。
#if defined(WXZ_FORBID_LEGACY_COMMUNICATION)
#error "communication/communication.h is legacy. Use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. See docs/ref/推荐用法-P0-通信抽象.md"
#endif

#if defined(WXZ_ENFORCE_LEGACY_COMMUNICATION_INTERNAL_ONLY) && !defined(WXZ_LEGACY_COMMUNICATION_ALLOWED)
#error "communication/communication.h is legacy and internal-only in this build. Use wxz::core::{FastddsChannel,InprocChannel,ShmChannel}. If you are platform internal code/tests, add WXZ_LEGACY_COMMUNICATION_ALLOWED=1 for that target."
#endif

// 实际的 legacy 定义放在 internal 头文件下，以明确边界。
#include "internal/legacy_communicator.h"

#endif // COMMUNICATION_H
