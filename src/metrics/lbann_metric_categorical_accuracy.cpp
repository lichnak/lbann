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
////////////////////////////////////////////////////////////////////////////////

#include "lbann/metrics/lbann_metric_categorical_accuracy.hpp"
#include "lbann/models/lbann_model.hpp"

using namespace std;
using namespace El;

lbann::metrics::categorical_accuracy::categorical_accuracy(lbann_comm* comm)
  : metric(comm),
    YsColMax(comm->get_model_grid()),
    YsColMaxStar(comm->get_model_grid()),
    YsColMax_v(comm->get_model_grid()),
    YsColMaxStar_v(comm->get_model_grid())
{
  this->type = metric_type::categorical_accuracy;
}

lbann::metrics::categorical_accuracy::~categorical_accuracy() {
  YsColMax.Empty();
  YsColMaxStar.Empty();
  m_max_index.Empty();
  m_reduced_max_indices.Empty();

  YsColMax_v.Empty();
  YsColMaxStar_v.Empty();
  m_max_index_v.Empty();
  m_reduced_max_indices_v.Empty();
}

void lbann::metrics::categorical_accuracy::setup(int num_neurons, int mini_batch_size) {
  metric::setup(num_neurons, mini_batch_size);
  // Clear the contents of the intermediate matrices
  Zeros(YsColMax, mini_batch_size, 1);
  Zeros(YsColMaxStar, mini_batch_size, 1);
  Zeros(m_max_index, mini_batch_size, 1); // Clear the entire matrix
  Zeros(m_reduced_max_indices, mini_batch_size, 1); // Clear the entire matrix
  m_max_mini_batch_size = mini_batch_size;
}

void lbann::metrics::categorical_accuracy::fp_set_std_matrix_view(int64_t cur_mini_batch_size) {
  // Set the view based on the size of the current mini-batch
  // Note that these matrices are transposed (column max matrices) and thus the mini-batch size effects the number of rows, not columns
  View(YsColMax_v, YsColMax, IR(0, cur_mini_batch_size), IR(0, YsColMax.Width()));
  View(YsColMaxStar_v, YsColMaxStar, IR(0, cur_mini_batch_size), IR(0, YsColMaxStar.Width()));
  View(m_max_index_v, m_max_index, IR(0, cur_mini_batch_size), IR(0, m_max_index.Width()));
  View(m_reduced_max_indices_v, m_reduced_max_indices, IR(0, cur_mini_batch_size), IR(0, m_reduced_max_indices.Width()));
}

double lbann::metrics::categorical_accuracy::compute_metric(ElMat& predictions_v, ElMat& groundtruth_v) {

  // Clear the contents of the intermediate matrices
  Zeros(YsColMax, m_max_mini_batch_size, 1);
  Zeros(YsColMaxStar, m_max_mini_batch_size, 1);

  /// Compute the error between the previous layers activations and the ground truth
  ColumnMaxNorms((DistMat) predictions_v, YsColMax_v); /// For each minibatch (column) find the maximimum value
  Copy(YsColMax_v, YsColMaxStar_v); /// Give every rank a copy so that they can find the max index locally

  Zeros(m_max_index, m_max_mini_batch_size, 1); // Clear the entire matrix

  /// Find which rank holds the index for the maxmimum value
  for(Int mb_index = 0; mb_index < predictions_v.LocalWidth(); mb_index++) { /// For each sample in mini-batch that this rank has
    int mb_global_index = predictions_v.GlobalCol(mb_index);
    DataType sample_max = YsColMaxStar_v.GetLocal(mb_global_index, 0);
    for(Int f_index = 0; f_index < predictions_v.LocalHeight(); f_index++) { /// For each feature
      if(predictions_v.GetLocal(f_index, mb_index) == sample_max) {
        m_max_index_v.Set(mb_global_index, 0, predictions_v.GlobalRow(f_index));
      }
    }
  }

  Zeros(m_reduced_max_indices, m_max_mini_batch_size, 1); // Clear the entire matrix
  /// Merge all of the local index sets into a common buffer, if there are two potential maximum values, highest index wins
  /// Note that this has to operate on the raw buffer, not the view
  comm->model_allreduce(m_max_index.Buffer(), m_max_index.Height() * m_max_index.Width(), m_reduced_max_indices.Buffer(), mpi::MAX);

  /// Check to see if the predicted results match the target results
  int num_errors = 0;

  //  Copy(groundtruth_v, Y_local);

  /// @todo - BVE I believe that the following code works, but doesn't
  /// need to be this hard  it shouldn't have to check the inequality
  /// since there should only be one category turned on in the ground truth

  /// Distributed search over the groundtruth matrix
  /// Each rank will search its local portion of the matrix to find if it has the true category
  for(Int mb_index= 0; mb_index < groundtruth_v.LocalWidth(); mb_index++) { /// For each sample in mini-batch
    Int targetidx = -1;
    for(Int f_index= 0; f_index < groundtruth_v.LocalHeight(); f_index++) {
      if(groundtruth_v.GetLocal(f_index, mb_index) == (DataType) 1.) {
        targetidx = groundtruth_v.GlobalRow(f_index); /// If this rank holds the correct category, return the global row index
      }
    }
    if(targetidx != -1) { /// Only check against the prediction if this rank holds the groundtruth value
      Int global_mb_index = groundtruth_v.GlobalCol(mb_index);
      if(m_reduced_max_indices_v.Get(global_mb_index, 0) != targetidx) {
        num_errors++;
      }
    }
  }
  
  num_errors = comm->model_allreduce(num_errors);
  return num_errors;
}

double lbann::metrics::categorical_accuracy::report_metric(execution_mode mode) {
  statistics *stats = get_statistics(mode);
  double errors_per_epoch = stats->m_error_per_epoch;
  long samples_per_epoch = stats->m_samples_per_epoch;

  double accuracy = (double)(samples_per_epoch - errors_per_epoch) / samples_per_epoch * 100;
  string score = std::to_string(accuracy);

  // std::cout << _to_string(type) << " reporting a metric with " << errors_per_epoch << " errors and " << samples_per_epoch << " samples, a accuracty of " << accuracy << " and a score of " << score << endl;
  return accuracy;
}

double lbann::metrics::categorical_accuracy::report_lifetime_metric(execution_mode mode) {
  statistics *stats = get_statistics(mode);
  double total_error = stats->m_total_error;
  long total_num_samples = stats->m_total_num_samples;

  double accuracy = (double)(total_num_samples - total_error) / total_num_samples * 100;
  string score = std::to_string(accuracy);

  // std::cout << _to_string(type) << " reporting a metric with " << total_error << " errors and " << total_num_samples << " samples, a accuracty of " << accuracy << " and a score of " << score << endl;
  return accuracy;
}
