#pragma once

// 开发阶段启用调试模式，便于追踪代码执行流程和问题定位；生产环境可关闭以提高性能。
const bool Debug = true;

// 时间单位乘数，用于不同网络环境调整，在网络延迟较高的环境中，可将debugMul调大（如 2 或 3），延长所有超时时间。
const int debugMul = 1;

/*
Raft 核心时间参数
*/

// leader发送心跳的时间间隔
/*
- 1.Raft 协议要求心跳时间必须远小于选举超时（通常小一个数量级），以确保领导者正常工作时不会触发新选举。
- 2.25ms 是一个较小的值，可快速检测领导者故障，但不会过度消耗网络资源。
*/
const int HeartBeatTimeout = 25 * debugMul; // 心跳超时时间(ms)

// Raft 状态机应用已提交日志的时间间隔
/*
- 1.过小的间隔会增加 CPU 负载（频繁应用日志）。
- 2.过大的间隔会导致状态机更新延迟，影响系统响应速度。
- 3.10ms 是一个平衡性能和响应速度的经验值。
*/
const int ApplyInterval = 10 * debugMul; // 应用日志间隔(ms)

// follower等待心跳的随机超时时间，超时后触发新选举
/*
- 1.随机化超时可避免多个follower同时触发选举导致的脑裂。
- 2.300-500ms 是 Raft 协议常见的选举超时范围（典型值为 150-300ms），此处取值略大是考虑到实际网络环境的延迟。
- 3.选举超时必须大于心跳超时（约 10 倍），确保领导者故障后有足够时间触发选举。
*/
const int minRandomizedElectionTime = 300 * debugMul; // 最小选举超时时间(ms)
const int maxRandomizedElectionTime = 500 * debugMul; // 最大选举超时时间(ms)

// 客户端等待 Raft 集群达成共识的最大时间。
/*
- 1.需大于选举超时时间（500ms ≥ 500ms），确保集群有足够时间完成 leader 选举和日志复制。
- 2.防止客户端无限等待，提高系统的可用性。
*/
const int CONSENSUS_TIMEOUT = 500 * debugMul; // 共识超时时间(ms)

// 协程相关设置
const int FIBER_THREAD_NUM = 1;             // 指定协程调度器使用的线程数量
const bool FIBER_USE_CALLER_THREAD = false; // 控制协程调度器是否使用调用者线程作为工作线程，false 表示使用独立的线程池，避免阻塞调用者线程（如主线程）。