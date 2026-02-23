#pragma once

#include "proteus/inference/belief_state.hpp"

#include <optional>
#include <string>
#include <vector>

namespace proteus::inference {

struct QuestionTrace {
    std::string question_id;
    AnswerOption answer;
    std::optional<double> time_to_answer_seconds;
};

struct InferenceTraceLog {
    std::string session_id;
    std::vector<QuestionTrace> asked_questions;
    std::string final_primary_target;
    std::vector<std::string> final_backup_targets;
    bool novelty_triggered = false;
};

}  // namespace proteus::inference
