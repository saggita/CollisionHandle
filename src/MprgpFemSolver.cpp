#include <FEMCollision.h>
#include <MPRGPSolver.h>
#include "MprgpFemSolver.h"
using namespace MATH;
USE_PRJ_NAMESPACE

MprgpFemSolver::MprgpFemSolver():FemSolverExt(3){

  boost::shared_ptr<FEMCollision> coll(new BVHFEMCollision);
  // boost::shared_ptr<FEMCollision> coll(new SBVHFEMCollision);
  collider = boost::shared_ptr<LinearConCollider>(new LinearConCollider(feasible_pos));

  _mesh.reset(new FEMMesh(3,coll));
  resetImplicitEuler();
  setCollK(1E5f);
  _mesh->setCellSz(1.0f);
  setSelfColl(false);
  mprgp_max_it = 1000;
  mprgp_tol = 1e-4;
  newton_inner_max_it = 1;
  newton_inner_tol = 1e-4;

  current_frame = 0;
  setTargetFold( "./tempt");
  use_simple_sim = false;
}

void MprgpFemSolver::advance(const double dt){

  FUNC_TIMER();
  buildVarOffset();
  initVelPos(dt);
  handleCollDetection();
  if(use_simple_sim){
	forwardSimple(dt);
  }else{
	forward(dt);
  }
  updateMesh(dt);
  current_frame ++;
}

void MprgpFemSolver::buildVarOffset(){}

void MprgpFemSolver::initVelPos(const double dt){}

void MprgpFemSolver::handleCollDetection(){}

void MprgpFemSolver::forward(const double dt){}

void MprgpFemSolver::forwardSimple(const double dt){}

void MprgpFemSolver::buildLinearSystem(Eigen::SparseMatrix<double> &LHS, VectorXd &RHS, const double dt){}

double MprgpFemSolver::updateVelPos(const VectorXd &new_pos, const double dt){}

void MprgpFemSolver::updateMesh(const double dt){}

void MprgpFemSolver::print()const{

  int num_var = 0;
  for(int i=0;i<_mesh->nrB();i++){
	assert(_mesh->getB(i)._system);
	num_var += _mesh->getB(i)._system->size();
  }

  INFO_LOG("number of nodes: "<<num_var/3);
  // INFO_LOG("number of tets: "<<); /// @todo
  // INFO_LOG("newton max outter it: "<<_tree.get<int>("maxIter"));
  // INFO_LOG("newton outter tol: "<<_tree.get<double>("eps"));
  INFO_LOG("newton max inner it: "<<newton_inner_max_it);
  INFO_LOG("newton inner tol: "<<newton_inner_tol);
  INFO_LOG("mprgp max it: "<<mprgp_max_it);
  INFO_LOG("mprgp tol: "<<mprgp_tol);
  INFO_LOG("use simple simulation: "<< (use_simple_sim ? "true":"false"));
  collider->print();
}

#define BLK(IN,I) (IN).block(_offVar[i],0,_offVar[i+1]-_offVar[i],1)
#define BLKL(IN,I) (IN).block(_offVarL[i],0,_offVarL[i+1]-_offVarL[i],1)
#define BLKF(IN,I) (IN).block(_offVarF[i],0,_offVarF[i+1]-_offVarF[i],1)
void FemSolverExtDebug::advance(const double dt){
  
  //initialize
  _mesh->buildOffset();

  //handle collision detection
  ostringstream oss3;
  oss3 << saveResultsTo()+"/collisions/coll_"<< currentFrame() << ".vtk";
  DebugFEMCollider coll_debug( oss3.str(), 3 );

  for(sizeType i=0; i<_mesh->nrB(); i++)
	_mesh->getB(i)._system->beforeCollision();
  Vec fE=Vec::Zero(_mesh->nrV()*3);
  Eigen::SparseMatrix<scalarD,0,sizeType> HE(_mesh->nrV()*3,_mesh->nrV()*3);
  {
	TRIPS HTrips;
	DefaultFEMCollider coll(fE,HTrips,_collK,1.0f,1.0f);
	if(_geom){
	  	DEBUG_FUN( _mesh->getColl().collideGeom( *_geom,coll_debug,true ) );
		_mesh->getColl().collideGeom(*_geom,coll,true);
	}
	if(_selfColl){
	  DEBUG_FUN( _mesh->getColl().collideMesh(coll_debug, true) );
	  _mesh->getColl().collideMesh(coll,true);
	}
	HE.setFromTriplets(HTrips.begin(),HTrips.end());
  }
  for(sizeType i=0; i<_mesh->nrB(); i++)
	_mesh->getB(i)._system->afterCollision();

  //find variable offset
  sizeType nrVar=0;
  vector<sizeType> offVar(_mesh->nrB(),0);
  for(sizeType i=0; i<_mesh->nrB(); i++) {
	FEMSystem& sys=*(_mesh->getB(i)._system);
	offVar[i]=nrVar;
	nrVar+=sys.size();
  }

  //initialize velocity and position
  Vec pos0(nrVar),vel0(nrVar),X0(_mesh->nrV()*3),X1(_mesh->nrV()*3);
  for(sizeType i=0; i<_mesh->nrB(); i++) {
	FEMSystem& sys=*(_mesh->getB(i)._system);
	Vec posB,velB,accelB,XB;
	sys.getPos(posB);
	sys.getVel(velB);
	sys.getAccel(accelB);
	_mesh->getB(i).getPos(XB);

	posB=posB+dt*velB+(1.0f-2.0f*_beta2)*dt*dt*0.5f*accelB;
	velB=velB+(1.0f-_gamma)*dt*accelB;
	sys.setPos(posB);
	sys.setVel(velB);

	pos0.block(offVar[i],0,sys.size(),1)=posB;
	vel0.block(offVar[i],0,sys.size(),1)=velB;
	X0.block(_mesh->getB(i)._offset,0,XB.rows(),1)=XB;
  }

  INFO("Newton Iteration")
    //main loop: we use Implicit Newmark Scheme
    TRIPS HTrips,UTrips;
  Vec RHS(nrVar),DELTA,pos1=pos0,vel1=vel0;
  Eigen::SparseMatrix<scalarD,0,sizeType> LHS(nrVar,nrVar),U(_mesh->nrV()*3,nrVar);
  for(sizeType i=0; i<_maxIter; i++) {
	//build LHS and RHS
	HTrips.clear();
	UTrips.clear();
	for(sizeType i=0; i<_mesh->nrB(); i++) {
	  //tell system to build: M+(beta*dt*dt)*K+(gamma*dt)*C with off
	  //tell system to build: U with offU
	  //tell system to build: M(dSn+((1-gamma)*dt)*ddSn-dS_{n+1})+(gamma*dt)*f_I-C*dS_{n+1} with offU[1]
	  FEMSystem& sys=*(_mesh->getB(i)._system);
	  Vec RHSB=Vec::Zero(sys.size());
	  Vec MRHSB=(vel0-vel1).block(offVar[i],0,sys.size(),1);
	  Vec KRHSB=Vec::Zero(sys.size());
	  Vec CRHSB=-vel1.block(offVar[i],0,sys.size(),1)*_gamma*dt;
	  sys.buildSystem(1.0f,_beta*dt*dt,_gamma*dt,HTrips,UTrips,
					  MRHSB,KRHSB,CRHSB,_gamma*dt,RHSB,offVar[i]);
	  RHS.block(offVar[i],0,sys.size(),1)=RHSB;
	}
	LHS.setFromTriplets(HTrips.begin(),HTrips.end());
	U.setFromTriplets(UTrips.begin(),UTrips.end());
	//add external term
	for(sizeType i=0; i<_mesh->nrB(); i++) {
	  Vec XB;
	  _mesh->getB(i).getPos(XB);
	  X1.block(_mesh->getB(i)._offset,0,XB.rows(),1)=XB;
	}
	RHS+=U.transpose()*(fE-HE*(X1-X0))*(_gamma*dt);
	LHS+=U.transpose()*(HE*U).eval()*(_beta*dt*dt);

	//solve
	_sol.compute(LHS);
	ASSERT_MSG(_sol.info() == Eigen::Success,"Factorization Fail!")
	  DELTA=_sol.solve(RHS);

	//update velocity and position
	vel1+=DELTA;
	pos1+=DELTA*_beta*dt/_gamma;
	for(sizeType i=0; i<_mesh->nrB(); i++) {
	  FEMSystem& sys=*(_mesh->getB(i)._system);
	  sys.setPos(pos1.block(offVar[i],0,sys.size(),1));
	  sys.setVel(vel1.block(offVar[i],0,sys.size(),1));
	}

	//exit test
	INFOV("\tResidue: %f",DELTA.cwiseAbs().maxCoeff())
	  if(DELTA.cwiseAbs().maxCoeff() < _eps)
		break;
  }

  //update acceleration
  vel1=(vel1-vel0)/(_gamma*dt);
  for(sizeType i=0; i<_mesh->nrB(); i++) {
	FEMSystem& sys=*(_mesh->getB(i)._system);
	sys.setAccel(vel1.block(offVar[i],0,sys.size(),1));
  }
  _mesh->updateMesh();

  current_frame ++;
}
#undef BLK
#undef BLKL
#undef BLKF
