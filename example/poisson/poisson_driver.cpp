//========================================================================================
// (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================

#include <iomanip>
#include <iostream>
#include <limits>

#include "poisson_driver.hpp"
#include "task_list/tasks.hpp"

using namespace parthenon::driver::prelude;

namespace poisson {
TaskList PoissonDriver::MakeTaskList(MeshBlock *pmb) {
  using parthenon::BlockTaskFunc;
  using parthenon::BlockTask;

  TaskList tl;
  auto AddBlockTask = [&tl, pmb, this](BlockTaskFunc func,
                                       TaskID dep) {
    return tl.AddTask<BlockTask>(func, dep, pmb);
  };
  auto AddContainerTask = [&tl](ContainerTaskFunc func, TaskID dep, Container<Real> &rc) {
    return tl.AddTask<ContainerTask>(func, dep, rc);
  };
  auto AddTwoContainerTask = [&tl](TwoContainerTaskFunc f, TaskID dep,
                                   Container<Real> &rc1, Container<Real> &rc2) {
    return tl.AddTask<TwoContainerTask>(f, dep, rc1, rc2);
  };
  TaskID none(0);

  Container<Real> &base = pmb->real_containers.Get();
  pmb->real_containers.Add("update", base);
  Container<Real> &update = pmb->real_containers.Get("update");

  auto start_recv = AddContainerTask(Container<Real>::StartReceivingTask, none, update);

  auto smooth = AddTwoContainerTask(Smooth, none, base, update);

  // update ghost cells
  auto send = AddContainerTask(Container<Real>::SendBoundaryBuffersTask, smooth,
                               update);
  auto recv = AddContainerTask(Container<Real>::ReceiveBoundaryBuffersTask, send, update);
  auto fill_from_bufs =
      AddContainerTask(Container<Real>::SetBoundariesTask, recv, update);
  auto clear_comm_flags =
      AddContainerTask(Container<Real>::ClearBoundaryTask, fill_from_bufs, update);

  auto prolongBound = AddBlockTask(
      [](MeshBlock *pmb) {
        pmb->pbval->ProlongateBoundaries(0.0, 0.0);
        return TaskStatus::complete;
      },
      fill_from_bufs);

  // set physical boundaries
  auto set_bc =
      AddContainerTask(parthenon::ApplyBoundaryConditions, prolongBound, update);

  // fill in derived fields
  auto fill_derived =
      AddContainerTask(parthenon::FillDerivedVariables::FillDerived, set_bc, update);

  // swap containers
  auto swap = AddBlockTask(
      [](MeshBlock *pmb) {
        pmb->real_containers.Swap("base", "update");
        return TaskStatus::complete;
      },
      fill_derived);

  // Update refinement
  if (pmesh->adaptive) {
    auto tag_regine = AddBlockTask(
        [](MeshBlock *pmb) {
          pmb->pmr->CheckRefinementCondition();
          return TaskStatus::complete;
        },
        swap);
  }

  return tl;
}

void PoissonDriver::OutputCycleDiagnostics() {
  const int precision = std::numeric_limits<Real>::max_digits10 - 1;
  const int ratio_precision = 3;
  if ((ncycle_out > 0) && (ncycle % ncycle_out == 0)
      && (parthenon::Globals::my_rank == 0)) {
    std::cout << "cycle=" << ncycle << std::scientific << std::setprecision(precision)
              << " reisidual=" << residual;

    // insert more diagnostics here
    std::cout << std::endl;
  }
  return;
}

}; // namespace poisson