#include "rotation_averaging/lagrange_dual_rotation_estimator.h"

#include <ceres/rotation.h>
#include <glog/logging.h>
#include <omp.h>

#include <Eigen/Eigenvalues>
#include <Eigen/QR>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>

#include "Spectra/MatOp/SparseSymMatProd.h"
#include "Spectra/SymEigsSolver.h"

#include "rotation_averaging/internal/rotation_estimator_util.h"
#include "solver/bcm_sdp_solver.h"
#include "solver/rbr_sdp_solver.h"
#include "solver/rank_restricted_sdp_solver.h"
#include "solver/riemannian_staircase.h"

namespace gopt {
LagrangeDualRotationEstimator::LagrangeDualRotationEstimator(const int N,
                                                             const int dim)
    : LagrangeDualRotationEstimator(N, dim, solver::SDPSolverOptions()) {}

LagrangeDualRotationEstimator::LagrangeDualRotationEstimator(
    const int N, const int dim, const solver::SDPSolverOptions& options)
    : options_(options), images_num_(N), dim_(dim) {
  R_ = Eigen::SparseMatrix<double>(dim_ * N, dim_ * N);
  alpha_max_ = 0.0;
}

void LagrangeDualRotationEstimator::SetViewIdToIndex(
    const std::unordered_map<image_t, int>& view_id_to_index) {
  view_id_to_index_ = view_id_to_index;
}

void LagrangeDualRotationEstimator::SetRAOption(
    const solver::SDPSolverOptions& options) {
  options_ = options;
}

const solver::Summary& LagrangeDualRotationEstimator::GetRASummary() const {
  return summary_;
}

double LagrangeDualRotationEstimator::GetErrorBound() const {
  return alpha_max_;
}

bool LagrangeDualRotationEstimator::EstimateRotations(
    const std::unordered_map<ImagePair, TwoViewGeometry>& view_pairs,
    std::unordered_map<image_t, Eigen::Vector3d>* global_rotations) {
  const int N = images_num_;

  CHECK_GT(view_pairs.size(), 0);
  CHECK_GT(N, 0);

  if (view_id_to_index_.empty()) {
    internal::ViewIdToAscentIndex(*global_rotations, &view_id_to_index_);
  }

  // Set for R_
  std::unordered_map<size_t, std::vector<size_t>> adj_edges;
  FillinRelativeGraph(view_pairs, R_, adj_edges);

  std::unique_ptr<solver::SDPSolver> solver = this->CreateSDPSolver(N, dim_);
  solver->SetCovariance(-R_);
  solver->SetAdjacentEdges(adj_edges);
  solver->Solve(summary_);
  Y_ = solver->GetSolution();

  RetrieveRotations(Y_, global_rotations);

  LOG(INFO) << "LagrangeDual converged in "
            << summary_.total_iterations_num << " iterations.";
  LOG(INFO) << "Total time [LagrangeDual]: " << summary_.TotalTime() << " ms.";

  return true;
}

void LagrangeDualRotationEstimator::ComputeErrorBound(
    const std::unordered_map<ImagePair, TwoViewGeometry>& view_pairs) {
  const int N = images_num_;

  std::vector<Eigen::Triplet<double>> a_triplets;
  // adjacency matrix
  Eigen::SparseMatrix<double> A(N, N);
  // degree matrix (a diagonal matrix)
  Eigen::SparseMatrix<double> D(N, N);
  std::vector<Eigen::Triplet<double>> d_triplets;
  std::vector<double> degrees(N, 0);

  for (auto& view_pair : view_pairs) {
    ImagePair pair = view_pair.first;
    const int i = view_id_to_index_[pair.first];
    const int j = view_id_to_index_[pair.second];
    degrees[i]++;
    degrees[j]++;
  }

  double max_degree = 0;
  for (auto& view_pair : view_pairs) {
    ImagePair pair = view_pair.first;
    const int i = view_id_to_index_[pair.first];
    const int j = view_id_to_index_[pair.second];

    a_triplets.push_back(Eigen::Triplet<double>(i, j, 1.0));
    a_triplets.push_back(Eigen::Triplet<double>(j, i, 1.0));

    d_triplets.push_back(Eigen::Triplet<double>(i, i, degrees[i]));
    d_triplets.push_back(Eigen::Triplet<double>(j, j, degrees[j]));
    max_degree = std::max(std::max(max_degree, degrees[i]), degrees[j]);
  }
  A.setFromTriplets(a_triplets.begin(), a_triplets.end());
  A.makeCompressed();
  D.setFromTriplets(d_triplets.begin(), d_triplets.end());
  D.makeCompressed();

  // laplacian matrix
  Eigen::SparseMatrix<double> L = D - A;

  // compute the bound of residual error
  Spectra::SparseSymMatProd<double> op(L);
  Spectra::SymEigsSolver<double, Spectra::SMALLEST_ALGE,
                         Spectra::SparseSymMatProd<double>>
      eigs(&op, 2, 5);
  eigs.init();
  eigs.compute();

  double lambda2 = 0.0;
  if (eigs.info() == Spectra::SUCCESSFUL) {
    lambda2 = eigs.eigenvalues()[0];
  } else {
    LOG(INFO) << "Computing Eigenvalue fails";
  }

  // get the second smallest eigen value
  alpha_max_ =
      2 * std::asin(std::sqrt(0.25 + lambda2 / (2.0 * max_degree)) - 0.5);
}

void LagrangeDualRotationEstimator::RetrieveRotations(
    const Eigen::MatrixXd& Y,
    std::unordered_map<image_t, Eigen::Vector3d>* global_rotations) {
  for (auto orientation : *global_rotations) {
    image_t view_id = orientation.first;
    const int i = view_id_to_index_[view_id];
    // After fixing Equ.(10)
    Eigen::Matrix3d R = Y.block(0, 3 * i, 3, 3).transpose();
    if (R.determinant() < 0) R = -R;

    // CHECK_GE(R.determinant(), 0);
    // CHECK_NEAR(R.determinant(), 1, 1e-8);

    Eigen::Vector3d angle_axis;
    ceres::RotationMatrixToAngleAxis(R.data(), angle_axis.data());
    // LOG(INFO) << angle_axis.norm();
    (*global_rotations)[view_id] = angle_axis;
  }
}

void LagrangeDualRotationEstimator::FillinRelativeGraph(
    const std::unordered_map<ImagePair, TwoViewGeometry>& view_pairs,
    Eigen::SparseMatrix<double>& R,
    std::unordered_map<size_t, std::vector<size_t>>& adj_edges) {
  std::vector<Eigen::Triplet<double>> triplets;
  for (auto it = view_pairs.begin(); it != view_pairs.end(); ++it) {
    // image_t i = it->first.first, j = it->first.second;
    const int i = view_id_to_index_[it->first.first];
    const int j = view_id_to_index_[it->first.second];
    // CHECK_LT(i, j);
    Eigen::Matrix3d R_ij;
    ceres::AngleAxisToRotationMatrix(it->second.rotation_2.data(), R_ij.data());

    // After fixing Eq.(9)
    // R.block(3 * i, 3 * j, 3, 3) = R_ij.transpose();
    // R.block(3 * j, 3 * i, 3, 3) = R_ij;
    for (int l = 0; l < 3; l++) {
      for (int r = 0; r < 3; r++) {
        triplets.push_back(
            Eigen::Triplet<double>(dim_ * i + l, dim_ * j + r, R_ij(r, l)));
        triplets.push_back(
            Eigen::Triplet<double>(dim_ * j + l, dim_ * i + r, R_ij(l, r)));
      }
    }

    adj_edges[i].push_back(j);
    adj_edges[j].push_back(i);
  }
  R.setFromTriplets(triplets.begin(), triplets.end());
  R.makeCompressed();
}

std::unique_ptr<solver::SDPSolver>
LagrangeDualRotationEstimator::CreateSDPSolver(
    const int n, const int dim) {
  switch (options_.solver_type) {
    case solver::RBR_BCM:
      return std::unique_ptr<solver::SDPSolver>(
          new solver::RBRSDPSolver(n, dim, options_));
      break;
    case solver::RANK_DEFICIENT_BCM:
      return std::unique_ptr<solver::SDPSolver>(
            new solver::RankRestrictedSDPSolver(n, dim, options_));
      break;
    case solver::RIEMANNIAN_STAIRCASE:
        return std::unique_ptr<solver::SDPSolver>(
            new solver::RiemannianStaircase(n, dim, options_));
        break;
    default:
      LOG(WARNING) << "Solve Type is not supported!";
      return nullptr;
      break;
  }
}

}  // namespace gopt