/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_RadiativeTransferStepper.H
  @brief  TimeStepper class for only evolving radiative transfer modules
  @author Robert Marskar
*/

#ifndef CD_RadiativeTransferStepper_H
#define CD_RadiativeTransferStepper_H

// Our includes
#include <CD_TimeStepper.H>
#include <CD_RtSolver.H>
#include <CD_NamespaceHeader.H>

namespace Physics {

  /*!
    @brief Namespace for encapsulating the radiative transfer physics module
  */
  namespace RadiativeTransfer {

    /*!
      @brief Implementation of TimeStepper for solving for a single radiative transfer species. This supports both Monte Carlo photons and Helmholtz approximations
      through the common RtSolver interface. 
    */
    template <class T>
    class RadiativeTransferStepper : public TimeStepper {
    public:

      /*!
	@brief Constructor. Reads a couple of input options.
      */
      RadiativeTransferStepper();

      /*!
	@brief Destructor (does nothing)
      */
      ~RadiativeTransferStepper();

      /*!
	@brief Set up the solver
      */      
      void setupSolvers() override;

      /*!
	@brief Fill simulation with initial data
      */
      void initialData() override;

      /*!
	@brief Allocate necessary memory for solvers.
      */      
      void allocate() override;

      /*!
	@brief Post-initialization functionality -- sets the source term. 
      */            
      void postInitialize() override;

#ifdef CH_USE_HDF5
      /*!  
	@brief Write checkpoint data to file
	@param[inout] a_handle HDF5 fil
	@param[in]    a_lvl    Grid level
      */      
      void writeCheckpointData(HDF5Handle& a_handle, const int a_lvl) const override;
#endif
      
#ifdef CH_USE_HDF5
      /*!
	@brief Read checkpoint data from file
	@param[inout] a_handle HDF5 file
	@param[in]    a_lvl    Grid level
      */      
      void readCheckpointData(HDF5Handle& a_handle, const int a_lvl) override;
#endif

      /*!
	@brief Perform post-checkpoint setup routines (sets the source in the solver)
      */
      void postCheckpointSetup() override;

      /*!
	@brief Get number of plot variables for this physics module
      */
      int getNumberOfPlotVariables() const override;

      /*!
	@brief Write plot data to file
	@param[inout] a_output            Output data holder.
	@param[in]    a_plotVariableNames Plot variable names.
	@param[inout] a_icomp             Starting component in a_output to begin at. 
      */
      void writePlotData(EBAMRCellData& a_output, Vector<std::string>& a_plotVariableNames, int& a_icomp) const override;

      /*!
	@brief Compute a time step to be used by Driver. This sets a_dt = m_dt.
	@param[out] a_dt       Time step to be used.
	@param[out] a_timeCode Identifier for how the time step was computed. 
      */      
      void computeDt(Real& a_dt, TimeCode& a_timeCode) override;

      /*!
	@brief Advancement method. Calls the solver function. 
	@param[in] a_dt Time step to be used for advancement
	@return    a_dt
      */      
      Real advance(const Real a_dt) override;

      /*!
	@brief Synchronzie solver times and time steps
	@param[in] a_step Time step
	@param[in] a_time Time (in seconds)
	@param[in] a_dt   Time step that was used. 
      */      
      void synchronizeSolverTimes(const int a_step, const Real a_time, const Real a_dt) override;

      /*!
	@brief Print a step report (does nothing)
	@details This is called by Driver after time step. 
      */      
      void printStepReport() override;

      /*!
	@brief Parse runtime options.
      */      
      void parseRuntimeOptions() override;

      /*!
	@brief Register realms
      */
      void registerRealms() override;

      /*!
	@brief Register operators -- calls the solver registration routine. 
      */
      void registerOperators() override;

      /*!
	@brief Perform pre-regrid operations -- calls the solver function. 
	operation takes a lot of memory. 
	@param[in] a_lmin           The coarsest level that changes
	@param[in] a_oldFinestLevel The finest level before the regrid. 
      */      
      void preRegrid(const int a_lmin, const int a_oldFinestLevel) override;

      /*!
	@brief Regrid function. Calls the solver function. 
	@param[in] a_lmin           The coarsest level that changed. 
	@param[in] a_oldFinestLevel The finest level before the regrid. 
	@param[in] a_newFinestLevel The finest level after the regrid. 
      */      
      void regrid(const int a_lmin, const int a_oldFinestLevel, const int a_newFinestLevel) override;

      /*!
	@brief Post-regrid function. Sets a Gaussian source. 
      */
      void postRegrid() override;

    protected:

      /*!
	@brief Realm where solver lives.
      */
      std::string m_realm;

      /*!
	@brief Time step to use
      */
      Real m_dt;      

      /*!
	@brief Reference to solver
      */
      RefCountedPtr<RtSolver>  m_solver;

      /*!
	@brief Reference to species
      */      
      RefCountedPtr<RtSpecies> m_species;

      /*!
	@brief For setting a Gaussian source in the radiative transfer equation solver
      */
      void setGaussianSource();
    };
  }
}

#include <CD_NamespaceFooter.H>

#include <CD_RadiativeTransferStepperImplem.H>

#endif