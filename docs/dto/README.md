# MotionCore DTO 规范（唯一入口）

适用范围：`MotionCore/dto/*.idl` 以及对外头 `MotionCore/include/dto/**`。

目标：把“DTO 是什么、怎么用、怎么演进、怎么验收”收敛为**一份可执行规范**，避免多份文档相互引用造成歧义。

## 0. 权威来源（以代码为准）

当出现“字段/版本策略是否变化”的疑问时，以以下内容为准：

- 对外头（Public API）：`MotionCore/include/dto/**`
- IDL（schema 唯一来源）：`MotionCore/dto/*.idl`
- baseline（已发布快照）：`MotionCore/dto/baseline/*.idl`（用于人工 diff 与兼容讨论）
- IDL SHA256 契约头：`MotionCore/include/dto/*_idl_version.h`
- 编解码实现：`MotionCore/src/*_dto_cdr.cpp`、`MotionCore/src/*_dto_utils.cpp`

## 1. DTO 形态与推荐用法

MotionCore 中 DTO 主要分两类：

- 通用事件封装 `EventDTO`：覆盖绝大部分“事件/状态/任务”等业务载荷（推荐优先使用）
- 强类型 DTO：用于大体量或强结构场景（图像、位姿、轨迹数组等），按需引入/新增

对外团队默认建议：**优先用 `EventDTO`**；只有在 payload 过大、强类型约束明确时再评估强类型 DTO。

## 2. EventDTO（通用事件封装）

- IDL：`MotionCore/dto/EventDTO.idl`
- C++：`MotionCore/include/dto/event_dto.h`

建议约定（对外可执行）：

- `schema_id`：`<domain>.<name>.v<ver>`，例如 `ws.detection.v1`
- `payload`：轻量可用 `k=v;k2=v2`；复杂内容用 JSON（文档需列字段语义）
- `timestamp/event_id/source`：建议生产端统一填充，便于审计与排障

推荐工具：`EventDTOUtil`（避免手工拼字符串/造 ID）

- `EventDTOUtil::buildPayloadKv` / `parsePayloadKv`
- `EventDTOUtil::fillMeta(dto, "my_service")`

如果你的业务侧采用 `wxz::framework`，推荐直接用框架层的 `Node::create_subscription_eventdto/create_publisher_eventdto`，并遵循“业务回调不跑 DDS listener 线程”的约定（见 `MotionCore/docs/框架层约定与用法.md`）。

## 3. 兼容性策略（IDL 维度，必须遵守）

### 3.1 允许的变更（向后兼容）

- 仅允许在结构体末尾追加字段（append-only）
- 追加字段必须给出“旧数据默认语义”（见下方变更记录模板）

### 3.2 不允许的变更（breaking）

以下任一变更视为 breaking：

- 调整字段顺序/重新排列字段
- 删除字段
- 修改字段类型
- 修改字段语义但仍复用原字段名

说明：当前 DTO 使用 Fast CDR 编解码，字段顺序必须与 IDL 匹配。

### 3.3 baseline 的含义

- `MotionCore/dto/baseline/<X>.idl` 表示“已发布对外”的 IDL 快照
- 发生 breaking 时：先明确 breaking 结论与迁移窗口，再刷新 baseline

## 4. 修改/新增 DTO 的最小流程（可执行）

1) 修改/新增 `MotionCore/dto/<X>.idl`
2) 更新/新增 `MotionCore/include/dto/<x_snake>_idl_version.h`（让 SHA256 与当前 IDL 一致）
3) 在本文件追加一条变更记录（append-only），写清：变更摘要、兼容性结论、新的 SHA256
4) 若为 append：写清默认语义（旧数据如何解释）
5) 若为 breaking：同步刷新 `MotionCore/dto/baseline/<X>.idl`

本地自检：

```bash
sha256sum MotionCore/dto/*.idl

diff -u MotionCore/dto/baseline/EventDTO.idl MotionCore/dto/EventDTO.idl || true
diff -u MotionCore/dto/baseline/HeartbeatDTO.idl MotionCore/dto/HeartbeatDTO.idl || true
```

## 5. 变更记录（Append-Only）

### 5.1 EventDTO

- 当前 IDL：`MotionCore/dto/EventDTO.idl`
- 当前 SHA256：`c72f4bf4025c3c34a3f3d58f1c90afba33c5bd5cb0fb0e67b17c72272da65603`
- baseline：`MotionCore/dto/baseline/EventDTO.idl`
- baseline SHA256：`d8425d6fb057e68d8740bab2b707922a91bc3d81718e6565a92ef67b2bac43c8`
- 记录：
  - 2026-01-14：文档收敛为单一入口；未对 IDL 做变更。

> 追加字段默认语义（如未来 append）：
> - `string`：默认空字符串
> - 数值：默认 0

### 5.2 HeartbeatDTO

- 当前 IDL：`MotionCore/dto/HeartbeatDTO.idl`
- 当前 SHA256：`9caf9790360ba32b9804f1f9b83177c058dffead9d036a74e9896d4ccba9c7b6`
- baseline：`MotionCore/dto/baseline/HeartbeatDTO.idl`
- baseline SHA256：`cf1a2a9488a876b222b3ece7046b59d2c59b1fe10e8f232d87f7c5592e19d396`
- 记录：
  - 2026-01-14：文档收敛为单一入口；未对 IDL 做变更。

> 追加字段默认语义（如未来 append）：
> - `string`：默认空字符串
> - 数值：默认 0
