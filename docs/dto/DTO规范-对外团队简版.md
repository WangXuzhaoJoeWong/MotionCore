# MotionCore DTO 规范 —— 对外团队简版

> 适用对象：使用 MotionCore 通信/核心库的业务团队、集成方。
> 重点：只介绍**必须掌握**的 DTO 概念和用法，细节实现留在内部文档。

## 0. 现状对齐（必读）

本文是“对外简版”，可能会滞后于代码演进；当出现“字段/版本策略是否已变化”的疑问时，请以代码为准：

- **权威字段定义（Public API）**：`MotionCore/include/dto/**`（例如 `MotionCore/include/dto/event_dto.h`、`MotionCore/include/dto/image2d_dto.h`、`MotionCore/include/dto/pose3d_dto.h`）。
- **权威序列化实现**：`MotionCore/src/` 下 DTO 相关实现文件（例如 `dto_core.cpp`、`*_dto_*.cpp`、`event_dto_utils.cpp`）。

最小可执行验收入口（如果你只想确认“DTO 能序列化/反序列化且保持兼容”）：

- `ctest -R TestDtoSerialization -V`（CDR round-trip：`Image2dDto`/`Pose3dDto`/`SimplePoseDto`）

现成示例入口（如果你想看 EventDTO 的实际发送/接收形态）：

- `MotionCore/platform_tools/publish_detection`：构造 `EventDTO`，用 `EventDTOUtil` 填充元数据，并通过 `FastddsChannel` + `encode_event_dto_cdr` 发送。
- `MotionCore/platform_tools/fastdds_node_daemon`：可收发并打印 `EventDTO`（用于端到端联调）。

---

## 1. DTO 是什么？
- `MotionCore/platform_tools/publish_detection`：构造 `EventDTO`，用 `EventDTOUtil` 填充元数据，并通过 `FastddsChannel` + `encode_event_dto_cdr` 发送。
- `MotionCore/platform_tools/fastdds_node_daemon`：可收发并打印 `EventDTO`（用于端到端联调）。
- 在 MotionCore 里，DTO 主要有两种形态：
  - **通用事件封装 EventDTO**：适合大部分“业务事件”（检测结果、任务、告警等）。
  - **强类型 DTO**：针对图像、位姿等结构化/大数据，使用专门的类型（如 `Image2dDto`）。
- 外部团队一般 **优先使用 EventDTO**，只有在有明确需求时才会使用或新增强类型 DTO。

---

## 2. 通用事件封装：EventDTO

### 2.1 结构定义

EventDTO 的 IDL 与 C++ 结构已内置在核心库中：

- IDL：`MotionCore/dto/EventDTO.idl`
- C++：`MotionCore/include/dto/event_dto.h`

核心字段如下（简化版）：

- `version`：
  - 协议版本号（整数），破坏性变更时升级，例如 1 → 2。
- `schema_id`：
  - 事件“类型”与版本，例如 `"ws.detection.v1"`、`"order.assigned.v2"`。
- `topic`：
  - 逻辑 topic 名，用于路由和归类，例如 `"perception.luggage"`、`"ws.status"`。
- `payload`：
  - 字符串形式的业务负载，推荐格式：`"key=value;key2=value2"` 或 JSON。
- `timestamp`：
  - 事件发生时间戳（Unix epoch 毫秒），用于审计、排序与链路追踪。
- `event_id`：
  - 事件唯一 ID（例如 `"<timestamp>-<random>"` 或 UUID），用于去重、排查问题。
- `source`：
  - 事件来源标识，例如 `"rw_luggage_workstation"`、`"my_service"`。

### 2.2 推荐使用约定

- **schema_id 命名建议**：
  - 结构：`<业务域>.<事件名>.v<版本>`
  - 示例：`ws.detection.v1`，`ws.status.v1`，`robot.alarm.v2`。
- **payload 约定**：
  - 轻量场景：使用 `key=value;key2=value2`，字段不包含 `;` 与 `=` 即可。
  - 复杂内容：使用 JSON 文本，并在文档中给出字段说明。
- **时间与溯源**：
  - `timestamp` 与 `event_id` 建议**始终填写**（可以用工具自动填充，见下节）。
  - `source` 建议使用稳定的“服务/节点名”，方便排查问题。

---

## 3. EventDTO 辅助工具（强烈推荐使用）

为避免大家自己拼字符串、造 ID，核心库提供了一个辅助工具 `EventDTOUtil`：

- 头文件：`include/dto/event_dto.h`
- 实现：`src/event_dto_utils.cpp`

主要接口：

### 3.1 payload 解析 / 构造

```cpp
using KvMap = std::unordered_map<std::string, std::string>;

// 将 "k1=v1;k2=v2" 解析为 {"k1"->"v1", "k2"->"v2"}
static KvMap EventDTOUtil::parsePayloadKv(const std::string& payload);

// 将 {"k1"->"v1"} 组装为 "k1=v1;k2=v2" 字符串
static std::string EventDTOUtil::buildPayloadKv(const KvMap& kvs);
```

- 建议：
  - **生产端**统一用 `buildPayloadKv` 生成 payload；
  - **消费端**统一用 `parsePayloadKv` 解析 payload，不再自己写 `split(';')/find('=')`。

### 3.2 元数据自动填充

```cpp
// 自动填充 timestamp / event_id / source
static void EventDTOUtil::fillMeta(EventDTO& dto,
                                   const std::string& default_source = {});
```

行为说明：

- 若 `dto.timestamp == 0`，填当前时间（毫秒）。
- 若 `dto.event_id` 为空，生成 `"<timestamp>-<random>"` 形式的 ID。
- 若 `dto.source` 为空且传入了 `default_source`，填入该来源字符串。

> 建议所有生产端统一在发送前调用一次 `fillMeta(dto, "my_service")`，保证链路可追踪。

---

## 4. 生产/消费示例（外部团队常用场景）

### 4.1 生产端：发送检测事件

```cpp
#include "dto/event_dto.h"
#include "dto/event_dto_cdr.h"
#include "fastdds_channel.h"

void send_detection(wxz::core::FastddsChannel& pub,
                    const std::string& bag_id,
                    const std::string& topic) {
    EventDTO dto;
    dto.version   = 1;
    dto.schema_id = "ws.detection.v1";
    dto.topic     = topic; // 如 "perception.luggage"

    EventDTOUtil::KvMap kv{
        {"id",    bag_id},
        {"pick",  "0,0,0,0,0,0,0"},
        {"place", "0.4,-0.1,0.2,0,0,0,0"},
    };
    dto.payload = EventDTOUtil::buildPayloadKv(kv);

    // 自动填充 timestamp / event_id / source
    EventDTOUtil::fillMeta(dto, "my_service_name");

  std::vector<std::uint8_t> buf;
  if (wxz::dto::encode_event_dto_cdr(dto, buf) && !buf.empty()) {
    pub.publish(buf.data(), buf.size());
  }
}
```

### 4.2 消费端：解析检测事件

```cpp
#include "dto/event_dto.h"
#include "dto/event_dto_cdr.h"
#include "fastdds_channel.h"

void install_detection_handler(wxz::core::FastddsChannel& sub) {
  sub.subscribe([](const std::uint8_t* data, std::size_t size) {
    std::vector<std::uint8_t> buf(data, data + size);

    EventDTO dto;
    if (!wxz::dto::decode_event_dto_cdr(buf, dto)) {
      return;
    }

    auto kv = EventDTOUtil::parsePayloadKv(dto.payload);
    auto get = [&](const std::string& key) -> std::string {
      auto it = kv.find(key);
      return (it == kv.end()) ? std::string() : it->second;
    };

    std::string bag_id = get("id");
    std::string pick   = get("pick");
    std::string place  = get("place");

    // 这里可以使用 dto.timestamp / dto.event_id / dto.source 做日志打点
    (void)bag_id;
    (void)pick;
    (void)place;

    // TODO: 解析关节字符串、入队任务等业务逻辑
  });
}
```

---

## 5. 版本与兼容策略（对外视角）

### 5.1 schema_id 与字段演进

- **字段新增（非破坏性）**：
  - 在 `payload` 中增加新的 key 即可（或 JSON 增加字段）；
  - 原有消费者忽略未知 key，新消费者可以按需读取。
- **破坏性变更**（字段语义改变、字段删除等）：
  - 建议**新建 schema_id 版本**：
    - 从 `ws.detection.v1` 升级为 `ws.detection.v2`；
    - 旧/新版本可以并行一段时间，逐步迁移消费者。

### 5.2 EventDTO 自身字段

- `version` 字段主要用于标识 EventDTO **整体结构** 的大版本（需要内部统一协调，一般外部团队不单独改）。
- `timestamp/event_id/source`：
  - 视为**通用元数据**，推荐所有业务统一使用：
    - 日志中输出三元组 `(event_id, timestamp, source)`，便于跨系统排查。

---

## 6. 何时考虑“强类型 DTO”？

大部分业务使用 EventDTO 即可。如果满足以下任一情况，建议和我们一起评估是否引入/复用强类型 DTO：

- 频繁发送**大体量数据**（如图像、点云、轨迹数组等）；
- 对字段类型/单位/坐标系有**严格约束**，不希望只用字符串表达；
- 需要与其他协议（如 ROS msg、第三方系统）强一致的结构。

当前已有的强类型 DTO 示例：

- 图像：`Image2dDto`（头文件见 `include/dto/image2d_dto.h`）。
- 位姿：`Pose3dDto`（头文件见 `include/dto/pose3d_dto.h`）。

如需新增，请与平台/核心库一起：

1. 定义字段与单位（可以先写在文档或表格里）。  
2. 通过内部生成脚本从 IDL 生成 DTO 代码。  
3. 约定接入方（生产者/消费者）与话题名、QoS 策略。

---

## 7. 行李工作站场景示例

> 下面以“行李工作站（rw_luggage_workstation）”为例，演示一个完整闭环：检测 → 工作站 → 中央调度。

### 7.1 话题与 schema 约定

- 检测事件（DTO）：
  - Topic：`perception.luggage`
  - schema_id：`ws.detection.v1`
  - DTO：`EventDTO`
  - payload 字段：
    - `id`：行李唯一标识（字符串）。
    - `pick`：抓取位姿（关节数组字符串，如 `"0,0,0,0,0,0,0"`）。
    - `place`：放置位姿（同上）。

- 工作站状态（字符串）：
  - Topic：`ws.status`
  - schema：当前为简单字符串，不使用 DTO，可后续升级为 EventDTO。
  - payload 约定：`"station=<id>;capacity=<n>;load=<n>;status=<text>"`。

- 调度指令（字符串）：
  - Topic：`ws.control`
  - schema：当前为简单字符串，不使用 DTO，可后续升级为 EventDTO。
  - payload 约定：按业务自行约定，例如 `"task_id=...;bag_id=..."`。

### 7.2 检测侧（上游系统）如何接入

最小接入步骤：

1. 选 topic：`perception.luggage`。
2. 选 schema_id：`ws.detection.v1`。
3. 构造 payload：包含 `id/pick/place` 三个字段。
4. 使用 EventDTO + EventDTOUtil 发送。

示例（C++）：

```cpp
EventDTO dto;
dto.version   = 1;
dto.schema_id = "ws.detection.v1";
dto.topic     = "perception.luggage";

EventDTOUtil::KvMap kv{
    {"id",    bag_id},
    {"pick",  pick_joints},   // 例如 "0,0,0,0,0,0,0"
    {"place", place_joints},  // 例如 "0.4,-0.1,0.2,0,0,0,0"
};
dto.payload = EventDTOUtil::buildPayloadKv(kv);

EventDTOUtil::fillMeta(dto, "luggage_detector");
std::vector<std::uint8_t> buf;
if (wxz::dto::encode_event_dto_cdr(dto, buf) && !buf.empty()) {
  pub.publish(buf.data(), buf.size());
}
```

### 7.3 工作站侧如何消费

工作站进程内部大致逻辑（简化）：

1. 从 `perception.luggage` 订阅 EventDTO；
2. 使用 `parsePayloadKv` 解析 `id/pick/place`；
3. 构造内部任务，进入规划与执行流水线；
4. 在不同阶段通过字符串 status 话题上报进度。

关键解析片段示例：

```cpp
// 在 FastddsChannel 的订阅回调中解码
std::vector<std::uint8_t> buf(data, data + size);
EventDTO dto;
if (!wxz::dto::decode_event_dto_cdr(buf, dto)) return;

auto kv = EventDTOUtil::parsePayloadKv(dto.payload);
std::string bag_id = kv["id"]; // 建议带存在性检查
std::string pick   = kv["pick"];
std::string place  = kv["place"];

// TODO: parse joints & plan motion
```

### 7.4 中央调度如何利用 DTO

当前中央调度主要消费 `ws.status`（字符串），也可以按以下方式逐步演进：

1. 保持现状：继续使用字符串 status，解析 `station/capacity/load/status` 四个字段。
2. 升级方案（可选）：
   - 将 status 也封装成 EventDTO：
     - Topic：`ws.status`
     - schema_id：`ws.status.v1`
     - payload 字段：`station/capacity/load/status/error_code/...`。
   - 调度端统一用 EventDTO + EventDTOUtil 解析，方便后续增加字段（如 `latency`、`error_code`）。

> 对外团队如果只接入“检测侧”，只需要实现 **7.2 检测侧如何接入** 即可；
> 如需对接调度或自研工作站，可以参考 7.3 / 7.4 的思路沿用 EventDTO 规范。

---

## 8. 外部团队实际接入 Checklist

1. **确认场景**：使用 EventDTO 即可，还是需要强类型 DTO？
2. **确定 topic 与 schema_id**：
   - 例如：`perception.luggage` + `ws.detection.v1`。
3. **约定 payload 字段**：
   - 列出 `id/pick/place/...` 等字段含义和格式。
4. **在代码中使用 EventDTO + EventDTOUtil**：
   - 发送前调用 `buildPayloadKv + fillMeta`；
   - 接收后调用 `parsePayloadKv`。
5. **做好版本演进预案**：
   - 预留 `.v2` 版本号，避免将来被动重构。

如在接入过程中遇到 DTO 相关问题（字段设计、版本演进、性能等），可以直接基于本规范提 issue 或沟通，我们会一起评估是否沉淀为“公司级通用 DTO”。
