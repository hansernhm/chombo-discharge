#ifdef CH_LANG_CC
/*
 *      _______              __
 *     / ___/ /  ___  __ _  / /  ___
 *    / /__/ _ \/ _ \/  V \/ _ \/ _ \
 *    \___/_//_/\___/_/_/_/_.__/\___/
 *    Please refer to Copyright.txt, in Chombo's root directory.
 */
#endif

#include "CylindricalTransitionDomain.H"
#include "RectangularUniformMap.H"
#include "CylindricalTransitionSectorMap.H"
// #include "UGIO.H"
// #include "CH_HDF5.H"

#include "NamespaceHeader.H"


//=====================================================================

CylindricalTransitionDomain::CylindricalTransitionDomain()
{
  m_isDefined = false;
}


// ---------------------------------------------------------
/// Construct CylindricalTransitionDomain with a single \em a_domBox as computational domain
CylindricalTransitionDomain::CylindricalTransitionDomain(const Box&        a_centralBox,
                                                         const RealVect&   a_center,
                                                         Real              a_bxWidth,
                                                         Real              a_outerRadius)
{
  define(a_centralBox, a_center, a_bxWidth, a_outerRadius);
}


// ---------------------------------------------------------
CylindricalTransitionDomain::~CylindricalTransitionDomain()
{
}


// ---------------------------------------------------------
void CylindricalTransitionDomain::define(const Box&        a_centralBox,
                                         const RealVect&   a_center,
                                         Real              a_bxWidth,
                                         Real              a_outerRadius)
{
  CylindricalDomain::define(a_centralBox, a_center, a_bxWidth, a_outerRadius);

  /*
    Define m_blockMaps[iblock] for iblock in 0:m_nblocks-1.
    These use the CYLINDERTRANSITION map.
   */
  m_blockMaps[CUBE] = new RectangularUniformMap(m_centralCornerLo,
                                                m_centralCornerHi,
                                                m_boxes[CUBE]);
  for (int iblock = 1; iblock < m_nblocks; iblock++) // sector blocks only
    {
      m_blockMaps[iblock] = new CylindricalTransitionSectorMap(m_center,
                                                               m_bxWidth,
                                                               m_outerRadius,
                                                               iblock,
                                                               m_boxes[iblock]);
    }

  /*
    Define MappedBlock m_mappedBlock[iblock] for iblock in 0:m_nblocks-1
  */
  for (int iblock = 0; iblock < m_nblocks; iblock++) // ALL blocks
    {
      m_mappedBlocks[iblock].define(m_boxes[iblock],
                                    m_blockMaps[iblock],
                                    m_blockBoundaries[iblock]);
    }

  defineMappedDomain();
}

#include "NamespaceFooter.H"