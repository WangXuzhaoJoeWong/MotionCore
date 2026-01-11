# DTO 规范示例（可按需裁剪）

本文档给出一份可落地的 DTO 设计与演进规范，用于跨模块/跨进程的数据交换（事件、任务、状态、遥测等）。你可以在此基础上补充业务字段或删除不需要的部分。

## 现状口径（P2，可执行）

本仓库把“DTO 的 schema 演进”收敛为三件可回归的事实契约：

- **IDL 是唯一 schema 源**：所有 DTO 的 schema 必须放在 `dto/*.idl`。
- **baseline 是已发布快照**：每个 `dto/<X>.idl` 必须有 `dto/baseline/<X>.idl`，用于向后兼容回归。
- **文档与 hash 受门禁约束**：
  - 每个 `dto/<X>.idl` 必须有对应的 `include/dto/<x_snake>_idl_version.h`，对外发布当前 IDL 的 SHA256（回归会校验）。
  - 兼容性策略与变更记录以 [DTO_IDL_兼容性策略与变更记录.md](DTO_IDL_兼容性策略与变更记录.md) 为唯一口径（回归会校验该文档包含每个 IDL 的 SHA256，且 append 字段时必须写清默认语义）。

说明：上述路径均以仓库根目录为基准；当前核心代码位于 `MotionCore/`。

### 修改/新增 DTO 的最小流程

1) 修改（或新增）`dto/<X>.idl`
2) 更新（或新增）`include/dto/<x_snake>_idl_version.h`（让 SHA256 与当前 IDL 一致）
3) 更新 [DTO_IDL_兼容性策略与变更记录.md](DTO_IDL_兼容性策略与变更记录.md)：写清变更摘要、兼容性结论，并包含新的 SHA256
4) 若是 append 字段：在对应 `### <X>` 小节下补充 `- <field_name>: 默认 <...>`（回归会校验格式）
5) 若是 breaking：先在文档中明确结论，再同步升级协议版本并刷新 baseline

### 验收入口

- `ctest -R TestDtoIdlVersion -V`
- `ctest -R TestDtoIdlCompatibilityPolicy -V`
- `ctest -R TestDtoIdlBackwardCompat -V`

### 辅助命令（开发者自检）

- `cmake -DSRC_DIR=$PWD -P MotionCore/tools/print_dto_idl_shas.cmake`

## 14. EventDTO 作为通用事件封装

当前仓库内的 DTO 示例以 EventDTO 为主（现状：已有 `dto/EventDTO.idl`、`dto/HeartbeatDTO.idl`）。

- Schema：`MotionCore/dto/EventDTO.idl`
- IDL hash 契约头：`MotionCore/include/dto/event_dto_idl_version.h`（由 `TestDtoIdlVersion` 校验）
- C++ 结构与工具：`MotionCore/include/dto/event_dto.h` / `MotionCore/src/event_dto_utils.cpp`
- 兼容性策略与变更记录：见 [DTO_IDL_兼容性策略与变更记录.md](DTO_IDL_兼容性策略与变更记录.md)

快速自检（开发态）：

- `ctest -R TestDtoIdlVersion -V`
- `ctest -R TestDtoIdlCompatibilityPolicy -V`
- `ctest -R TestDtoIdlBackwardCompat -V`
    1. 使用 `receiveDTO(topic, dto)` 获取事件。
    2. 通过 `EventDTOUtil::parsePayloadKv(dto.payload)` 获得 `{key->value}` 映射。
    3. 读取所需字段，并可利用 `timestamp/event_id/source` 做审计和日志关联。

## 15. 开发者接入指引（对外暴露）
- 发布内容：
  - 头文件/IDL：install/include/MotionCore/dto 或 dto/*.idl（生成代码一起发布）。
  - 类型注册：在文档中列出 DTO 名称、版本、对应话题、QoS 建议。
  - 示例代码：生产/消费示例（FastDDS 发布/订阅或插件接口调用）。
- 扩展流程：
  1) 优先复用既有 DTO；不足时新增专用 DTO（或临时使用 Envelope+payload，并计划转正）。
  2) 新增时定义版本与枚举，补充字段表、默认值、单位/坐标系。
  3) 提交变更说明（影响话题/生产者/消费者），更新文档与生成代码。

## 16. 仓库落地与使用指引
- 核心接口与序列化：见 [MotionCore/include/dto/dto_core.h](../../include/dto/dto_core.h) 与 [MotionCore/src/dto_core.cpp](../../src/dto_core.cpp)，支持 CDR 二进制后端（可扩展更多类型）。
- 已实现强类型 DTO 示例：
  - 图像 [MotionCore/include/dto/image2d_dto.h](../../include/dto/image2d_dto.h) / [MotionCore/src/image2d_dto.cpp](../../src/image2d_dto.cpp)
  - 位姿 [MotionCore/include/dto/pose3d_dto.h](../../include/dto/pose3d_dto.h) / [MotionCore/src/pose3d_dto.cpp](../../src/pose3d_dto.cpp)
- 生成器：已从核心库仓库移除；如需从 IDL/YAML 自动生成 DTO/注册代码，建议在外部工具仓库/流水线中实现并把生成物按“普通源码”纳入构建与回归。
- 测试：`ctest --output-on-failure` 包含 DTO round-trip 用例（Image2dDto + Pose3dDto）。
- 安装与打包：
  ```bash
  cd MotionCore/build
  make install
  tar czf MotionCore-install.tar.gz ../_install/
  ```

## 17. 对外 API/DTO 兼容与发布节奏
- 版本策略：核心 API/DTO 采用 SemVer，当前基线冻结为 1.x（新增字段仅增量发布 minor；破坏性变更需 major）。TypeInfo.version 与文档同步更新。
- 兼容窗口：向后兼容 2 个次版本（N、N-1、N-2）；旧版本保留读能力，写能力仅提供 N/N-1。
- 发布节奏：建议月度 minor、季度或半年 major，重大变更需提前至少一版公告。
- 回归矩阵：
  - 序列化后端：CDR（必测），后续扩展 JSON/CBOR 时补充。
  - 通道类型：FastDDS、inproc、shm（如启用）。
  - 平台：Linux x86_64（必测），如有 ARM/多发行版需求需补充。
- 契约测试：
  - DTO 契约：IDL/TypeInfo 与生成代码一致性检查，序列化/反序列化往返 + schema_hash 校验。
  - API 契约：TypeRegistry 注册/查找行为、插件加载/卸载流程、默认 QoS/线程安全约束。
  - 兼容用例：新旧版本互通（Reader 解析旧 Writer、Writer 发送新字段 Reader 忽略）。








# 以下是面向“稳定核心 + 可插拔边缘”的 DTO 功能模块设计方案（实现思路 + 开发流程），满足 4 点要求。

**核心层（稳定，4-5 个虚函数）**
- 抽象接口 `IDto`（永不改动）：
  - `virtual const TypeInfo& type() const = 0;`
  - `virtual bool serialize(Serializer& out) const = 0;`
  - `virtual bool deserialize(Deserializer& in) = 0;`
  - `virtual uint32_t version() const = 0;`
  - （可选）`virtual std::unique_ptr<IDto> clone() const = 0;`
- `TypeInfo`：包含 `name`（如 "sensor.image2d.v1"）、`schema_hash`、`version`、`content_type`（json/cbor/proto/idl-binary）、`compat_policy`。
- 类型注册表 `TypeRegistry`：
  - `registerFactory(TypeInfo, FactoryFn)`，`find(name)`，`list()`
  - 运行时可查询；支持按 name/version 解析；对未知字段容忍。
- 序列化引擎 `Serializer`/`Deserializer`（接口稳定）：
  - 后端插件：JSON/CBOR/Protobuf/FlatBuffers/IDL binary。
  - 通过工厂获取具体后端。

**中间层（隔离变化）**
- 适配器工厂：将 `TypeInfo.name` 映射到具体 DTO 类（插件自动注册）。
- 协议转换器：在 FastDDS/自定义通道间做封装/拆封（封装 `TypeInfo + payload`）。
- 缓存管理器：可选，做零拷贝引用或外部资源（SHM/URI）。

**插件层（变化收敛处）**
- 自动生成的 DTO 类（每个传感器/执行器/业务消息）：
  - 继承 `IDto`，实现 serialize/deserialize，内嵌字段表。
  - 编译为插件 `.so`，加载即注册到 `TypeRegistry`。
- 插件接口（业务逻辑）：消费/生产 DTO，按注册表查找类型。
- YAML 描述文件：声明字段、类型、可选性、默认值、单位/坐标系、版本。

**开发流程（自动化生成）**
1) 新传感器 → 编写 YAML 描述（schema 名、version、字段、单位、可选/必选、默认值、枚举等）。
2) 运行生成器 → 输出：
   - 头文件/源文件：DTO 类（继承 `IDto`），序列化代码。
   - 插件入口：`extern "C" IDto* Create()` + 注册逻辑。
   - 可选：ROS2 msg/IDL 同步生成，保证多协议一致。
3) 实现插件业务逻辑：只处理字段与业务，不关心框架核心。
4) 部署时插件自动注册：加载 `.so` 后在 `TypeRegistry` 可见；运行时按 `TypeInfo` 解析消息。

**双向兼容策略**
- 新版理解旧版：反序列化时对缺省字段使用默认值；枚举追加不重排。
- 旧版忽略新增字段：反序列化时跳过未知字段（依赖后端支持，比如 CBOR/JSON/Protobuf 的“未知字段忽略”特性）。
- 版本字段：每个 DTO 内含 `version()`；TypeInfo 也带 version；支持 name+version 选择。
- schema_hash：用于防呆；兼容策略允许 hash 变化但版本递增。

**元数据驱动**
- `TypeInfo` + YAML/IDL schema 在运行时可查询（注册表提供 introspection：字段列表、类型、可选性、默认值、单位、说明）。
- 可导出 schema（JSON Schema/IDL）供前端或监控查看。

**通用 Envelope（建议）**
- 固定骨架：Header(id, timestamp, source, version) + TypeInfo + payload bytes。
- payload 由具体 DTO 负责 encode/decode；Envelope 只关心调度与路由。
- QoS/通道选择由上层决定（FastDDS/ROS2/topic 名）。

**最小接口示例（稳定核心）**
```cpp
struct TypeInfo {
  std::string name;        // e.g. "sensor.image2d"
  uint32_t    version;     // e.g. 1
  std::string content_type;// "cbor"/"json"/"proto"/"idl-bin"
  uint64_t    schema_hash; // optional
};

class Serializer { /* abstract */ };
class Deserializer { /* abstract */ };

class IDto {
public:
  virtual ~IDto() = default;
  virtual const TypeInfo& type() const = 0;
  virtual uint32_t version() const = 0;
  virtual bool serialize(Serializer& out) const = 0;
  virtual bool deserialize(Deserializer& in) = 0;
  virtual std::unique_ptr<IDto> clone() const = 0;
};
```

**生成器输入 YAML 示例（简化）**
```yaml
name: sensor.image2d
version: 1
content_type: cbor
fields:
  - name: width      ; type: uint32
  - name: height     ; type: uint32
  - name: encoding   ; type: string ; enum: [rgb8,bgr8,mono8]
  - name: step       ; type: uint32
  - name: data       ; type: bytes  ; note: allow external reference
  - name: frame_id   ; type: string ; optional: true
compat:
  ignore_unknown_fields: true
  default_values:
    frame_id: "map"
```

**典型传感器/执行器 DTO（生成器可预置模板）**
- 传感器：Image2D, PointCloud, IMU, Temperature/Thermal, Encoder/WheelTick, ForceTorque。
- 执行器：JointCommand/State, ChassisCommand/State, PLCCommand/State, GripperCommand/State。
- 规划/控制：TaskCommand/TaskStatus, PathPlan/Trajectory, HealthReport/Heartbeat。

**分层职责与独立性**
- 核心层只定义接口 + 注册表 + 序列化抽象，不含具体字段。
- 所有具体类型都在插件层生成/实现；升级时不触碰核心。
- 中间层负责协议转换与缓存，降低对核心的侵入。

**与 ROS2 类比**
- ROS2 的 msg/srv/action 即强类型 DTO；版本演进靠新增字段、保留未知字段。
- 方案可复用相同理念：强类型为主，Envelope/Generic 兜底，生成器同步多协议（FastDDS/ROS2）定义，保持核心接口不动。



## 15. 稳定核心接口与注册表设计（4-5 虚函数上限）
- `IDto`（永不改动）：`type()`、`version()`、`serialize()`、`deserialize()`、（可选）`clone()`。
- `TypeInfo`：`name`、`version`、`content_type`、`schema_hash`、`compat_policy`。
- `TypeRegistry`：
  - `registerFactory(TypeInfo, FactoryFn)`：插件加载时注册。
  - `create(name_or_typeinfo)`：运行时实例化；找不到则返回 nullptr。
  - `list()`：列出已知类型；提供字段/元数据 introspection。
- 线程安全：注册通常在启动期；查询在运行时多线程；可用读写锁或原子快照。

## 16. 序列化后端插件与元数据
- `Serializer` / `Deserializer` 抽象：接口稳定，后端可插拔（JSON/CBOR/Protobuf/FlatBuffers/IDL binary）。
- `content_type` 选择策略：
  - 控制/配置类：JSON/CBOR（可读性高）。
  - 高频/大数据：CBOR/Protobuf/IDL binary（性能/体积）。
  - 零拷贝：结合共享内存/loaned buffer，payload 为引用句柄。
- 元数据导出：注册表可输出 JSON Schema/IDL 描述，供监控/可视化/调试。

## 17. 生成器输入/输出规范（自动化流水）
- 输入 YAML（或 IDL）：定义 name/version/content_type/字段/单位/可选/默认值/枚举。
- 生成产物：
  - C++ DTO 类（继承 `IDto`）及序列化代码。
  - 插件入口（`extern "C" IDto* Create()` 或工厂注册函数）。
  - 可选：IDL/自定义协议 同步生成，保证多协议一致。
- 生成路径：`build/generated` 或 `src/generated`（不手工改）。
- CI 校验：IDL/YAML → 生成代码一致性；禁止直接修改生成文件。

## 18. 兼容策略细化
- 双向兼容：
  - 新版→旧数据：缺省字段使用默认值。
  - 旧版→新数据：忽略未知字段（后端需支持 unknown-field skip）。
- 枚举扩展：仅追加，不重排；保留 UNKNOWN=0。
- schema_hash：用于防呆；hash 变则 version 递增；允许多版本共存。
- 升级流程：新增字段→回归→发布 changelog→预留观察期再考虑废弃字段。

## 19. 典型接入流水线（新传感器示例）
1) 写 YAML：`sensor.image2d` v1，字段/单位/可选/默认值。
2) 产出 DTO 类 + TypeRegistry 注册代码（可手写或由外部生成工具产出）；必要时同步生成 ROS2 msg/IDL。
3) 编译并回归：仅依赖核心接口；不改核心。
4) 部署：把构建产物按你们的部署系统发布；类型注册在进程启动时完成。
5) 消费/生产：业务逻辑按 `TypeInfo` 发布/订阅；未知字段自动忽略，兼容旧版。

## 20. 开发者/插件接入清单
- 必备：
  - 安装包中的头文件/IDL（`install/include/MotionCore/dto` 或 `dto/*.idl`）。
  - TypeRegistry API 文档（注册/查询/列表）。
- 可选：
  - 示例 YAML + 生成后的示例插件（传感器/执行器/任务）。
  - 多协议对照表（FastDDS/ROS2 话题名、QoS 建议）。
- 约束：
  - 不修改核心接口；如需新类型，走“新增 DTO/版本”流程。
  - 对新增字段提供默认值与兼容性说明。

## 21. 代码落地路径（当前实现）
- 核心接口与注册表：`include/dto/dto_core.h`，实现 `src/dto_core.cpp`（含 Binary + CDR 适配示例）。
- 示例 DTO：`include/dto/event_dto_sample.h` + `src/event_dto_sample.cpp`，演示 `IDto` 继承、序列化及静态注册。
- 说明：生成器脚本已从核心库仓库移除；如需自动生成，请在外部工具链落地。

## 22. 使用示例（调用侧）
- 创建/发布：
  1) `register_event_dto_sample()`（若未静态注册）。
  2) `auto dto = TypeRegistry::instance().create("sample.event");`
  3) 填充字段并 `serialize(CdrSerializer or BinarySerializer)` 获取字节；通过 FastDDS topic 发布。
- 订阅/消费：收到 payload 后用 `CdrDeserializer`（或 Binary）构造 DTO，调用 `deserialize()`，按字段处理；未知字段由底层跳过（需保持 encoding 与序列化后端一致）。
- 版本兼容：按 `TypeInfo.version` 进行选择或回退；新增字段需设默认值，旧 consumer 自动忽略。