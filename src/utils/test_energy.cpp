//
// C++ Implementation: swarm_test_energy
//
// Author: Mario Juric <mjuric@cfa.harvard.edu>, (C) 2010
//
// Copyright: See COPYING file that comes with this distribution
//

/*! \file test_energy.cpp
 *  \brief Program to computechange in energy during integration
 *
 * Used binary files generated by swarm GPU logging subsystem for input.
 * See docs/swarm_test_energy.html for more information
 *
 * usage: swarm_test_energy datafile
 *
*/

#include "swarm/swarm.h"
#include "swarm/io.hpp"
#include "swarm/log.hpp"

#include <fstream>

// aux class to sort indices by energy error (biggest first)
struct energy_sorter
{
	const std::valarray<double> &dE;

	energy_sorter(const std::valarray<double> &dE_) : dE(dE_) {};
	bool operator()(const int  &a, const int  &b) const
	{
		double dEa = fabs(dE[a]);
		double dEb = fabs(dE[b]);
		return dEa > dEb;
	}
};

// aux class to sort indices by time (smallest first)
struct time_sorter
{
	swarm::ensemble &ens;

	time_sorter(swarm::ensemble &ens_) : ens(ens_) {};
	bool operator()(const int  &a, const int  &b) const
	{
		return ens.time(a) < ens.time(b);
	}
};

// just a simple dump to stdout of one system
void write_output(const swarm::cpu_ensemble &ens, const int sys, std::valarray<double>  &Eold, std::valarray<double> &Enew)
{
	for (int bod = 0; bod != ens.nbod(); bod++)
	{
		double m; double x[3], v[3];
		ens.get_body(sys, bod, m, x[0], x[1], x[2], v[0], v[1], v[2]);

		double dEoverE = (Enew[sys] - Eold[sys]) / Eold[sys];
		printf("%5d %5d  T=%f  m=%f  pos=(% 9.5f % 9.5f % 9.5f)  vel=(% 9.5f % 9.5f % 9.5f)  E=%g  dE/E=%g\n", sys, bod, ens.time(sys), m, x[0], x[1], x[2], v[0], v[1], v[2], Enew[sys], dEoverE);
	}
}

int main(int argc, char **argv)
{
	using namespace swarm;
	cpu_ensemble ens;

	if(argc != 2)
	{
		std::cerr << "Usage: " << argv[0] << " <datafile>\n";
		return -1;
	}
	std::string datafile = argv[1];

	swarmdb in(datafile);
	swarmdb::snapshots snaps = in.get_snapshots(swarm::ALL);
	snaps.next(ens);
	unsigned int nprint = std::min(2, ens.nsys());

	// Calculate energy at beginning of integration
	std::valarray<double> Einit(ens.nsys()), Efinal(ens.nsys());
	ens.calc_total_energy(&Einit[0]);

	printf("Snapshot #0 (initial conditions):\n");
	for (unsigned int i = 0;i < nprint;++i)
		write_output(ens, i, Einit, Einit);

	// find the final snapshot
	int cnt = 0;
	while(snaps.next(ens)) { cnt++; }

	// Calculate energy at end of integration
	ens.calc_total_energy(&Efinal[0]);

	// write output
	printf("\nSnapshot #%d (end of simulation)\n", cnt);
	for (unsigned int i = 0;i < nprint;++i)
		write_output(ens, i, Einit, Efinal);

	// find systems with worst E conservation
	std::valarray<double> dEoverE = Efinal / Einit - 1.;
	std::vector<int > idx; idx.reserve(ens.nsys());
	for (int i = 0; i != ens.nsys(); i++) idx.push_back(i);
	std::stable_sort(idx.begin(), idx.end(), energy_sorter(dEoverE));
	printf("\nSystems with worst energy conservation:\n");
	for (unsigned int i = 0;i < nprint;++i)
		write_output(ens, idx[i], Einit, Efinal);

	// find systems with smallest end-of-integration time
	std::stable_sort(idx.begin(), idx.end(), time_sorter(ens));
	printf("\nSystems that ended earliest:\n");
	for (unsigned int i = 0;i < nprint;++i)
		write_output(ens, idx[i], Einit, Efinal);

	return 0;
}
