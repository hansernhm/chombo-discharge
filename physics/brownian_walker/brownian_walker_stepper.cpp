/*!
  @file   brownian_walker_stepper.cpp
  @brief  Implementation of brownian_walker_stepper.H
  @author Robert Marskar
  @data   March 2020
*/

#include "brownian_walker_stepper.H"
#include "brownian_walker_species.H"

using namespace physics::brownian_walker;

brownian_walker_stepper::brownian_walker_stepper(){

}

brownian_walker_stepper::brownian_walker_stepper(RefCountedPtr<ito_solver>& a_solver){
  m_solver = a_solver;
}

brownian_walker_stepper::~brownian_walker_stepper(){

}

void brownian_walker_stepper::initial_data(){
  m_species = RefCountedPtr<ito_species> (new brownian_walker_species());

  
  MayDay::Abort("brownian_walker_stepper::initial_data - not implemented");
}


void brownian_walker_stepper::write_checkpoint_data(HDF5Handle& a_handle, const int a_lvl) const{

}

void brownian_walker_stepper::read_checkpoint_data(HDF5Handle& a_handle, const int a_lvl) {
  
}

void brownian_walker_stepper::post_checkpoint_setup() {

}

int brownian_walker_stepper::get_num_plot_vars() const {

}

void brownian_walker_stepper::write_plot_data(EBAMRCellData& a_output, Vector<std::string>& a_plotvar_names, int& a_icomp) const {

}

void brownian_walker_stepper::compute_dt(Real& a_dt, time_code::which_code& a_timecode) {

}

void brownian_walker_stepper::synchronize_solver_times(const int a_step, const Real a_time, const Real a_dt) {

}

void brownian_walker_stepper::print_step_report() {

}

bool brownian_walker_stepper::need_to_regrid() {
  return false;
}

void brownian_walker_stepper::cache() {

}

void brownian_walker_stepper::deallocate() {

}

void brownian_walker_stepper::setup_solvers() {

}

Real brownian_walker_stepper::advance(const Real a_dt) {

}

void brownian_walker_stepper::regrid(const int a_lmin, const int a_old_finest_level, const int a_new_finest_level) {

}