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
////////////////////////////////////////////////////////////////////////////////
#include "lbann/data_readers/lbann_data_reader_synthetic.hpp"
#include "lbann/lbann.hpp"

using namespace std;
using namespace lbann;
#ifdef __LIB_ELEMENTAL
using namespace El;
#endif


//@todo use param options

int main(int argc, char* argv[])
{
    // El initialization (similar to MPI_Init)
    Initialize(argc, argv);
    init_random(42);
    init_data_seq_random(42);
    lbann_comm* comm = NULL;

  try {


        ///////////////////////////////////////////////////////////////////
        // initalize grid, block
        ///////////////////////////////////////////////////////////////////
      TrainingParams trainParams;
      trainParams.LearnRate = 0.0001;
      trainParams.DropOut = -1.0f;
      trainParams.ProcsPerModel = 0;
      trainParams.PercentageTrainingSamples = 1.0;
      trainParams.PercentageValidationSamples = 0.5;
      PerformanceParams perfParams;
      perfParams.BlockSize = 256;

      // Parse command-line inputs
      trainParams.parse_params();
      perfParams.parse_params();

      // Read in the user specified network topology
      NetworkParams netParams;
      netParams.parse_params();

      ProcessInput();
      PrintInputReport();

      // set algorithmic blocksize
      SetBlocksize(perfParams.BlockSize);


        // Set up the communicator and get the grid.
      lbann_comm* comm = new lbann_comm(trainParams.ProcsPerModel);
      Grid& grid = comm->get_model_grid();
      if (comm->am_world_master()) {
        cout << "Number of models: " << comm->get_num_models() << endl;
        cout << "Grid is " << grid.Height() << " x " << grid.Width() << endl;
        cout << endl;
      }

      int parallel_io = perfParams.MaxParIOSize;
      if(parallel_io == 0) {
        cout << "\tMax Parallel I/O Fetch: " << grid.Size() << " (Limited to # Processes)" << endl;
        parallel_io = grid.Size();
      }else {
        cout << "\tMax Parallel I/O Fetch: " << parallel_io << endl;
      }

        ///////////////////////////////////////////////////////////////////
        // load training data
        ///////////////////////////////////////////////////////////////////
      clock_t load_time = clock();
      data_reader_synthetic synthetic_trainset(trainParams.MBSize, trainParams.TrainingSamples, netParams.Network[0]);
      synthetic_trainset.set_validation_percent(trainParams.PercentageValidationSamples);
      synthetic_trainset.load();


      ///////////////////////////////////////////////////////////////////
      // create a validation set from the unused training data 
      ///////////////////////////////////////////////////////////////////
      data_reader_synthetic synthetic_validation_set(synthetic_trainset); // Clone the training set object
      synthetic_validation_set.use_unused_index_set();


        if (comm->am_world_master()) {
          size_t num_train = synthetic_trainset.getNumData();
          size_t num_validate = synthetic_trainset.getNumData();
          double validate_percent = num_validate / (num_train+num_validate)*100.0;
          double train_percent = num_train / (num_train+num_validate)*100.0;
          cout << "Training using " << train_percent << "% of the training data set, which is " << synthetic_trainset.getNumData() << " samples." << endl
               << "Validating training using " << validate_percent << "% of the training data set, which is " << synthetic_validation_set.getNumData() << " samples." << endl;
        }

      ///////////////////////////////////////////////////////////////////
        // load testing data 
      ///////////////////////////////////////////////////////////////////
      data_reader_synthetic synthetic_testset(trainParams.MBSize, trainParams.TestingSamples,netParams.Network[0]);
      synthetic_testset.load();

      ///////////////////////////////////////////////////////////////////
        // initalize neural network (layers)
      ///////////////////////////////////////////////////////////////////
      optimizer_factory *optimizer_fac;
      if (trainParams.LearnRateMethod == 1) { // Adagrad
        optimizer_fac = new adagrad_factory(comm, trainParams.LearnRate);
      }else if (trainParams.LearnRateMethod == 2) { // RMSprop
        optimizer_fac = new rmsprop_factory(comm, trainParams.LearnRate);
      } else if (trainParams.LearnRateMethod == 3) { // Adam
        optimizer_fac = new adam_factory(comm, trainParams.LearnRate);
      } else {
        optimizer_fac = new sgd_factory(comm, trainParams.LearnRate, 0.9, trainParams.LrDecayRate, true);
      }
      layer_factory* lfac = new layer_factory();
      greedy_layerwise_autoencoder gla(trainParams.MBSize, comm, new objective_functions::mean_squared_error(comm), lfac, optimizer_fac);

      std::map<execution_mode, DataReader*> data_readers = {std::make_pair(execution_mode::training,&synthetic_trainset),
                                                             std::make_pair(execution_mode::validation, &synthetic_validation_set),
                                                             std::make_pair(execution_mode::testing, &synthetic_testset)};

      input_layer *input_layer = new input_layer_distributed_minibatch_parallel_io(data_layout::DATA_PARALLEL, comm, parallel_io,
                                (int) trainParams.MBSize, data_readers);
      gla.add(input_layer);

      gla.add("FullyConnected", data_layout::MODEL_PARALLEL, netParams.Network[1], trainParams.ActivationType, weight_initialization::glorot_uniform, {new dropout(data_layout::MODEL_PARALLEL, comm, trainParams.DropOut)});



      if (comm->am_world_master()) {
        cout << "Parameter settings:" << endl;
        cout << "\tTraining sample size: " << trainParams.TrainingSamples << endl;
        cout << "\tTesting sample size: " << trainParams.TestingSamples << endl;
        cout << "\tFeature vector size: " << netParams.Network[0] << endl;
        cout << "\tMini-batch size: " << trainParams.MBSize << endl;
        cout << "\tLearning rate: " << trainParams.LearnRate << endl;
        cout << "\tEpoch count: " << trainParams.EpochCount << endl;
      }



      gla.setup();

      // set checkpoint directory and checkpoint interval
      // @TODO: add to lbann_proto
      gla.set_checkpoint_dir(trainParams.ParameterDir);
      gla.set_checkpoint_epochs(trainParams.CkptEpochs);
      gla.set_checkpoint_steps(trainParams.CkptSteps);
      gla.set_checkpoint_secs(trainParams.CkptSecs);

      // restart model from checkpoint if we have one
      gla.restartShared();

      if (comm->am_world_master()) cout << "(Pre) train autoencoder - unsupersived training" << endl;
      gla.train(trainParams.EpochCount,true);

      delete optimizer_fac;
      delete comm;
    }
    catch (exception& e) { ReportException(e); }

    // free all resources by El and MPI
    Finalize();

    return 0;
}
