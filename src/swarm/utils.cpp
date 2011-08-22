/*************
 *  Author : Saleh Dindar
 *
 *
 */
#include "common.hpp"
#include "utils.hpp"

using std::max;
using namespace swarm;
using std::string;

swarm::hostEnsemble generate_ensemble(swarm::config& cfg)  {
	int nsys = cfg.require("nsys",0);
	int nbod = cfg.require("nbod",0);

	hostEnsemble ens = hostEnsemble::create( nbod, nsys );


	for(unsigned int sys=0;sys<ens.nsys();++sys)
	{
		// set sun to unit mass and at origin
		double mass_sun = 1.;
		double x=0, y=0, z=0, vx=0, vy=0, vz=0;
		ens.set_body(sys, 0, mass_sun, x, y, z, vx, vy, vz);

		// add near-Jupiter-mass planets on nearly circular orbits
		for(unsigned int bod=1;bod<ens.nbod();++bod)
		{
			float mass_planet = 0.001; // approximately (mass of Jupiter)/(mass of sun)
			double rmag = pow(1.4,int(bod-1));  // semi-major axes exceeding this spacing results in systems are stable for nbody=3 and mass_planet=0.001
			double vmag = sqrt(mass_sun/rmag);  // spped for uniform circular motion
			double theta = (2.*M_PI*rand())/static_cast<double>(RAND_MAX);  // randomize initial positions along ecah orbit
			x  =  rmag*cos(theta); y  = rmag*sin(theta); z  = 0;
			vx = -vmag*sin(theta); vy = vmag*cos(theta); vz = 0.;

			// assign body a mass, position and velocity
			ens.set_body(sys, bod, mass_planet, x, y, z, vx, vy, vz);
		}
		ens[sys].active() = true;
		ens[sys].time() = 0;
	}
	return ens;
}

double find_max_energy_conservation_error(ensemble& ens, ensemble& reference_ensemble ) {
	std::vector<double> energy_init(reference_ensemble.nsys());
	reference_ensemble.calc_total_energy(&energy_init[0]);
	std::vector<double> energy_final(ens.nsys());
	ens.calc_total_energy(&energy_final[0]);
	double max_deltaE = 0.;
	for(int sysid=0;sysid<ens.nsys();++sysid)
	{
	
		double deltaE = fabs ((energy_final[sysid]-energy_init[sysid])/energy_init[sysid] ) ;
		max_deltaE = max(deltaE, max_deltaE);
	}
	return max_deltaE;
}


bool validate_configuration(config& cfg){
  bool valid = true;                 // Indicates whether cfg parameters are valid
  int nsystems = cfg.require("nsys",0);
  int nbodypersystem = cfg.require("nbod",0);
  int bs = cfg.optional("block size",16);

  // Check that parameters from command line are ok
  if((bs<8)||(bs>32)) valid =false;
  if(!(nsystems>=1)||!(nsystems<=32720)) valid = false;
  if(!(nbodypersystem>=3)||!(nbodypersystem<=10)) valid = false;

  return valid;
}

void outputConfigSummary(std::ostream& o,swarm::config& cfg) {
	o << "# Integrator:\t" << cfg["integrator"] << "\n"
		<< "# Time step\t" << cfg["time step"] << "\n"
		<< "# Destination time\t" << cfg["destination time"] << "\n"
		<< "# Min time step\t" << cfg["min time step"] << "\n"
		<< "# Max time step\t" << cfg["max time step"] << "\n"
		<< "# No. Systems\t" << cfg["nsys"] << "\n"
		<< "# No. Bodies\t" << cfg["nbod"] << "\n"
		<< "# Blocksize\t" << cfg["blocksize"] << "\n"
		<< std::endl;
}

config default_config() {
	config cfg;
	cfg["nsys"] = 16;
	cfg["nbod"] = 3;
	cfg["integrator"] = "hermite"; // Set to use a GPU integrator
	cfg["time step"] = "0.001";       // time step
	cfg["nbod"] = "3";
	cfg["nsys"] = "16";
	cfg["blocksize"] = "16";
	cfg["log writer"] = "null";
	return cfg;
}
std::ostream& operator << (std::ostream& o, const swarm::ensemble::range_t& r){
	if(r.min-r.max < 1e-11) 
		return o << r.average;
	else
		return o << r.average << "[" << (r.min-r.average) << "," << (r.max-r.average) << "] ";
}

bool compare_ensembles( swarm::ensemble& e1, swarm::ensemble &e2 , double & pos_diff, double & vel_diff, double & time_diff ) {
	if (e1.nsys() != e2.nsys() || e1.nbod() != e2.nbod() ) return false;

	pos_diff = vel_diff = time_diff = 0;

	for(int i = 0; i < e1.nsys(); i++) {
		for(int j = 0; j < e1.nbod() ; j++){

			double dp = sqrt( 
					  sqr ( e1[i][j][0].pos() - e2[i][j][0].pos() ) 
					+ sqr ( e1[i][j][1].pos() - e2[i][j][1].pos() ) 
					+ sqr ( e1[i][j][2].pos() - e2[i][j][2].pos() ) ) ;

			double dv = sqrt( 
					  sqr ( e1[i][j][0].vel() - e2[i][j][0].vel() ) 
					+ sqr ( e1[i][j][1].vel() - e2[i][j][1].vel() ) 
					+ sqr ( e1[i][j][2].vel() - e2[i][j][2].vel() ) ) ;

			if ( dp > pos_diff ) pos_diff = dp;
			if ( dv > vel_diff ) vel_diff = dv;

		}

		double dt = fabs(e1[i].time() - e2[i].time());
		if ( dt > time_diff ) time_diff = dt;

	}
	return true;
}
