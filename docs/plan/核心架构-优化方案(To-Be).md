# 核心架构优化方案（To‑Be，基于现有仓库落地）

更新时间：2026-01-03

> 状态标注规则：
> - 【已落地】代码/示例/验收入口齐全
> - 【部分落地】核心实现已在，但仍缺示例/验收/文档对齐
> - 【计划】目标口径，尚未实现
>
> 执行主线：请以 [路线图-下一阶段.md](../plan/路线图-下一阶段.md) 为“唯一阶段性目标与验收入口”，本文用于描述 To‑Be 蓝图与落地策略。
> 历史参考：已完成的 P0/P1/P2 实现状态见 [路线图-P0P1P2.md](路线图-P0P1P2.md)。

> 依据：你的历史输入（已归档至 [docs/_archive/临时草稿_2025-12-27.md](../_archive/临时草稿_2025-12-27.md)）以及当前仓库“平台层实现现状”。
> 目标：在不引入 ROS2 全家桶的前提下，做出一个可长期演进、可部署、可观测、可治理的企业级 FastDDS 分布式架构。

As‑Is vs To‑Be 差异对照表：见 [核心架构-差异对照(As-Is_vs_To-Be).md](核心架构-差异对照(As-Is_vs_To-Be).md)

## 0. 你的核心诉求（从草稿提炼）

- 去 ROS2 生态与编排体系，但 **底座用 FastDDS**。
- 企业级：稳定可靠、易部署（YAML/launch 风格）、可观测、安全、故障恢复。
- 业务纵向扩展：外设/视觉/运动规划/调度/故障恢复等 **统一插件式管理**。

你草稿里隐含的“硬约束”也很明确：

- 工业现场部署（机场/车间网络、硬件 SDK 绑定、权限与实时性）优先于云原生花活。
- 同机大流量（点云/图像）必须有稳定的共享内存数据路径。
- 对外接口要可版本化（DTO/IDL），否则跨团队协作会很痛苦。

## 1. 现状与差距（最关键的 6 个点）

### 1.1 通信抽象存在“双体系”

- 现有：
  - `ICommunicator/FastDDSCommunicator`（string + EventDTO）
  - `wxz::core::FastddsChannel/ShmChannel/InprocChannel`（更接近平台层通道抽象）
- 风险：
  - 两套都在用会导致“QoS/域/安全/指标”配置分散、难治理。

**To‑Be 建议**：

- 以 `wxz::core::*Channel` 作为主通道（pub/sub/req/resp 都用它衍生封装），
- `ICommunicator` 仅保留为兼容层或调试/legacy（逐步收敛）。

### 1.2 “必须启用 FastDDS 共享内存传输”需要重新表述为“必须有共享内存数据路径”

草稿强调 FastDDS SHM Transport。

现有仓库已经有 **独立的同机共享内存通道** `wxz::core::ShmChannel`（POSIX shm + semaphore），并且 `config/wxz_config.yaml` 已支持 `transport: shm`。

**To‑Be 建议**：

- 同机大流量（点云/图像/大轨迹）默认走 `ShmChannel`（更可控、问题隔离更清晰）。
- 跨机/跨进程广域通信走 `FastddsChannel`。
- 不强依赖 FastDDS 自身 SHM Transport（可作为后续可选优化项），避免“SHM 配置/权限/资源上限”把 FastDDS 整体稳定性拖下水。

补充说明（与当前实现一致）：

- 你当前 `FastDDSCommunicator` 构建 participant 时禁用了内置 transport 并显式使用 UDPv4（更偏“稳定可预期”，避免 FastDDS SHM 引入额外变量）。

### 1.3 缺少统一“节点容器（NodeContainer）”落地点

草稿里 NodePlugin/PluginContainer/LaunchManager 是关键，但现仓库缺一个标准入口可执行文件来承载：

- 读取 YAML
- 初始化 discovery/param/channels
- 加载插件
- 统一 lifecycle + health/capability/fault

**To‑Be 建议**：

- 增加一个标准可执行：`node_container`（平台层提供骨架，业务层只写插件）。
- 所有业务节点（相机/视觉/规划/控制/调度/BT）都以“容器 + 插件配置”部署，做到一套运维方法。

### 1.4 插件体系也存在“双接口”

- （已移除）插件相关 public API/实现：`MotionCore/plugin*.h`、`plugins/*` 与 dlopen 插件管理器。

**To‑Be 建议**：

- （已移除）不再提供插件 ABI 与 dlopen 管理器。
- 中期：把 `wxz::core::IPlugin` 作为对外稳定 API，把当前的 PluginManager 作为内部实现或适配层（避免两套 API 永远并存）。

### 1.5 监控/故障恢复建议“先做控制面”

草稿有 HealthMonitor/PerformanceMonitor/FaultRecovery YAML。

现有仓库已经有：

- `NodeBase`：健康文件 + capability/status + fault/status 的 FastDDS 发布
- `DiscoveryClient`：HTTP 注册/心跳/拉取 peers
- `channel_registry`：可输出 metrics JSON

**To‑Be 建议**：

- P0 先做“可观测基线”：
  - 每个 NodeContainer 输出：health file + capability topic + fault topic
  - channel_registry 定期 dump JSON（给日志或监控采集）
- P1 再做“故障恢复执行器”：
  - 先支持 restart / degrade 两类（交给外部进程管理器或启动脚本接管重启），再扩展 failover。

### 1.6 部署方式：优先裸机脚本，再扩展容器化/k8s

机场行李场景通常是工业现场网络/权限/硬件绑定更强。

**To‑Be 建议**：

- P0：启动脚本 + YAML 配置（仓库不内置任何进程管理器模板）。
- P1：容器化（需要处理硬件 SDK、GPU、/dev/shm、实时权限）。
- P2：k8s（在现场可行性评估后再推进）。

## 2. 优化后的分层（结合你草稿的三层）

### 2.1 数据与通信层（Data Plane）

- `FastddsChannel`：跨机、可靠/尽力 QoS、domain/topic
- `ShmChannel`：同机大流量
- `InprocChannel`：同进程零拷贝

### 2.2 分布式服务层（Control Plane / Governance）

- 配置：`config/wxz_config.yaml`（可拆分多文件，但建议先保证 schema 稳定）
- 服务发现：【已落地】HTTP `DiscoveryClient` + 最小 HTTP server 示例 `platform_tools/discovery_server`（用于本机/CI 可复现闭环）

**状态/验收入口（服务发现）**
- 状态：【已落地】
- 验收：`ctest -R SmokeDiscoveryServer -V`
- 参考：`MotionCore/docs/ref/服务发现API.md`

- 参数中心：优先对外走 public `wxz::core::DistributedParamServer`（HTTP 拉取 + snapshot + param.export 调试导出 RPC 已对齐并可回归）；如后续需要更复杂的运维能力，再在此基础上扩展并固化协议。

**状态/验收入口（参数中心）**
- 状态：【部分落地】对外 API 已可用且有回归，但 public 与 internal 仍存在差异（以现状总览为准）。
- 验收：`ctest -R 'TestParamServer|TestDistributedParamServer|TestDistributedParamServerHttpFetch|TestParamServerSnapshot|TestDistributedParamServerExportService' -V`
- 参考：`docs/ref/参数服务与配置中心.md`、`docs/ref/核心架构-现状总览.md`

- 观测：NodeBase + channel_registry metrics

**状态/验收入口（可观测性基线）**
- 状态：【已落地】最小 trace/metrics hook + NodeBase 基础 topic。
- 验收：`ctest -L guard -V`
- 参考：`docs/ref/核心架构-现状总览.md`

- 安全：FastDDS XML profiles +（可选）DDS Security

**状态/验收入口（安全）**
- 状态：【已落地/可选】profiles 配置化入口 + DDS-Security 最小闭环。
- 验收：`ctest -R SmokeDdsSecurity -V`
- 参考：见本节回归入口（ctest）

### 2.3 应用服务层（Business Nodes via NodeContainer）

- 统一可执行 `node_container`
- 通过 YAML 声明：
  - 本节点要加载哪些插件
  - 插件配置文件路径
  - 本节点发布/订阅哪些 topic（映射到 channels）
  - health/capability/fault 上报参数

## 3. 建议的“节点/插件”最小契约（P0 可落地）

> 先把 NodePlugin 做到“可运行 + 可运维”，不要一开始追求覆盖所有业务接口。

### 3.1 NodePlugin 最小接口（建议）

- `init(config)`：读取配置、创建所需 channels
- `start()`：开始工作线程/订阅回调
- `stop()`：停止对外服务（可幂等）
- `health()`：返回 bool（用于 NodeBase / readiness）
- `meta()`：name/version/api_level/capabilities

### 3.2 业务插件类别（映射你的业务）

- 外设：camera_driver / robot_driver
- 算法：vision_algo / planner_algo / palletizing_algo
- 调度：scheduler_strategy
- 编排：bt_engine（可选，和 Workstation 解耦）
- 恢复：recovery_policy

### 3.3 插件 ABI（P0 先收敛到一种）

你仓库现在同时存在两套插件接口：

- （已移除）插件框架相关内容。

To‑Be 的落地策略：

- （已移除）NodeContainer/platform_tools 已从仓库删除。
- P1：提供一个适配层，把 `plugins::IPlugin` 的实现包装成 `wxz::core::IPlugin`（或反过来），然后逐步把对外文档/SDK 固定到 `wxz::core::*`。

这样做的目的：避免业务团队写插件时被两套 API 分裂。

构建注意：

- （已移除）不再支持 `WXZ_ENABLE_DYNAMIC_PLUGINS` 与插件自动加载。
- 模板插件位于 `plugins/`，需要开启：`-DENABLE_SAMPLE_PLUGINS=ON` 才会编译。

最小编译示例：

```bash
cmake -S . -B build_plugins \
  -DWXZ_BUILD_TESTS=OFF \
  -DENABLE_SAMPLE_PLUGINS=ON
cmake --build build_plugins -j"$(nproc)" --target node_container
```

### 3.4 插件订阅生命周期契约（P0 必须遵守）

这条是“平台稳定性”的硬约束：**任何订阅回调都不能在插件 `dlclose` 之后才析构**。

原因（你已经遇到过的崩溃形态）：

- 插件把 lambda/std::function 回调注册到 channel/registry；
- 进程退出或热卸载时，插件 `.so` 被 `dlclose`；
- 后续某个时刻 channel/registry 析构 `std::function`，其析构逻辑/捕获对象代码在已卸载的 `.so` 中 → SIGSEGV。

为降低第三方“忘记 stop/unsubscribe”的概率，平台层已经做了接口收敛与兜底：

- Channel 提供 `subscribe_scoped(handler, owner)`，返回一个 move-only 的 `wxz::core::Subscription` token（RAII）。
- `Subscription` 的取消逻辑由 core 持有；`token.reset()` 或 token 析构会触发退订（可重入/幂等）。
- 每个 channel 支持 `unsubscribe_owner(owner)`；`owner` 推荐传插件实例指针 `this`。
- `PluginManager` 在 `dlclose` 前会调用 `ChannelRegistry::unsubscribe_owner(plugin_ptr)` 作为兜底清理。

**插件侧推荐写法（强烈建议）**：

- 把订阅 token 保存为插件成员变量；
- 在 `stop()` 中显式 `reset()`（或让成员在 stop/shutdown 时被析构）；
- 注册时带上 owner（通常是 `this`），即使插件忘记 reset，也能被卸载流程兜底清掉。

示例（与模板插件一致）：

```cpp
class VisionPlugin final : public IPlugin {
public:
  bool start() override {
    sub_ = channel_->subscribe_scoped(
      [this](const uint8_t* data, size_t n) { onPointCloud(data, n); },
      this);
    return true;
  }

  void stop() override {
    sub_.reset();
  }

private:
  wxz::core::Subscription sub_;
};
```

## 4. 你当前仓库立刻能做的“高收益优化”

### 4.0 【已落地】（截至 2026-01-03，本仓库现状）

下面这些已经在仓库中落地，可直接作为平台层“硬约束/基线能力”：

- `node_container` 作为统一入口：可读取 YAML、初始化 ChannelRegistry、按配置自动加载插件。
- 配置相对路径语义收敛：YAML 中涉及路径（例如 `plugin_manager.dirs`）支持相对路径，按“配置文件所在目录”解析。
- 有序停机（graceful shutdown）基线：退出时先卸载插件，再停止服务/通道，最后清理 registry，避免资源析构顺序引发崩溃。
- 插件订阅生命周期兜底：见“3.4”，支持 token/RAII + owner 批量退订；并在 `dlclose` 前强制 `unsubscribe_owner`。
- 通信抽象口径收敛：业务侧统一用 `wxz::core::{FastddsChannel, InprocChannel, ShmChannel}`；legacy 通信头可通过宏硬禁止误用。
- 最小可观测接口已落地：`observability.h` 提供 `TraceHook/MetricsSink`，并在通道/队列关键路径做了最小埋点。
- 服务发现最小闭环：`MotionCore/platform_tools/discovery_server` + `SmokeDiscoveryServer`（验证 register/ttl/peers 与 `DiscoveryClient` 兼容）。
- 一前缀安装可选：`WXZ_INSTALL_BUNDLED_FASTDDS=ON` 可把 `fastrtps/fastcdr` 随 MotionCore 安装到同一前缀，便于下游 `find_package(MotionCore)` 一步链接。
- （历史，已移除）最小健康探针闭环：`wxz::core::HealthProbeServer`（`/healthz`、`/readyz`）。

**状态/验收入口（健康探针 / readiness）**
- 状态：【历史（已移除）】
- 参考：`MotionCore/docs/ref/核心架构-现状总览.md`

**状态/验收入口（服务发现）**
- 状态：【已落地】
- 验收：`ctest -R SmokeDiscoveryServer -V`
- 参考：`MotionCore/docs/ref/服务发现API.md`

### 4.1 【部分落地】P0（把“能跑”变成“可运维、可验证”）

- 明确 P0 的“容器骨架”范围：统一初始化 Config/Channels/Discovery/ParamServer/Plugin autoload + health tick。
- 统一配置入口：延续 `WXZ_CONFIG_PATH`，在 YAML 中补齐 node/service 基本信息。
- 建立 topic/QoS 的“白名单与治理”：继续用 `channels:` 统一声明，不允许业务代码私自拼 topic。
- 固化启动/回归脚本（至少覆盖：编译、autoload 启动、SIGINT/SIGTERM 退出）。
- 补齐运维约定：
  - shm 段创建职责（仅提供方创建，消费者不创建）
  - plugin dirs 的部署形态（开发路径 vs 安装路径）
  - 统一的日志/metrics 输出频率与格式（先日志 JSON，后接采集器）

对应现有落地点（P0 读者最常用的入口）：

- `node_container` 示例入口：`platform_tools/node_container.cpp`
- `node_container` 示例入口：`MotionCore/platform_tools/node_container.cpp`
- 配置样例（包含 `channels:` / `plugin_manager` / `discovery` / `param_server`）：`MotionCore/config/wxz_config.yaml.sample`
- 本地回归（smoke）与 CI 回归入口：见 [CI/回归入口](#ci-regression)
- 进程拉起/回收：平台层不再随仓库提供通用进程管理模板；建议按部署环境自行编写启动脚本或集成到现场的进程管理系统。

### 4.2 验收与回归（建议写进 CI/本地自测）

最小可复现的一组回归命令（本仓库 build_plugins 模式）：

```bash
cmake --build build_plugins -j"$(nproc)" \
  --target MotionCore node_container \
  template_camera_node_plugin template_vision_node_plugin \
  template_planner_node_plugin template_arm_node_plugin

# 生成一份测试配置：打开插件 autoload，并把插件目录指向 build_plugins/plugins
cp MotionCore/config/wxz_config.yaml.sample build_plugins/wxz_config_test.yaml
sed -i 's/^  enable: false$/  enable: true/' build_plugins/wxz_config_test.yaml
sed -i 's#- ../build_plugins/plugins#- ./plugins#' build_plugins/wxz_config_test.yaml

# 启动并创建 shm（注意：只在提供方进程创建 shm 段）
WXZ_CONFIG_PATH=$PWD/build_plugins/wxz_config_test.yaml \
  ./build_plugins/platform_tools/node_container --create-shm
```

CI/回归入口（ctest + 本地 smoke 脚本）统一见： [CI/回归入口](#ci-regression)

验收标准（建议最少满足这些）：

- 插件链路能跑：`camera.pointcloud -> vision.pose -> planner.trajectory -> arm.status` 的计数持续增长。
- 退出无崩溃：连续多次 SIGINT/SIGTERM 退出均为 `EXIT:0`。
- 无回调跨 `dlclose`：卸载插件后不应再看到通道在析构阶段触发崩溃。

<a id="ci-regression"></a>

### 4.3 CI/回归入口（单一入口：ctest）

目标：让平台层所有“验收口径”都有可执行入口，并能在 CI 中长期跑通。

1) 推荐的 configure 开关（按需启用）

- 必选：`-DWXZ_BUILD_TESTS=ON`
- （历史）NodeContainer/插件链路 smoke：`-DENABLE_SAMPLE_PLUGINS=ON`

2) 推荐的执行方式（统一用 `ctest` 驱动）

- 推荐：优先用下方“常用回归用例”的精确入口（`ctest -R ...`）。
- 兜底（批量入口）：`ctest -L guard -V`、`ctest -L smoke -V`

3) 常用回归用例（按需定位）

- Public headers（对外 API 白名单/可编译）：`ctest -R TestPublicHeaders -V`
- 禁止动态加载（默认构建策略守卫）：`ctest -R TestNoDynamicLoading -V`
- CMake 变量守卫（拒绝遗留/未知 -D 变量）：`ctest -R TestCMakeRejectLegacyBtVar -V`、`ctest -R TestCMakeRejectUnknownCliVar -V`
- （历史）插件框架基础：`ctest -R TestPluginManager -V`
- 同机共享内存通道（同机大流量）：`ctest -R TestShmChannel -V`
- （历史）NodeContainer autoload：`ctest -R SmokeNodeContainerAutoload -V`
- （历史）NodeContainer 探针（/healthz,/readyz）：`ctest -R SmokeNodeContainerHealthProbe -V`
- （历史）readiness 负向回归：`ctest -R SmokeNodeContainerReadyzNegative -V`
- 服务发现最小闭环：`ctest -R SmokeDiscoveryServer -V`
- 故障恢复（degrade/restart）：`ctest -R SmokeFaultRecoveryDegrade -V`、`ctest -R SmokeFaultRecoveryRestart -V`
- DDS-Security（依赖 openssl/timeout，缺依赖需显式跳过并打印原因）：`ctest -R SmokeDdsSecurity -V`
- 文档守卫（企业级验收清单关键字）：`ctest -R TestPlatformAcceptanceDoc -V`

CI 矩阵维度（OS/Debug&Release/Offline/特性开关）统一以 [平台层企业级验收清单.md](../平台层企业级验收清单.md) 为准。

### 【部分落地】P1（稳定性与运维）

- 提供“服务契约”的一组固定 topic：
  - `capability/status`、`fault/status`、`health file`、`param.*`、`discovery`
- 故障恢复执行器：优先和外部进程管理器/启动脚本（或 k8s）的重启能力对接（不要先做复杂自研 failover）。

#### P1 验收口径（可运维、可观测、可自动拉起）

1. Metrics（channel_registry）输出约定

- 输出方式：先日志 JSON（后续再接 Prometheus/OTel）。
- 输出频率：建议 1s~5s 可配。
- JSON schema（最小约定）：

```json
{
  "fastdds": [
    {"channel": "demo_raw", "messages_received": 0, "last_publish_duration_ns": 0}
  ],
  "inproc": [
    {"channel": "foo", "publish_success": 0, "publish_fail": 0, "messages_delivered": 0}
  ],
  "shm": [
    {"channel": "camera.pointcloud", "publish_success": 0, "publish_fail": 0, "messages_delivered": 0}
  ]
}
```

说明：

- `fastdds[]` 侧至少包含：`channel/messages_received/last_publish_duration_ns`。
- `shm[]/inproc[]` 侧至少包含：`channel/publish_success/publish_fail/messages_delivered`。
- 所有数值字段必须为非负整数；未知值用 `0`。

1. 进程管理与故障恢复（P0/P1）

- 仓库不提供任何进程管理器模板；建议按部署环境自建启动脚本或使用现场的进程管理系统。
- 进程级恢复：崩溃/卡死 -> 外部进程管理器重启（或脚本拉起）。
- 业务级降级：可通过参数/配置切换到 mock/降采样/停发布（P1 可先做手动触发）。

最小实现建议（平台 P1 已提供可直接复用的执行器配置）：

```yaml
fault_recovery:
  enable: true
  topic: fault/status
  rules:
    # 业务降级：写 marker 文件，作为“降级已触发”的可观测落点
    - match: { fault: demo.degrade }
      action: degrade
      marker_file: /tmp/wxz_degraded

    # 进程重启：请求进程以指定 exit_code 退出，交由外部进程管理器/脚本（或 k8s）拉起
    - match: { fault: demo.restart }
      action: restart
      exit_code: 77
```

验收：

- 手动 `kill -SEGV <pid>` 后，外部进程管理器能自动拉起，且下次启动仍能正常 autoload 插件并跑通链路。
- 退出/卸载过程中不出现“回调跨 dlclose”崩溃（见 3.4）。

自动化验收（推荐纳入 CI）：

- 统一入口：见 [CI/回归入口](#ci-regression)
- 对应用例：`SmokeFaultRecoveryDegrade` / `SmokeFaultRecoveryRestart`

### 【部分落地】P2（安全/性能/规模化）

- DDS Security：用 FastDDS profiles 管理证书/权限（不在代码里手搓 SecurityManager）。
- DTO：把关键业务 DTO 用 IDL 固化并建立版本策略（而不是散落结构体）。

#### P2 验收口径（可控配置、可版本化、可扩展）

CI 矩阵建议（P2 项目也必须可自动化回归）：

- 统一入口：见 [CI/回归入口](#ci-regression)
- 最小要求：Release job 覆盖 `SmokeDdsSecurity`（可用时）

1. DDS profiles（配置可控性）

- 目标：不改代码即可切换 participant/QoS/transport（至少覆盖 fastdds 通道）。
- 验收：通过设置 profiles 文件路径（例如 `FASTDDS_DEFAULT_PROFILES_FILE` 或等价机制）即可影响 QoS（如 reliability/history/deadline），并可在日志中确认生效。

1. DDS Security（可选，按现场合规/安全需求启用）

- 目标：证书/权限文件不进代码、不进业务仓库；通过 profiles/部署配置管理。
- 验收：开启安全后：跨机通信可用；未授权 participant 无法订阅/发布目标 topic。

1. DTO/IDL 版本策略（跨团队 contract）

- 目标：关键 topic 的 payload 从“口头约定”变为“可生成/可校验的契约”。
- 最小规则：
  - 新增字段必须保持向后兼容（reader 忽略未知字段或有默认值）。
  - 破坏性变更必须提升 `api_version` 或调整 topic/类型名。

验收：

- 同一 topic 上，新旧两个版本的 reader/writer 组合至少能做到“旧 reader 不崩、新 reader 可读旧消息”。
- 为每个关键 DTO 提供最小的序列化/反序列化回归（单测或离线可执行）。

## 5. 迁移策略（不推倒重来）

- 先保留现有 `Workstation/`，把“通用 NodeContainer”沉到平台层，Workstation 只做应用编排。
- 先让新业务节点都通过 NodeContainer 运行；老的入口按需逐步迁移。

## 6. 配置与启动（“launch 风格”的最小可落地方案）

你草稿希望类似 ROS2 的 launch/yaml。结合工业现场最务实的路径：

- **配置**：每个进程只认一个 YAML（通过 `WXZ_CONFIG_PATH` 指向）。
- **启动**：启动脚本或外部进程管理器负责进程拉起/重启；YAML 负责“该进程加载哪些插件、用哪些 topic/QoS、启不启用 param/discovery”。

### 6.1 NodeContainer 的最小启动命令

```bash
WXZ_CONFIG_PATH=/opt/MotionCore/share/MotionCore/config/wxz_config.yaml \
  /opt/MotionCore/bin/node_container \
  --service camera_node \
  --type camera \
  --domain 0 \
  --health-file /run/wxz_robot/camera_node.health \
  --capability-topic capability/status \
  --fault-topic fault/status
```

说明：

- `--service/--type` 用于能力上报（capability/fault）与运维识别。
- `--create-shm` 仅在“需要创建 shm 段”的提供方进程使用（消费者不需要）。

### 6.2 YAML（平台层统一治理）

以下片段直接对应你仓库已有的解析逻辑（`Config`）：

```yaml
discovery:
  endpoint: "http://discovery.example.com:8080/api/discovery"
  heartbeat_period_ms: 2000
  ttl_ms: 6000
  node_role: "workstation"
  zone: "zone-a"
  node_endpoints: ["fastdds:0"]

param_server:
  enable: true
  set_topic: param.set
  ack_topic: param.ack

plugin_manager:
  enable: true
  recursive: false
  dirs:
    - /opt/MotionCore/lib/MotionCore/plugins

channels:
  # 同机大流量：shm
  camera.pointcloud:
    transport: shm
    shm:
      name: /wxz_camera_pointcloud
      capacity: 8
      slot_size: 2097152
  # 跨机/控制面：fastdds
  arm.command:
    transport: fastdds
    domain: 0
    topic: arm/command
    max_payload: 4096
    qos:
      reliability: reliable
      history: keep_last
      depth: 16
      deadline_ns: 5000000
      transport_priority: 50
      async_publish: false
      realtime_hint: true
```

重要约定（治理）：

- 业务代码不允许“随手 new Channel 并自定义 topic”；必须从 `ChannelRegistry` 按名字取。
- 大 payload（图像/点云/轨迹）默认 shm；FastDDS 保留给跨机与控制面。

路径语义约定（P0 必须统一）：

- YAML 中出现的路径字段（例如 `plugin_manager.dirs`）允许写相对路径。
- 相对路径的基准目录是“配置文件所在目录”，而不是进程工作目录（cwd）。

共享内存段创建约定（P0 必须统一）：

- `ShmChannel` 以 POSIX shm + semaphore 实现；如果 shm 段不存在，消费者 attach 会失败。
- 因此：需要创建 shm 段的进程（通常是“该通道的提供方”）必须带 `--create-shm` 启动；纯消费者进程不需要。

### 6.3 discovery 的配置键规范

当前实现支持：

- canonical：`discovery.ttl_ms`
- 兼容：`discovery.heartbeat_ttl_ms`

建议后续文档统一使用 `ttl_ms`，减少歧义。

**状态/验收入口（discovery 配置）**
- 状态：【已落地】
- 验收：`ctest -R SmokeDiscoveryServer -V`
- 参考：`MotionCore/docs/ref/服务发现API.md`、`MotionCore/config/wxz_config.yaml.sample`

## 7. DTO/IDL 与版本策略（跨团队协作的关键）

你草稿里写了大量 IDL。结合现状（仓库已有 `EventDTO` 手写 TypeSupport）：

- P0：继续保持 `EventDTO` 作为“通用信封”（header + payload），用于快速把系统跑起来。
- P1：把关键业务接口（机械臂指令、轨迹、检测结果、任务状态）固化成 IDL，并建立版本规则：
  - `api_version`：语义级破坏性变更
  - `schema_version`：字段增量/兼容变更
  - wire 协议：topic 命名 + IDL 名称固定
- P2：在 CI 中引入生成步骤（可选：fastddsgen），把 generated code 作为构建产物或可缓存依赖。

注意：IDL 不是为了“好看”，而是为了把跨团队接口从“口头约定”变成“可验证契约”。

## 8. 安全（先用 FastDDS profiles，不在代码里堆安全逻辑）

建议路径：

- P0：先把网络域隔离（domain id / VLAN / 防火墙）与最小权限跑通。
- P1：引入 FastDDS XML profiles 统一配置 participant / QoS / transport（由运维管理，不改代码）。

  - 落地方式（两条都支持）：
    - 进程环境变量：`FASTDDS_ENVIRONMENT_FILE` / `FASTDDS_LOG_FILENAME` / `FASTDDS_VERBOSITY`
    - YAML 注入（仅在对应 env 未设置时生效）：
      - `fastdds.environment_file`
      - `fastdds.log_filename`
      - `fastdds.verbosity`
  - 物料入口：
    - XML 示例：`MotionCore/resources/fastdds_peers_example.xml`
    - 环境变量注入示例：按部署环境自建（例如由启动脚本或进程管理系统统一注入）
- P2：需要时启用 DDS Security（证书、权限文件、加密），仍通过 profiles 管理。

### 8.1 DDS-Security（最小可跑示例与验收）

目标：给出一套“授权可通信、未授权不可通信”的最小闭环，便于现场/CI 快速验证安全开关与证书配置没有被部署环境破坏。

说明：仓库已移除脚本与 demo 证书生成逻辑；验收入口以 `ctest -R SmokeDdsSecurity -V` 为准。需要生成 demo 证书/权限文件时，请在外部工具链中完成。

CI/回归入口（ctest）统一见： [CI/回归入口](#ci-regression)

**状态/验收入口（DDS-Security）**
- 状态：【已落地/可选】缺依赖时应显式跳过并打印原因。
- 验收：`ctest -R SmokeDdsSecurity -V`
- 参考：见本节回归入口（ctest）

注意：

- 这是“功能验收/集成自检”级别的 demo，证书与密钥仅用于本地验证；生产环境需替换为你们的 CA/证书体系。
- 生产建议通过部署环境统一注入 `FASTDDS_ENVIRONMENT_FILE`，避免业务代码感知安全细节。

原则：密钥/证书/权限文件不进仓库，不硬编码到业务代码。

## 9. 可观测性与故障恢复（从“能定位问题”开始）

P0 必做（你仓库已经具备基础能力）：

- 每个进程：health file（外部进程管理器/脚本可直接用）
- 每个进程：`capability/status` 与 `fault/status` topic
- 每个进程：周期性输出 `channel_registry` metrics（先日志，再接指标系统）

P1 故障恢复：

- 先做“重启/降级”的自动策略（由外部进程管理器/脚本接管自动重启）
- 再做复杂的 failover/熔断/隔离（尤其是调度节点）

**状态/验收入口（可观测性基线）**
- 状态：【已落地】最小 trace/metrics hook + NodeBase 的基础 topic。
- 验收：`ctest -L guard -V`
- 参考：`docs/ref/核心架构-现状总览.md`

**状态/验收入口（故障恢复）**
- 状态：【已落地】restart/degrade 端到端用例已固化。
- 验收：`ctest -R SmokeFaultRecoveryDegrade -V`、`ctest -R SmokeFaultRecoveryRestart -V`
- 参考：`scripts/smoke_fault_recovery.sh`


