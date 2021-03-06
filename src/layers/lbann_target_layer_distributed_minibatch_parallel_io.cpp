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

#include "lbann/layers/lbann_target_layer_distributed_minibatch_parallel_io.hpp"
#include "lbann/utils/lbann_exception.hpp"
#include "lbann/lbann_Elemental_extensions.h"
#include "lbann/models/lbann_model.hpp"
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;
using namespace El;

lbann::target_layer_distributed_minibatch_parallel_io::target_layer_distributed_minibatch_parallel_io(data_layout data_dist, lbann_comm* comm, int num_parallel_readers, uint mini_batch_size, std::map<execution_mode, DataReader*> data_readers, bool shared_data_reader, bool for_regression)
  : target_layer(data_dist, comm, mini_batch_size, data_readers, shared_data_reader, for_regression), 
    distributed_minibatch_parallel_io(comm, num_parallel_readers, mini_batch_size, data_readers),
    Ys(comm->get_model_grid())
{
  m_type = layer_type::target_distributed_minibatch_parallel_io;
  //  NumNeurons = m_training_data_reader->get_linearized_label_size(); /// @todo NumNeurons should be hidden inside of an accessor function
}

void lbann::target_layer_distributed_minibatch_parallel_io::setup(int num_prev_neurons) {
  target_layer::setup(num_prev_neurons);
  if(!m_shared_data_reader) { /// If the target layer shares a data reader with an input layer, do not setup the data reader a second time
    if(io_layer::m_data_sets_span_models) {
      int stride = Layer::comm->get_num_models() * m_num_parallel_readers_training * Layer::m_mini_batch_size;
      int base_offset = Layer::comm->get_rank_in_model() * Layer::comm->get_num_models() * Layer::m_mini_batch_size;
      int model_offset = Layer::comm->get_model_rank() * Layer::m_mini_batch_size;
      //cout << "Setting up input layer, with " << Layer::comm->get_num_models() << " models and " << m_num_parallel_readers_training << " parallel readers and " << Layer::m_mini_batch_size << " mb size, which gives a stride of " << stride << endl;
      io_layer::setup_data_readers_for_training(base_offset,
                                                stride,
                                                model_offset);
      /// Note that the data readers for evaluation should not be partitioned over multiple models (otherwise each model will be scored on a different set of data)
      io_layer::setup_data_readers_for_evaluation(Layer::comm->get_rank_in_model() * Layer::m_mini_batch_size,
                                                  m_num_parallel_readers_training * Layer::m_mini_batch_size);
    }else {
      io_layer::setup_data_readers_for_training(Layer::comm->get_rank_in_model() * Layer::m_mini_batch_size,
                                                m_num_parallel_readers_training * Layer::m_mini_batch_size);
      io_layer::setup_data_readers_for_evaluation(Layer::comm->get_rank_in_model() * Layer::m_mini_batch_size,
                                                  m_num_parallel_readers_training * Layer::m_mini_batch_size);
    }
  }

  /// @todo put in warning about bad target size
  if(num_prev_neurons != NumNeurons) {
    throw lbann_exception("lbann_target_layer_distributed_minibatch_parallel_io: number of neurons in previous layer does not match the number of neurons in the target layer.");
  }

  Zeros(*m_error_signal, NumNeurons, Layer::m_mini_batch_size);
  Zeros(Y_local, NumNeurons, Layer::m_mini_batch_size);
  Zeros(Ys, NumNeurons, Layer::m_mini_batch_size);
  Zeros(*m_prev_activations, num_prev_neurons, m_mini_batch_size);
  Zeros(*m_weighted_sum, NumNeurons, m_mini_batch_size);
  Zeros(*m_activations, NumNeurons, m_mini_batch_size);

  m_local_data_valid = false;
  m_local_reader_done = false;
  m_num_data_per_epoch = 0;
}

void lbann::target_layer_distributed_minibatch_parallel_io::fp_linearity() {
  int num_samples_in_batch = fetch_to_local_matrix(Y_local);
  if(is_current_root()) {
    /// Only update the number of samples processed by this parallel reader, when it is the current root
    target_layer::update_num_samples_processed(num_samples_in_batch);
  }

  int64_t curr_mini_batch_size = neural_network_model->get_current_mini_batch_size();
  if(is_current_root() && num_samples_in_batch != curr_mini_batch_size) {
    throw lbann_exception("lbann_target_layer_distributed_minibatch_parallel_io: number of labels does not match the current mini-batch size.");
  }
  /// @todo should this distribute the entire matrix even if there is only a partial mini-batch
  distribute_from_local_matrix(Y_local, Ys);
  Copy(Ys, *m_activations);

  /// Compute and record the objective function score
  DataType avg_error = neural_network_model->obj_fn->compute_obj_fn(*m_prev_activations_v, *m_activations_v);
  neural_network_model->obj_fn->record_obj_fn(m_execution_mode, avg_error);

  for (auto&& m : neural_network_model->metrics) {
    double num_errors = (int) m->compute_metric(*m_prev_activations_v, *m_activations_v);
    m->record_error(num_errors, curr_mini_batch_size);
  }

  return;
}


void lbann::target_layer_distributed_minibatch_parallel_io::bp_linearity() {

  // Compute initial error signal
  neural_network_model->obj_fn->compute_obj_fn_derivative(m_prev_layer_type,
                                                          *m_prev_activations_v,
                                                          *m_activations_v,
                                                          *m_error_signal_v);

}

/**
 * Once a mini-batch is processed, resuffle the data for the next batch if necessary
 */
bool lbann::target_layer_distributed_minibatch_parallel_io::update() {
  return is_data_set_processed();
}

int lbann::target_layer_distributed_minibatch_parallel_io::fetch_from_data_reader(Mat &M_local) {
  DataReader *data_reader = target_layer::select_data_reader();
  if (is_for_regression())
    return data_reader->fetch_response(M_local);
  else
    return data_reader->fetch_label(M_local);
}

void lbann::target_layer_distributed_minibatch_parallel_io::preprocess_data_samples(Mat& M_local, int num_samples_in_batch) {
  return;
}

bool lbann::target_layer_distributed_minibatch_parallel_io::update_data_reader() {
  DataReader *data_reader = target_layer::select_data_reader();
  if(m_shared_data_reader) { 
    /// If the data reader is shared with an input layer, don't update the reader just check to see if the epoch is done
    /// or will be done on the next update of the input layer (which includes adding the stride).
    /// Note that target layers are always update before input layers, which is why the position
    /// is not up to date yet.
    return (data_reader->get_next_position() < data_reader->getNumData());
  }else {
    return data_reader->update();
  }
}
  
execution_mode lbann::target_layer_distributed_minibatch_parallel_io::get_execution_mode() {
  return m_execution_mode;
}
