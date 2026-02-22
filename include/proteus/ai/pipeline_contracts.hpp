#pragma once

#include <string>

namespace proteus::ai {

enum class ProposalStage {
    Staging,
    Curate,
    Test,
    Promote,
};

struct ExpansionProposal {
    std::string proposal_id;
    std::string domain;
    double novelty_score = 0.0;
    ProposalStage stage = ProposalStage::Staging;
};

class ExpansionPipeline {
public:
    virtual ~ExpansionPipeline() = default;
    virtual bool advance(ExpansionProposal& proposal) = 0;
};

}  // namespace proteus::ai
