/* chombo-discharge
 * Copyright © 2025 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_ItoKMCIntegrateVariableOnDomainWallImplem.H
  @brief  Implementation of CD_ItoKMCIntegrateVariableOnDomainWall.H
  @author Robert Marskar
*/

#ifndef CD_ItoKMCIntegrateVariableOnDomainWallImplem_H
#define CD_ItoKMCIntegrateVariableOnDomainWallImplem_H

// Our includes
#include <CD_ItoKMCIntegrateVariableOnDomainWall.H>
#include <CD_NamespaceHeader.H>

using namespace Physics::ItoKMC;

// HK not sure about this
using namespace AmrMesh; 

template <typename I, typename C, typename R, typename F>
ItoKMCIntegrateVariableOnDomainWall<I, C, R, F>::ItoKMCIntegrateVariableOnDomainWall(RefCountedPtr<ItoKMCPhysics>& a_physics) noexcept
  : ItoKMCGodunovStepper<I, C, R, F>(a_physics)
{
  CH_TIME("ItoKMCIntegrateVariableOnDomainWall::ItoKMCIntegrateVariableOnDomainWall");

  ParmParse pp("ItoKMCIntegrateVariableOnDomainWall");

  m_maxFieldExitCrit       = std::numeric_limits<Real>::max();
  m_maxFieldChange = -1.0;
  m_relFieldChange = -1.0;

  pp.query("max_field_exit_crit", m_maxFieldExitCrit);
  pp.query("rel_field_exit_crit", m_relFieldExitCrit);
}

template <typename I, typename C, typename R, typename F>
void
ItoKMCIntegrateVariableOnDomainWall<I, C, R, F>::postInitialize() noexcept
{
  CH_TIME("ItoKMCIntegrateVariableOnDomainWall::postInitialize");
  if (this->m_verbosity > 5) {
    pout() << "ItoKMCIntegrateVariableOnDomainWall::postInitialize" << endl;
  }

  ItoKMCGodunovStepper<I, C, R, F>::postInitialize();

}

template <typename I, typename C, typename R, typename F>
void
ItoKMCIntegrateVariableOnDomainWall<I, C, R, F>::postRegrid() noexcept
{
  CH_TIME("ItoKMCIntegrateVariableOnDomainWall::postRegrid");
  if (this->m_verbosity > 5) {
    pout() << "ItoKMCIntegrateVariableOnDomainWall::postRegrid" << endl;
  }

  ItoKMCGodunovStepper<I, C, R, F>::postRegrid();

}

template <typename I, typename C, typename R, typename F>
void
ItoKMCIntegrateVariableOnDomainWall<I, C, R, F>::postCheckpointSetup() noexcept
{
  CH_TIME("ItoKMCIntegrateVariableOnDomainWall::postCheckpointSetup");
  if (this->m_verbosity > 5) {
    pout() << "ItoKMCIntegrateVariableOnDomainWall::postCheckpointSetup" << endl;
  }

  ItoKMCGodunovStepper<I, C, R, F>::postCheckpointSetup();

}

template <typename I, typename C, typename R, typename F>
Real
ItoKMCIntegrateVariableOnDomainWall<I, C, R, F>::advance(const Real a_dt)
{
  CH_TIME("ItoKMCIntegrateVariableOnDomainWall::advance");
  if (this->m_verbosity > 5) {
    pout() << "ItoKMCIntegrateVariableOnDomainWall::advance" << endl;
  }

  const Real actualDt = ItoKMCGodunovStepper<I, C, R, F>::advance(a_dt);

  const Real integratedValue = this->integrateVariableOnDomainWall();

  m_relFieldChange = fieldChange.first;
  m_maxFieldChange = fieldChange.second;

  return actualDt;
}

template <typename I, typename C, typename R, typename F>
Real
ItoKMCIntegrateVariableOnDomainWall<I, C, R, F>::computeDt()
{
  CH_TIME("ItoKMCIntegrateVariableOnDomainWall::computeDt");
  if (this->m_verbosity > 5) {
    pout() << "ItoKMCIntegrateVariableOnDomainWall::computeDt" << endl;
  }

  Real newDt = 0.0;

  newDt = ItoKMCGodunovStepper<I, C, R, F>::computeDt();


  return newDt;
}

template <typename I, typename C, typename R, typename F>
void
ItoKMCIntegrateVariableOnDomainWall<I, C, R, F>::printStepReport() noexcept
{
  CH_TIME("ItoKMCIntegrateVariableOnDomainWall::printStepReport");
  if (this->m_verbosity > 5) {
    pout() << "ItoKMCIntegrateVariableOnDomainWall::printStepReport" << endl;
  }

  ItoKMCGodunovStepper<I, C, R, F>::printStepReport();
  const std::string whitespace = "                                   ";
  pout() << whitespace + "Delta E(max) = " << 100.0 * m_maxFieldChange << " (%)" << endl;
  pout() << whitespace + "Delta E(rel) = " << 100.0 * m_relFieldChange << " (%)" << endl;
}


template <typename I, typename C, typename R, typename F>
std::Real
ItoKMCIntegrateVariableOnDomainWall<I, C, R, F>::integrateVariableOnDomainWall() noexcept
{
  CH_TIME("ItoKMCIntegrateVariableOnDomainWall::integrateVariableOnDomainWall");
  if (this->m_verbosity > 5) {
    pout() << "ItoKMCIntegrateVariableOnDomainWall::integrateVariableOnDomainWall" << endl;
  }

  EBAMRCellData poissonNorm;
 
  (this->m_amr)->allocate(poissonNorm, this->m_fluidRealm, this->m_plasmaPhase, 1);
 
  // Get the fields HK: HOW TO GET THE y-FIELD IN THE SOLID PHASE?
  const MFAMRCellData electricField = (this->m_fieldSolver)->getElectricField();
   
  // Iterate through the grid cells to find elements on the domain wall .
  //
  // PS: I'm doing this with a direct loop because DataOps::getMaxMin will do ALL cells, including ones that are
  //     covered by a finer level. But we only want to evaluate the valid region.
  Real integratedValue = -1.0;

  for (int lvl = 0; lvl <= (this->m_amr)->getFinestLevel(); lvl++) {
    const DisjointBoxLayout& dbl   = (this->m_amr)->getGrids(this->m_fluidRealm)[lvl];
    const DataIterator&      dit   = dbl.dataIterator();
    const EBISLayout&        ebisl = (this->m_amr)->getEBISLayout(this->m_fluidRealm, this->m_plasmaPhase)[lvl];

    const int nbox = dit.size();

#pragma omp parallel for schedule(runtime)
    for (int mybox = 0; mybox < nbox; mybox++) {
      const DataIndex&     din        = dit[mybox];
      const Box            box        = dbl[din];
      const EBISBox&       ebisbox    = ebisl[din];
      const BaseFab<bool>& validCells = (*(this->m_amr)->getValidCells(this->m_fluidRealm)[lvl])[din];

      const EBCellFAB& data    = (*deltaE[lvl])[din];
      const FArrayBox& dataReg = data.getFArrayBox();

      auto regularKernel = [&](const IntVect& iv) -> void {
        if (validCells(iv) && ebisbox.isRegular(iv)) {
          relChange = std::max(relChange, std::abs(dataReg(iv, 0)));
        }
      };

      auto irregularKernel = [&](const VolIndex& vof) -> void {
        if (validCells(vof.gridIndex()) && ebisbox.isIrregular(vof.gridIndex())) {
          relChange = std::max(relChange, std::abs(data(vof, 0)));
        }
      };

      VoFIterator& vofit = (*(this->m_amr)->getVofIterator(this->m_fluidRealm, this->m_plasmaPhase)[lvl])[din];

      // Get domain box . but How ? 
      //ProblemDomain AmrMesh::getProblemDomains()
      ///ProblemDomain::domainBox
      
      BoxLoops::loop(box, regularKernel);
      BoxLoops::loop(vofit, irregularKernel);
    }
  }

  relChange = ParallelOps::max(relChange);
  maxChange = maxSpaceChargeField / maxBackgroundField - 1.0;

  return integratedValue;
}

#include <CD_NamespaceFooter.H>

#endif
