/* chombo-discharge
 * Copyright © SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @brief CD_CylindricalVoid.cpp
  @brief Implementation of CD_CylindricalVoid.H
  @author Robert Marskar
*/

// Std includes
#include <string>
#include <iostream>

// Chombo includes
#include <CH_Timer.H>
#include <ParmParse.H>

// Our includes
#include <CD_EBGeometryIF.H>
#include <CD_RoundedCylinderIF.H>
#include <CD_CylindricalVoid.H>
#include <CD_NamespaceHeader.H>

using namespace EBGeometry;

using Vec3    = EBGeometry::Vec3T<Real>;
using ImpFunc = EBGeometry::ImplicitFunction<Real>;

CylindricalVoid::CylindricalVoid() noexcept
{
  CH_TIME("CylindricalVoid::CylindricalVoid");

  ParmParse pp("CylindricalVoid");

  m_electrodes.resize(0);
  m_dielectrics.resize(0);

  this->defineDielectric();

}

void
CylindricalVoid::defineDielectric() noexcept
{
  CH_TIME("CylindricalVoid::defineDielectric");

  Real cavityThickness = 0.0;
  Real cavityRadius    = 0.0;
  Real cavityCurvature = 0.0;
  Real cavitySmooth    = 0.0;
  Real gasPermittivity    = 1.0;
  Real solidPermittivity    = 1.0;
  Real rotationAngle   = 0.0;
  int rotationAxis     = 0;
  bool invert;
  
  Vector<Real> v(3, 0.0);

  ParmParse pp("CylindricalVoid");

  // Get input parameters
  pp.get("cavity_extra_thickness", cavityThickness);
  pp.get("cavity_radius", cavityRadius);
  pp.get("cavity_curvature", cavityCurvature);
  pp.get("cavity_smooth", cavitySmooth);
  pp.get("gas_permittivity", gasPermittivity);
  pp.get("solid_permittivity", solidPermittivity);  
  pp.get("rotation_angle", rotationAngle);
  pp.get("rotation_axis", rotationAxis);
  pp.get("invert", invert);
  pp.getarr("cavity_center", v, 0, SpaceDim);
  
  this->setGasPermittivity(gasPermittivity);
  // Create cavity using EBGeometry. This is an elongation of the union of a torus and a cylinder. Created
  // in the xy-plane and then put into place later.

  if (gasPermittivity <= 0.0) {
    MayDay::Error("Aerosol::Aerosol() -- eps_gas cannot be <= 0.0");
  }
  if (solidPermittivity <= 0.0) {
    MayDay::Error("Aerosol::Aerosol() -- permittivity cannot be <= 0.0");
  }
  auto torus    = std::make_shared<TorusSDF<Real>>(Vec3::zero(), cavityRadius, cavityCurvature);
  auto cylinder = std::make_shared<CylinderSDF<Real>>(Vec3::unit(2) * cavityCurvature,
                                                      -Vec3::unit(2) * cavityCurvature,
                                                      cavityRadius);

  // Make smooth union and put into place.
  auto cavity = Union<Real>(torus, cylinder);

  if (cavityThickness > 0.0) {
    cavity = Elongate<Real>(cavity, 0.5 * cavityThickness * Vec3::unit(2));
  }
  cavity = Rotate<Real>(cavity, rotationAngle, rotationAxis);
  cavity = Translate<Real>(cavity, Vec3(v[0], v[1], v[2]));

  if (SpaceDim == 2) {
    cavity = Elongate<Real>(cavity, std::numeric_limits<Real>::max() * Vec3::unit(2));
  }

  // Finally, create our dielectric.
  m_dielectrics.push_back(Dielectric(RefCountedPtr<BaseIF>(new EBGeometryIF<Real>(cavity, !invert)), solidPermittivity));
}

#include <CD_NamespaceFooter.H>
