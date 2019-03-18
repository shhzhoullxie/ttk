#include <HarmonicField.h>

ttk::HarmonicField::HarmonicField()
  : vertexNumber_{}, edgeNumber_{}, constraintNumber_{}, useCotanWeights_{true},
    triangulation_{}, sources_{}, constraints_{}, outputScalarFieldPointer_{},
    solvingMethod_{ttk::SolvingMethodUserType::Auto}, logAlpha_{5} {
}

ttk::SolvingMethodType ttk::HarmonicField::findBestSolver() const {

  // for switching between Cholesky factorization and Iterate
  // (conjugate gradients) method
  SimplexId threshold = 500000;

  // compare threshold to number of non-zero values in laplacian matrix
  if(2 * edgeNumber_ + vertexNumber_ > threshold) {
    return ttk::SolvingMethodType::Iterative;
  }
  return ttk::SolvingMethodType::Cholesky;
}

// main routine
template <typename scalarFieldType>
int ttk::HarmonicField::execute() const {

  using std::cerr;
  using std::cout;
  using std::endl;
  using std::stringstream;

#ifdef TTK_ENABLE_EIGEN

#ifdef TTK_ENABLE_OPENMP
  Eigen::setNbThreads(threadNumber_);
#endif // TTK_ENABLE_OPENMP

  using SpMat = Eigen::SparseMatrix<scalarFieldType>;
  using SpVec = Eigen::SparseVector<scalarFieldType>;
  using TripletType = Eigen::Triplet<scalarFieldType>;

  Timer t;

  // scalar field constraints vertices
  auto identifiers = static_cast<SimplexId *>(sources_);
  // scalar field: 0 everywhere except on constraint vertices
  auto sf = static_cast<scalarFieldType *>(constraints_);

  {
    stringstream msg;
    msg << "[HarmonicField] Beginnning computation" << endl;
    dMsg(cout, msg.str(), advancedInfoMsg);
  }

  // get unique constraint vertices
  std::set<SimplexId> identifiersSet;
  for(SimplexId i = 0; i < constraintNumber_; ++i) {
    identifiersSet.insert(identifiers[i]);
  }
  // contains vertices with constraints
  std::vector<SimplexId> identifiersVec(
    identifiersSet.begin(), identifiersSet.end());

  // graph laplacian of current mesh
  SpMat lap;
  if(useCotanWeights_) {
    lap = compute_laplacian_with_cotan_weights<SpMat, TripletType,
                                               scalarFieldType>();
  } else {
    lap = compute_laplacian<SpMat, TripletType, scalarFieldType>();
  }

  // constraints vector
  SpVec constraints(vertexNumber_);
  for(size_t i = 0; i < identifiersVec.size(); i++) {
    // put constraint at identifier index
    constraints.coeffRef(identifiersVec[i]) = sf[i];
  }

  auto sm = ttk::SolvingMethodType::Cholesky;

  switch(solvingMethod_) {
    case ttk::SolvingMethodUserType::Auto:
      sm = findBestSolver();
      break;
    case ttk::SolvingMethodUserType::Cholesky:
      sm = ttk::SolvingMethodType::Cholesky;
      break;
    case ttk::SolvingMethodUserType::Iterative:
      sm = ttk::SolvingMethodType::Iterative;
      break;
  }

  // penalty matrix
  SpMat penalty(vertexNumber_, vertexNumber_);
  // penalty value
  const scalarFieldType alpha = pow10(logAlpha_);

  std::vector<TripletType> triplets;
  triplets.reserve(identifiersVec.size());
  for(auto i : identifiersVec) {
    triplets.emplace_back(TripletType(i, i, alpha));
  }
  penalty.setFromTriplets(triplets.begin(), triplets.end());

  int res = 0;
  SpMat sol;

  switch(sm) {
    case ttk::SolvingMethodType::Cholesky:
      res = solve<SpMat, SpVec, Eigen::SimplicialCholesky<SpMat>>(
        lap, penalty, constraints, sol);
      break;
    case ttk::SolvingMethodType::Iterative:
      res = solve<SpMat, SpVec,
                  Eigen::ConjugateGradient<SpMat, Eigen::Upper | Eigen::Lower>>(
        lap, penalty, constraints, sol);
      break;
  }

  {
    stringstream msg;
    auto info = static_cast<Eigen::ComputationInfo>(res);
    switch(info) {
      case Eigen::ComputationInfo::Success:
        msg << "[HarmonicField] Success!" << endl;
        break;
      case Eigen::ComputationInfo::NumericalIssue:
        msg << "[HarmonicField] Numerical Issue!" << endl;
        break;
      case Eigen::ComputationInfo::NoConvergence:
        msg << "[HarmonicField] No Convergence!" << endl;
        break;
      case Eigen::ComputationInfo::InvalidInput:
        msg << "[HarmonicField] Invalid Input!" << endl;
        break;
    }
    dMsg(cout, msg.str(), advancedInfoMsg);
  }

  auto outputScalarField
    = static_cast<scalarFieldType *>(outputScalarFieldPointer_);

  // convert to dense Eigen matrix
  Eigen::Matrix<scalarFieldType, Eigen::Dynamic, 1> solDense(sol);

  // copy solver solution into output array
#ifdef TTK_ENABLE_OPENMP
#pragma omp parallel for num_threads(threadNumber_)
#endif // TTK_ENABLE_OPENMP
  for(SimplexId i = 0; i < vertexNumber_; ++i) {
    // cannot avoid copy here...
    outputScalarField[i] = -solDense(i, 0);
  }

  {
    stringstream msg;
    msg << "[HarmonicField] Ending computation after " << t.getElapsedTime()
        << "s (";
    if(useCotanWeights_) {
      msg << "cotan weights, ";
    } else {
      msg << "discrete laplacian, ";
    }
    if(sm == ttk::SolvingMethodType::Iterative) {
      msg << "iterative solver, ";
    } else {
      msg << "Cholesky, ";
    }
    msg << threadNumber_ << " thread(s))" << endl;
    dMsg(cout, msg.str(), infoMsg);
  }

#else
  {
    stringstream msg;
    msg << "[HarmonicField]" << endl;
    msg << "[HarmonicField]" << endl;
    msg << "[HarmonicField] Eigen support disabled, computation "
           "skipped!"
        << endl;
    msg << "[HarmonicField] Please re-compile TTK with Eigen support to enable"
        << " this feature." << endl;
    msg << "[HarmonicField]" << endl;
    msg << "[HarmonicField]" << endl;
    dMsg(cerr, msg.str(), infoMsg);
  }
#endif // TTK_ENABLE_EIGEN

  return 0;
}

// explicit instantiations for double and float types
template int ttk::HarmonicField::execute<double>() const;
template int ttk::HarmonicField::execute<float>() const;