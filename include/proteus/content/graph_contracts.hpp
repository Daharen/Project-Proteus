#pragma once

#include <string>
#include <vector>

namespace proteus::content {

struct GraphNode {
    std::string id;
    std::string type;
};

struct GraphEdge {
    std::string source_id;
    std::string target_id;
    double weight = 0.0;
    std::string relation;
};

class ContentGraph {
public:
    virtual ~ContentGraph() = default;
    virtual std::vector<GraphNode> nearest_targets(const std::string& node_id, std::size_t k) const = 0;
};

}  // namespace proteus::content
