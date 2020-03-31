/*!
  @file   driver.cpp
  @brief  Implementation of driver.H
  @author Robert Marskar
  @date   Nov. 2017
*/

#include "driver.H"
#include "data_ops.H"
#include "mfalias.H"
#include "units.H"
#include "memrep.H"

#include <EBArith.H>
#include <EBAlias.H>
#include <LevelData.H>
#include <EBCellFAB.H>
#include <EBAMRIO.H>
#include <EBAMRDataOps.H>
#include <ParmParse.H>

driver::driver(){
  CH_TIME("driver::driver(weak)");
  if(m_verbosity > 5){
    pout() << "driver::driver(weak)" << endl;
  }

  MayDay::Abort("driver::driver - weak construction is not allowed (yet)");

}

driver::driver(const RefCountedPtr<computational_geometry>& a_compgeom,
	       const RefCountedPtr<time_stepper>&           a_timestepper,
	       const RefCountedPtr<amr_mesh>&               a_amr,
	       const RefCountedPtr<cell_tagger>&            a_celltagger,
	       const RefCountedPtr<geo_coarsener>&          a_geocoarsen){
  CH_TIME("driver::driver(full)");

  parse_verbosity();
  if(m_verbosity > 5){
    pout() << "driver::driver(full)" << endl;
  }
  
  set_computational_geometry(a_compgeom);              // Set computational geometry
  set_time_stepper(a_timestepper);                     // Set time stepper
  set_amr(a_amr);                                      // Set amr
  set_cell_tagger(a_celltagger);                       // Set cell tagger
  set_geo_coarsen(a_geocoarsen);                       // Set geo coarsener

  // Parse some class options
  parse_regrid();
  parse_restart();
  parse_memrep();
  parse_coarsen();
  parse_output_directory();
  parse_output_file_names();
  parse_verbosity();
  parse_output_intervals();
  parse_geo_refinement();
  parse_num_plot_ghost();
  parse_grow_tags();
  parse_geom_only();
  parse_ebis_memory_load_balance();
  parse_write_ebis();
  parse_read_ebis(); 
  parse_simulation_time();
  parse_file_depth();
  parse_plot_vars();


  // Debugging stuff
  // this->set_dump_mass(false);                                // Dump mass to file
  // this->set_dump_charge(false);                              // Dump charges to file
  this->set_output_centroids(true);                          // Use cell centroids for output

  // AMR does its thing
  m_amr->sanity_check();                 // Sanity check, make sure everything is set up correctly
  m_amr->build_domains();                // Build domains and resolutions, nothing else

  // Parse the geometry generation method
  parse_geometry_generation();

  // Ok we're ready to go. 
  m_step          = 0;
  m_time          = 0.0;
}

driver::~driver(){
  CH_TIME("driver::~driver");
}

int driver::get_num_plot_vars() const {
  CH_TIME("driver::get_num_plot_vars");
  if(m_verbosity > 5){
    pout() << "driver::get_num_plot_vars" << endl;
  }

  int num_output = 0;

  if(m_plot_tags)            num_output = num_output + 1;
  if(m_plot_ranks)           num_output = num_output+1;

  return num_output;
}

Vector<std::string> driver::get_plotvar_names() const {
  CH_TIME("driver::get_plotvar_names");
  if(m_verbosity > 5){
    pout() << "driver::get_plotvar_names" << endl;
  }
  
  Vector<std::string> names(0);
  
  if(m_plot_tags) names.push_back("cell_tags");
  if(m_plot_ranks) names.push_back("mpi_rank");
  return names;
}

void driver::allocate_internals(){
  CH_TIME("driver::allocate_internals");
  if(m_verbosity > 5){
    pout() << "driver::allocate_internals" << endl;
  }
  
  const int ncomp        = 1;
  const int finest_level = m_amr->get_finest_level();
  const IntVect ghost    = IntVect::Zero;

  m_tags.resize(1 + finest_level);
  
  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    m_tags[lvl] = RefCountedPtr<LayoutData<DenseIntVectSet> > (new LayoutData<DenseIntVectSet>(dbl));

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      DenseIntVectSet& divs = (*m_tags[lvl])[dit()];
      divs = DenseIntVectSet(dbl.get(dit()), false);
    }
  }
}

void driver::cache_tags(const EBAMRTags& a_tags){
  CH_TIME("driver::cache_tags");
  if(m_verbosity > 5){
    pout() << "driver::cache_tags" << endl;
  }

  const int ncomp         = 1;
  const int finest_level  = m_amr->get_finest_level();
  const int ghost         = 0;

  m_amr->allocate(m_cached_tags, ncomp, ghost);
  m_cached_tags.resize(1+finest_level);
  
  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];

    // Copy tags onto boolean mask
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      BaseFab<bool>& cached_tags  = (*m_cached_tags[lvl])[dit()];
      cached_tags.setVal(false);

      const IntVectSet divs = IntVectSet((*m_tags[lvl])[dit()]);
      for (IVSIterator ivsIt(divs); ivsIt.ok(); ++ivsIt){
	const IntVect iv = ivsIt();
	cached_tags(iv,0) = true;
      }
    }
  }
}

void driver::deallocate_internals(){
  CH_TIME("driver::deallocate_internals");
  if(m_verbosity > 5){
    pout() << "driver::deallocate_internals" << endl;
  }

  //  m_amr->deallocate(m_tags);
}

void driver::write_ebis(){
  CH_TIME("driver::write_ebis");
  if(m_verbosity > 5){
    pout() << "driver::write_ebis" << endl;
  }

  const std::string path_gas = m_output_dir + "/geo/" + m_ebis_gas_file;
  const std::string path_sol = m_output_dir + "/geo/" + m_ebis_sol_file;

  const RefCountedPtr<EBIndexSpace> ebis_gas = m_mfis->get_ebis(phase::gas);
  const RefCountedPtr<EBIndexSpace> ebis_sol = m_mfis->get_ebis(phase::solid);

  if(!ebis_gas.isNull()){
    HDF5Handle gas_handle(path_gas.c_str(), HDF5Handle::CREATE);
    ebis_gas->write(gas_handle);
    gas_handle.close();
  }
  if(!ebis_sol.isNull()){
    HDF5Handle sol_handle(path_sol.c_str(), HDF5Handle::CREATE);
    ebis_sol->write(sol_handle);
    sol_handle.close();
  }
}

void driver::get_geom_tags(){
  CH_TIME("driver::get_geom_tags");
  if(m_verbosity > 5){
    pout() << "driver::get_geom_tags" << endl;
  }

  const int maxdepth = m_amr->get_max_amr_depth();

  m_geom_tags.resize(maxdepth);

  const RefCountedPtr<EBIndexSpace> ebis_gas = m_mfis->get_ebis(phase::gas);
  const RefCountedPtr<EBIndexSpace> ebis_sol = m_mfis->get_ebis(phase::solid);

  CH_assert(ebis_gas != NULL);

  for (int lvl = 0; lvl < maxdepth; lvl++){ // Don't need tags on maxdepth, we will never generate grids below that.
    const ProblemDomain& cur_dom = m_amr->get_domains()[lvl];
    const int which_level = ebis_gas->getLevel(cur_dom);

    IntVectSet cond_tags;
    IntVectSet diel_tags;
    IntVectSet gas_tags;
    IntVectSet solid_tags;
    IntVectSet gas_diel_tags;
    IntVectSet gas_solid_tags;

    // Conductor cells
    if(m_conductor_tag_depth > lvl){ 
      cond_tags = ebis_gas->irregCells(which_level);
      if(!ebis_sol.isNull()){
	cond_tags |= ebis_sol->irregCells(which_level);
	cond_tags -= m_mfis->interface_region(cur_dom);
      }
    }

    // Dielectric cells
    if(m_dielectric_tag_depth > lvl){ 
      if(!ebis_sol.isNull()){
	diel_tags = ebis_sol->irregCells(which_level);
      }
    }

    // Gas-solid interface cells
    if(m_gas_solid_interface_tag_depth > lvl){ 
      if(!ebis_sol.isNull()){
	gas_tags = ebis_gas->irregCells(which_level);
      }
    }

    // Gas-dielectric interface cells
    if(m_gas_dielectric_interface_tag_depth > lvl){
      if(!ebis_sol.isNull()){
	gas_diel_tags = m_mfis->interface_region(cur_dom);
      }
    }

    // Gas-conductor interface cells
    if(m_gas_conductor_interface_tag_depth > lvl){ 
      gas_solid_tags = ebis_gas->irregCells(which_level);
      if(!ebis_sol.isNull()){
	gas_solid_tags -= m_mfis->interface_region(cur_dom);
      }
    }

    // Solid-solid interfaces
    if(m_solid_solid_interface_tag_depth > lvl){ 
      if(!ebis_sol.isNull()){
	solid_tags = ebis_sol->irregCells(which_level);

	// Do the intersection with the conductor cells
	IntVectSet tmp = ebis_gas->irregCells(which_level);
	tmp |= ebis_sol->irregCells(which_level);
	tmp -= m_mfis->interface_region(cur_dom);

	solid_tags &= tmp;
      }
    }

    m_geom_tags[lvl].makeEmpty();
    m_geom_tags[lvl] |= diel_tags;
    m_geom_tags[lvl] |= cond_tags;
    m_geom_tags[lvl] |= gas_diel_tags;
    m_geom_tags[lvl] |= gas_solid_tags;
    m_geom_tags[lvl] |= gas_tags;
    m_geom_tags[lvl] |= solid_tags;
  }

  // Remove tags using the geocoarsener if we have it
  if(!m_geocoarsen.isNull()){
    m_geocoarsen->coarsen_tags(m_geom_tags, m_amr->get_dx(), m_amr->get_prob_lo());
  }

  // Grow tags. This is an ad-hoc fix that prevents ugly grid near EBs (i.e. cases where only ghost cells are used
  // for elliptic equations)
  const int growth = Max(1, m_amr->get_irreg_growth());
  for (int lvl = 0; lvl < maxdepth; lvl++){
    m_geom_tags[lvl].grow(growth);
  }
}

void driver::get_loads_and_boxes(long long& a_myPoints,
				 long long& a_myPointsGhosts,
				 long long& a_myBoxes,
				 long long& a_totalPoints,
				 long long& a_totalPointsGhosts,
				 long long& a_totalBoxes,
				 Vector<long long>& a_my_level_boxes,
				 Vector<long long>& a_total_level_boxes,
				 Vector<long long>& a_my_level_points,
				 Vector<long long>& a_total_level_points,
				 const int& a_finestLevel,
				 const Vector<DisjointBoxLayout>& a_grids){
  CH_TIME("driver::get_loads_and_boxes");
  if(m_verbosity > 5){
    pout() << "driver::get_loads_and_boxes" << endl;
  }

  a_myPoints          = 0;
  a_myPointsGhosts    = 0;
  a_myBoxes           = 0;
  a_totalPoints       = 0;
  a_totalPointsGhosts = 0;
  a_totalBoxes        = 0;

  a_my_level_boxes.resize(1 + a_finestLevel);
  a_total_level_boxes.resize(1 + a_finestLevel);
  a_my_level_points.resize(1 + a_finestLevel);
  a_total_level_points.resize(1 + a_finestLevel);

  const int ghost = m_amr->get_num_ghost();

  for (int lvl = 0; lvl <= a_finestLevel; lvl++){
    const DisjointBoxLayout& dbl = a_grids[lvl];
    const Vector<Box> boxes      = dbl.boxArray();
    const Vector<int> procs      = dbl.procIDs();
    
    // Find the total number of points and boxes for this level
    long long pointsThisLevel       = 0;
    long long pointsThisLevelGhosts = 0;
    long long boxesThisLevel        = 0;
    for (LayoutIterator lit = dbl.layoutIterator(); lit.ok(); ++lit){
      Box box      = dbl[lit()];
      Box grownBox = dbl[lit()];
      grownBox.grow(ghost);
      
      //
      pointsThisLevel       += box.numPts();
      pointsThisLevelGhosts += grownBox.numPts();
      boxesThisLevel        += 1;
    }
    a_total_level_points[lvl] = pointsThisLevel;
    a_total_level_boxes[lvl]  = boxesThisLevel;


    // Find the total number of points and boxes that this processor owns
    long long myPointsLevel       = 0;
    long long myPointsLevelGhosts = 0;
    long long myBoxesLevel        = 0;
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      Box box      = dbl[dit()];
      Box grownBox = dbl[dit()];
      grownBox.grow(3);
      
      myPointsLevel       += box.numPts();
      myPointsLevelGhosts += grownBox.numPts();
      myBoxesLevel        += 1;
    }


    // Total for this level
    a_totalPoints           += pointsThisLevel;
    a_totalPointsGhosts     += pointsThisLevelGhosts;
    a_totalBoxes            += boxesThisLevel;
    a_myPoints              += myPointsLevel;
    a_myPointsGhosts        += myPointsLevelGhosts;
    a_myBoxes               += myBoxesLevel;
    a_my_level_boxes[lvl]    = myBoxesLevel;
    a_total_level_boxes[lvl] = boxesThisLevel;
    a_my_level_points[lvl]   = myPointsLevel;
    a_my_level_boxes[lvl]    = myBoxesLevel;
  }
}

void driver::grid_report(){
  CH_TIME("driver::grid_report");
  if(m_verbosity > 5){
    pout() << "driver::grid_report" << endl;
  }

  pout() << endl;

  const int finest_level                 = m_amr->get_finest_level();
  const Vector<DisjointBoxLayout>& grids = m_amr->get_grids();
  const Vector<ProblemDomain>& domains   = m_amr->get_domains();
  const Vector<Real> dx                  = m_amr->get_dx();

  // Grid stuff goes into here
  long long totPoints;
  long long totPointsGhosts;
  long long totBoxes;
  long long myPoints;
  long long myPointsGhosts;
  long long myBoxes;
  Vector<long long> my_level_boxes;
  Vector<long long> total_level_boxes;
  Vector<long long> my_level_points;
  Vector<long long> total_level_points;

  //
  const long long uniformPoints = (domains[finest_level].domainBox()).numPts();

  // Track memory
#ifdef CH_USE_MEMORY_TRACKING
  int BytesPerMB = 1024*1024;
  long long curMem;
  long long peakMem;
  overallMemoryUsage(curMem, peakMem);

#ifdef CH_MPI
  int unfreed_mem = curMem;
  int peak_mem    = peakMem;

  int max_unfreed_mem;
  int max_peak_mem;

  int result1 = MPI_Allreduce(&unfreed_mem, &max_unfreed_mem, 1, MPI_INT, MPI_MAX, Chombo_MPI::comm);
  int result2 = MPI_Allreduce(&peak_mem,    &max_peak_mem,    1, MPI_INT, MPI_MAX, Chombo_MPI::comm);
#endif

  //  ReportUnfreedMemory(pout());
#endif

  
  // Get loads and boxes
  this->get_loads_and_boxes(myPoints,
			    myPointsGhosts,
			    myBoxes,
			    totPoints,
			    totPointsGhosts,
			    totBoxes,
			    my_level_boxes,
			    total_level_boxes,
			    my_level_points,
			    total_level_points,
			    finest_level,
			    grids);

  const ProblemDomain coarsest_domain = m_amr->get_domains()[0];
  const ProblemDomain finest_domain   = m_amr->get_domains()[finest_level];
  const Box finestBox   = finest_domain.domainBox();
  const Box coarsestBox = coarsest_domain.domainBox();
  Vector<int> refRat = m_amr->get_ref_rat();
  Vector<int> ref_rat(1 + finest_level);
  for (int lvl = 0; lvl < finest_level; lvl++){
    ref_rat[lvl] = refRat[lvl];
  }

  // Write a report
  
  pout() << "-----------------------------------------------------------------------" << endl
	 << "driver::Grid report - timestep = " << m_step << endl
	 << "\t\t\t        Finest level           = " << finest_level << endl
	 << "\t\t\t        Finest domain          = " << finestBox.size()[0] << " x " << finestBox.size()[1] <<
#if CH_SPACEDIM==2
    endl
#elif CH_SPACEDIM==3
    " x " << finestBox.size()[2] << endl
#endif
    	 << "\t\t\t        Coarsest domain        = " << coarsestBox.size()[0] << " x " << coarsestBox.size()[1] <<
#if CH_SPACEDIM==2
    endl
#elif CH_SPACEDIM==3
    " x " << coarsestBox.size()[2] << endl
#endif
	 << "\t\t\t        Refinement ratios      = " << ref_rat << endl
    	 << "\t\t\t        Grid sparsity          = " << 1.0*totPoints/uniformPoints << endl
	 << "\t\t\t        Finest dx              = " << dx[finest_level] << endl
    	 << "\t\t\t        Total number boxes     = " << totBoxes << endl
    	 << "\t\t\t        Total number of cells  = " << totPoints << " (" << totPointsGhosts << ")" << endl
	 << "\t\t\t        Proc. # of cells       = " << myPoints << " (" << myPointsGhosts << ")" << endl
	 << "\t\t\t        Proc. # of boxes       = " << myBoxes << endl
	 << "\t\t\t        Total # of boxes (lvl) = " << total_level_boxes << endl
    	 << "\t\t\t        Proc. # of boxes (lvl) = " << my_level_boxes << endl
    	 << "\t\t\t        Total # of cells (lvl) = " << total_level_points << endl
	 << "\t\t\t        Proc. # of cells (lvl) = " << my_level_points << endl
#ifdef CH_USE_MEMORY_TRACKING
	 << "\t\t\t        Unfreed memory        = " << curMem/BytesPerMB << " (MB)" << endl
    	 << "\t\t\t        Peak memory usage     = " << peakMem/BytesPerMB << " (MB)" << endl
#ifdef CH_MPI
    	 << "\t\t\t        Max unfreed memory    = " << max_unfreed_mem/BytesPerMB << " (MB)" << endl
	 << "\t\t\t        Max peak memory       = " << max_peak_mem/BytesPerMB << " (MB)" << endl
#endif
    	 << "-----------------------------------------------------------------------" << endl
#endif
	 << endl;

  pout() << endl;
}

void driver::memory_report(const memory_report_mode::which_mode a_mode){
#ifdef CH_USE_MEMORY_TRACKING
  CH_TIME("driver::grid_report");
  if(m_verbosity > 5){
    pout() << "driver::grid_report" << endl;
  }

  if(a_mode == memory_report_mode::overall){
    overallMemoryUsage();
  }
  else if(a_mode == memory_report_mode::unfreed){
    ReportUnfreedMemory(pout());
  }
  else if(a_mode == memory_report_mode::allocated){
    ReportAllocatedMemory(pout());
  }
  pout() << endl;
#endif
}

void driver::read_checkpoint_file(const std::string& a_restart_file){
  CH_TIME("driver::read_checkpoint_file");
  if(m_verbosity > 3){
    pout() << "driver::read_checkpoint_file" << endl;
  }


  // Read the header that was written by new_read_checkpoint_file
  HDF5Handle handle_in(a_restart_file, HDF5Handle::OPEN_RDONLY);
  HDF5HeaderData header;
  header.readFromFile(handle_in);

  m_time        = header.m_real["time"];
  m_dt          = header.m_real["dt"];
  m_step        = header.m_int["step"];
  
  const Real coarsest_dx = header.m_real["coarsest_dx"];
  const int finest_level = header.m_int["finest_level"];

  // Abort if base resolution has changed. 
  if(!coarsest_dx == m_amr->get_dx()[0]){
    MayDay::Abort("driver::read_checkpoint_file - coarsest_dx != dx[0], did you change the base level resolution?!?");
  }

  // Read in grids. If the file has no grids we must abort. 
  Vector<Vector<Box> > boxes(1 + finest_level);
  for (int lvl = 0; lvl <= finest_level; lvl++){
    handle_in.setGroupToLevel(lvl);
    
    const int status = read(handle_in, boxes[lvl]);
    
    if(status != 0) {
      MayDay::Error("driver::read_checkpoint_file - file has no grids");
    }
  }

  // Define amr_mesh
  const int regsize = m_timestepper->get_redistribution_regsize();
  m_amr->set_finest_level(finest_level); 
  m_amr->set_grids(boxes, regsize);      
  
  // Instantiate solvers
  m_timestepper->setup_solvers();  

  // Allocate internal stuff (e.g. space for tags)
  this->allocate_internals();

  // Go through level by level and have solvers extract their data
  for (int lvl = 0; lvl <= m_amr->get_finest_level(); lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    handle_in.setGroupToLevel(lvl);

    // time stepper reads in data
    m_timestepper->read_checkpoint_data(handle_in, lvl);

    // Read in internal data
    read_checkpoint_level(handle_in, lvl);
  }

  // Close input file
  handle_in.close();
}

void driver::regrid(const int a_lmin, const int a_lmax, const bool a_use_initial_data){
  CH_TIME("driver::regrid");
  if(m_verbosity > 2){
    pout() << "driver::regrid" << endl;
  }

  // We need to be careful with memory allocations here. Therefore we do:
  // --------------------------------------------------------------------
  // 1.  Tag cells, this calls cell_tagger which allocates and deallocate its own storage so 
  //     there's a peak in memory consumption here. We have to eat this one because we
  //     potentially need all the solver data for tagging, so that data can't be touched.
  //     If we don't get new tags, we exit this routine already here. 
  // 2.  Deallocate internal storage for the time_stepper - this frees up a bunch of memory that
  //     we don't need since we won't advance until after regridding anyways. 
  // 3.  Cache tags, this doubles up on the memory for m_tags but that shouldn't matter.
  // 4.  Free up m_tags for safety because it will be regridded anyways. 
  // 5.  Cache solver states
  // 6.  Deallocate internal storage for solver. This should free up a bunch of memory. 
  // 7.  Regrid amr_mesh - this shouldn't cause any extra memory issues
  // 8.  Regrid driver - this
  // 9.  Regrid the cell tagger. I'm not explicitly releasing storage from here since it's so small.
  // 10. Solve elliptic equations and fill solvers


  Vector<IntVectSet> tags;

  const Real start_time = MPI_Wtime();   // Timer

  const bool got_new_tags = this->tag_cells(tags, m_tags); // Tag cells

  if(!got_new_tags){
    if(a_use_initial_data){
      m_timestepper->initial_data();
    }

    if(m_verbosity > 1){
      pout() << "\ndriver::regrid - Didn't find any new cell tags. Skipping the regrid step\n" << endl;
    }
    return;
  }
  else{ // Compact tags
    for (int i = 0; i < tags.size(); i++){
      tags[i].compact();
    }
  }


  // Store things that need to be regridded
  this->cache_tags(m_tags);              // Cache m_tags because after regrid, ownership will change
  m_timestepper->cache();

  // Deallocate unnecessary storage
  this->deallocate_internals();          // Deallocate internal storage for driver
  m_timestepper->deallocate();           // Deallocate storage for time_stepper
  
  const Real cell_tags = MPI_Wtime();    // Timer

  // Regrid AMR. Only levels [lmin, lmax] are allowed to change. 
  const int old_finest_level = m_amr->get_finest_level();
  const int regsize = m_timestepper->get_redistribution_regsize();
  m_amr->regrid(tags, a_lmin, a_lmax, regsize, old_finest_level + 1);
  const int new_finest_level = m_amr->get_finest_level();
  const Real base_regrid = MPI_Wtime(); // Base regrid time

  
  // Regrid driver, timestepper, and celltagger
  this->regrid_internals(old_finest_level, new_finest_level);          // Regrid internals for driver
  m_timestepper->regrid(a_lmin, old_finest_level, new_finest_level);   // Regrid solvers
  if(a_use_initial_data){
    m_timestepper->initial_data();
  }
  m_celltagger->regrid();                                              // Regrid cell tagger

  const Real solver_regrid = MPI_Wtime(); // Timer

  if(m_verbosity > 1){
    this->regrid_report(solver_regrid - start_time,
			cell_tags - start_time,
			base_regrid - cell_tags,
			solver_regrid - base_regrid);
  }
}

void driver::regrid_internals(const int a_old_finest_level, const int a_new_finest_level){
  CH_TIME("driver::regrid_internals");
  if(m_verbosity > 2){
    pout() << "driver::regrid_internals" << endl;
  }

  this->allocate_internals();

  // Copy cached tags back over to m_tags
  for (int lvl = 0; lvl <= Min(a_old_finest_level, a_new_finest_level); lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];

    // Copy mask
    LevelData<BaseFab<bool> > tmp;
    tmp.define(dbl, 1, IntVect::Zero);
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      tmp[dit()].setVal(false);
    }
    m_cached_tags[lvl]->copyTo(tmp);
    
    for(DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const BaseFab<bool>& tmpFab = tmp[dit()];
      const Box& box = dbl.get(dit());

      DenseIntVectSet& tags = (*m_tags[lvl])[dit()];
      
      for (BoxIterator bit(box); bit.ok(); ++bit){
	const IntVect iv = bit();
	if(tmpFab(iv,0)){
	  tags |= iv;
	}
      }
    }
  }
}

void driver::regrid_report(const Real a_total_time,
			   const Real a_tag_time,
			   const Real a_base_regrid_time,
			   const Real a_solver_regrid_time){
  CH_TIME("driver::regrid_report");
  if(m_verbosity > 5){
    pout() << "driver::regrid_report" << endl;
  }

  const Real elapsed    = a_total_time;
  const int elapsed_hrs = floor(elapsed/3600);
  const int elapsed_min = floor((elapsed - 3600*elapsed_hrs)/60);
  const int elapsed_sec = floor( elapsed - 3600*elapsed_hrs - 60*elapsed_min);
  const int elapsed_ms  = floor((elapsed - 3600*elapsed_hrs - 60*elapsed_min - elapsed_sec)*1000.);

  char metrics[30];
  sprintf(metrics, "%3.3ih %2.2im %2.2is %3.3ims",
	  elapsed_hrs, 
	  elapsed_min, 
	  elapsed_sec, 
	  elapsed_ms);

  pout() << "-----------------------------------------------------------------------" << endl
	 << "driver::regrid_report breakdown - Time step #" << m_step << endl
	 << "\t\t\t" << "Total regrid time : " << metrics << endl
	 << "\t\t\t" << "Cell tagging      : " << 100.*(a_tag_time/a_total_time) << "%" << endl
    	 << "\t\t\t" << "Base regrid       : " << 100.*(a_base_regrid_time/a_total_time) << "%" << endl
	 << "\t\t\t" << "Solver regrid     : " << 100.*(a_solver_regrid_time/a_total_time) << "%" << endl
	 << "-----------------------------------------------------------------------" << endl;
}

void driver::run(const Real a_start_time, const Real a_end_time, const int a_max_steps){
  CH_TIME("driver::run");
  if(m_verbosity > 1){
    pout() << "driver::run" << endl;
  }

  if(m_verbosity > 0){
    pout() << "=================================" << endl;
    if(!m_restart){
      pout() << "driver::run -- starting run" << endl;
    }
    else{
      pout() << "driver::run -- restarting run" << endl;
    }
  }

  if(a_max_steps > 0){
    if(!m_restart){
      m_time = a_start_time;
      m_step = 0;
    }

    m_timestepper->compute_dt(m_dt, m_timecode);
    m_timestepper->synchronize_solver_times(m_step, m_time, m_dt);

    bool last_step     = false;
    bool first_step    = true;
    const Real init_dt = m_dt;

    if(m_verbosity > 0){
      this->grid_report();
    }

    m_wallclock_start = MPI_Wtime();

    // This is actually a debugging
    ofstream mass_dump_file;
    ofstream charge_dump_file;
    // if(m_dump_mass){
    //   this->open_mass_dump_file(mass_dump_file);
    // }
    // if(m_dump_charge){
    //   this->open_charge_dump_file(charge_dump_file);
    // }

    while(m_time < a_end_time && m_step < a_max_steps && !last_step){
      const int max_sim_depth = m_amr->get_max_sim_depth();
      const int max_amr_depth = m_amr->get_max_amr_depth();

      // This is the regrid test. We do some dummy tests first and then do the recursive/non-recursive stuff
      // inside the loop. 
      const bool can_regrid        = max_sim_depth > 0 && max_amr_depth > 0;
      const bool check_step        = m_step%m_regrid_interval == 0 && m_regrid_interval > 0;
      const bool check_timestepper = m_timestepper->need_to_regrid() && m_regrid_interval > 0;
      if(can_regrid && (check_step || check_timestepper)){
	if(!first_step){

	  // We'll regrid levels lmin through lmax. As always, new grids on level l are generated through tags
	  // on levels (l-1);
	  int lmin, lmax;
	  if(!m_recursive_regrid){
	    lmin = 1; // level = 0 never changes
	    lmax = m_amr->get_finest_level();
	  }
	  else{
	    int iref = 1;//m_regrid_interval;
	    lmax = m_amr->get_finest_level();
	    lmin = 1;
	    for (int lvl = m_amr->get_finest_level(); lvl > 0; lvl--){
	      if(m_step%(iref*m_regrid_interval) == 0){
		lmin = lvl;
	      }
	      iref *= m_amr->get_ref_rat()[lvl-1];
	    }
	  }
#if 0 // Debug test
	  const Real t0 = MPI_Wtime();
#endif

	  // Regrid, the two options tells us to generate tags on [(lmin-1),(lmax-1)];
	  this->regrid(lmin, lmax, false);
	  if(m_verbosity > 0){
	    this->grid_report();
	  }
#if 0 // Debug test
	  const Real t1 = MPI_Wtime();
	  if(procID() == 0){
	    std::cout << "step = " << m_step << "\t tagging levels = [" << lmin << "," << lmax << "]"
		      << "\t time = " << t1-t0 <<std::endl;
	  }
#endif
	}
      }

      // if(m_dump_mass){
      // 	this->dump_mass(mass_dump_file);
      // }
      // if(m_dump_charge){
      // 	this->dump_charge(charge_dump_file);
      // }


      if(!first_step){
	m_timestepper->compute_dt(m_dt, m_timecode);
      }

      if(first_step){
	first_step = false;
      }

      // Did the time step become too small?
      if(m_dt < 1.0E-5*init_dt){
	m_step++;

	if(m_write_memory){
	  this->write_memory_usage();
	}
#ifdef CH_USE_HDF5
	this->write_plot_file();
	this->write_checkpoint_file();
#endif

	MayDay::Abort("driver::run - the time step became too small");
      }

      // Last time step can be smaller than m_dt so that we end on a_end_time
      if(m_time + m_dt > a_end_time){
	m_dt = a_end_time - m_time;
	last_step = true;
      }

      // Time stepper advances solutions
      m_wallclock1 = MPI_Wtime();
      const Real actual_dt = m_timestepper->advance(m_dt);
      m_wallclock2 = MPI_Wtime();

      // Synchronize times
      m_dt    = actual_dt;
      m_time += actual_dt;
      m_step += 1;
      m_timestepper->synchronize_solver_times(m_step, m_time, m_dt);

      if(Abs(m_time - a_end_time) < m_dt*1.E-5){
	last_step = true;
      }

      // Print step report
      if(m_verbosity > 0){
	this->step_report(a_start_time, a_end_time, a_max_steps);
      }

#if 0 // Development feature
      // Positive current = current OUT OF DOMAIN
      const Real electrode_I  = m_timestepper->compute_electrode_current();
      const Real dielectric_I = m_timestepper->compute_dielectric_current();
      const Real ohmic_I      = m_timestepper->compute_ohmic_induction_current();
      const Real domain_I     = m_timestepper->compute_domain_current();
      if(procID() == 0){
	std::cout << m_time << "\t" << electrode_I << "\t" << domain_I << "\t" << ohmic_I << std::endl;
      }
#endif


#ifdef CH_USE_HDF5
      if(m_step%m_plot_interval == 0 && m_plot_interval > 0 || last_step == true && m_plot_interval > 0){
	if(m_verbosity > 2){
	  pout() << "driver::run -- Writing plot file" << endl;
	}
	if(m_write_memory){
	  this->write_memory_usage();
	}
	this->write_plot_file();
      }

      // Write checkpoint file
      if(m_step % m_chk_interval == 0 && m_chk_interval > 0 || last_step == true && m_chk_interval > 0){
	if(m_verbosity > 2){
	  pout() << "driver::run -- Writing checkpoint file" << endl;
	}
	this->write_checkpoint_file();
      }
#endif
    }

    // if(m_dump_mass){
    //   this->close_mass_dump_file(mass_dump_file);
    // }
    // if(m_dump_charge){
    //   this->close_charge_dump_file(charge_dump_file);
    // }
  }

  m_timestepper->deallocate();

  if(m_verbosity > 0){
    this->grid_report();
  }

  if(m_verbosity > 0){
    pout() << "==================================" << endl;
    pout() << "driver::run -- ending run  " << endl;
    pout() << "==================================" << endl;
  }
}

void driver::setup_and_run(){
  CH_TIME("driver::setup_and_run");
  if(m_verbosity > 0){
    pout() << "driver::setup_and_run" << endl;
  }

  char iter_str[100];
  sprintf(iter_str, ".check%07d.%dd.hdf5", m_restart_step, SpaceDim);
  const std::string restart_file = m_output_dir + "/chk/" + m_output_names + std::string(iter_str);

  this->setup(m_init_regrids, m_restart, restart_file);

  if(!m_geometry_only){
    this->run(m_start_time, m_stop_time, m_max_steps);
  }
}

void driver::set_computational_geometry(const RefCountedPtr<computational_geometry>& a_compgeom){
  CH_TIME("driver::set_computational_geometry");
  if(m_verbosity > 5){
    pout() << "driver::set_computational_geometry" << endl;
  }
  m_compgeom = a_compgeom;
  m_mfis     = a_compgeom->get_mfis();
}

void driver::set_time_stepper(const RefCountedPtr<time_stepper>& a_timestepper){
  CH_TIME("driver::set_time_stepper");
  if(m_verbosity > 5){
    pout() << "driver::set_time_stepper" << endl;
  }
  m_timestepper = a_timestepper;
}

void driver::set_cell_tagger(const RefCountedPtr<cell_tagger>& a_celltagger){
  CH_TIME("driver::set_cell_tagger");
  if(m_verbosity > 5){
    pout() << "driver::set_cell_tagger" << endl;
  }

  m_celltagger = a_celltagger;
  if(!a_celltagger.isNull()){
    m_celltagger->parse_options();
  }
}

void driver::set_geo_coarsen(const RefCountedPtr<geo_coarsener>& a_geocoarsen){
  CH_TIME("driver::set_geo_coarsen");
  if(m_verbosity > 5){
    pout() << "driver::set_geo_coarsen" << endl;
  }
  m_geocoarsen = a_geocoarsen;
}

void driver::set_geom_refinement_depth(const int a_depth1,
				       const int a_depth2,
				       const int a_depth3,
				       const int a_depth4,
				       const int a_depth5,
				       const int a_depth6){
  CH_TIME("driver::set_geom_refinement_depth(full");
  if(m_verbosity > 5){
    pout() << "driver::set_geom_refinement_depth(full)" << endl;
  }
  
  const int max_depth = m_amr->get_max_amr_depth();

  m_conductor_tag_depth                = Min(a_depth1, max_depth);
  m_dielectric_tag_depth               = Min(a_depth2, max_depth);
  m_gas_conductor_interface_tag_depth  = Min(a_depth3, max_depth);
  m_gas_dielectric_interface_tag_depth = Min(a_depth4, max_depth);
  m_gas_solid_interface_tag_depth      = Min(a_depth5, max_depth);
  m_solid_solid_interface_tag_depth    = Min(a_depth6, max_depth);


  m_geom_tag_depth = 0;
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth1);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth2);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth3);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth4);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth5);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth6);
}

void driver::parse_geometry_generation(){
  CH_TIME("driver::parse_geometry_generation");
  if(m_verbosity > 5){
    pout() << "driver::parse_geometry_generation" << endl;
  }

  ParmParse pp("driver");
  pp.get("geometry_generation", m_geometry_generation);
  pp.get("geometry_scan_level", m_geo_scan_level);
  

  if(m_geometry_generation == "plasmac"){
    computational_geometry::s_use_new_gshop = true;
    computational_geometry::s_ScanDomain = m_amr->get_domains()[m_geo_scan_level];
  }
  else if(m_geometry_generation == "chombo"){
  }
  else{
    MayDay::Abort("driver:parse_geometry_generation - unsupported argument requested");
  }
}

void driver::parse_verbosity(){
  CH_TIME("driver::parse_verbosity");

  ParmParse pp("driver");
  pp.get("verbosity", m_verbosity);
}

void driver::parse_regrid(){
  CH_TIME("driver::parse_regrid");
  if(m_verbosity > 5){
    pout() << "driver::parse_regrid" << endl;
  }

  ParmParse pp("driver");

  std::string str;
  pp.get("regrid_interval", m_regrid_interval);
  pp.get("initial_regrids", m_init_regrids);
  pp.get("recursive_regrid", str); m_recursive_regrid = (str == "true") ? true : false;
}

void driver::parse_restart(){
  CH_TIME("driver::parse_restart");
  if(m_verbosity > 5){
    pout() << "driver::parse_restart" << endl;
  }

  ParmParse pp("driver");

  // Get restart step
  pp.get("restart", m_restart_step); // Get restart step
  m_restart = (m_restart_step > 0) ? true : false;
}

void driver::parse_memrep(){
  CH_TIME("driver::parse_memrep");
  if(m_verbosity > 5){
    pout() << "driver::parse_memrep" << endl;
  }

  std::string str;
  ParmParse pp("driver");
  pp.query("memory_report_mode", str);
  if(str == "overall"){
    m_memory_mode = memory_report_mode::overall;
  }
  else if(str == "unfreed"){
    m_memory_mode = memory_report_mode::unfreed;
  }
  else if(str == "allocated"){
    m_memory_mode = memory_report_mode::allocated;
  }
  pp.get("write_memory", m_write_memory);
}

void driver::parse_output_directory(){
  CH_TIME("driver::parse_output_directory");
  if(m_verbosity > 5){
    pout() << "driver::parse_output_directory" << endl;
  }

  ParmParse pp("driver");
  pp.get("output_directory", m_output_dir);

  // If directory does not exist, create it
  int success = 0;
  if(procID() == 0){
    std::string cmd;

    cmd = "mkdir -p " + m_output_dir;
    success = system(cmd.c_str());
    if(success != 0){
      std::cout << "driver::set_output_directory - master could not create directory" << std::endl;
    }

    cmd = "mkdir -p " + m_output_dir + "/plt";
    success = system(cmd.c_str());
    if(success != 0){
      std::cout << "driver::set_output_directory - master could not create plot directory" << std::endl;
    }

    cmd = "mkdir -p " + m_output_dir + "/geo";
    success = system(cmd.c_str());
    if(success != 0){
      std::cout << "driver::set_output_directory - master could not create geo directory" << std::endl;
    }

    cmd = "mkdir -p " + m_output_dir + "/chk";
    success = system(cmd.c_str());
    if(success != 0){
      std::cout << "driver::set_output_directory - master could not create checkpoint directory" << std::endl;
    }

    cmd = "mkdir -p " + m_output_dir + "/mpi";
    success = system(cmd.c_str());
    if(success != 0){
      std::cout << "driver::set_output_directory - master could not create proc directory" << std::endl;
    }    
  }
  
  MPI_Barrier(Chombo_MPI::comm);
  if(success != 0){
    MayDay::Abort("driver::set_output_directory - could not create directories for output");
  }
}

void driver::parse_output_file_names(){
  CH_TIME("driver::set_output_names");
  if(m_verbosity > 5){
    pout() << "driver::set_output_names" << endl;
  }

  ParmParse pp("driver");
  pp.get("output_names", m_output_names);
}

void driver::parse_output_intervals(){
  CH_TIME("driver::set_plot_interval");
  if(m_verbosity > 5){
    pout() << "driver::set_plot_interval" << endl;
  }

  ParmParse pp("driver");
  pp.get("plot_interval", m_plot_interval);
  pp.get("checkpoint_interval", m_chk_interval);
}

void driver::parse_geo_refinement(){
  CH_TIME("driver::set_geom_refinement_depth");
  if(m_verbosity > 5){
    pout() << "driver::set_geom_refinement_depth" << endl;
  }

  const int max_depth = m_amr->get_max_amr_depth();
  
  int depth     = max_depth;
  int depth1    = depth;
  int depth2    = depth;
  int depth3    = depth;
  int depth4    = depth;
  int depth5    = depth;
  int depth6    = depth;

  { // Get parameter from input script
    ParmParse pp("driver");
    pp.get("refine_geometry", depth);
    depth  = (depth < 0) ? max_depth : depth;
    depth1 = depth;
    depth2 = depth;
    depth3 = depth;
    depth4 = depth;
    depth5 = depth;
    depth6 = depth;
  }

  { // Get fine controls from input script
    ParmParse pp("driver");
    pp.get("refine_electrodes",               depth1);
    pp.get("refine_dielectrics",              depth2);
    pp.get("refine_electrode_gas_interface",  depth3);
    pp.get("refine_dielectric_gas_interface", depth4);
    pp.get("refine_solid_gas_interface",      depth5);
    pp.get("refine_solid_solid_interface",    depth6);

    depth1 = (depth1 < 0) ? depth : depth1;
    depth2 = (depth2 < 0) ? depth : depth2;
    depth3 = (depth3 < 0) ? depth : depth3;
    depth4 = (depth4 < 0) ? depth : depth4;
    depth5 = (depth5 < 0) ? depth : depth5;
    depth6 = (depth6 < 0) ? depth : depth6;
  }
  
  set_geom_refinement_depth(depth1, depth2, depth3, depth4, depth5, depth6);
}

void driver::parse_num_plot_ghost(){
  CH_TIME("driver::parse_num_plot_ghost");
  if(m_verbosity > 5){
    pout() << "driver::parse_num_plot_ghost" << endl;
  }

  ParmParse pp("driver");
  pp.get("num_plot_ghost", m_num_plot_ghost);

  m_num_plot_ghost = (m_num_plot_ghost < 0) ? 0 : m_num_plot_ghost;
  m_num_plot_ghost = (m_num_plot_ghost > 3) ? 3 : m_num_plot_ghost;
}

void driver::parse_coarsen(){
  CH_TIME("driver::parse_coarsen");
  if(m_verbosity > 5){
    pout() << "driver::parse_coarsen" << endl;
  }

  std::string str;
  ParmParse pp("driver");
  pp.get("allow_coarsening", str);
  if(str == "true"){
    m_allow_coarsen = true;
  }
  else if(str == "false"){
    m_allow_coarsen = false;
  }
}

void driver::parse_grow_tags(){
  CH_TIME("driver::parse_grow_tags");
  if(m_verbosity > 5){
    pout() << "driver::parse_grow_tags" << endl;
  }

  ParmParse pp("driver");
  pp.get("grow_tags", m_grow_tags);

  m_grow_tags = Max(0, m_grow_tags);
}

void driver::parse_geom_only(){
  CH_TIME("driver::parse_geom_only");
  if(m_verbosity > 5){
    pout() << "driver::parse_geom_only" << endl;
  }

  std::string str;
  ParmParse pp("driver");
  pp.get("geometry_only", str);
  if(str == "true"){
    m_geometry_only = true;
  }
  else if(str == "false"){
    m_geometry_only = false;
  }
}

void driver::parse_ebis_memory_load_balance(){
  CH_TIME("driver::parse_ebis_memory_load_balance");
  if(m_verbosity > 5){
    pout() << "driver::parse_ebis_memory_load_balance" << endl;
  }

  std::string str;
  ParmParse pp("driver");
  pp.get("ebis_memory_load_balance", str);
  if(str == "true"){
    m_ebis_memory_load_balance = true;
  }
  else if(str == "false"){
    m_ebis_memory_load_balance = false;
  }
}

void driver::parse_write_ebis(){
  CH_TIME("driver::parse_write_ebis");
  if(m_verbosity > 5){
    pout() << "driver::parse_write_ebis" << endl;
  }

  m_ebis_gas_file = m_output_names + ".ebis.gas.hdf5";
  m_ebis_sol_file = m_output_names + ".ebis.sol.hdf5";

  std::string str;
  ParmParse pp("driver");
  pp.get("write_ebis", str);
  if(str == "true"){
    m_write_ebis = true;
  }
  else if(str == "false"){
    m_write_ebis = false;
  }
}

void driver::parse_read_ebis(){
  CH_TIME("driver::parse_read_ebis");
  if(m_verbosity > 5){
    pout() << "driver::parse_read_ebis" << endl;
  }

  std::string str;
  ParmParse pp("driver");
  pp.get("read_ebis", str);
  if(str == "true"){
    m_read_ebis = true;
  }
  else if(str == "false"){
    m_read_ebis = false;
  }
}

void driver::parse_simulation_time(){
  CH_TIME("driver::parse_simulation_time");
  if(m_verbosity > 5){
    pout() << "driver::parse_simulation_time" << endl;
  }

  ParmParse pp("driver");
  pp.get("max_steps", m_max_steps);
  pp.get("start_time", m_start_time);
  pp.get("stop_time", m_stop_time);
}

void driver::parse_file_depth(){
  CH_TIME("driver::parse_file_depth");
  if(m_verbosity > 5){
    pout() << "driver::parse_file_depth" << endl;
  }

  ParmParse pp("driver");
  pp.get("max_plot_depth", m_max_plot_depth);
  pp.get("max_chk_depth", m_max_chk_depth);
}

void driver::parse_plot_vars(){
  ParmParse pp("driver");
  const int num = pp.countval("plt_vars");
  Vector<std::string> str(num);
  pp.getarr("plt_vars", str, 0, num);

  m_plot_tags   = false;
  m_plot_ranks  = false;
  
  for (int i = 0; i < num; i++){
    if(     str[i] == "tags")     m_plot_tags   = true;
    else if(str[i] == "mpi_rank") m_plot_ranks  = true;
  }
}



void driver::set_amr(const RefCountedPtr<amr_mesh>& a_amr){
  CH_TIME("driver::set_amr");
  if(m_verbosity > 5){
    pout() << "driver::set_amr" << endl;
  }

  m_amr = a_amr;
  m_amr->set_mfis(m_compgeom->get_mfis());
}

void driver::setup(const int a_init_regrids, const bool a_restart, const std::string a_restart_file){
  CH_TIME("driver::setup");
  if(m_verbosity > 5){
    pout() << "driver::setup" << endl;
  }

  if(m_geometry_only){
    this->setup_geometry_only();
  }
  else{
    if(!a_restart){
      this->setup_fresh(a_init_regrids);
#ifdef CH_USE_HDF5
      if(m_plot_interval > 0){
	if(m_write_memory){
	  this->write_memory_usage();
	}
	this->write_plot_file();
      }
#endif
    }
    else{
      this->setup_for_restart(a_init_regrids, a_restart_file);
    }
  }
}

void driver::setup_geometry_only(){
  CH_TIME("driver::setup_geometry_only");
  if(m_verbosity > 5){
    pout() << "driver::setup_geometry_only" << endl;
  }

  this->sanity_check();

  if(m_ebis_memory_load_balance){
    EBIndexSpace::s_useMemoryLoadBalance = true;
  }
  else {
    EBIndexSpace::s_useMemoryLoadBalance = false;
  }

  m_compgeom->build_geometries(m_amr->get_finest_domain(),
			       m_amr->get_prob_lo(),
			       m_amr->get_finest_dx(),
			       m_amr->get_max_ebis_box_size());
  if(m_write_ebis){
    this->write_ebis();
  }
  if(m_write_memory){
    this->write_memory_usage();
  }

  this->get_geom_tags();       // Get geometric tags.

  if(m_write_memory){
    this->write_memory_usage();
  }

  //  m_amr->set_num_ghost(m_timestepper->query_ghost()); // Query solvers for ghost cells. Give it to amr_mesh before grid gen.
  
  Vector<IntVectSet> tags = m_geom_tags;
  const int a_lmin = 0;
  const int a_lmax = m_geom_tag_depth;
  m_amr->build_grids(tags, a_lmin, a_lmax);//m_geom_tag_depth);
  m_amr->define_eblevelgrid(a_lmin);
  //  m_amr->regrid(m_geom_tags, m_geom_tag_depth);       // Regrid using geometric tags for now

  if(m_verbosity > 0){
    this->grid_report();
  }

  //  this->write_memory_usage();
  this->write_geometry();                             // Write geometry only
}

void driver::setup_fresh(const int a_init_regrids){
  CH_TIME("driver::setup_fresh");
  if(m_verbosity > 5){
    pout() << "driver::setup_fresh" << endl;
  }

  this->sanity_check();                                    // Sanity check before doing anything expensive

  if(m_ebis_memory_load_balance){
    EBIndexSpace::s_useMemoryLoadBalance = true;
  }
  else {
    EBIndexSpace::s_useMemoryLoadBalance = false;
  }

  if(!m_read_ebis){
    m_compgeom->build_geometries(m_amr->get_finest_domain(),
				 m_amr->get_prob_lo(),
				 m_amr->get_finest_dx(),
				 m_amr->get_max_ebis_box_size());
    if(m_write_ebis){
      this->write_ebis();        // Write EBIndexSpace's for later use
    }
  }
  else{
    const std::string path_gas = m_output_dir + "/geo/" + m_ebis_gas_file;
    const std::string path_sol = m_output_dir + "/geo/" + m_ebis_sol_file;

    m_compgeom->build_geo_from_files(path_gas, path_sol);
  }

  // Get geometry tags
  this->get_geom_tags();
  
  // Determine the redistribution register size
  const int regsize = m_timestepper->get_redistribution_regsize();

  // When we're setting up fresh, we need to regrid everything from the base level
  // and upwards. We have tags on m_geom_tag_depth, so that is our current finest level. 
  const int lmin = 0;
  const int lmax = m_geom_tag_depth;
  m_amr->regrid(m_geom_tags, lmin, lmax, regsize, m_geom_tag_depth);

  // Allocate internal storage 
  this->allocate_internals();

  // Do a grid report of the initial grid
  if(m_verbosity > 0){
    this->grid_report();
  }

  // Provide time_stepper with amr and geometry
  m_timestepper->set_amr(m_amr);
  m_timestepper->set_computational_geometry(m_compgeom);       // Set computational geometry

  // time_stepper setup
  m_timestepper->setup_solvers();                                 // Instantiate solvers
  m_timestepper->synchronize_solver_times(m_step, m_time, m_dt);  // Sync solver times
  m_timestepper->initial_data();                                  // Fill solvers with initial data

  // cell_tagger
  if(!m_celltagger.isNull()){
    m_celltagger->regrid();
  }

  // Initial regrids
  for (int i = 0; i < a_init_regrids; i++){
    if(m_verbosity > 5){
      pout() << "driver::initial_regrids" << endl;
    }

    const int lmin = 1;
    const int lmax = m_amr->get_finest_level();
    this->regrid(lmin, lmax, true);

    if(m_verbosity > 0){
      this->grid_report();
    }
  }
}

void driver::setup_for_restart(const int a_init_regrids, const std::string a_restart_file){
  CH_TIME("driver::setup_for_restart");
  if(m_verbosity > 5){
    pout() << "driver::setup_for_restart" << endl;
  }

  this->sanity_check();                                    // Sanity check before doing anything expensive

  if(!m_read_ebis){
    m_compgeom->build_geometries(m_amr->get_finest_domain(),
				 m_amr->get_prob_lo(),
				 m_amr->get_finest_dx(),
				 m_amr->get_max_ebis_box_size());
  }
  else{
    const std::string path_gas = m_output_dir + "/geo/" + m_ebis_gas_file;
    const std::string path_sol = m_output_dir + "/geo/" + m_ebis_sol_file;
    m_compgeom->build_geo_from_files(path_gas, path_sol);
  }

  this->get_geom_tags();       // Get geometric tags.

  m_timestepper->set_amr(m_amr);                         // Set amr
  m_timestepper->set_computational_geometry(m_compgeom); // Set computational geometry

  // Read checkpoint file
  this->read_checkpoint_file(a_restart_file); // Read checkpoint file - this sets up amr, instantiates solvers and fills them

  // Time stepper does post checkpoint setup
  m_timestepper->post_checkpoint_setup();
  
  // Prepare storage for cell_tagger
  if(!m_celltagger.isNull()){
    m_celltagger->regrid();         
  }

  // Initial regrids
  for (int i = 0; i < a_init_regrids; i++){
    if(m_verbosity > 0){
      pout() << "driver -- initial regrid # " << i + 1 << endl;
    }

    const int lmin = 1;
    const int lmax = m_amr->get_finest_level();
    this->regrid(lmin, lmax, false);

    if(m_verbosity > 0){
      this->grid_report();
    }
  }
}

// void driver::set_dump_mass(const bool a_dump_mass){
//   CH_TIME("driver::set_dump_mass");
//   if(m_verbosity > 5){
//     pout() << "driver::set_dump_mass" << endl;
//   }

//   m_dump_mass = a_dump_mass;

//   { // get parameter from input script
//     std::string str;
//     ParmParse pp("driver");
//     pp.query("dump_mass", str);
//     if(str == "true"){
//       m_dump_mass = true;
//     }
//     else if(str == "false"){
//       m_dump_mass = false;
//     }
//   }
// }

// void driver::set_dump_charge(const bool a_dump_charge){
//   CH_TIME("driver::set_dump_charge");
//   if(m_verbosity > 5){
//     pout() << "driver::set_dump_charge" << endl;
//   }

//   m_dump_charge = a_dump_charge;

//   { // get parameter from input script
//     std::string str;
//     ParmParse pp("driver");
//     pp.query("dump_charge", str);
//     if(str == "true"){
//       m_dump_charge = true;
//     }
//     else if(str == "false"){
//       m_dump_charge = false;
//     }
//   }
// }

void driver::set_output_centroids(const bool a_output_centroids){
  CH_TIME("driver::set_output_centroids");
  if(m_verbosity > 5){
    pout() << "driver::set_output_centroids" << endl;
  }

  m_output_centroids = a_output_centroids;

  { // get parameter from input script
    std::string str;
    ParmParse pp("driver");
    pp.query("output_centroids", str);
    if(str == "true"){
      m_output_centroids = true;
    }
    else if(str == "false"){
      m_output_centroids = false;
    }
  }
}



void driver::sanity_check(){
  CH_TIME("driver::sanity_check");
  if(m_verbosity > 4){
    pout() << "driver::sanity_check" << endl;
  }

  CH_assert(!m_timestepper.isNull());
}

void driver::step_report(const Real a_start_time, const Real a_end_time, const int a_max_steps){
  CH_TIME("driver::step_report");
  if(m_verbosity > 5){
    pout() << "driver::step_report" << endl;
  }

  pout() << endl;
  pout() << "driver::Time step report -- Time step #" << m_step << endl
	 << "                                   Time  = " << m_time << endl
	 << "                                   dt    = " << m_dt << endl;

  // Get the total number of poitns across all levels
  const int finest_level                 = m_amr->get_finest_level();
  const Vector<DisjointBoxLayout>& grids = m_amr->get_grids();
  const Vector<ProblemDomain>& domains   = m_amr->get_domains();
  const Vector<Real>& dx                 = m_amr->get_dx();
  long long totalPoints = 0;
  long long uniformPoints = (domains[finest_level].domainBox()).numPts();
  
  for (int lvl = 0; lvl <= finest_level; lvl++){
    long long pointsThisLevel = 0;
    for (LayoutIterator lit = grids[lvl].layoutIterator(); lit.ok(); ++lit){
      pointsThisLevel += grids[lvl][lit()].numPts();
    }
    totalPoints += pointsThisLevel;
  }

  char metrics[300];

  // Percentage completed of time steps
  const Real percentStep = (1.0*m_step/a_max_steps)*100.;
  sprintf(metrics,"%31c -- %5.2f percentage of time steps completed",' ', percentStep);
  pout() << metrics << endl;

  const Real percentTime = ((m_time - a_start_time)/(a_end_time - a_start_time))*100.;
  sprintf(metrics,"%31c -- %5.2f percentage of simulation time completed",' ', percentTime);
  pout() << metrics << endl;


  // Hours, minutes, seconds and millisecond of the previous iteration
  const Real elapsed   = m_wallclock2 - m_wallclock_start;
  const int elapsedHrs = floor(elapsed/3600);
  const int elapsedMin = floor((elapsed - 3600*elapsedHrs)/60);
  const int elapsedSec = floor( elapsed - 3600*elapsedHrs - 60*elapsedMin);
  const int elapsedMs  = floor((elapsed - 3600*elapsedHrs - 60*elapsedMin - elapsedSec)*1000);

  // Write a string with total elapsed time
  sprintf(metrics, 
	  "%31c -- Elapsed time          : %3.3ih %2.2im %2.2is %3.3ims",
	  ' ',
	  elapsedHrs, 
	  elapsedMin, 
	  elapsedSec, 
	  elapsedMs);
  pout() << metrics << endl;

  // Hours, minutes, seconds and millisecond of the previous iteration
  const Real lastadv = m_wallclock2 - m_wallclock1;
  const int advHrs = floor(lastadv/3600);
  const int advMin = floor((lastadv - 3600*advHrs)/60);
  const int advSec = floor( lastadv - 3600*advHrs - 60*advMin);
  const int advMs  = floor((lastadv - 3600*advHrs - 60*advMin - advSec)*1000);

  // Write a string with the previous iteration metrics
  sprintf(metrics, 
	  "%31c -- Last time step        : %3.3ih %2.2im %2.2is %3.3ims",
	  ' ',
	  advHrs, 
	  advMin, 
	  advSec, 
	  advMs);
  pout() << metrics << endl;

  // Hours, minutes, seconds and millisecond of the previous iteration
  const Real wt_ns = (m_wallclock2 - m_wallclock1)*1.E-9/m_dt;
  const int wt_Hrs = floor(wt_ns/3600);
  const int wt_Min = floor((wt_ns - 3600*wt_Hrs)/60);
  const int wt_Sec = floor( wt_ns - 3600*wt_Hrs - 60*wt_Min);
  const int wt_Ms  = floor((wt_ns - 3600*wt_Hrs - 60*wt_Min - wt_Sec)*1000);
  sprintf(metrics, 
	  "%31c -- Wall time per ns      : %3.3ih %2.2im %2.2is %3.3ims",
	  ' ',
	  wt_Hrs, 
	  wt_Min, 
	  wt_Sec, 
	  wt_Ms);
  pout() << metrics << endl;


  // This is the time remaining
  const Real maxPercent = Max(percentTime, percentStep);
  const Real remaining  = 100.*elapsed/maxPercent - elapsed;
  const int remHrs = floor(remaining/3600);
  const int remMin = floor((remaining - 3600*remHrs)/60);
  const int remSec = floor( remaining - 3600*remHrs - 60*remMin);
  const int remMs  = floor((remaining - 3600*remHrs - 60*remMin - remSec)*1000);

  // Write a string with the previous iteration metrics
  sprintf(metrics, 
	  "%31c -- Estimated remaining   : %3.3ih %2.2im %2.2is %3.3ims",
	  ' ',
	  remHrs, 
	  remMin, 
	  remSec, 
	  remMs);
  pout() << metrics << endl;

  // Write memory usage
#ifdef CH_USE_MEMORY_TRACKING
  const int BytesPerMB = 1024*1024;
  long long curMem;
  long long peakMem;
  overallMemoryUsage(curMem, peakMem);

  pout() << "                                -- Unfreed memory        : " << curMem/BytesPerMB << "(MB)" << endl;
  pout() << "                                -- Peak memory usage     : " << peakMem/BytesPerMB << "(MB)" << endl;

#ifdef CH_MPI
  int unfreed_mem = curMem;
  int peak_mem    = peakMem;

  int max_unfreed_mem;
  int max_peak_mem;

  int result1 = MPI_Allreduce(&unfreed_mem, &max_unfreed_mem, 1, MPI_INT, MPI_MAX, Chombo_MPI::comm);
  int result2 = MPI_Allreduce(&peak_mem,    &max_peak_mem,    1, MPI_INT, MPI_MAX, Chombo_MPI::comm);
  pout() << "                                -- Max unfreed memory    : " << max_unfreed_mem/BytesPerMB << "(MB)" << endl;
  pout() << "                                -- Max peak memory usage : " << max_peak_mem/BytesPerMB << "(MB)" << endl;
#endif
#endif

  // Time stepper writes his report now
  m_timestepper->print_step_report();

}

int driver::get_finest_tag_level(const EBAMRTags& a_cell_tags) const{
  CH_TIME("driver::get_finest_tag_level");
  if(m_verbosity > 5){
    pout() << "driver::get_finest_tag_level" << endl;
  }

  int finest_tag_level = -1;
  for (int lvl = 0; lvl < a_cell_tags.size(); lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const DenseIntVectSet& tags = (*a_cell_tags[lvl])[dit()];

      if(!tags.isEmpty()){
	finest_tag_level = Max(finest_tag_level, lvl);
      }
    }
  }

#ifdef CH_MPI
  int finest;
  MPI_Allreduce(&finest_tag_level, &finest, 1, MPI_INT, MPI_MAX, Chombo_MPI::comm);

  finest_tag_level = finest;
#endif

  return finest_tag_level;
}

bool driver::tag_cells(Vector<IntVectSet>& a_all_tags, EBAMRTags& a_cell_tags){
  CH_TIME("driver::tag_cells");
  if(m_verbosity > 5){
    pout() << "driver::tag_cells" << endl;
  }

  bool got_new_tags = false;

  // Note that when we regrid we add at most one level at a time. This means that if we have a
  // simulation with AMR depth l and we want to add a level l+1, we need tags on levels 0 through l.
  const int finest_level  = m_amr->get_finest_level();
  a_all_tags.resize(1 + finest_level, IntVectSet());

  if(!m_celltagger.isNull()){
    got_new_tags = m_celltagger->tag_cells(a_cell_tags);
  }


  // Gather tags from a_tags
  for (int lvl = 0; lvl <= finest_level; lvl++){
    for (DataIterator dit = a_cell_tags[lvl]->dataIterator(); dit.ok(); ++dit){
      a_all_tags[lvl] |= IntVectSet((*a_cell_tags[lvl])[dit()]);// This should become a TreeIntVectSet
    }

    // Grow tags with cell taggers buffer
    if(!m_celltagger.isNull()){
      const int buf = m_celltagger->get_buffer();
      a_all_tags[lvl].grow(buf);
    }
  }

  // Add geometric tags.
  int tag_level = get_finest_tag_level(a_cell_tags);
  if(m_allow_coarsen){
    for (int lvl = 0; lvl <= finest_level; lvl++){
      if(lvl <= tag_level){
	a_all_tags[lvl] |= m_geom_tags[lvl];
      }
    }
  }
  else{
    // Loop only goes to the current finest level because we only add one level at a time
    for (int lvl = 0; lvl <= finest_level; lvl++){
      if(lvl < m_amr->get_max_amr_depth()){ // Geometric tags don't exist on amr_mesh.m_max_amr_depth
	a_all_tags[lvl] |= m_geom_tags[lvl];
      }
    }
  }

#if 0 // Debug - if this fails, you have tags on m_amr->m_max_amr_depth and something has gone wrong. 
  if(finest_level == m_amr->get_max_amr_depth()){
    for (int lvl = 0; lvl <= finest_level; lvl++){
      pout() << "level = " << lvl << "\t num_pts = " << a_all_tags[lvl].numPts() << endl;
    }
    CH_assert(a_all_tags[finest_level].isEmpty());
  }
#endif

  // Get the total number of tags
  Vector<int> num_local_tags(1+finest_level);
  for (int lvl = 0; lvl <= finest_level; lvl++){
    num_local_tags[lvl] = a_all_tags[lvl].numPts();
  }

  return got_new_tags;
}

void driver::write_memory_usage(){
  CH_TIME("driver::write_memory_usage");
  if(m_verbosity > 3){
    pout() << "driver::write_memory_usage" << endl;
  }

  char file_char[1000];
  const std::string prefix = m_output_dir + "/mpi/" + m_output_names;
  sprintf(file_char, "%s.memory.step%07d.%dd.dat", prefix.c_str(), m_step, SpaceDim);
  std::string fname(file_char);

  // Get memory stuff
  Vector<Real> peak, unfreed;
  memrep::get_memory(peak, unfreed);
  
  // Begin writing output
  if(procID() == 0){
    std::ofstream f;
    f.open(fname, std::ios_base::trunc);
    const int width = 12;

    // Write header
    f << std::left << std::setw(width) << "# MPI rank" << "\t"
      << std::left << std::setw(width) << "Peak memory" << "\t"
      << std::left << std::setw(width) << "Unfreed memory" << "\t"
      << endl;

    // Write memory 
    for (int i = 0; i < numProc(); i++){
      f << std::left << std::setw(width) << i << "\t"
	<< std::left << std::setw(width) << peak[i] << "\t"
	<< std::left << std::setw(width) << unfreed[i] << "\t"
	<< endl;
    }
  }
}

void driver::write_geometry(){
  CH_TIME("driver::write_geometry");
  if(m_verbosity > 3){
    pout() << "driver::write_geometry" << endl;
  }

  EBAMRCellData output;
  m_amr->allocate(output, phase::gas, 1);
  data_ops::set_value(output, 0.0);
  Vector<std::string> names(1, "dummy_data");

  const int finest_level                 = m_amr->get_finest_level();
  const Vector<DisjointBoxLayout>& grids = m_amr->get_grids();
  const Vector<ProblemDomain>& domains   = m_amr->get_domains();
  const Vector<Real>& dx                 = m_amr->get_dx();
  const Vector<int>& ref_rat             = m_amr->get_ref_rat();

  bool replace_covered = false;
  Vector<Real> covered_values;

  Vector<LevelData<EBCellFAB>*> output_ptr(1 + finest_level);
  m_amr->alias(output_ptr, output);

  // Dummy file name
  char file_char[1000];
  const std::string prefix = m_output_dir + "/geo/" + m_output_names;
  sprintf(file_char, "%s.geometry.%dd.hdf5", prefix.c_str(), SpaceDim);
  string fname(file_char);

  writeEBHDF5(fname, 
	      grids,
	      output_ptr,
	      names, 
	      domains[0],
	      dx[0], 
	      m_dt,
	      m_time,
	      ref_rat,
	      finest_level + 1,
	      replace_covered,
	      covered_values,
	      m_num_plot_ghost*IntVect::Unit);
}

void driver::write_plot_file(){
  CH_TIME("driver::write_plot_file");
  if(m_verbosity > 3){
    pout() << "driver::write_plot_file" << endl;
  }

  // Filename
  char file_char[1000];
  const std::string prefix = m_output_dir + "/plt/" + m_output_names;
  sprintf(file_char, "%s.step%07d.%dd.hdf5", prefix.c_str(), m_step, SpaceDim);
  string fname(file_char);

  // Output file
  EBAMRCellData output;

  // Names for output variables  
  Vector<std::string> names(0);

  // Get total number of components for output
  int ncomp = m_timestepper->get_num_plot_vars();
  if(!m_celltagger.isNull()) {
    ncomp += m_celltagger->get_num_plot_vars();
  }
  ncomp += this->get_num_plot_vars();

  // Allocate storage
  m_amr->allocate(output, phase::gas, ncomp);
  data_ops::set_value(output, 0.0);

  // Assemble data
  int icomp = 0;             // Used as reference for output components
  Real t_assemble = -MPI_Wtime();
  if(m_verbosity >= 3){
    pout() << "driver::write_plot_file - assembling data..." << endl;
  }
  
  // Time stepper writes its data
  m_timestepper->write_plot_data(output, names, icomp);

  // Cell tagger writes data
  if(!m_celltagger.isNull()){
    m_celltagger->write_plot_data(output, names, icomp);
  }

  // Write internal data
  names.append(this->get_plotvar_names());
  this->write_plot_data(output, icomp);
  t_assemble += MPI_Wtime();
									       
  // Data file aliasing, because Chombo IO wants dumb pointers. 
  Vector<LevelData<EBCellFAB>* > output_ptr(1 + m_amr->get_finest_level());
  m_amr->alias(output_ptr, output);

  // Restrict plot depth if need be
  int plot_depth;
  if(m_max_plot_depth < 0){
    plot_depth = m_amr->get_finest_level();
  }
  else{
    plot_depth = Min(m_max_plot_depth, m_amr->get_finest_level());
  }

  // Write HDF5 file
  if(m_verbosity >= 3){
    pout() << "driver::write_plot_file - writing plot file..." << endl;
  }
  Real t_write = -MPI_Wtime();
  writeEBHDF5(fname, 
	      m_amr->get_grids(),
	      output_ptr,
	      names, 
	      m_amr->get_domains()[0],
	      m_amr->get_dx()[0], 
	      m_dt,
	      m_time,
	      m_amr->get_ref_rat(),
	      plot_depth + 1,
	      false,
	      Vector<Real>(),
	      m_num_plot_ghost*IntVect::Unit);
  t_write += MPI_Wtime();

  const Real t_tot = t_write + t_assemble;
  if(m_verbosity >= 3){
    pout() << "driver::write_plot_file - writing plot file... DONE!. " << endl
      	   << "\t Total time    = " << t_tot << " seconds" << endl
	   << "\t Assemble data = " << 100.*t_assemble/t_tot << "%" << endl
      	   << "\t Write time    = " << 100.*t_write/t_tot << "%" << endl;
  }
}

void driver::write_plot_data(EBAMRCellData& a_output, int& a_comp){
  CH_TIME("driver::write_plot_data");
  if(m_verbosity > 3){
    pout() << "driver::write_plot_data" << endl;
  }

  if(m_plot_tags)   write_tags(a_output, a_comp);
  if(m_plot_ranks)  write_ranks(a_output, a_comp);
}

void driver::write_tags(EBAMRCellData& a_output, int& a_comp){
  CH_TIME("driver::write_tags");
  if(m_verbosity > 3){
    pout() << "driver::write_tags" << endl;
  }

  
  // Alloc some temporary storage
  EBAMRCellData tags;
  m_amr->allocate(tags, phase::gas, 1);
  data_ops::set_value(tags, 0.0);
    
  // Set tagged cells = 1
  for (int lvl = 0; lvl <= m_amr->get_finest_level(); lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl      = m_amr->get_ebisl(phase::gas)[lvl];
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const DenseIntVectSet& ivs = (*m_tags[lvl])[dit()];
      const Box box              = dbl.get(dit());

      // Do regular cells only.
      BaseFab<Real>& tags_fab = (*tags[lvl])[dit()].getSingleValuedFAB();
      for (BoxIterator bit(box); bit.ok(); ++bit){
	const IntVect iv = bit();
	if(ivs[iv]){
	  tags_fab(iv, 0) = 1.0;
	}
      }
    }
  }

  data_ops::set_covered_value(tags, 0, 0.0);

  const Interval src_interv(0, 0);
  const Interval dst_interv(a_comp, a_comp);
  for (int lvl = 0; lvl <= m_amr->get_finest_level(); lvl++){
    tags[lvl]->localCopyTo(src_interv, *a_output[lvl], dst_interv);
  }

  a_comp++;
}

void driver::write_ranks(EBAMRCellData& a_output, int& a_comp){
  CH_TIME("driver::write_ranks");
  if(m_verbosity > 3){
    pout() << "driver::write_ranks" << endl;
  }

  EBAMRCellData scratch;
  m_amr->allocate(scratch, phase::gas, 1);

  const Real rank = 1.0*procID();
  data_ops::set_value(scratch, rank);

  const Interval src_interv(0, 0);
  const Interval dst_interv(a_comp, a_comp);
  for (int lvl = 0; lvl <= m_amr->get_finest_level(); lvl++){
    scratch[lvl]->localCopyTo(src_interv, *a_output[lvl], dst_interv);
  }

  a_comp++; 
}

void driver::write_checkpoint_file(){
  CH_TIME("driver::write_checkpoint_file");
  if(m_verbosity > 3){
    pout() << "driver::write_checkpoint_file" << endl;
  }
  
  const int finest_level = m_amr->get_finest_level();
  int finest_chk_level  = Min(m_max_chk_depth, finest_level);
  if(m_max_chk_depth < 0){
    finest_chk_level = finest_level;
  }

  HDF5HeaderData header;
  header.m_real["coarsest_dx"] = m_amr->get_dx()[0];
  header.m_real["time"]        = m_time;
  header.m_real["dt"]          = m_dt;
  header.m_int["step"]         = m_step;
  header.m_int["finest_level"] = finest_level;

  // Output file name
  char str[100];
  const std::string prefix = m_output_dir + "/chk/" + m_output_names;
  sprintf(str, "%s.check%07d.%dd.hdf5", prefix.c_str(), m_step, SpaceDim);

  // Output file
  HDF5Handle handle_out(str, HDF5Handle::CREATE);
  header.writeToFile(handle_out);

  // Write stuff level by level
  const Real t0 = MPI_Wtime();
  if(m_verbosity >= 3){
    pout() << "driver::write_checkpoint_file - writing checkpoint file..." << endl;
  }
  
  for (int lvl = 0; lvl <= finest_chk_level; lvl++){
    handle_out.setGroupToLevel(lvl);

    // write amr grids
    write(handle_out, m_amr->get_grids()[lvl]); // write AMR grids

    // time stepper checkpoints data
    m_timestepper->write_checkpoint_data(handle_out, lvl); 

    // driver checkpoints internal data
    this->write_checkpoint_level(handle_out, lvl); 
  }
  const Real t1 = MPI_Wtime();

  if(m_verbosity >= 3){
    pout() << "driver::write_checkpoint_file - writing checkpoint file... DONE! " << endl
	   << "\t Total time    = " << t1 - t0 << " seconds" << endl;
  }
  
  handle_out.close();
}

void driver::write_checkpoint_level(HDF5Handle& a_handle, const int a_level){
  CH_TIME("driver::write_checkpoint_level");
  if(m_verbosity > 5){
    pout() << "driver::write_checkpoint_level" << endl;
  }

  // Create some scratch data = 0 which can grok
  EBCellFactory fact(m_amr->get_ebisl(phase::gas)[a_level]);
  LevelData<EBCellFAB> scratch(m_amr->get_grids()[a_level], 1, 3*IntVect::Unit, fact);
  data_ops::set_value(scratch, 0.0);

  // Set tags = 1
  const DisjointBoxLayout& dbl = m_amr->get_grids()[a_level];
  const EBISLayout& ebisl      = m_amr->get_ebisl(phase::gas)[a_level];
    
  for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
    const Box box = dbl.get(dit());
    const DenseIntVectSet& tags = (*m_tags[a_level])[dit()];

    BaseFab<Real>& scratch_fab = scratch[dit()].getSingleValuedFAB();
    for (BoxIterator bit(box); bit.ok(); ++bit){
      const IntVect iv = bit();
      if(tags[iv]){
	scratch_fab(iv, 0) = 1.0;
      }
    }

    data_ops::set_covered_value(scratch, 0, 0.0);
  }

  // Write tags
  write(a_handle, scratch, "tagged_cells");
}

void driver::read_checkpoint_level(HDF5Handle& a_handle, const int a_level){
  CH_TIME("driver::read_checkpoint_level");
  if(m_verbosity > 5){
    pout() << "driver::read_checkpoint_level" << endl;
  }

  const DisjointBoxLayout& dbl = m_amr->get_grids()[a_level];
  const EBISLayout& ebisl      = m_amr->get_ebisl(phase::gas)[a_level];

  // Some scratch data we can use
  EBCellFactory fact(ebisl);
  LevelData<EBCellFAB> scratch(dbl, 1, 3*IntVect::Unit, fact);
  data_ops::set_value(scratch, 0.0);

  // Read in tags
  read<EBCellFAB>(a_handle, scratch, "tagged_cells", dbl, Interval(0,0), false);

  // Instantiate m_tags
  for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
    const Box box          = dbl.get(dit());
    const EBCellFAB& tmp   = scratch[dit()];
    const EBISBox& ebisbox = tmp.getEBISBox();
    const EBGraph& ebgraph = ebisbox.getEBGraph();
    const IntVectSet ivs(box);

    DenseIntVectSet& tagged_cells = (*m_tags[a_level])[dit()];

    BaseFab<Real>& scratch_fab = scratch[dit()].getSingleValuedFAB();
    for (BoxIterator bit(box); bit.ok(); ++bit){
      const IntVect iv = bit();
      if(scratch_fab(iv, 0) > 0.9999){
	tagged_cells |= iv;
      }
    }
  }
}

void driver::write_vector_data(HDF5HeaderData&     a_header,
			       const Vector<Real>& a_data,
			       const std::string   a_name,
			       const int           a_elements){
  CH_TIME("driver::write_vector_data");
  if(m_verbosity > 3){
    pout() << "driver::write_vector_data" << endl;
  }

  char step[100];

  const int last = a_data.size() < a_elements ? a_data.size() : a_elements;
  for (int i = 0; i < last; i++){
    sprintf(step, "%07d", i);

    const std::string identifier = a_name + std::string(step);
    a_header.m_real[identifier] = a_data[i];
  }
}

void driver::read_vector_data(HDF5HeaderData& a_header,
			      Vector<Real>&         a_data,
			      const std::string     a_name,
			      const int             a_elements){
  CH_TIME("driver::read_vector_data");
  if(m_verbosity > 3){
    pout() << "driver::read_vector_data" << endl;
  }

  char step[100];

  const int last = a_data.size() < a_elements ? a_data.size() : a_elements;
  for (int i = 0; i < last; i++){
    sprintf(step, "%07d", i);

    const std::string identifier = a_name + std::string(step);
    a_data[i] = a_header.m_real[identifier];
  }
}

// void driver::open_mass_dump_file(ofstream& a_file){
//   if(procID() == 0){
//     const std::string prefix = m_output_dir + "/" + "mass_dump.txt";
//     a_file.open(prefix);

//     const Vector<std::string> names = m_timestepper->get_cdr()->get_names();

//     a_file << "# time step" << "\t" << "time";
//     for (int i = 0; i < names.size(); i++){
//       a_file << "\t" << names[i];
//     }
//     a_file << endl;
//   }
// }

// void driver::open_charge_dump_file(ofstream& a_file){
//   if(procID() == 0){
//     const std::string prefix = m_output_dir + "/" + "charge_dump.txt";
//     a_file.open(prefix);

//     const Vector<std::string> names = m_timestepper->get_cdr()->get_names();

//     a_file << "# time step" << "\t" << "time";
//     for (int i = 0; i < names.size(); i++){
//       a_file << "\t" << names[i];
//     }
//     a_file << "\t" << "surface charge" << endl;
//     a_file << endl;
//   }
// }

// void driver::dump_mass(ofstream& a_file){
//   const Vector<Real> masses = m_timestepper->get_cdr()->compute_mass();
//   if(procID() == 0){
//     a_file << m_step << "\t" << m_time;
//     for (int i = 0; i < masses.size(); i++){
//       a_file << "\t" << masses[i];
//     }
//     a_file << endl;
//   }
// }

// void driver::dump_charge(ofstream& a_file){
//   const Real surface_charge  = m_timestepper->get_sigma()->compute_charge();
//   const Vector<Real> charges = m_timestepper->get_cdr()->compute_charge();
//   if(procID() == 0){
//     a_file << m_step << "\t" << m_time;
//     for (int i = 0; i < charges.size(); i++){
//       a_file << "\t" << charges[i]/units::s_Qe;
//     }
//     a_file << "\t" << surface_charge/units::s_Qe;
//     a_file << endl;
//   }
// }

// void driver::close_mass_dump_file(ofstream& a_file){
//   if(procID() == 0){
//     a_file.close();
//   }
// }

// void driver::close_charge_dump_file(ofstream& a_file){
//   if(procID() == 0){
//     a_file.close();
//   }
// }

// void driver::compute_norm(std::string a_chk_coarse, std::string a_chk_fine){
//   CH_TIME("driver::compute_norm");
//   if(m_verbosity > 5){
//     pout() << "driver::compute_norm" << endl;
//   }

//   // TLDR: This routine is long and ugly. In short, it instantiates a all solvers and read the fine-level checkpoint file
//   // into those solvers.
//   RefCountedPtr<cdr_layout>& cdr         = m_timestepper->get_cdr();

//   this->read_checkpoint_file(a_chk_fine); // This reads the fine-level data

//   Vector<EBISLayout>& fine_ebisl        = m_amr->get_ebisl(phase::gas);
//   Vector<DisjointBoxLayout>& fine_grids = m_amr->get_grids();
//   Vector<ProblemDomain>& fine_domains   = m_amr->get_domains();

//   // Read the second file header
//   HDF5Handle handle_in(a_chk_coarse, HDF5Handle::OPEN_RDONLY);
//   HDF5HeaderData header;
//   header.readFromFile(handle_in);
//   int finest_coar_level = header.m_int["finest_level"];


//   // Read coarse data into these data holders
//   Vector<EBAMRCellData> cdr_densities(1 + m_plaskin->get_num_species());
//   for (int i = 0; i < cdr_densities.size(); i++){
//     cdr_densities[i].resize(1+finest_coar_level);
//   }

//   // Read cdr data
//   for (int lvl = 0; lvl <= finest_coar_level; lvl++){
//     handle_in.setGroupToLevel(lvl);
    
//     for (cdr_iterator solver_it = cdr->iterator(); solver_it.ok(); ++solver_it){
//       const int idx = solver_it.get_solver();
//       RefCountedPtr<cdr_solver>& solver = solver_it();
//       const std::string solver_name = solver->get_name();
      
//       EBCellFactory cellfact(fine_ebisl[lvl]);
//       cdr_densities[idx][lvl] = RefCountedPtr<LevelData<EBCellFAB> > 
// 	(new LevelData<EBCellFAB>(fine_grids[lvl], 1, 3*IntVect::Unit, cellfact));
      
//       read<EBCellFAB>(handle_in, *cdr_densities[idx][lvl], solver_name, fine_grids[lvl], Interval(), false);
//     }
//   }


//   // We will now compute n_2h - A(n_h)
//   pout() << "files are '"  << a_chk_coarse << "' and '" << a_chk_fine << "'" << endl;
//   EBCellFactory cellfact(fine_ebisl[finest_coar_level]);
//   LevelData<EBCellFAB> diff(fine_grids[finest_coar_level], 1, 0*IntVect::Unit, cellfact);
//   for (cdr_iterator solver_it = cdr->iterator(); solver_it.ok(); ++solver_it){
//     const int idx = solver_it.get_solver();
//     RefCountedPtr<cdr_solver>& solver = solver_it();
//     const std::string solver_name = solver->get_name();

//     LevelData<EBCellFAB>& fine_sol = *solver->get_state()[finest_coar_level];
//     LevelData<EBCellFAB>& coar_sol = *cdr_densities[idx][finest_coar_level];

//     data_ops::set_value(diff, 0.0);
//     data_ops::incr(diff, coar_sol,   1.0);
//     data_ops::incr(diff, fine_sol,  -1.0);
//     data_ops::scale(diff, 1.E-18);

//     writeEBLevelname (&diff, "diff.hdf5");
//     Real volume;

//     Real Linf;
//     Real L1;
//     Real L2;

//     Linf = EBLevelDataOps::kappaNorm(volume, diff, EBLEVELDATAOPS_ALLVOFS, fine_domains[finest_coar_level], 0);
//     L1   = EBLevelDataOps::kappaNorm(volume, diff, EBLEVELDATAOPS_ALLVOFS, fine_domains[finest_coar_level], 1);
//     L2   = EBLevelDataOps::kappaNorm(volume, diff, EBLEVELDATAOPS_ALLVOFS, fine_domains[finest_coar_level], 2);
    
//     pout() << idx << "\t Linf = " << Linf << "\t L1 = " << L1 << "\t L2 = " << L2 << endl;
//   }
  
//   handle_in.close();
// }

// void driver::compute_coarse_norm(const std::string a_chk_coarse, const std::string a_chk_fine, const int a_species){
//   CH_TIME("driver::compute_coarse_norm");
//   if(m_verbosity > 5){
//     pout() << "driver::compute_coarse_norm" << endl;
//   }

//   // TLDR: This routine is long and ugly. In short, it instantiates a all solvers and read the fine-level checkpoint file
//   // into those solvers.
//   RefCountedPtr<cdr_layout>& cdr         = m_timestepper->get_cdr();

//   this->read_checkpoint_file(a_chk_fine); // This reads the fine-level data

//   Vector<EBISLayout>& fine_ebisl        = m_amr->get_ebisl(phase::gas);
//   Vector<DisjointBoxLayout>& fine_grids = m_amr->get_grids();
//   Vector<ProblemDomain>& fine_domains   = m_amr->get_domains();

//   // Read the second file header
//   HDF5Handle handle_in(a_chk_coarse, HDF5Handle::OPEN_RDONLY);
//   HDF5HeaderData header;
//   header.readFromFile(handle_in);
//   int finest_coar_level = 0;
//   Real dt = header.m_real["dt"];

//   // Read coarse data into these data holders
//   Vector<EBAMRCellData> cdr_densities(1 + m_plaskin->get_num_species());
//   for (int i = 0; i < cdr_densities.size(); i++){
//     cdr_densities[i].resize(1+finest_coar_level);
//   }

//   // Read cdr data
//   for (int lvl = 0; lvl <= finest_coar_level; lvl++){
//     handle_in.setGroupToLevel(lvl);
    
//     for (cdr_iterator solver_it = cdr->iterator(); solver_it.ok(); ++solver_it){
//       const int idx = solver_it.get_solver();
//       RefCountedPtr<cdr_solver>& solver = solver_it();
//       const std::string solver_name = solver->get_name();
      
//       EBCellFactory cellfact(fine_ebisl[lvl]);
//       cdr_densities[idx][lvl] = RefCountedPtr<LevelData<EBCellFAB> > 
// 	(new LevelData<EBCellFAB>(fine_grids[lvl], 1, 3*IntVect::Unit, cellfact));
      
//       read<EBCellFAB>(handle_in, *cdr_densities[idx][lvl], solver_name, fine_grids[lvl], Interval(), false);
//     }
//   }


//   // For each of the
//   if(a_species == -1){
//     pout() << "files are '"  << a_chk_coarse << "' and '" << a_chk_fine << "'" << endl;
//   }
//   const int compute_level = 0;
//   EBCellFactory cellfact(fine_ebisl[compute_level]);
//   LevelData<EBCellFAB> diff(fine_grids[compute_level], 1, 0*IntVect::Unit, cellfact);
//   if(a_species == -1){
//     pout() << "Linf" << "\t L1" << "\t L2" << endl;
//   }
//   for (cdr_iterator solver_it = cdr->iterator(); solver_it.ok(); ++solver_it){
//     const int idx = solver_it.get_solver();
//     RefCountedPtr<cdr_solver>& solver = solver_it();
//     const std::string solver_name = solver->get_name();

//     LevelData<EBCellFAB>& fine_sol = *solver->get_state()[compute_level];
//     LevelData<EBCellFAB>& coar_sol = *cdr_densities[idx][compute_level];

//     data_ops::set_value(diff, 0.0);
//     data_ops::incr(diff, coar_sol,   1.0);
//     data_ops::incr(diff, fine_sol,  -1.0);

//     writeEBLevelname (&diff, "diff.hdf5");
//     Real volume;


//     const Real Linf = EBLevelDataOps::kappaNorm(volume, diff, EBLEVELDATAOPS_ALLVOFS, fine_domains[compute_level], 0);
//     const Real L1   = EBLevelDataOps::kappaNorm(volume, diff, EBLEVELDATAOPS_ALLVOFS, fine_domains[compute_level], 1);
//     const Real L2   = EBLevelDataOps::kappaNorm(volume, diff, EBLEVELDATAOPS_ALLVOFS, fine_domains[compute_level], 2);

//     const Real sLinf = EBLevelDataOps::kappaNorm(volume, fine_sol, EBLEVELDATAOPS_ALLVOFS, fine_domains[compute_level], 0);
//     const Real sL1   = EBLevelDataOps::kappaNorm(volume, fine_sol, EBLEVELDATAOPS_ALLVOFS, fine_domains[compute_level], 1);
//     const Real sL2   = EBLevelDataOps::kappaNorm(volume, fine_sol, EBLEVELDATAOPS_ALLVOFS, fine_domains[compute_level], 2);

//     if(a_species == -1){
//       pout() << Linf/sLinf << "\t" << L1/sL1 << "\t" << L2/sL2 << endl;
//     }
//     else if(idx == a_species){
//       pout() << dt << "\t" << Linf/sLinf << "\t" << L1/sL1 << "\t" << L2/sL2 << endl;
//     }
//   }
  
//   handle_in.close();
// }