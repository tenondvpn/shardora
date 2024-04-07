#pragma once


namespace shardora {

namespace consensus {

class Rule {
    Rule() = default;
    virtual ~Rule() = 0;

    Rule(const Rule&) = delete;
    Rule& operator=(const Rule&) = delete;

    virtual bool VoteRule() = 0;
    virtual bool CommitRule() = 0;
};

} // namespace consensus

} // namespace shardora

