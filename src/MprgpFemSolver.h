#ifndef _MPRGPFEMSOLVER_H_
#define _MPRGPFEMSOLVER_H_

#include <boost/filesystem.hpp>
#include <FEMSystem.h>
#include <MPRGPSolver.h>
#include "LinearConCollider.h"
using namespace MATH;
USE_PRJ_NAMESPACE

class FemSolverExt:public FEMSolver{

public:
  enum COLL_DETECTION_TYPE {DCD, CCD};
	
public:
  FemSolverExt(const sizeType cOption=2): FEMSolver(3, cOption){

	_tree.put<bool>("denseSystem",false);
	current_frame = 0;
	setTargetFold( "./tempt");
	solver_name = "FemSolverExt";
	debug_coll = false;
	use_iterative_solver = true;
	collideGround(false);
	coll_type = DCD;
  }
  virtual void init(){}
  void setCollDetectionType(const COLL_DETECTION_TYPE type){
	coll_type = type;
  }
  void setVel(const Vector3d &vel, const int body_id);
  void setTargetFold(const string &fold_for_saving_results){
	save_results_to = fold_for_saving_results;
	boost::filesystem::create_directory(save_results_to);
  }
  void collideGround(const bool coll, const double ground_y=0, const double delta_y=0){
	this->collide_ground = coll;
	this->ground_y = ground_y;
	this->delta_y = delta_y;
  }

  virtual void advance(const double dt);
  virtual void setLinearSolverParameters(const double mprgp_tol, const int mprgp_it){}
  virtual void setFriction(const double mu_s, const double mu_k){}

  const string &saveResultsTo()const{
	return save_results_to;
  }
  int currentFrame()const{
	return current_frame;
  }
  virtual void print()const{

	int num_var = 0;
	for(int i = 0; i < _mesh->nrB(); i++){
	  assert(_mesh->getB(i)._system);
	  num_var += _mesh->getB(i)._system->size();
	}
	INFO_LOG("FEM Solver: "<< solver_name);
	INFO_LOG("number of nodes: "<<num_var/3);
	// INFO_LOG("number of tets: "<<); /// @todo
	INFO_LOG("newton it: "<<_tree.get<int>("maxIter"));
	INFO_LOG("newton tol: "<<_tree.get<double>("eps"));
	INFO_LOG("debug collision: "<< (debug_coll ? "true":"false"));
	INFO_LOG("use iterative solver: "<< (use_iterative_solver ? "true":"false"));
  }

protected:
  void solve(const FEMSystemMatrix &LHS, const Vec &RHS, Vec &DELTA);

protected:
  string solver_name;
  int current_frame;
  string save_results_to;
  bool debug_coll;
  bool use_iterative_solver;
  SparseMatrix<double> LHS_mat;
  bool collide_ground;
  double ground_y, delta_y;
  COLL_DETECTION_TYPE coll_type;
};

class MprgpFemSolver:public FemSolverExt{

public:
  MprgpFemSolver(const int cOption=2);
  virtual void init(){
	FemSolverExt::init();
	ccd_collider = boost::shared_ptr<ContinueCollider>(new ContinueCollider(this->getMesh(), this->_geom));
	ccd_collider->init();
  }
  void advance(const double dt);
  void setLinearSolverParameters(const double mprgp_tol, const int mprgp_it){
	assert_gt(mprgp_it, 0);
	assert_gt(mprgp_tol, 0.0);
	this->mprgp_max_it = mprgp_it;
	this->mprgp_tol = mprgp_tol;
  }
  void setFriction(const double mu_s, const double mu_k){
	// collider->setFriction(mu_s, mu_k);
  }

  const VVVec4d &getLinearCon()const{return dcd_collider->getLinearCon();}
  void print()const;

protected:
  virtual void forward(const double dt){
	ERROR_LOG("undefined function: virtual void MprgpFemSolver::forward(const double dt)");
  }
  virtual void forwardWithoutColl(const double dt);

  void buildVarOffset();
  void initPos(const double dt);
  void CCDhandle(const double dt);
  void DCDhandle();
  void handleCollDetection(const double dt);
  void getCollConstraints(SparseMatrix<double> &J, VectorXd &c)const;
  void initVel(const double dt);
  double updatePos();
  void updateMesh(const double dt);
  void buildLinearSystem(Eigen::SparseMatrix<double> &LHS, VectorXd &RHS, const double dt);
  void saveQP(const SparseMatrix<double> &J,const VectorXd &c,const VectorXd &RHS)const{
	ostringstream oss;
	oss << saveResultsTo()+"/QP/qp"<< currentFrame() << ".b";
	writeQP(LHS_mat, RHS, J, c, x1, oss.str());
  }

protected:
  boost::shared_ptr<LinearConCollider> dcd_collider;
  boost::shared_ptr<ContinueCollider> ccd_collider;
  VectorXd x0, x1, X0, X1, PHI, PSI, new_pos;
  double mprgp_tol;
  int mprgp_max_it;
};

class MoseckFemSolver:public MprgpFemSolver{

public:
  MoseckFemSolver(const int cOption=2):MprgpFemSolver(cOption){
	solver_name = "MoseckFemSolver";
  }

protected:
  void forward(const double dt);

};

class ICAFemSolver:public MprgpFemSolver{

public:
  ICAFemSolver(const int cOption=2):MprgpFemSolver(cOption){
	solver_name = "ICAFemSolver";
  }

protected:
  void forward(const double dt);

};

class DecoupledMprgpFemSolver:public MprgpFemSolver{

public:
  DecoupledMprgpFemSolver(const int cOption=2):MprgpFemSolver(cOption){
	dcd_collider->setDecoupleConstraints(true);
	solver_name = "decoupled mprgp";
  }

protected:
  void forward(const double dt);

};

#endif /*_MPRGPFEMSOLVER_H_*/
