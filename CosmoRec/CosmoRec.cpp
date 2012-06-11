//===========================================================================================================
//
// Author: Jens Chluba 
// last modification: March 2012
//
//===========================================================================================================

//===========================================================================================================
// Standards
//===========================================================================================================
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <iomanip>
#include <cmath>
#include <limits.h>
#include <vector>

//===========================================================================================================
// GSL
//===========================================================================================================
#include <gsl/gsl_integration.h>
#include <gsl/gsl_sort_double.h>

//===========================================================================================================
// several libs
//===========================================================================================================
#include "CosmoRec.h"
#include "Atom.h"
#include "HeI_Atom.h"

#include "Photoionization_cross_section.h"
#include "Sobolev.h"
#include "Cosmos.h"

#include "physical_consts.h"
#include "routines.h"

//===========================================================================================================
// net rates for Helium case
//===========================================================================================================
#include "ODEdef_Xi.HeI.h"

//===========================================================================================================
// PDE routines (Raman-scattering, two-gamma corrections & Diffusion stuff)
//===========================================================================================================
#include "Solve_PDEs.h"
#include "two_photon_profile.2s1s.h"
#include "Raman_profiles.h"

//===========================================================================================================
// ODE definitions for effective HI & HeI atom and the CosmoRec ODE solver
//===========================================================================================================
#include "ODE_effective.h"
#include "ODE_solver_Rec.h"

//===========================================================================================================
// access to effective rates for HI & HeI atom
//===========================================================================================================
#include "get_effective_rates.HI.h"
#include "get_effective_rates.HeI.h"


//===========================================================================================================
// namespaces that are used
//===========================================================================================================
using namespace std;
using namespace ODE_HI_effective;
using namespace ODE_solver_Rec;

//===========================================================================================================
void print_message(string mess);

//===========================================================================================================
// these are several Module (i.e. plugin) that are directly loaded when compiling. If these are changes one 
// should type "make clean" before "make"
//===========================================================================================================
#include "./Modules/global_variables.cpp"
#include "./Modules/HI_routines.cpp"
#include "./Modules/He_routines.cpp"
#include "./Modules/HeI.feedback.cpp"
#include "./Modules/DPesc_HI_abs_tabulation.cpp"
#include "./Modules/Diffusion_correction.cpp"   

//===========================================================================================================
// variable transformation. For numerical reasons it is better 
// to rescale the populations. This is done here.
//===========================================================================================================
#include "./Modules/var_trans.cpp"

//===========================================================================================================
// functions to read parameter file and set up memory etc
//===========================================================================================================
#include "./Modules/aux_functions.cpp"

//===========================================================================================================
// DM annihilation module
//===========================================================================================================
#include "./Modules/DM_annihilation.cpp"

//===========================================================================================================
// additional definitions for CosmoRec-ODE system
//===========================================================================================================
#include "./Modules/ODEdef_CosmoRec.cpp"

//===========================================================================================================
// writing the output
//===========================================================================================================
#include "./Modules/output_methods.cpp"

//===========================================================================================================
void print_message(string mess)
{
    if(show_CosmoRec_mess>=0)
    {
        cout << "\n ====================================================="
             << "===================================================== " << endl;
        cout << " || " << mess << endl;
        cout << " ====================================================="
             << "===================================================== " << endl << endl;   
    }
    
    return;
}


//===========================================================================================================
//
// Normal Recfast-run
//
//===========================================================================================================

//===========================================================================================================
// initialization
//===========================================================================================================
void read_startup_data_RECFAST(string filename, Parameters_form_initfile &inparams, Cosmos &cosmos, int corr_fac)
{
    ifstream paramfile(filename.c_str());
    if(!paramfile) { cerr << " Error opening parameter file. Exiting. " << endl; exit(1); }
    
    double param[12];
    read_entries_from_parameter_file(paramfile, param);
    
    // compute Omega_L according to Omega_K
    if(param[7]<=0.0) param[7]=1.0-param[5]-param[8]-cosmos.calc_Orel(param[4], param[10], param[9]);
    
    if(show_CosmoRec_mess>=1)
    {
        cout << "\n Using parameters corresponding to " << filename << endl;
        cout << "\n zs: " << param[1] << "\t ze: " << param[2] 
             << "\t nzpts: " << (int)param[0] << endl;
        cout << " Y_p: " << param[3] << "\t TCMB: " << param[4] << endl;
        cout << " OmegaM: " << param[5] << "\t OmegaB: " << param[6] 
             << "\n OmegaL: " << param[7] << "\t OmegaK: " << param[8] 
             << "\t Omega_rel: " << cosmos.calc_Orel(param[4], param[10], param[9]) 
             << " Nnu= " << param[10] << endl;
        cout << " Hubble constant in units of 100 km s-1 Mpc-1: " << param[9] << endl;
        cout << " Fudge factor for H recombination: " << param[11] << endl << endl;
    }   
    
    set_startup_data(param, inparams);
    set_cosmology(inparams, cosmos, fDM_CosmoRec, corr_fac);
    
    return;
}

//===========================================================================================================
// Just RECFAST
//===========================================================================================================
void run_RECFAST(string filename, int corr_fac)
{
    //=======================================================================================================
    // initialize cosmology and the general startup
    //=======================================================================================================
    read_startup_data_RECFAST(filename, parameters, cosmos, corr_fac);
    
    double *za=new double[parameters.nz]; 
    //=======================================================================================================
    // initialise zarr
    //=======================================================================================================
    init_xarr(parameters.zstart, max(1.0e-8, parameters.zend), za, parameters.nz, 0, 0);
    
    if(show_CosmoRec_mess>=0) cout << " writing data to " << path << endl;
    
    ofstream ofile;
    string name=path;
    if(corr_fac==1) name+="Xe_Recfast.corr_fac.dat";
    else name+="Xe_Recfast.dat";
    ofile.open(name.c_str());
    ofile.precision(10); 
    
    ofile << "# Recfast++ output. The columns are z, Xe=Ne/NH, Te\n#" << endl;
    
    for(int l=0; l<parameters.nz; l++) ofile << za[l] << " " 
                                             << cosmos.Xe_Seager(za[l]) << " " 
                                             << cosmos.Te(za[l]) << endl;
    
    if(show_CosmoRec_mess>=0) cout << " finished RECFAST++ run. Now exiting program ... " 
                                   << endl << endl;

    delete [] za;
    
    exit(0);        
    
    return;
}



//===========================================================================================================
//
// extend computation using RECFAST
//
//===========================================================================================================
void compute_Recfast_part(double zi, double ze, double Xe_Hi, double Xe_Hei, 
                          double Xei, double rhoi, double dXei,
                          ofstream &ofile, ofstream &ofileRecfast)
{
    if(show_CosmoRec_mess>=0) 
        cout << " compute_Recfast_part:: the solution will be extended to z= " << ze 
             << " solving the Recfast system" << endl;
    
    int nz=nz_Recfast_part;
    double *zarr=new double[nz];
    double *Xe_H=new double[nz];
    double *Xe_He=new double[nz];
    double *Xe=new double[nz];
    double *TM=new double[nz];
    
    init_xarr(zi, max(1.0e-5, ze), zarr, nz, 0, 0);
    
    double TMi=cosmos.TCMB(zi)*rhoi;
    cosmos.recombine_using_Recfast_system(nz, zi, ze, fDM_CosmoRec, Xe_Hi, max(1.0e-30, Xe_Hei),  
                                          Xei, TMi, dXei, zarr, Xe_H, Xe_He, Xe, TM);
    
    //=======================================================================================================
    // only Xe and Tm
    //=======================================================================================================
    for(int i=1; i<nz; i++)
    {
        ofile << zarr[i] << " " << Xe[i] << " " << TM[i] << endl;
        write_X_Recfast_z_to_file(ofileRecfast, zarr[i], cosmos);
    }
    
    //========================================================================================
    // Save results for output to CosmoMC
    //========================================================================================
    if(!Diffusion_correction || (Diffusion_correction && iteration_count==Diff_iteration_max) )
    {
        vector<double> dum(3);
        for(int i=1; i<nz; i++)
        {
            dum[0]=zarr[i]; 
            dum[1]=Xe[i]; 
            dum[2]=TM[i];
        
            output_CosmoRec.push_back(dum);
        }
    }
    
    delete [] zarr;
    delete [] Xe_H;
    delete [] Xe_He;
    delete [] Xe;
    delete [] TM;
    
    return;
}


//===========================================================================================================
//
// for main calculation for CosmoRec
//
//===========================================================================================================
#include "./Modules/main.CosmoRec.cpp"


//===========================================================================================================
//
// CosmoRec part after setup
//
//===========================================================================================================
int CosmoRec()
{
    output_CosmoRec.clear();
    
    string addname_in=addname;
    
    //==============================================================================
    // setup for the iteration
    //==============================================================================
    bool iteration=(Diffusion_correction ? 1 : 0);
    int include_2s1s_corr=0, err=0;
    //
    if(DI1_2s_correction)
    {
        include_2s1s_corr=1; 
        DI1_2s_correction=0;
        
        if(include_stimulated_term) setup_DI2_2s_spline_data(Rec_database_path);  
    }   
    
    //==============================================================================
    // enter the main run of ODE --> PDE-solver --> ODE
    //==============================================================================
    do
    {
        if(Diffusion_correction && iteration_count>0)
        { 
            if(show_CosmoRec_mess>=0) cout << "\n\n Entering diffusion part " << endl; 
            
            if(include_2s1s_corr==1 && iteration_count==1) DI1_2s_correction=1;
            
            //======================================================================
            // reset parameters
            //======================================================================
            restart_CosmoRec();
                        
            print_message("computing DR from iteration " + int_to_string(iteration_count-1)); 
            
            //======================================================================
            // file from previous output
            //======================================================================
            string addnamePesc=".it_"+int_to_string(iteration_count-1)+addname;
            if(iteration_count==1) addnamePesc=addname;
            
            vector<double> DF_vec_z;
            vector<vector<double> > DF_2_gamma_vec;
            vector<vector<double> > DF_Raman_vec;
            vector<double> DI1_2s_vec;  
            
            //======================================================================
            // solver for the diffusion correction
            // HI populations are communicated; the effective rates have to be
            // preloaded.
            //======================================================================            
            compute_DPesc_with_diffusion_equation_effective(DF_vec_z, DF_2_gamma_vec, 
                                                            DF_Raman_vec, DI1_2s_vec, 
                                                            min(2200.0, parameters.zstart/1.001), 
                                                            max(500.0, parameters.zend*1.001), 
                                                            nS_effective, ntg_max, nR_max, cosmos, 
                                                            Hydrogen_Atoms, 
                                                            pass_on_the_Solution_CosmoRec,
                                                            iteration_count-1);
            
            setup_DF_spline_data(DF_vec_z, DF_2_gamma_vec, DF_Raman_vec);
            
            if(DI1_2s_correction) setup_DI1_2s_spline_data(DF_vec_z, DI1_2s_vec);
            
            //======================================================================
            // change the name for output
            //======================================================================
            if(iteration_count<Diff_iteration_max) 
                addname=".it_"+int_to_string(iteration_count)+addname;
            else addname=".final"+addname;
            
            print_message("starting iteration # " + int_to_string(iteration_count)); 
            
            Diffusion_correction_is_on=1;
        }
        
        err=compute_history_with_effective_rates();  // defined in main.CosmoRec.cpp
        
        if(err!=0) iteration=0;
            
        iteration_count++;      
        
        if(iteration_count>Diff_iteration_max) iteration=0;
    }
    while(iteration);

    //==============================================================================
    // restore initial state before run (important for batch mode run)
    //==============================================================================
    iteration_count=Diff_iteration_min;
    flag_He=1;              // has to be reset, since after each run it will be == 0
    addname=addname_in;
    Diffusion_correction_is_on=0;
    
    return err;
}


//===========================================================================================================
//
// CosmoRec for run from console giving a parameter file
//
//===========================================================================================================
int CosmoRec(int narg, char *args[])
{
    string filename=args[narg-1], mode="";
    
    if(narg==2){}
    else if(narg==3)
    {
        string solvermode=args[narg-2];
        mode=args[narg-2];
        if(mode=="REC") print_message(" Running Recfast++ module.");
        else if(mode=="RECcf") print_message(" Running Recfast++ module with correction function."); 
        else{ mode=""; print_message(" Runmode does not exist. Exiting"); exit(0); }
    }
    else{ cerr << " Too many/few parameters " << endl; exit(1); }
        
    //========================================================================
    // just run history using the recfast module in the cosmology class
    //========================================================================
    if(mode=="REC") run_RECFAST(filename, 0);
    if(mode=="RECcf") run_RECFAST(filename, 1);

    //========================================================================
    // initialize cosmology and the general startup
    //========================================================================
    read_startup_data(filename, parameters, cosmos);
    set_array_dimensions_and_parameters();
    allocate();
    set_variables();
    
    //========================================================================
    // initialise zarr (linear)
    //========================================================================
    init_xarr(parameters.zstart, parameters.zend, zarr, parameters.nz, 0, 0);
    
    //========================================================================
    // enter CosmoRec
    //========================================================================
    int err=CosmoRec();
    if(err!=0){ cerr << " CosmoRec:: run was not completed. Exiting. " << endl; }
    
    //========================================================================
    // clean up memory
    //========================================================================
    deallocate();
    clear_atoms();
    free_all_splines_JC();

    if(show_CosmoRec_mess>=0) cout << " finished... " << endl;

    return err;
}



//===========================================================================================================
//
// to call CosmoRec in batch mode, i.e. avoiding initialization & loading tables
//
//===========================================================================================================
static bool CosmoRec_is_initialized=0;

//===========================================================================================================
int return_solution_mem_Xe;
int return_solution_mem_Te;

void return_solution_to_calling_program(const int nz, double *z_arr, double *Xe_arr, double *Te_arr)
{
    int nz_loc=output_CosmoRec.size();
    vector<double> za(nz_loc);
    vector<double> ya(nz_loc);
    
    //======================================================================
    // Xe
    //======================================================================
    for(int i=0; i<nz_loc; i++)
    { 
        za[nz_loc-1-i]=output_CosmoRec[i][0]; 
        ya[nz_loc-1-i]=log(output_CosmoRec[i][1]); 
    }

    double zmin=min(za[0], za[nz_loc-1]);
    double zmax=max(za[0], za[nz_loc-1]);
    
    if(!CosmoRec_is_initialized)
        return_solution_mem_Xe=calc_spline_coeffies_JC(nz_loc, &za[0], &ya[0],
                                                       "return_solution_to_calling_program :: Xe");      
    else update_spline_coeffies_JC(return_solution_mem_Xe, nz_loc, &za[0], &ya[0]);      
        
    //======================================================================
    // Te
    //======================================================================
    for(int i=0; i<nz_loc; i++)
        ya[nz_loc-1-i]=log(output_CosmoRec[i][2]); 
    
    if(!CosmoRec_is_initialized)
        return_solution_mem_Te=calc_spline_coeffies_JC(nz_loc, &za[0], &ya[0],
                                                       "return_solution_to_calling_program :: Te");      
    else update_spline_coeffies_JC(return_solution_mem_Te, nz_loc, &za[0], &ya[0]);      
    
    //======================================================================
    // copy output
    //======================================================================
    for(int i=0; i<nz; i++)
    { 
        if(z_arr[i]>=zmax || z_arr[i]<=zmin) 
        {
            Xe_arr[i]=cosmos.Xe_Seager(z_arr[i]);
            Te_arr[i]=cosmos.Te(z_arr[i]);
        }
        else 
        {
            Xe_arr[i]=exp(calc_spline_JC(z_arr[i], return_solution_mem_Xe));
            Te_arr[i]=exp(calc_spline_JC(z_arr[i], return_solution_mem_Te));                
        }
    }
    
    return;
}

//===========================================================================================================
// Just return Recfast++ output
//===========================================================================================================
void run_RECFAST_pp(double param[12], const int nz, double *z_arr, 
                    double *Xe_arr, double *Te_arr, double fDM, int CT_corr=0)
{
    //======================================================================
    // compute Omega_L according to Omega_K
    //======================================================================
    if(param[7]<=0.0) 
        param[7]=1.0-param[5]-param[8]-cosmos.calc_Orel(param[4], param[10], param[9]);
    
    if(show_CosmoRec_mess>=1)
    {
        cout << "\n zs: " << param[1] << "\t ze: " << param[2] 
             << "\t nzpts: " << (int)param[0] << endl;
        cout << " Y_p: " << param[3] << "\t TCMB: " << param[4] << endl;
        cout << " OmegaM: " << param[5] << "\t OmegaB: " << param[6] 
             << "\n OmegaL: " << param[7] << "\t OmegaK: " << param[8] 
             << "\t Omega_rel: " << cosmos.calc_Orel(param[4], param[10], param[9]) 
             << " Nnu= " << param[10] << endl;
        cout << " Hubble constant in units of 100 km s-1 Mpc-1: " << param[9] << endl;
        cout << " Fudge factor for H recombination: " << param[11] << endl << endl;
    }   
    
    set_startup_data(param, parameters);
    set_cosmology(parameters, cosmos, fDM, CT_corr);
    
    for(int l=0; l<nz; l++)
    {
        Xe_arr[l]=cosmos.Xe_Seager(z_arr[l]);
        Te_arr[l]=cosmos.Te(z_arr[l]);
    }
    
    return;
}

//===========================================================================================================
// to call CosmoRec with a list of parameters in batch mode (e.g. when calling it from CosmoMC).
// 
// runmode == 0: CosmoRec run with diffusion
//            1: CosmoRec run without diffusion
//            2: Recfast++ run (equivalent of the original Recfast version)
//            3: Recfast++ run with correction function of Chluba & Thomas, 2010
//
// On entry, the array z_arr should contain the redshifts at which Xe and Te are required. nz 
// determines the number of redshift points. Xe_arr and Te_arr will contain the solution on exit.
//
// Furthermore, runpars[0] defines the dark matter annihilation efficiency in eV/s.
// runpars[1] switches the accuracy of the recombination model:
//
// runpars[1]==-1: closest equivalent of 'HyRec' case (Haimoud & Hirata, 2010)
// runpars[1]== 0: default
// runpars[1]== 1: 2g for n<=4 & Raman for n<=3
// runpars[1]== 2: 2g for n<=8 & Raman for n<=7
// runpars[1]== 3: 2g for n<=8 & Raman for n<=7 + Helium feedback up to n=5
//
// The value of runpars[1] is only important for runmode 0 & 1.
//===========================================================================================================
int CosmoRec(const int runmode, const double runpars[5], 
             const double omegac, const double omegab, 
             const double omegak, const double Nnu,  
             const double h0, const double tcmb, const double yhe,
             const int nz, double *z_arr, double *Xe_arr, double *Te_arr, 
             const int label)
{
    //======================================================================
    // switch off messages 
    //======================================================================
    show_CosmoRec_mess=-1;
    write_Xe_Te_sol=0;
    write_populations=0;
    
    double params[12];
    
    //======================================================================
    // general setup 
    //======================================================================
    params[0]=1000; // number of redshift points for CosmoRec computation
    params[1]=3000; // starting redshift
    params[2]=0;    // ending redshift

    //======================================================================
    // cosmological parameters 
    //======================================================================
    params[3]= yhe;              // Helium mass fraction
    params[4]= tcmb;             // CMB temperature today
    //
    params[5]= omegac + omegab;  // Omega_matter
    params[6]= omegab;           // Omega_b
    params[7]= 0.0;              // Omega_Lambda (computed internally)
    params[8]= omegak;           // Omega_curv          
    //
    params[9] = h0;              // h100
    params[10]= Nnu;             // N_nu_eff
    params[11]= 1.14;            // Recfast++ fudge-factor
    
    //======================================================================
    // runmode 2: return Recfast++ result
    //======================================================================
    if(runmode==2) 
    {
        run_RECFAST_pp(params, nz, z_arr, Xe_arr, Te_arr, runpars[0], 0);
        return 0;
    }

    //======================================================================
    // runmode 3: return Recfast++ result with CT2010 correction function
    //======================================================================
    if(runmode==3) 
    {
        run_RECFAST_pp(params, nz, z_arr, Xe_arr, Te_arr, runpars[0], 1);
        return 0;
    }
    
    //======================================================================
    // parameters for the runmodes & output
    //======================================================================
    nShells=3;
    nS_effective=500;
    fDM_CosmoRec=runpars[0];
    
    nShellsHeI=2;
    flag_HI_absorption=2;
    flag_spin_forbidden=1;
    nHeFeedback=0;
    
    Diffusion_flag=1;
    induced_flag=2;
    ntg_max=3;
    nR_max=2;   
    
    //======================================================================
    // settings for non-default cases
    //======================================================================
    if((int)runpars[1]==-1){ nShells=4; ntg_max=4; nR_max=3; nShellsHeI=2; nHeFeedback=-2; flag_HI_absorption=1; }
    else if((int)runpars[1]==1){ nShells=4; ntg_max=4; nR_max=3; }
    else if((int)runpars[1]==2){ nShells=10; ntg_max=8; nR_max=7; nShellsHeI=2; nHeFeedback=0; }
    else if((int)runpars[1]==3){ nShells=10; ntg_max=8; nR_max=7; nShellsHeI=5; nHeFeedback=-5; }        
    
    //======================================================================
    if(runmode==1) Diffusion_flag=0;

    path=COSMORECDIR+"./outputs/";
    addname=".batch."+int_to_string(label, 6)+".dat";

    //======================================================================
    // initialize cosmology and the general startup
    //======================================================================
    if(!CosmoRec_is_initialized)
    {
        cout << "\n ====================================================="
             << "===================================================== " << endl;
        cout << " || You are entering CosmoRec "+CosmoRec_version+" (batch mode)." 
             << " Several initializations will be executed now." << endl;
        cout << " ====================================================="
             << "===================================================== " << endl;
    }
    
    int err=0;
    
    do
    {
        set_startup_data("", params, parameters, cosmos);
        set_array_dimensions_and_parameters();  
        
        if(!CosmoRec_is_initialized && err==0) allocate();
        
        set_variables();
        cosmos.dump_cosmology(COSMORECDIR+"./temp/batch."+int_to_string(label, 6)+".dat");
        
        //========================================================================
        // initialise zarr (only has to be done once...)
        //========================================================================
        init_xarr(parameters.zstart, parameters.zend, zarr, parameters.nz, 0, 0);
        
        //======================================================================
        // enter CosmoRec
        //======================================================================
        err=CosmoRec();
        
        if(err!=0)
        { 
            cout << " CosmoRec:: run was not completed. Restarting Solver. " << err << endl; 
            wait_f_r(); 
        }
    }   
    while(err!=0);

    //======================================================================
    // copy solution to output arrays
    //======================================================================
    return_solution_to_calling_program(nz, z_arr, Xe_arr, Te_arr);
    
    //======================================================================
    // exiting CosmoRec
    //======================================================================
    if(show_CosmoRec_mess>=0) cout << " finished run... " << endl;
    
    if(!CosmoRec_is_initialized)
    {
        cout << "\n ====================================================="
             << "===================================================== " << endl;
        cout << " || The first run was successful." 
             << " Will now switch to 'silent' mode." << endl;
        cout << " ====================================================="
             << "===================================================== " << endl;
    }

    CosmoRec_is_initialized=1;

    return err;
}

//===========================================================================================================
// Wrap the C++ Fortran routine to be allow calling from Fortran. Arguments are as above.
// Added 06.03.2011 (Richard Shaw)
//===========================================================================================================
extern "C" {
    
    void cosmorec_calc_cpp_(int * runmode, double * runpars, 
                            double * omega_c, double * omega_b, double * omega_k, 
                            double * num_nu, double * h0, double * t_cmb, double * y_he, 
                            double * za_in, double * xe_out, double * tb_out, int * len, int* label) 
    {
        
        //======================================================================
        // Call standard CosmoRec routine
        //======================================================================
        CosmoRec(*runmode, runpars, *omega_c, *omega_b, *omega_k, *num_nu, 
                 *h0 / 100.0, *t_cmb, *y_he, *len, za_in, xe_out, tb_out, *label);
    } 
    
}

//===========================================================================================================
// to cleanup after finishing with CosmoRec
//===========================================================================================================
void cleanup_CosmoRec()
{
    //======================================================================
    // clean up memory
    //======================================================================
    deallocate();
    clear_atoms();
    free_all_splines_JC();
    
    return;
}
//===========================================================================================================

