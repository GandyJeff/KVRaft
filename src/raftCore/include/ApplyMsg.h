#ifndef APPLYMSG_H
#define APPLYMSG_H

#include <string>

// 将Raft集群中已提交的日志条目或快照数据从Raft核心组件传递给状态机，确保状态机能够正确、有序地更新自身状态
class ApplyMsg
{
public:
    bool CommandValid;   // 标识当前消息是否包含可执行的命令
    std::string Command; // 存储待执行的具体命令内容（增删改查）
    int CommandIndex;    // 标识该命令在Raft日志中的索引位置

    bool SnapshotValid;   // 标识当前消息是否包含快照数据
    std::string Snapshot; // 存储快照数据
    int SnapshotTerm;     // 标识快照对应的任期号
    int SnapshotIndex;    // 标识快照对应的日志索引

public:
    // 两个valid最开始要赋予false！！
    ApplyMsg()
        : CommandValid(false),
          Command(),
          CommandIndex(-1),
          SnapshotValid(false),
          SnapshotTerm(-1),
          SnapshotIndex(-1) {

          };
};

#endif // APPLYMSG_H