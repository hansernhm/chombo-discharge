#include <CD_Driver.H>
#include <CD_FieldSolverFactory.H>
#include <CD_FieldSolverMultigrid.H>
#include <CD_ItoLayout.H>
#include <CD_ItoSolver.H>
#include <CD_RtLayout.H>
#include <CD_McPhoto.H>
#include <CD_ItoKMCJSON.H>
#include <CD_CylindricalVoid.H>
#include <CD_ItoKMCGodunovStepper.H>
#include <CD_ItoKMCStreamerTagger.H>
#include "ParmParse.H"

// This is the potential curve (constant in this case). Modify it if you want to.
Real g_potential = 1.0;
Real f = 1e8;  // Hz
Real potential_curve(const Real a_time){
  double value = sin(2*3.141593*f*a_time) >= 0.0 ? 1.0:-1.0;
  //  return g_potential * sin(2*3.14*f*a_time);
    return g_potential * value;
}


// Real g_potential;
// Real potential_curve(const Real a_time){
//   return g_potential;
// }

using namespace ChomboDischarge;
using namespace Physics::ItoKMC;

int main(int argc, char* argv[]){

#ifdef CH_MPI
  MPI_Init(&argc, &argv);
#endif

  // Build class options from input script and command line options
  const std::string input_file = argv[1];
  ParmParse pp(argc-2, argv+2, NULL, input_file.c_str());

  // Get potential from input script 
  std::string basename; 
  {
     ParmParse pp("ItoKMCCylindricalVoid");
     pp.get("potential", g_potential);
     pp.get("basename",  basename);
     pp.get("frequency",  f);
     setPoutBaseName(basename);
  }

  // Initialize RNG
  Random::seed();

  auto compgeom    = RefCountedPtr<ComputationalGeometry> (new CylindricalVoid());
  auto amr         = RefCountedPtr<AmrMesh> (new AmrMesh());
  auto physics     = RefCountedPtr<ItoKMCPhysics> (new ItoKMCJSON());
  auto timestepper = RefCountedPtr<ItoKMCStepper<>> (new ItoKMCGodunovStepper<>(physics));
  auto tagger      = RefCountedPtr<CellTagger> (new ItoKMCStreamerTagger<ItoKMCStepper<>>(physics, timestepper, amr));

  // Set potential 
  timestepper->setVoltage(potential_curve);

  // Set up the Driver and run it
  RefCountedPtr<Driver> engine = RefCountedPtr<Driver> (new Driver(compgeom, timestepper, amr, tagger));
  engine->setupAndRun(input_file);

#ifdef CH_MPI
  CH_TIMER_REPORT();
  MPI_Finalize();
#endif
}
