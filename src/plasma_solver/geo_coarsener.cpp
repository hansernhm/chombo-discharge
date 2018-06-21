/*!
  @file   geo_coarsener.cpp
  @brief  Implementation of geo_coarsener.H
  @author Robert Marskar
  @date   June 2018
*/

#include "geo_coarsener.H"

#include <ParmParse.H>
#include <sstream>

geo_coarsener::geo_coarsener(){
  m_coarsen_boxes.resize(0);
  m_coarsen_levels.resize(0);

  
  { // Info from input script
    ParmParse pp("geo_coarsener");

    int num_boxes = 0;
    pp.query("num_boxes", num_boxes);

    if(num_boxes > 0){
      m_coarsen_boxes.resize(num_boxes);
      m_coarsen_levels.resize(num_boxes);
      
      const int ndigits = (int) log10((double) num_boxes) + 1;
      
      for (int ibox = 0; ibox < num_boxes; ibox++){
	char* cstr = new char[ndigits];
	sprintf(cstr, "%d", 1+ibox);

	std::string str1 = "box" + std::string(cstr) + "_lo";
	std::string str2 = "box" + std::string(cstr) + "_hi";
	std::string str3 = "box" + std::string(cstr) + "_lvl";

	Vector<Real> corner_lo(SpaceDim);
	Vector<Real> corner_hi(SpaceDim);
	int finest_box_lvl;

	pp.getarr(str1.c_str(), corner_lo, 0, SpaceDim);
	pp.getarr(str2.c_str(), corner_hi, 0, SpaceDim);
	pp.get(str3.c_str(),    finest_box_lvl);

	const RealVect c1 = RealVect(D_DECL(corner_lo[0], corner_lo[1], corner_lo[2]));
	const RealVect c2 = RealVect(D_DECL(corner_hi[0], corner_hi[1], corner_hi[2]));

	m_coarsen_boxes[ibox] = real_box(c1,c2);
	m_coarsen_levels[ibox] = finest_box_lvl;
	
	delete cstr;
      }

    }
  }
}

geo_coarsener::~geo_coarsener(){
}

Vector<real_box> geo_coarsener::get_coarsen_boxes(){
  return m_coarsen_boxes;
}


Vector<int> geo_coarsener::get_coarsen_levels(){
  return m_coarsen_levels;
}