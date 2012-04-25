// @HEADER
// ***********************************************************************
//
//           Panzer: A partial differential equation assembly
//       engine for strongly coupled complex multiphysics systems
//                 Copyright (2011) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Roger P. Pawlowski (rppawlo@sandia.gov) and
// Eric C. Cyr (eccyr@sandia.gov)
// ***********************************************************************
// @HEADER

#ifndef PANZER_EVALUATOR_GATHER_SOLUTION_BLOCKEDEPETRA_DECL_HPP
#define PANZER_EVALUATOR_GATHER_SOLUTION_BLOCKEDEPETRA_DECL_HPP

#include "Phalanx_ConfigDefs.hpp"
#include "Phalanx_Evaluator_Macros.hpp"
#include "Phalanx_MDField.hpp"

#include "Teuchos_ParameterList.hpp"

#include "Panzer_config.hpp"
#include "Panzer_Dimension.hpp"
#include "Panzer_Traits.hpp"
#include "Panzer_CloneableEvaluator.hpp"

class Epetra_Vector;
class Epetra_CrsMatrix;

namespace panzer {

template <typename LocalOrdinalT,typename GlobalOrdinalT>
class UniqueGlobalIndexer; //forward declaration

template <typename LocalOrdinalT,typename GlobalOrdinalT>
class BlockedDOFManager; //forward declaration

/** \brief Gathers solution values from the Newton solution vector into 
    the nodal fields of the field manager

    Currently makes an assumption that the stride is constant for dofs
    and that the nmber of dofs is equal to the size of the solution
    names vector.
*/
template<typename EvalT, typename Traits,typename LO,typename GO> class GatherSolution_BlockedEpetra
  : public PHX::EvaluatorWithBaseImpl<Traits>,
    public PHX::EvaluatorDerived<panzer::Traits::Residual, Traits>,
    public panzer::CloneableEvaluator  {
public:

   GatherSolution_BlockedEpetra(const Teuchos::RCP<const BlockedDOFManager<LO,int> > & indexer)
   { }

   GatherSolution_BlockedEpetra(const Teuchos::RCP<const BlockedDOFManager<LO,int> > & gidProviders,
                                const Teuchos::ParameterList& p)
   { std::cout << "unspecialized version of \"GatherSolution_BlockedEpetra\" on \""+PHX::TypeString<EvalT>::value+"\" should not be used!" << std::endl;
    TEUCHOS_ASSERT(false); }

  virtual Teuchos::RCP<CloneableEvaluator> clone(const Teuchos::ParameterList & pl) const
  { return Teuchos::rcp(new GatherSolution_BlockedEpetra<EvalT,Traits,LO,GO>(Teuchos::null,pl)); }

  void postRegistrationSetup(typename Traits::SetupData d, PHX::FieldManager<Traits>& vm) {}
  void evaluateFields(typename Traits::EvalData d) {}
};

// **************************************************************
// **************************************************************
// * Specializations
// **************************************************************
// **************************************************************


// **************************************************************
// Residual 
// **************************************************************
template<typename Traits,typename LO,typename GO>
class GatherSolution_BlockedEpetra<panzer::Traits::Residual,Traits,LO,GO>
  : public PHX::EvaluatorWithBaseImpl<Traits>,
    public PHX::EvaluatorDerived<panzer::Traits::Residual, Traits>,
    public panzer::CloneableEvaluator  {
   
  
public:
  
   GatherSolution_BlockedEpetra(const Teuchos::RCP<const BlockedDOFManager<LO,int> > & indexer)
     : gidIndexer_(indexer) {}

   GatherSolution_BlockedEpetra(const Teuchos::RCP<const BlockedDOFManager<LO,int> > & indexer,
                                const Teuchos::ParameterList& p);
  
  void postRegistrationSetup(typename Traits::SetupData d,
			     PHX::FieldManager<Traits>& vm);
  
  void evaluateFields(typename Traits::EvalData d);

  virtual Teuchos::RCP<CloneableEvaluator> clone(const Teuchos::ParameterList & pl) const
  { return Teuchos::rcp(new GatherSolution_BlockedEpetra<panzer::Traits::Residual,Traits,LO,GO>(gidIndexer_,pl)); }
  
private:

  typedef typename panzer::Traits::Residual::ScalarT ScalarT;

  // maps the local (field,element,basis) triplet to a global ID
  // for scattering
  Teuchos::RCP<const BlockedDOFManager<LO,int> > gidIndexer_;

  std::vector<int> fieldIds_; // field IDs needing mapping

  std::vector< PHX::MDField<ScalarT,Cell,NODE> > gatherFields_;

  Teuchos::RCP<std::vector<std::string> > indexerNames_;
  bool useTimeDerivativeSolutionVector_;

  GatherSolution_BlockedEpetra();
};

// **************************************************************
// Jacobian
// **************************************************************
template<typename Traits,typename LO,typename GO>
class GatherSolution_BlockedEpetra<panzer::Traits::Jacobian,Traits,LO,GO>
  : public PHX::EvaluatorWithBaseImpl<Traits>,
    public PHX::EvaluatorDerived<panzer::Traits::Jacobian, Traits>,
    public panzer::CloneableEvaluator  {
  
public:
  GatherSolution_BlockedEpetra(const Teuchos::RCP<const BlockedDOFManager<LO,int> > & indexer)
     : gidIndexer_(indexer) {}

  GatherSolution_BlockedEpetra(const Teuchos::RCP<const BlockedDOFManager<LO,int> > & indexer,
                               const Teuchos::ParameterList& p);
  
  void postRegistrationSetup(typename Traits::SetupData d,
			     PHX::FieldManager<Traits>& vm);
  
  void evaluateFields(typename Traits::EvalData d);

  virtual Teuchos::RCP<CloneableEvaluator> clone(const Teuchos::ParameterList & pl) const
  { return Teuchos::rcp(new GatherSolution_BlockedEpetra<panzer::Traits::Jacobian,Traits,LO,GO>(gidIndexer_,pl)); }
  
private:

  typedef typename panzer::Traits::Jacobian::ScalarT ScalarT;

  // maps the local (field,element,basis) triplet to a global ID
  // for scattering
  Teuchos::RCP<const BlockedDOFManager<LO,int> > gidIndexer_;

  std::vector<int> fieldIds_; // field IDs needing mapping

  std::vector< PHX::MDField<ScalarT,Cell,NODE> > gatherFields_;

  Teuchos::RCP<std::vector<std::string> > indexerNames_;
  bool useTimeDerivativeSolutionVector_;

  GatherSolution_BlockedEpetra();
};

}

// **************************************************************
#endif
