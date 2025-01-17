#ifndef SOLVER_L1_SOLVER_H_
#define SOLVER_L1_SOLVER_H_

#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <algorithm>
#include <string>
#include <iomanip>

#include <glog/logging.h>

#include "math/sparse_cholesky_llt.h"
#include "util/stringprintf.h"

namespace gopt {

// These are template overrides that allow the sparse linear solvers to work
// with sparse or dense matrices. The sparseView() method is not implemented for
// Eigen::SparseMatrix.
namespace l1_solver_internal {

inline void Compute(const Eigen::SparseMatrix<double>& spd_mat,
                    SparseCholeskyLLt* linear_solver) {
  linear_solver->Compute(spd_mat);
}

inline void Compute(const Eigen::MatrixXd& spd_mat,
                    SparseCholeskyLLt* linear_solver) {
  linear_solver->Compute(spd_mat.sparseView());
}

}  // namespace l1_solver_internal

// An L1 norm approximation solver. This class will attempt to solve the
// problem: || A * x - b || under L1-norm (as opposed to L2 i.e. "least-squares"
// norm). This problem can be solved with the alternating direction method of
// multipliers (ADMM) as a least unsquared deviations minimizer. A full
// description of the method, including how to use ADMM for L1 minimization can
// be found in "Distributed Optimization and Statistical Learning via the
// Alternating Direction Method of Multipliers" by Boyd et al, Foundations and
// Trends in Machine Learning (2012). The paper can be found at:
//   https://web.stanford.edu/~boyd/papers/pdf/admm_distr_stats.pdf
//
// ADMM can be much faster than interior point methods but convergence may be
// slower. Generally speaking, ADMM solvers converge to good solutions in only a
// few number of iterations, but can spend many iterations subsuquently refining
// the solution to optain the global optimum. The speed improvements are because
// the matrix A only needs to be factorized (by Cholesky decomposition) once, as
// opposed to every iteration.
//
// This implementation is based off of the code found at:
//   https://web.stanford.edu/~boyd/papers/admm/least_abs_deviations/lad.html
template <class MatrixType>
class L1Solver {
 public:
  struct Options {
    int max_num_iterations = 1000;
    // Rho is the augmented Lagrangian parameter.
    double rho = 1.0;
    // Alpha is the over-relaxation parameter (typically between 1.0 and 1.8).
    double alpha = 1.0;

    double absolute_tolerance = 1e-4;
    double relative_tolerance = 1e-2;
  };

  L1Solver(const Options& options, const MatrixType& mat)
      : options_(options), a_(mat) {
    // Analyze the sparsity pattern once. Only the values of the entries will be
    // changed with each iteration.
    const MatrixType spd_mat = a_.transpose() * a_;
    l1_solver_internal::Compute(spd_mat, &linear_solver_);
    CHECK_EQ(linear_solver_.Info(), Eigen::Success);
  }

  void SetMaxIterations(const int max_iterations) {
    options_.max_num_iterations = max_iterations;
  }

  // Solves ||Ax - b||_1 for the optimial L1 solution given an initial guess for
  // x. To solve this we introduce an auxillary variable y such that the
  // solution to:
  //        min   1 * y
  //   s.t. [  A   -I ] [ x ] < [  b ]
  //        [ -A   -I ] [ y ]   [ -b ]
  // which is an equivalent linear program.
  void Solve(const Eigen::VectorXd& rhs, Eigen::VectorXd* solution) {
    CHECK_NOTNULL(solution);
    Eigen::VectorXd& x = *solution;
    Eigen::VectorXd z(a_.rows()), u(a_.rows());
    z.setZero();
    u.setZero();

    Eigen::VectorXd a_times_x(a_.rows()), z_old(z.size()), ax_hat(a_.rows());
    // Precompute some convergence terms.
    const double rhs_norm = rhs.norm();
    const double primal_abs_tolerance_eps =
        std::sqrt(a_.rows()) * options_.absolute_tolerance;
    const double dual_abs_tolerance_eps =
        std::sqrt(a_.cols()) * options_.absolute_tolerance;
    LOG(INFO) << std::setw(12) << std::setfill(' ') << "Iter "
              << std::setw(16) << std::setfill(' ') << "R norm  "
              << std::setw(16) << std::setfill(' ') << "S norm  "
              << std::setw(16) << std::setfill(' ') << "Primal eps "
              << std::setw(16) << std::setfill(' ') << "Dual eps ";

    for (int i = 0; i < options_.max_num_iterations; i++) {
      // Update x.
      x.noalias() = linear_solver_.Solve(a_.transpose() * (rhs + z - u));
      if (linear_solver_.Info() != Eigen::Success) {
        LOG(ERROR) << "L1 Minimization failed. Could not solve the sparse "
                      "linear system with Cholesky Decomposition";
        return;
      }

      a_times_x.noalias() = a_ * x;
      ax_hat.noalias() = options_.alpha * a_times_x;
      ax_hat.noalias() += (1.0 - options_.alpha) * (z + rhs);

      // Update z and set z_old.
      std::swap(z, z_old);
      z.noalias() = Shrinkage(ax_hat - rhs + u, 1.0 / options_.rho);

      // Update u.
      u.noalias() += ax_hat - z - rhs;

      // Compute the convergence terms.
      const double r_norm = (a_times_x - z - rhs).norm();
      const double s_norm =
          (-options_.rho * a_.transpose() * (z - z_old)).norm();
      const double max_norm = std::max({a_times_x.norm(), z.norm(), rhs_norm});
      const double primal_eps =
          primal_abs_tolerance_eps + options_.relative_tolerance * max_norm;
      const double dual_eps = dual_abs_tolerance_eps +
                              options_.relative_tolerance *
                                  (options_.rho * a_.transpose() * u).norm();

      // Log the result to the screen.
      LOG(INFO) << std::setw(12) << std::setfill(' ') << i
          << std::setw(16) << std::setfill(' ') << r_norm
          << std::setw(16) << std::setfill(' ') << s_norm
          << std::setw(16) << std::setfill(' ') << primal_eps
          << std::setw(16) << std::setfill(' ') << dual_eps;

      // Determine if the minimizer has converged.
      if (r_norm < primal_eps && s_norm < dual_eps) {
        break;
      }
    }
  }

 private:
  Options options_;

  // Matrix A where || Ax - b ||_1 is the problem we are solving.
  MatrixType a_;

  // Cholesky linear solver. Since our linear system will be a SPD matrix we can
  // utilize the Cholesky factorization.
  SparseCholeskyLLt linear_solver_;

  Eigen::VectorXd Shrinkage(const Eigen::VectorXd& vec, const double kappa) {
    Eigen::ArrayXd zero_vec(vec.size());
    zero_vec.setZero();
    return zero_vec.max(vec.array() - kappa) -
           zero_vec.max(-vec.array() - kappa);
  }
};

}  // namespace gopt

#endif  // SOLVER_L1_SOLVER_H_
