#include "graph/graph_cut.h"

#include <unordered_map>

extern "C" {
#include "Graclus/metisLib/metis.h"
}

#include <glog/logging.h>

namespace gopt {
namespace graph {

// Wrapper class for weighted, undirected Graclus graph.
class GraclusGraph {
 public:
  GraclusGraph(const std::vector<std::pair<int, int>>& edges,
               const std::vector<int>& weights) {
    CHECK_EQ(edges.size(), weights.size());

    std::unordered_map<int, std::vector<std::pair<int, int>>> adjacency_list;
    for (size_t i = 0; i < edges.size(); ++i) {
      const auto& edge = edges[i];
      const auto weight = weights[i];
      const int vertex_idx1 = GetVertexIdx(edge.first);
      const int vertex_idx2 = GetVertexIdx(edge.second);
      adjacency_list[vertex_idx1].emplace_back(vertex_idx2, weight);
      adjacency_list[vertex_idx2].emplace_back(vertex_idx1, weight);
    }

    xadj_.reserve(vertex_id_to_idx_.size() + 1);
    adjncy_.reserve(2 * edges.size());
    adjwgt_.reserve(2 * edges.size());

    idxtype edge_idx = 0;
    for (size_t i = 0; i < vertex_id_to_idx_.size(); ++i) {
      xadj_.push_back(edge_idx);

      if (adjacency_list.count(i) == 0) {
        continue;
      }

      for (const auto& edge : adjacency_list[i]) {
        edge_idx += 1;
        adjncy_.push_back(edge.first);
        adjwgt_.push_back(edge.second);
      }
    }

    xadj_.push_back(edge_idx);

    CHECK_EQ(edge_idx, 2 * edges.size());
    CHECK_EQ(xadj_.size(), vertex_id_to_idx_.size() + 1);
    CHECK_EQ(adjncy_.size(), 2 * edges.size());
    CHECK_EQ(adjwgt_.size(), 2 * edges.size());

    data.gdata = data.rdata = nullptr;

    data.nvtxs = vertex_id_to_idx_.size();
    data.nedges = 2 * edges.size();
    data.mincut = data.minvol = -1;

    data.xadj = xadj_.data();
    data.adjncy = adjncy_.data();

    data.vwgt = nullptr;
    data.adjwgt = adjwgt_.data();

    data.adjwgtsum = nullptr;
    data.label = nullptr;
    data.cmap = nullptr;

    data.where = data.pwgts = nullptr;
    data.id = data.ed = nullptr;
    data.bndptr = data.bndind = nullptr;
    data.rinfo = nullptr;
    data.vrinfo = nullptr;
    data.nrinfo = nullptr;

    data.ncon = 1;
    data.nvwgt = nullptr;
    data.npwgts = nullptr;

    data.vsize = nullptr;

    data.coarser = data.finer = nullptr;
  }

  int GetVertexIdx(const int id) {
    const auto it = vertex_id_to_idx_.find(id);
    if (it == vertex_id_to_idx_.end()) {
      const int idx = vertex_id_to_idx_.size();
      vertex_id_to_idx_.emplace(id, idx);
      vertex_idx_to_id_.emplace(idx, id);
      return idx;
    } else {
      return it->second;
    }
  }

  int GetVertexId(const int idx) { return vertex_idx_to_id_.at(idx); }

  GraphType data;

 private:
  std::unordered_map<int, int> vertex_id_to_idx_;
  std::unordered_map<int, int> vertex_idx_to_id_;
  std::vector<idxtype> xadj_;
  std::vector<idxtype> adjncy_;
  std::vector<idxtype> adjwgt_;
};

std::unordered_map<int, int> ComputeNormalizedMinGraphCut(
    const std::vector<std::pair<int, int>>& edges,
    const std::vector<int>& weights, const int num_parts) {
  GraclusGraph graph(edges, weights);

  const int levels =
      amax((graph.data.nvtxs) / (40 * log2_metis(num_parts)), 20 * (num_parts));

  std::vector<idxtype> cut_labels(graph.data.nvtxs);

  int options[11];
  options[0] = 0;
  int wgtflag = 1;
  int numflag = 0;
  int chain_length = 0;
  int edgecut;
  int var_num_parts = num_parts;

  MLKKM_PartGraphKway(&graph.data.nvtxs, graph.data.xadj, graph.data.adjncy,
                      graph.data.vwgt, graph.data.adjwgt, &wgtflag, &numflag,
                      &var_num_parts, &chain_length, options, &edgecut,
                      cut_labels.data(), levels);

  float lbvec[MAXNCON];
  ComputePartitionBalance(&graph.data, num_parts, cut_labels.data(), lbvec);
  ComputeNCut(&graph.data, &cut_labels[0], num_parts);
  std::unordered_map<int, int> labels;
  for (size_t idx = 0; idx < cut_labels.size(); ++idx) {
    labels.emplace(graph.GetVertexId(idx), cut_labels[idx]);
  }

  return labels;
}

}  // namespace graph
}  // namespace gopt
