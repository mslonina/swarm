#include "swarm.h"
#include <cux/cux.h>
#include <astro/util.h>
#include <cassert>

////////// Utilities
#if __DEVICE_EMULATION__
	extern "C" void debug_hook();
#else
	#define debug_hook()
#endif

/*!
  \brief Computes the global linear ID of the thread. Used from kernels.

  NOTE: Supports 3D grids with 1D blocks of threads
  @return threadId
*/
inline __device__ uint32_t threadId()
{
// This will be in inner loops, so may want to optimize
#if USE_1D_GRID
	const uint32_t id = blockIdx.x * blockDim.x + threadIdx.x;
#else
	const uint32_t id = ((blockIdx.z * gridDim.y + blockIdx.y) * gridDim.x + blockIdx.x) * blockDim.x + threadIdx.x;
#endif
	return id;
}

/*!
  \brief Computes the global linear ID of the thread. Used from kernels.

  NOTE: Supports 3D grids with 1D blocks of threads
*/
inline __device__ uint32_t threadsPerBlock()
{
#if USE_1D_GRID
	return blockDim.x;
#else
	return blockDim.x * blockDim.y * blockDim.z;
#endif
}

#include "swarmlog.h"

namespace swarm {

/*!
  \brief compute and store the number of systems that remain active

  NOTE: assumes not more than MAXTHREADSPERBLOCK threads per block
  NOTE: assumes a nthreads is a power of 2
  NOTE: assumes *nrunning = 0 on input
 @param[out] nrunning number of active systems
 @param[in] ens ensemble
*/
static const int MAXTHREADSPERBLOCK = 256;
__device__ void count_running(int *nrunning, double *Tstop, ensemble &ens)
{
#if 0
	// Direct counting (via atomicAdd) of the number of running threads. Slower
	// but requires no shared memory.
	// TODO: Test if this implementation is fast enough to be activated
	int sys = threadId();
	int running = sys < ens.nsys() ? !(ens.flags(sys) & ensemble::INACTIVE || ens.time(sys) >= Tstop[sys]) : 0;
	if(running) { atomicAdd(nrunning, 1); }
	return;
#else
	__shared__ int running[MAXTHREADSPERBLOCK];	// takes up 1k of shared memory (for MAXTHREADSPERBLOCK=256)

	int sys = threadId();
	const int widx = sys % MAXTHREADSPERBLOCK;
	running[widx] = sys < ens.nsys() ? !(ens.flags(sys) & ensemble::INACTIVE || ens.time(sys) >= Tstop[sys]) : 0;

	// Prefix sum algorithm (assumes block size <= MAXTHREADSPERBLOCK).
	// 1) sum up the number of running threads in this block
	int tpb = threadsPerBlock();
	for(int i = 2; i <= tpb; i *= 2)
	{
		 __syncthreads();
		if(widx % i) { running[widx] += running[widx + i/2]; }
	}

	// 2) add the sum to the total number of running threads
	if(widx == 0)
	{
		atomicAdd(nrunning, running[0]);
	}
#endif
}

#if 0
/*!
  \brief Standalone kernel for counting the number of active ensembles in a GPU ensemble

 @param[out] nrunning number of active systems 
 @param[in] ens ensemble
*/
__global__ void get_nactive_kernel(int *nrunning, ensemble ens)
{
	count_running(nrunning, ens);
}

TODO: Need to dynamically compute grid size for this to work properly.
      Do not enable until this is done.
int gpu_ensemble::get_nactive() const
{
	if(nactive_gpu)
	{
		cudaMalloc((void**)&nactive_gpu, sizeof(*nactive_gpu));
	}

	cudaMemset(nactive_gpu, 0, sizeof(*nactive_gpu));
	get_nactive_kernel<<<60, 64>>>(nactive_gpu, *this);

	// fetch the result
	int nrunning;
	memcpyToHost(&nrunning, nactive_gpu, 1);
	return nrunning;
}
#endif
	
#if 0  // compiler says not allowed, need to figure out
__device__ void gpu_ensemble::set_time_end_all_kernel(const real_time tend) 	
{
int sys = threadId();
if(sys>= nsys()) { return; }
time_end(sys) = tend;
}

void gpu_ensemble::set_time_end_all(const real_time tend) 	
{
  set_time_end_all_kernel<<<60, 64>>>(tend);
}
#endif



///////////////////
//// Generic versatile integrator template framework
///////////////////

#define MAX_GPU_ENSEMBLES 4
__constant__ ensemble gpu_integ_ens[MAX_GPU_ENSEMBLES];

/// generic GPU integrator return value
struct retval_t
{
	int nrunning;
};

__device__ bool needs_output(ensemble &ens, double T, int sys)
{
	// simple output
	return T >= ens.time_output(sys, 0);
}

template<typename L>
__device__ void output_system(L &log, ensemble &ens, double T, int sys)
{
	// store the snapshot
	log::system(log, ens, sys, T);

	// set next stopping time
	ens.time_output(sys, 0) += ens.time_output(sys, 1);
}

/*!
 \brief output if needed
 
 ...
 @param[out] log
 @param[in] ens
 @param[in] T
 @param[in] sys
*/
template<typename L>
__device__ void output_if_needed(L &log, ensemble &ens, double T, int sys)
{
	// simple output
	if(needs_output(ens, T, sys))
	{
//		debug_hook();

		output_system(log, ens, T, sys);
	}
}

/*!
 \brief generic integrate system  
 
 ...
 ...
 @param[out] retval
 @param[in,out] ens
 @param[in] sys
 @param[in] max_steps
 @param[in,out] H
 @param[in,out] stop
*/
template<typename stopper_t, typename propagator_t>
__device__ void generic_integrate_system(retval_t *retval, ensemble &ens, int sys, int max_steps, propagator_t &H, stopper_t &stop, double Tstop)
{
	// initialize propagator and stopper per-thread states
	double T = ens.time(sys);
	typename stopper_t::thread_state_t    stop_ts(stop, ens, sys, T, Tstop);
	typename propagator_t::thread_state_t    H_ts(H, ens, sys, T, Tstop);

	// advance the system until we reach max_steps, Tstop, or stop becomes true
	unsigned int step = 0;
	while(true)
	{
		// stopping conditions
		if(T >= Tstop) 				{ if(T >= ens.time_end(sys)) { ens.flags(sys) |= ensemble::INACTIVE; } break; }
		if(stop(stop_ts, ens, sys, step, T)) 	{ ens.flags(sys) |= ensemble::INACTIVE; break; }
		if(step == max_steps) 			{ break; }

		output_if_needed(dlog, ens, T, sys);

		// actual work
		T = H.advance(ens, H_ts, sys, T, Tstop, stop, stop_ts, step);

		step++;
	}
	ens.nstep(sys) += step;
	output_if_needed(dlog, ens, T, sys);

	ens.time(sys) = T;
}

/*!
 \brief gpu integrate driver 
 
 ...
 @param[out] retval
 @param[in] max_steps
 @param[in] H
 @param[in] stop
 @param[in] gpu_ensemble_id
*/
template<typename stopper_t, typename propagator_t>
__global__ void gpu_integ_driver(retval_t *retval, int max_steps, propagator_t H, stopper_t stop, const int gpu_ensemble_id, real_time *Tstop, double dT)
{
	debug_hook();

	// Need to test that don't get bogus gpu_ensemble_id
 	if((gpu_ensemble_id<0)||(gpu_ensemble_id>=MAX_GPU_ENSEMBLES))
		// Is this proper way to bail from GPU?
 		{ retval->nrunning = -1; return; }	

	// find the system we're to work on
	ensemble &ens = gpu_integ_ens[gpu_ensemble_id];
	int sys = threadId();

#if __DEVICE_EMULATION__
	if(sys == 2)
	{
		printf("sys=%d T=%g Tend=%g Tstop=%g dT=%g flags=%d\n", sys, ens.time(sys), ens.time_end(sys), Tstop[sys], dT, ens.flags(sys));
	}
#endif
	if(sys < ens.nsys() && !(ens.flags(sys) & ensemble::INACTIVE))
	{
		if(dT != 0)
		{
			// if dT != 0, recompute the stop time in Tstop array as the current 
			// time + dT, making sure not to exceed the system end time, ens.time_end().

			Tstop[sys] = min(ens.time_end(sys), ens.time(sys) + dT);
		}

		generic_integrate_system(retval, ens, sys, max_steps, H, stop, Tstop[sys]);
	}

#if __DEVICE_EMULATION__
	if(sys == 2)
	{
		printf("sys=%d T=%g Tend=%g Tstop=%g dT=%g flags=%d\n", sys, ens.time(sys), ens.time_end(sys), Tstop[sys], dT, ens.flags(sys));
	}
#endif

	count_running(&retval->nrunning, Tstop, ens);
	
#if __DEVICE_EMULATION__
	if(sys == 0)
	{
		printf("nrunning=%d\n", retval->nrunning);
	}
#endif
}


/*!
 \brief gpu generic integrate class
 
 ...
*/
template<typename stopper_t, typename propagator_t>
class gpu_generic_integrator : public integrator
{
protected:
	stopper_t	stop;
	propagator_t	H;

	int gpu_ensemble_id;
	int steps_per_kernel_run;

	dim3 gridDim;
	int threadsPerBlock;

	cuxDeviceAutoPtr<retval_t> retval_gpu;	// temp variable for return values (gpu pointer)

public:
	gpu_generic_integrator(const config &cfg);

public:
	void integrate(gpu_ensemble &ens, double T);
};

/*!
 \brief gpu integrate 
 
 ...
 @param[out] ens
 @param[in] dT
*/
template<typename stopper_t, typename propagator_t>
void gpu_generic_integrator<stopper_t, propagator_t>::integrate(gpu_ensemble &ens, double dT)
{
	// Upload the kernel parameters
	if(ens.last_integrator() != this)
	{
		ens.set_last_integrator(this);
		configure_grid(gridDim, threadsPerBlock, ens.nsys());

		// upload ensemble
		assert((gpu_ensemble_id>=0)&&(gpu_ensemble_id<MAX_GPU_ENSEMBLES));
		cudaMemcpyToSymbol(gpu_integ_ens[gpu_ensemble_id], &ens, sizeof(gpu_integ_ens[gpu_ensemble_id]));

		// initialize propagator, stopping condition
		H.initialize(ens);
		stop.initialize(ens);

		if(dT == 0.) { return; }
	}

	// flush CPU/GPU logs
	log::flush(log::memory | log::if_full);

	// initialize stop-time temporary array
	cuxDeviceAutoPtr<real_time> Tstop(ens.nsys());

	// execute the kernel in blocks of steps_per_kernel_run timesteps
	int nactive0 = -1;
	int iter = 0;
	do
	{
//		lprintf(hlog, "Starting kernel run #%d\n", iter);
//		lprintf(hlog, "Another unnecessary message from the CPU side\n");

		retval_gpu.memset(0);
		gpu_integ_driver<typename stopper_t::gpu_t, typename propagator_t::gpu_t><<<gridDim, threadsPerBlock>>>(retval_gpu, steps_per_kernel_run, H, stop, gpu_ensemble_id, Tstop, iter == 0 ? dT : 0.);
		cuxErrCheck( cudaThreadSynchronize() );
		iter++;

		// flush CPU/GPU logs
		log::flush(log::memory | log::if_full);

		retval_t retval;
		retval_gpu.get(&retval);
		if(nactive0 == -1) { nactive0 = retval.nrunning; }

		// check if we should compactify or stop
		if(retval.nrunning == 0)
		{
			break;
		}
		if(retval.nrunning - nactive0 > 128)
		{
			// TODO: compactify here
			nactive0 = retval.nrunning;
		}
	} while(true);
//	lprintf(hlog, "Exiting integrate");

	log::flush(log::memory);
}

/// check a parameter is power of two
inline bool is_power_of_two(int x) { return !(x & (x-1)); }

/*!
 \brief gpu generic integrator 
 
 ...
 @param[in] cfg 
*/
template<typename stopper_t, typename propagator_t>
gpu_generic_integrator<stopper_t, propagator_t>::gpu_generic_integrator(const config &cfg)
	: H(cfg), stop(cfg), retval_gpu(1)
{
	steps_per_kernel_run = cfg.count("steps per kernel run") ? static_cast<int>(std::floor(atof(cfg.at("steps per kernel run").c_str()))) : 100;
	threadsPerBlock = cfg.count("threads per block") ? atoi(cfg.at("threads per block").c_str()) : 64;
	gpu_ensemble_id = cfg.count("gpu_ensemble_id") ? atoi(cfg.at("gpu_ensemble_id").c_str()) : 0;
	
	// test the number of threads for validity
	if(threadsPerBlock <= 0) { ERROR("'threads per block' must be greater than zero (currently, " + str(threadsPerBlock) + ")"); }
	if(!is_power_of_two(threadsPerBlock)) { ERROR("'threads per block' must be a power of two (currently, " + str(threadsPerBlock) + ")"); }
	if(threadsPerBlock > MAXTHREADSPERBLOCK) { ERROR("'threads per block' cannot be greater than " + str(MAXTHREADSPERBLOCK) + " (currently, " + str(threadsPerBlock) + ")"); }
}

} // end namespace swarm

///////////////
//// Some generically useful force/jerk computation functions
///////////////
#include "ThreeVector.hpp"

namespace swarm {
/*!
  Calculate acceleration and jerk for the system, storing the outputs into arrays aa, jj
  Is the ordering of dimensions what we want?

	NOTE: The second loop goes from (nbod..0], which is the optimal choice
	from numerical precision standpoint if bod=0 is the most massive
	in the system.

  @param[in] ens
  @param[in] sys
  @param[out] aa
  @param[out] jj
*/
template<typename real>
__device__ void compute_acc_jerk(ensemble &ens, const int sys, const cuxDevicePtr<real, 3> &aa, const cuxDevicePtr<real, 3> &jj)
{
	typedef ThreeVector<real> V3;

	for ( unsigned int i=0;i<ens.nbod();++i )
	{
		V3 xi( ens.x ( sys,i ), ens.y ( sys,i ), ens.z ( sys,i ) );
		V3 vi( ens.vx ( sys,i ),ens.vy ( sys,i ),ens.vz ( sys,i ) );
		V3 ai(0.), ji(0.);
		for (int j=ens.nbod()-1; j >= 0; j--)
		{
			if ( j==i ) continue; // Ignore body interacting with itself
			V3 dx(ens.x(sys,j), ens.y(sys,j), ens.z(sys,j));  dx -= xi;
			V3 dv(ens.vx(sys,j),ens.vy(sys,j),ens.vz(sys,j)); dv -= vi;
			real r2 = dx.MagnitudeSquared();
			real rv = dot ( dx,dv );
			real rinv = 1./sqrt ( r2 );
			rv *= 3./r2;
			rinv *= ens.mass ( sys,j );
			real rinv3 = rinv/r2;

			dx *= rinv3;
			ai += dx;
			dv *= rinv3;
			ji += dv;
			dx *= rv;
			ji -= dx;
		}

		aa ( sys, i, 0 ) = ai.X();
		aa ( sys, i, 1 ) = ai.Y();
		aa ( sys, i, 2 ) = ai.Z();
		jj ( sys, i, 0 ) = ji.X();
		jj ( sys, i, 1 ) = ji.Y();
		jj ( sys, i, 2 ) = ji.Z();
	} // end loop over bodies
}

/*!
  Calculate accelerations for the system, storing the output into array aa

   NOTE: The second loop goes from (nbod..0], which is the optimal choice
   from numerical precision standpoint if bod=0 is the most massive
   in the system.

  @param[in] ens
  @param[in] sys
  @param[out] aa
*/
template<typename real>
__device__ void compute_acc(ensemble &ens, const int sys, const cuxDevicePtr<real, 3> &aa)
{
	typedef ThreeVector<real> V3;

	for ( unsigned int i=0;i<ens.nbod();++i )
	{
		V3 xi( ens.x ( sys,i ), ens.y ( sys,i ), ens.z ( sys,i ) );
		V3 ai(0.);
		for (int j=ens.nbod()-1; j >= 0; j--)
		{
			if ( j==i ) continue; // Ignore body interacting with itself

			V3 dx(ens.x(sys,j), ens.y(sys,j), ens.z(sys,j));  dx -= xi;
			real r2 = dx.MagnitudeSquared();
			real rinv = 1./sqrt ( r2 );
			rinv *= ens.mass ( sys,j );
			real rinv3 = rinv/r2;

			dx *= rinv3;
			ai += dx;
		}
		aa ( sys, i, 0 ) = ai.X();
		aa ( sys, i, 1 ) = ai.Y();
		aa ( sys, i, 2 ) = ai.Z();
	} // end loop over bodies
}

} // end namespace swarm
