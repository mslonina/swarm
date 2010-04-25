#include "swarm.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

#include "random.h"
#include "kepler.h"

#define PARANOID_CPU_CHECK 0  // WARNING:  Setting to 1 takes a very long time

void set_initial_conditions_for_demo(swarm::ensemble& ens, const swarm::config& cfg);
void print_selected_systems_for_demo(swarm::ensemble& ens);


int main(int argc, const char **argv)
{
  using namespace swarm;
  srand(42u);    // Seed random number generator, so output is reproducible

  // Get name of configuration file
  if(argc != 2)
    {
      std::cerr << "Usage: " << argv[0] << " <integrator.cfg>\n";
      return -1;
    }
  std::string icfgfn = argv[1];

  config cfg;
  load_config(cfg,icfgfn);

  std:: cerr << "Initialize the library\n";
  swarm::init(cfg);

  std:: cerr << "Initialize the GPU integrator\n";
  std::auto_ptr<integrator> integ_gpu(integrator::create(cfg));
  
  std::cerr << "# Initialize ensemble on host to be used with GPU integration.\n";
  unsigned int nsystems = cfg.count("num_systems") ? atoi(cfg.at("num_systems").c_str()) : 1024;
  unsigned int nbodyspersystem = cfg.count("num_bodies") ? atoi(cfg.at("num_bodies").c_str()) : 3;
  cpu_ensemble ens(nsystems, nbodyspersystem);
  
  std::cerr << "Set initial conditions on CPU.\n";
  set_initial_conditions_for_demo(ens,cfg);
  
  std::vector<double> energy_init(nsystems), energy_final(nsystems);
  ens.calc_total_energy(&energy_init[0]);
  
  // Print initial conditions on CPU for use w/ GPU
  std::cerr << "Print selected initial conditions for upload to GPU.\n";
  print_selected_systems_for_demo(ens);
  
#if PARANOID_CPU_CHECK
  std::cerr << "Create identical ensemble on host to check w/ CPU.\n";
  cpu_ensemble ens_check(ens);
  
  // Print initial conditions for checking w/ CPU 
  std::cerr << "Print selected initial conditions for CPU.\n";
  print_selected_systems_for_demo(ens_check);	
#endif
  
  std::cerr << "Set integration duration for all systems.\n";
  double dT = 10.;
  get_config(dT,cfg,"integration end");
  dT *= 2.*M_PI;
  ens.set_time_end_all(dT);
  ens.set_time_output_all(1, 1.01*dT);	// time of next output is after integration ends

  // Perform the integration on gpu
  std::cerr << "Upload data to GPU.\n";
  gpu_ensemble gpu_ens(ens);
  std::cerr << "Integrate ensemble on GPU.\n";
  integ_gpu->integrate(gpu_ens, dT);				
  std::cerr << "Download data to host.\n";
  ens.copy_from(gpu_ens);					
  std::cerr << "GPU integration complete.\n";
  
#if PARANOID_CPU_CHECK
  // Perform the integration on the cpu
  std:: cerr << "Initialize the CPU integrator\n";
  cfg["integrator"] = "cpu_hermite";
  std::auto_ptr<integrator> integ_cpu(integrator::create(cfg));
  std::cerr << "Integrate a copy of ensemble on CPU to check.\n";
  integ_cpu->integrate(ens_check, dT);				
  std::cerr << "CPU integration complete.\n";
#endif
  
  // Check Energy conservation
  ens.calc_total_energy(&energy_final[0]);
  double max_deltaE = 0;
  for(int sysid=0;sysid<ens.nsys();++sysid)
    {
      double deltaE = (energy_final[sysid]-energy_init[sysid])/energy_init[sysid];
      if(fabs(deltaE)>max_deltaE)
	{ max_deltaE = fabs(deltaE); }
      if(fabs(deltaE)>0.00001)
	std::cout << "# Warning: " << sysid << " dE/E= " << deltaE << '\n';
    }
  std::cerr << "# Max dE/E= " << max_deltaE << "\n";
  
  // Print results
  std::cerr << "Print selected results from GPU's calculation.\n";
  print_selected_systems_for_demo(ens);
#if PARANOID_CPU_CHECK
  std::cerr << "Print selected results from CPU's calculation.\n";
  print_selected_systems_for_demo(ens_check);
#endif
  // both the integrator & the ensembles are automatically deallocated on exit
  // so there's nothing special we have to do here.
  return 0;
}




void set_initial_conditions_for_demo(swarm::ensemble& ens, const swarm::config& cfg) 
{
  using namespace swarm;
  std::cerr << "Set initial time for all systems = ";
  double time_init = cfg.count("time_init") ? atof(cfg.at("time_init").c_str()) : 0.;
  ens.set_time_all(time_init);	
  std::cerr << time_init << ".\n";

  bool use_jacobi = cfg.count("use_jacobi") ? atoi(cfg.at("use_jacobi").c_str()) : 0;

  for(unsigned int sys=0;sys<ens.nsys();++sys)
    {
      // set sun to unit mass and at origin
      float mass_star = cfg.count("mass_star") ? atof(cfg.at("mass_star").c_str()) : 1.;
      double x=0, y=0, z=0, vx=0, vy=0, vz=0;
      ens.set_body(sys, 0, mass_star, x, y, z, vx, vy, vz);

      double mass_enclosed = mass_star;
      for(unsigned int bod=1;bod<ens.nbod();++bod)
	{
	  float mass_planet = draw_value_from_config(cfg,"mass",bod,0.,mass_star);
	  mass_enclosed += mass_planet;

	  double a = draw_value_from_config(cfg,"a",bod,0.001,10000.);
	  double e = draw_value_from_config(cfg,"ecc",bod,0.,1.);
	  double i = draw_value_from_config(cfg,"inc",bod,-180.,180.);
	  double O = draw_value_from_config(cfg,"node",bod,-720.,720.);
	  double w = draw_value_from_config(cfg,"omega",bod,-720.,720.);
	  double M = draw_value_from_config(cfg,"meananom",bod,-720.,720.);
	  //	  std::cout << "# Drawing sys= " << sys << " bod= " << bod << ' ' << mass_planet << "  " << a << ' ' << e << ' ' << i << ' ' << O << ' ' << w << ' ' << M << '\n';

	  i *= M_PI/180.;
	  O *= M_PI/180.;
	  w *= M_PI/180.;
	  M *= M_PI/180.;

	  double mass = use_jacobi ? mass_enclosed : mass_star+mass_planet;
	  calc_cartesian_for_ellipse(x,y,z,vx,vy,vz, a, e, i, O, w, M, mass);

	  double bx, by, bz, bvx, bvy, bvz;
	  ens.get_barycenter(sys,bx,by,bz,bvx,bvy,bvz,bod-1);
	  x  += bx;	  y  += by;	  z  += bz;
	  vx += bvx;	  vy += bvy;	  vz += bvz;
	  
	  // assign body a mass, position and velocity
	  ens.set_body(sys, bod, mass_planet, x, y, z, vx, vy, vz);
	}  // end loop over bodies

      // Shift into barycentric frame
      ens.get_barycenter(sys,x,y,z,vx,vy,vz);
      for(unsigned int bod=0;bod<ens.nbod();++bod)
	{
	  ens.set_body(sys, bod, ens.mass(sys,bod), 
		       ens.x(sys,bod)-x, ens.y(sys,bod)-y, ens.z(sys,bod)-z, 
		       ens.vx(sys,bod)-vx, ens.vy(sys,bod)-vy, ens.vz(sys,bod)-vz);	  
	}  // end loop over bodies
    } // end loop over systems
}

void print_selected_systems_for_demo(swarm::ensemble& ens)
{
  using namespace swarm;
  std::streamsize cout_precision_old = std::cout.precision();
  std::cout.precision(10);
  unsigned int nprint = std::min(10,ens.nsys());
  for(unsigned int systemid = 0; systemid< nprint; ++systemid)
    {
      std::cout << "sys= " << systemid << " time= " << ens.time(systemid) << "\n";
      double mass_enclosed = 0.;
      for(unsigned int bod=0;bod<ens.nbod();++bod)
	{
	  double mass = ens.mass(systemid,bod);
	  mass_enclosed += mass;

	  if(bod>0)
	    {
	      std::cout << "body= " << bod << ": ";
	      //	  std::cout << "pos= (" << ens.x(systemid, bod) << ", " <<  ens.y(systemid, bod) << ", " << ens.z(systemid, bod) << ") vel= (" << ens.vx(systemid, bod) << ", " <<  ens.vy(systemid, bod) << ", " << ens.vz(systemid, bod) << ").\n";

	      double bx, by, bz, bvx, bvy, bvz;
	      ens.get_barycenter(systemid,bx,by,bz,bvx,bvy,bvz,bod-1);
	      double x = ens.x(systemid,bod)-bx;
	      double y = ens.y(systemid,bod)-by;
	      double z = ens.z(systemid,bod)-bz;
	      double vx = ens.vx(systemid,bod)-bvx;
	      double vy = ens.vy(systemid,bod)-bvy;
	      double vz = ens.vz(systemid,bod)-bvz;

	      double a, e, i, O, w, M;
	      calc_keplerian_for_cartesian(a,e,i,O,w,M, x,y,z,vx,vy,vz, mass_enclosed);
	      i *= 180/M_PI;
	      O *= 180/M_PI;
	      w *= 180/M_PI;
	      M *= 180/M_PI;
	      std::cout << " a= " << a << " e= " << e << " i= " << i << " Omega= " << O << " omega= " << w << " M= " << M << "\n";
	    }

	}
    }
  std::cout.precision(cout_precision_old);
}
