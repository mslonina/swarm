/*************************************************************************
 * Copyright (C) 2011 by Saleh Dindar and the Swarm-NG Development Team  *
 *                                                                       *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 3 of the License.        *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ************************************************************************/

/*! \file euler.hpp
 *   \brief Defines and implements \ref swarm::gpu::bppt::EulerPropagator class.
 *
 */

#include "swarm/swarmplugin.h"

namespace swarm {

namespace gpu {
namespace bppt {

/*! Paramaters for EulerPropagator
 * \ingroup propagator_parameters
 *
 */
struct EulerPropagatorParams {
	double time_step;
        //! Constructor
	EulerPropagatorParams(const config& cfg){
		time_step = cfg.require("time_step", 0.0);
	}
};

/*! GPU implementation of euler propagator
 * It is of no practical use. Given here as a working example. @ref TutorialPropagator 
 * is based on this propagator.
 * 
 * \ingroup propagators
 *
 */
template<class T,class Gravitation>
struct EulerPropagator {
	typedef EulerPropagatorParams params;
	const static int nbod = T::n;

	params _params;


	//! Runtime variables
	ensemble::SystemRef& sys;
	Gravitation& calcForces;
	int b;
	int c;
	int ij;
	bool body_component_grid;
	bool first_thread_in_system;
	double max_timestep;

        //! Constructor for EulerPropagator
	GPUAPI EulerPropagator(const params& p,ensemble::SystemRef& s,
			Gravitation& calc)
		:_params(p),sys(s),calcForces(calc){}

	GPUAPI void init()  { }

	GPUAPI void shutdown() { }

        GPUAPI void convert_internal_to_std_coord() {} 
        GPUAPI void convert_std_to_internal_coord() {}

	static GENERIC int thread_per_system(){
		return nbod * 3;
	}

	static GENERIC int shmem_per_system() {
		 return 0;
	}

	__device__ bool is_in_body_component_grid()
//        { return body_component_grid; }	
        { return  ((b < nbod) && (c < 3)); }	

	__device__ bool is_in_body_component_grid_no_star()
//        { return ( body_component_grid && (b!=0) ); }	
        { return ( (b!=0) && (b < nbod) && (c < 3) ); }	

	__device__ bool is_first_thread_in_system()
//        { return first_thread_in_system; }	
        { return (thread_in_system()==0); }	

        //! Advance the timesteps
	GPUAPI void advance(){
		double h = min(_params.time_step, max_timestep);
		double pos = 0.0, vel = 0.0;
		double acc = 0.0, jerk = 0.0;
		const double third = 1.0/3.0;
		
		if( is_in_body_component_grid() )
			pos = sys[b][c].pos() , vel = sys[b][c].vel();


		calcForces(ij,b,c,pos,vel,acc,jerk);
		//! Integration
		pos = pos +  h*(vel+(h*0.5)*(acc+(h*third)*jerk));
		vel = vel +  h*(acc+(h*0.5)*jerk);


		//! Finalize the step
		if( is_in_body_component_grid() )
			sys[b][c].pos() = pos , sys[b][c].vel() = vel;
		if( is_first_thread_in_system() ) 
			sys.time() += h;
	}
};
  
}
}
}
