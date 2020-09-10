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

#include "particles.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// *************************************************//
// redefine some internal parthenon functions      *//
// *************************************************//
namespace particles_example {

Packages_t ProcessPackages(std::unique_ptr<ParameterInput> &pin) {
  Packages_t packages;
  packages["particles_package"] = particles_example::Particles::Initialize(pin.get());
  return packages;
}

void ProblemGenerator(MeshBlock *pmb, ParameterInput *pin) {
  auto &rc = pmb->real_containers.Get();
  auto pkg = pmb->packages["particles_package"];

  auto &sc = pmb->real_containers.GetSwarmContainer();
  auto &s = sc->Get("my particles");

  // Add the number of empty particles requested in parameter file
  const int &num_particles_to_add = pkg->Param<int>("num_particles");
  std::vector<int> empty_particle_mask = s->AddEmptyParticles(num_particles_to_add);

  // WARNING do not get these references before resizing the swarm. Otherwise,
  // you'll get segfaults
  auto &x = s->GetReal("x").Get();
  auto &y = s->GetReal("y").Get();
  auto &z = s->GetReal("z").Get();
  auto &vx = s->GetReal("vx").Get();
  auto &vy = s->GetReal("vy").Get();
  auto &vz = s->GetReal("vz").Get();
  auto &weight = s->GetReal("weight").Get();
  auto &mask = s->GetMask().Get();

  pmb->par_for("particles_package::ProblemGenerator", 0, s->get_max_active_index(),
    KOKKOS_LAMBDA(const int n) {
      if (mask(n)) {
        x(n) = 1.e-1*n;
        y(n) = 1.e-2*n;
        z(n) = 1.e-3*n;
        vx(n) = 0.1;
        vy(n) = 1.e-5;
        vz(n) = 1.e-4*n;
        weight(n) = 1.0;
      }
    });
}

// *************************************************//
// define the "physics" package particles_package, *//
// which includes defining various functions that  *//
// control how parthenon functions and any tasks   *//
// needed to implement the "physics"               *//
// *************************************************//

namespace Particles {

std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin) {
  auto pkg = std::make_shared<StateDescriptor>("particles_package");

  int num_particles = pin->GetOrAddInteger("Particles", "num_particles", 100);
  pkg->AddParam<>("num_particles", num_particles);
  Real particle_speed = pin->GetOrAddReal("Particles", "particle_speed", 1.0);
  pkg->AddParam<>("particle_speed", particle_speed);

  std::string swarm_name = "my particles";
  Metadata swarm_metadata;
  pkg->AddSwarm(swarm_name, swarm_metadata);
  Metadata real_swarmvalue_metadata({Metadata::Real});
  pkg->AddSwarmValue("weight", swarm_name, real_swarmvalue_metadata);
  pkg->AddSwarmValue("vx", swarm_name, real_swarmvalue_metadata);
  pkg->AddSwarmValue("vy", swarm_name, real_swarmvalue_metadata);
  pkg->AddSwarmValue("vz", swarm_name, real_swarmvalue_metadata);

  pkg->EstimateTimestep = EstimateTimestep;

  return pkg;
}

AmrTag CheckRefinement(Container<Real> &rc) {
  return AmrTag::same;
}

Real EstimateTimestep(std::shared_ptr<Container<Real>> &rc) {
  return 0.5;
}

TaskStatus SetTimestepTask(std::shared_ptr<Container<Real>> &rc) {
  MeshBlock *pmb = rc->pmy_block;
  pmb->SetBlockTimestep(parthenon::Update::EstimateTimestep(rc));
  return TaskStatus::complete;
}

//}

} // namespace Particles

// *************************************************//
// define the application driver. in this case,    *//
// that just means defining the MakeTaskList       *//
// function.                                       *//
// *************************************************//
// first some helper tasks
TaskStatus UpdateContainer(MeshBlock *pmb, int stage,
                           std::vector<std::string> &stage_name, Integrator *integrator) {
  // const Real beta = stage_wghts[stage-1].beta;
  const Real beta = integrator->beta[stage - 1];
  const Real dt = integrator->dt;
  auto &base = pmb->real_containers.Get();
  auto &cin = pmb->real_containers.Get(stage_name[stage - 1]);
  auto &cout = pmb->real_containers.Get(stage_name[stage]);
  auto &dudt = pmb->real_containers.Get("dUdt");
  parthenon::Update::AverageContainers(cin, base, beta);
  parthenon::Update::UpdateContainer(cin, dudt, beta * dt, cout);
  return TaskStatus::complete;
}

TaskStatus UpdateSwarm(MeshBlock *pmb, int stage,
                       std::vector<std::string> &stage_name,
                       Integrator *integrator) {
  auto swarm = pmb->real_containers.GetSwarmContainer()->Get("my particles");
  parthenon::Update::TransportSwarm(swarm, swarm, integrator->dt);
  return TaskStatus::complete;
}

TaskStatus MyContainerTask(std::shared_ptr<Container<Real>> container) {
  return TaskStatus::complete;
}

// See the advection_driver.hpp declaration for a description of how this function gets called.
TaskList ParticleDriver::MakeTaskList(MeshBlock *pmb, int stage) {
  TaskList tl;

  TaskID none(0);
  // first make other useful containers
  if (stage == 1) {
    auto container = pmb->real_containers.Get();
    pmb->real_containers.Add("my container", container);
    auto base = pmb->real_containers.GetSwarmContainer();
  }

  auto sc = pmb->real_containers.GetSwarmContainer();

  auto swarm = sc->Get("my particles");

  auto update_swarm = tl.AddTask(UpdateSwarm, none, pmb, stage,
                                            stage_name, integrator);

  auto container = pmb->real_containers.Get("my container");

  auto update_container = tl.AddTask(MyContainerTask, none, container);

  return tl;
}

} // namespace particles_example
