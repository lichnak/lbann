////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2016, Lawrence Livermore National Security, LLC. 
// Produced at the Lawrence Livermore National Laboratory. 
// Written by the LBANN Research Team (B. Van Essen, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-697807.
// All rights reserved.
//
// This file is part of LBANN: Livermore Big Artificial Neural Network
// Toolkit. For details, see http://software.llnl.gov/LBANN or
// https://github.com/LLNL/LBANN. 
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
//
// This file is an extension of several Elemental functions
////////////////////////////////////////////////////////////////////////////////

#include "El.hpp"

#include "lbann/lbann_Elemental_extensions.h"

namespace El {

template<typename F>
void ColumnSum( const Matrix<F>& X, Matrix<F>& sums )
{
//    DEBUG_ONLY(CSE cse("ColumnSum"))
  
    // Input matrix parameters
    const Int m = X.Height();
    const Int n = X.Width();
    const F* XBuf = X.LockedBuffer();
    const Int XLDim = X.LDim();

    // Initialize output
    Zeros( sums, n, 1 );
    F* sumsBuf = sums.Buffer();

    // Compute sum over each column
    EL_PARALLEL_FOR
    for( Int j=0; j<n; ++j )
    {
        for( Int i=0; i<m; ++i ) {
            sumsBuf[j] += XBuf[i+j*XLDim];
        }
    }

}

template<typename F,Dist U,Dist V,DistWrap W>
void ColumnSum
( const DistMatrix<F,U,V,W>& A, DistMatrix<F,V,STAR,W>& sums )
{
//    DEBUG_ONLY(CSE cse("ColumnSum"))
    const Int n = A.Width();
    sums.AlignWith( A );
    sums.Resize( n, 1 );
    ColumnSum( A.LockedMatrix(), sums.Matrix() );
    AllReduce( sums.Matrix(), A.ColComm(), mpi::SUM );
}

template<typename F>
void RowSum(const Matrix<F>& X, Matrix<F>& sums) {
  const Int m = X.Height();
  const Int n = X.Width();
  const F* XBuf = X.LockedBuffer();
  const Int XLDim = X.LDim();
  Zeros(sums, m, 1);
  F* sumsBuf = sums.Buffer();
  // Note: Iterating over columns helps cache locality for X but means we can't
  // naively parallelize the outer loop (race conditions).
  // Could probably do a local accumulation to avoid this.
  for (Int j = 0; j < n; ++j) {
    for (Int i = 0; i < m; ++i) {
      sumsBuf[i] += XBuf[i+j*XLDim];
    }
  }
}

template <typename F,Dist U,Dist V,DistWrap W>
void RowSum(const DistMatrix<F,U,V,W>& A, DistMatrix<F,U,STAR,W>& sums) {
  sums.AlignWith(A);
  sums.Resize(A.Height(), 1);
  RowSum(A.LockedMatrix(), sums.Matrix());
  AllReduce(sums, A.RowComm(), mpi::SUM);
}

LBANN_PROTO_FLOAT
LBANN_PROTO_DOUBLE

} // namespace El
