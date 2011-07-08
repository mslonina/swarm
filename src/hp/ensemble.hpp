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

#pragma once

#include "allocators.hpp"
#include "datatypes.hpp"

namespace swarm {
namespace hp {

const int ENSEMBLE_WARPSIZE = 16;

typedef long double_int;

/**
 *
 * TODO: use allocator template parameter
 */
template< int _WARPSIZE>
class EnsembleBase {
	public:
	static const int WARPSIZE = _WARPSIZE;

	struct Body {
		struct Component {
			double _pos[WARPSIZE];
			double _vel[WARPSIZE];
			GPUAPI double& pos() { return _pos[0]; } 
			GPUAPI double& vel() { return _vel[0]; } 
			GPUAPI const double& pos() const { return _pos[0]; } 
			GPUAPI const double& vel() const { return _vel[0]; } 
		} component[3];

		double _mass[WARPSIZE];

		// Accessors 
		GPUAPI double& mass() { return _mass[0];  }
		GPUAPI const double& mass() const { return _mass[0];  }
		GPUAPI Component& operator[] (const int & i) { return component[i]; };
		GPUAPI const Component& operator[] (const int & i) const { return component[i]; };
	};

	struct Sys {
		double _time[WARPSIZE];
		double_int _active[WARPSIZE];
		GPUAPI double& time() { return _time[0];  }
		GPUAPI double_int& active() { return _active[0];  }
	};




	struct SystemRef {
		const int _nbod;
		Body* _body;
		Sys* _sys;

		// Constructor
		GPUAPI SystemRef(const int& nbod,Body* body,Sys* sys):_nbod(nbod),_body(body),_sys(sys){}

		// Accessor
		GPUAPI Body& operator[](const int & i ) const { return _body[i]; };
		GPUAPI double& time() const { return _sys[0].time(); }
		GPUAPI double_int& active() const { return _sys[0].active(); }
		GPUAPI const int& nbod()const{ return _nbod;	}

		GPUAPI double distance_squared_between(const int& i , const int & j ) {
			const Body& b1 = _body[i], & b2 = _body[j];
			return sqr(b1[0].pos()-b2[0].pos())
				+ sqr(b1[1].pos()-b2[1].pos())
				+ sqr(b1[2].pos()-b2[2].pos());
		}
	};

	//! Constant encapsulation of SystemRef
	struct SystemRefConst {
		SystemRef _ref;

		// Constructor
		GPUAPI SystemRefConst(const SystemRef& ref):_ref(ref){}

		// Accessor
		GPUAPI const Body& operator[](const int & i ) const { return _ref[i]; }
		GPUAPI const double& time() { return _ref.time(); }
		GPUAPI const double_int& active() { return _ref.active(); }
		GPUAPI const int& nbod()const{ return _ref.nbod();	}
		GPUAPI double distance_squared_between(const int& i , const int & j ) { return _ref.distance_squared_between(i,j); }
	};

	GPUAPI static size_t body_element_count(const int& nbod,const int& nsys){
		return (nsys + WARPSIZE) / WARPSIZE * nbod ;
	}

	GPUAPI static size_t sys_element_count(const int& nsys){
		return (nsys + WARPSIZE) / WARPSIZE ;
	}

	protected:
	int _nbod;
	int _nsys;
	CoalescedStructArray< Body, double, WARPSIZE> _body;
	CoalescedStructArray< Sys, double, WARPSIZE> _sys;

	public:
	CoalescedStructArray< Body, double, WARPSIZE> &
		bodies() { return _body; }
	CoalescedStructArray< Sys, double, WARPSIZE> &
		systems() { return _sys; }


	public:
	// Constructors
	GPUAPI EnsembleBase():_nbod(0),_nsys(0),_body(0,0),_sys(0,0) {};
	GPUAPI explicit EnsembleBase(const int& nbod, const int& nsys,Body* body_array, Sys* sys_array):_nbod(nbod),_nsys(nsys),_body(body_array,body_element_count(nbod,nsys)),_sys(sys_array,sys_element_count(nsys)){}
	~EnsembleBase() { 
		// TODO: We need reference counting before using this:
		// release();
	}
	// Accessors
	GPUAPI const int& nbod()const{ return _nbod;	}
	GPUAPI const int& nsys()const{ return _nsys;	}


	/** 
	 * Accessor function for first body of a system
	 * The hierarchy is like this:
	 *    Blocks ( nsys / WARPSIZE )
	 *    Bodies ( nbod )
	 *    Warps  ( nsys % WARPSIZE )
	 *
	 *    body index should come in the middle to 
	 *    provide efficient dynamic addressing.
	 *
	 */
	GPUAPI SystemRef operator[] (const int & i) { 
		const int sysinblock= i % WARPSIZE;
		const int blockid = i / WARPSIZE;
		const int idx = blockid * _nbod * WARPSIZE +  sysinblock;
		return SystemRef(_nbod,&_body[idx], &_sys[i] ) ;
	};

	GPUAPI const SystemRefConst operator[] (const int & i) const { 
		return SystemRefConst( const_cast<EnsembleBase*>(this)->operator[](i) ) ;
	};

	//// COMPATIBILITY ACCESSORS
	
	GPUAPI double& mass(const int& sys, const int & bod){
		return operator[] ( sys )[bod].mass();
	}

	GPUAPI double& p(const int& sys, const int & bod, const int& c){
		return operator[] ( sys )[bod][c].pos();
	}

	GPUAPI double& v(const int& sys, const int & bod, const int& c){
		return operator[] ( sys )[bod][c].pos();
	}

	GPUAPI double& x(const int& sys, const int& bod ) { return p(sys,bod,0); }
	GPUAPI double& y(const int& sys, const int& bod ) { return p(sys,bod,1); }
	GPUAPI double& z(const int& sys, const int& bod ) { return p(sys,bod,2); }

	GPUAPI double& vx(const int& sys, const int& bod ) { return v(sys,bod,0); }
	GPUAPI double& vy(const int& sys, const int& bod ) { return v(sys,bod,1); }
	GPUAPI double& vz(const int& sys, const int& bod ) { return v(sys,bod,2); }

	GPUAPI double& time( const int & sys ) {
		return operator[] ( sys ).time();
	}

	//! should not be used
	GPUAPI double& time_end( const int & sys ) {
		return time(sys);
	}

	//! should not be used
	GPUAPI double& time_output( const int & sys , const int & k) {
		double x = time(sys);
		return x;
	}

	GPUAPI int& flags(const int& sys){
		return operator[] ( sys ).active();
	}

	//// Const Versions
	
	GPUAPI const double& mass(const int& sys, const int & bod)const{
		return operator[] ( sys )[bod].mass();
	}

	GPUAPI const double& p(const int& sys, const int & bod, const int& c)const{
		return operator[] ( sys )[bod][c].pos();
	}

	GPUAPI const double& v(const int& sys, const int & bod, const int& c)const{
		return operator[] ( sys )[bod][c].pos();
	}

	GPUAPI const double& x(const int& sys, const int& bod )const { return p(sys,bod,0); }
	GPUAPI const double& y(const int& sys, const int& bod )const { return p(sys,bod,1); }
	GPUAPI const double& z(const int& sys, const int& bod )const { return p(sys,bod,2); }

	GPUAPI const double& vx(const int& sys, const int& bod ) const { return v(sys,bod,0); }
	GPUAPI const double& vy(const int& sys, const int& bod ) const { return v(sys,bod,1); }
	GPUAPI const double& vz(const int& sys, const int& bod ) const { return v(sys,bod,2); }

	GPUAPI const double& time( const int & sys ) const {
		return operator[] ( sys ).time();
	}

	//! should not be used
	GPUAPI const double& time_end( const int & sys )const  {
		return time(sys);
	}

	//! should not be used
	GPUAPI const double& time_output( const int & sys , const int & k) const {
		double x = time(sys);
		return x;
	}

	GPUAPI const int& flags(const int& sys)const {
		return operator[] ( sys ).active();
	}

	GPUAPI void set_time( const int& sys, const double& time ) {
		SystemRef s = operator[] ( sys );
		s.time() = time;	
	}

	GPUAPI bool is_active(const int& sys) const { 
		return operator[] ( sys ).active();
	}
	GPUAPI bool is_inactive(const int& sys) const { 
		return ! operator[] ( sys ).active();
	}
	GPUAPI void set_active(const int& sys) { 
		operator[] ( sys ).active() = true;
	}
	GPUAPI void set_inactive(const int& sys) { 
		operator[] ( sys ).active() = false;
	}


	GPUAPI void set_body( const int& sys, const int& bod, const double& mass_planet
			, const double& x, const double & y, const double& z
			, const double&  vx, const double&  vy, const double&  vz) {
		SystemRef s = operator[] ( sys );
		s[bod][0].pos()  = x ;
		s[bod][1].pos()  = y ;
		s[bod][2].pos()  = z ;

		s[bod][0].vel() = vx ;
		s[bod][1].vel() = vy ;
		s[bod][2].vel() = vz ;

		s[bod].mass() = mass_planet;

	}
	
	GPUAPI void get_body(const int & sys, const int &  bod, double &  m
			, double& x, double & y, double& z
			, double&  vx, double&  vy, double&  vz) const {
		SystemRefConst s = operator[] ( sys );

		x = s[bod][0].pos();
		y = s[bod][1].pos();
		z = s[bod][2].pos();

		vx = s[bod][0].vel();
		vy = s[bod][1].vel();
		vz = s[bod][2].vel();

		m = s[bod].mass();
	}


	// Utilities
	//
	
	GPUAPI void get_barycenter(const int& sys, double& x, double& y, double& z, double& vx, double& vy, double& vz, const int& max_body_id = 1000) const 
	{

		x = 0.; y = 0.; z = 0.; vx = 0.; vy = 0.; vz = 0.;
		double mass_sum = 0.;
		for(int bod=0;bod<=min(nbod()-1,max_body_id);++bod)
		{
			double m = mass(sys,bod);
			x  += m* this->x(sys,bod);
			y  += m* this->y(sys,bod);
			z  += m* this->z(sys,bod);
			vx += m* this->vx(sys,bod);
			vy += m* this->vy(sys,bod);
			vz += m* this->vz(sys,bod);
			mass_sum += m;
		}
		x  /= mass_sum;
		y  /= mass_sum;
		z  /= mass_sum;
		vx /= mass_sum;
		vy /= mass_sum;
		vz /= mass_sum;
	};

	GPUAPI void   set_time_all(const double tend) 
	{
		for(int sys=0;sys<nsys();++sys)
			time(sys) = tend;
	}
	/// set all systems destination time 
	GPUAPI void   set_time_end_all(const double tend) 
	{
		for(int sys=0;sys<nsys();++sys)
			time_end(sys) = tend;
	}
	/// advance all systems time 
	GPUAPI void   advance_time_end_all(const double dur) 
	{
		for(int sys=0;sys<nsys();++sys)
			time_end(sys) += dur;
	}
	/// ... 
	GPUAPI void   set_time_output_all(int k, const double tout) 
	{ 
		for(int sys=0;sys<nsys();++sys)
			time_output(sys,k) = tout;
	}

	GPUAPI double calc_total_energy( int sys ) {
		double E = 0.;
		for (int bod1 = 0; bod1 != nbod(); bod1++)
		{
			double m1; double x1[3], v1[3];
			get_body(sys, bod1, m1, x1[0], x1[1], x1[2], v1[0], v1[1], v1[2]);
			E += 0.5 * m1 * (v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2]);

			for (int bod2 = 0; bod2 < bod1; bod2++)
			{
				double m2; double x2[3], v2[3];
				get_body(sys, bod2, m2, x2[0], x2[1], x2[2], v2[0], v2[1], v2[2]);
				double dist = sqrt((x2[0] - x1[0]) * (x2[0] - x1[0]) + (x2[1] - x1[1]) * (x2[1] - x1[1]) + (x2[2] - x1[2]) * (x2[2] - x1[2]));

				E -= m1 * m2 / dist;
			}
		}
		return E;
	}

	GPUAPI void calc_total_energy(double* E){
		for (int sys = 0; sys != nsys(); sys++)
			E[sys] = calc_total_energy(sys);
	}

};

template< int W , template<class T> class _Allocator >
struct EnsembleAlloc : public EnsembleBase<W> {
	typedef EnsembleBase<W> Base;
	typedef EnsembleAlloc Self;
	typedef typename Base::Body Body;
	typedef typename Base::Sys Sys;
	typedef _Allocator<Body> BodyAllocator;
	typedef _Allocator<Sys> SysAllocator;

	EnsembleAlloc clone() {
		Body* b = BodyAllocator::clone(Base::bodies().begin(),Base::bodies().end());
		Sys* s = SysAllocator::clone(Base::systems().begin(),Base::systems().end());
		return EnsembleAlloc(Base::nbod(),Base::nsys(),b,s);
	}

	static EnsembleAlloc create(const int& nbod, const int& nsys) {
		Body* b = BodyAllocator::alloc( Base::body_element_count(nbod,nsys) );
		Sys* s = SysAllocator::alloc( Base::sys_element_count(nsys) );
		return EnsembleAlloc(nbod,nsys,b,s);
	}

	template< class Other > 
	Other cloneTo() {
		Other o = Other::create(Base::nbod(),Base::nsys());
		copyTo(o);
		return o;
	}

	template< class Other > 
	void copyTo(Other& o){
		assert(o.nsys() == Base::nsys() && o.nbod() == Base::nbod());
		alloc_copy(BodyAllocator(),typename Other::BodyAllocator(),Base::bodies().begin(),Base::bodies().end(),o.bodies().begin());
		alloc_copy(SysAllocator(),typename Other::SysAllocator(),Base::systems().begin(),Base::systems().end(),o.systems().begin());
	}

	EnsembleAlloc(){}
	private:
	EnsembleAlloc(const int& nbod,const int& nsys, Body* b, Sys* s):Base(nbod,nsys,b,s){}
};

typedef EnsembleBase< ENSEMBLE_WARPSIZE > ensemble;

typedef EnsembleAlloc< ENSEMBLE_WARPSIZE , DefaultAllocator > defaultEnsemble;
typedef EnsembleAlloc< ENSEMBLE_WARPSIZE , DefaultAllocator > hostEnsemble;
typedef EnsembleAlloc< ENSEMBLE_WARPSIZE , DeviceAllocator > deviceEnsemble;

}
}
