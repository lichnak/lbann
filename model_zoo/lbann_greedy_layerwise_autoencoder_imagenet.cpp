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
// lbann_dnn_imagenet.cpp - DNN application for image-net classification
////////////////////////////////////////////////////////////////////////////////

#include "lbann/lbann.hpp"
#include "lbann/regularization/lbann_dropout.hpp"

#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include <iomanip>
#include <string>

//#include <algorithm>
//#include <random>

using namespace std;
using namespace lbann;
using namespace El;


// train/test data info
const string g_ImageNet_TrainDir = "resized_256x256/train/";
const string g_ImageNet_ValDir = "resized_256x256/val/";
const string g_ImageNet_TestDir = "resized_256x256/val/"; //test/";
const string g_ImageNet_LabelDir = "labels/";
const string g_ImageNet_TrainLabelFile = "train_c0-9.txt";
const string g_ImageNet_ValLabelFile = "val.txt";
const string g_ImageNet_TestLabelFile = "val_c0-9.txt"; //"test.txt";



int main(int argc, char* argv[])
{
    // El initialization (similar to MPI_Init)
    Initialize(argc, argv);
    lbann_comm *comm = NULL;

    try {
        ///////////////////////////////////////////////////////////////////
        // initalize grid, block
        ///////////////////////////////////////////////////////////////////
        TrainingParams trainParams;
        trainParams.DatasetRootDir = "/p/lscratchf/brainusr/datasets/ILSVRC2012/";
        trainParams.DropOut = 0.1;
        trainParams.ProcsPerModel = 0;
        trainParams.parse_params();
        trainParams.PercentageTrainingSamples = 1.0;
        trainParams.PercentageValidationSamples = 0.2;
        PerformanceParams perfParams;
        perfParams.parse_params();
        // Read in the user specified network topology
        NetworkParams netParams;
        netParams.parse_params();
        // Get some environment variables from the launch
        SystemParams sysParams;
        sysParams.parse_params();


        int decayIterations = 1;

        ProcessInput();
        PrintInputReport();

        // set algorithmic blocksize
        SetBlocksize(perfParams.BlockSize);


        // Set up the communicator and get the grid.
        comm = new lbann_comm(trainParams.ProcsPerModel);
        Grid& grid = comm->get_model_grid();
        if (comm->am_world_master()) {
          cout << "Number of models: " << comm->get_num_models() << endl;
          cout << "Grid is " << grid.Height() << " x " << grid.Width() << endl;
          cout << endl;
        }

        int parallel_io = perfParams.MaxParIOSize;
        if(parallel_io == 0) {
          if(comm->am_world_master()) {
             cout << "\tMax Parallel I/O Fetch: " << comm->get_procs_per_model() << " (Limited to # Processes)" << endl;
          }
          parallel_io = comm->get_procs_per_model();
        }else {
          if(comm->am_world_master()) {
            cout << "\tMax Parallel I/O Fetch: " << parallel_io << endl;
          }
        }

        parallel_io = 1;
        ///////////////////////////////////////////////////////////////////
        // load training data (ImageNet)
        ///////////////////////////////////////////////////////////////////
        DataReader_ImageNet imagenet_trainset(trainParams.MBSize, true);
        imagenet_trainset.set_file_dir(trainParams.DatasetRootDir + g_ImageNet_TrainDir);
        imagenet_trainset.set_data_filename(trainParams.DatasetRootDir + g_ImageNet_LabelDir + g_ImageNet_TrainLabelFile);
        imagenet_trainset.set_validation_percent(trainParams.PercentageValidationSamples);
        imagenet_trainset.load();

        ///////////////////////////////////////////////////////////////////
        // create a validation set from the unused training data (ImageNet)
        ///////////////////////////////////////////////////////////////////
        DataReader_ImageNet imagenet_validation_set(imagenet_trainset); // Clone the training set object
        imagenet_validation_set.use_unused_index_set();

        if (comm->am_world_master()) {
          size_t num_train = imagenet_trainset.getNumData();
          size_t num_validate = imagenet_trainset.getNumData();
          double validate_percent = num_validate / (num_train+num_validate)*100.0;
          double train_percent = num_train / (num_train+num_validate)*100.0;
          cout << "Training using " << train_percent << "% of the training data set, which is " << imagenet_trainset.getNumData() << " samples." << endl
               << "Validating training using " << validate_percent << "% of the training data set, which is " << imagenet_validation_set.getNumData() << " samples." << endl;
        }

        ///////////////////////////////////////////////////////////////////
        // load testing data (ImageNet)
        ///////////////////////////////////////////////////////////////////
        DataReader_ImageNet imagenet_testset(trainParams.MBSize, true);
        imagenet_testset.set_file_dir(trainParams.DatasetRootDir + g_ImageNet_TestDir);
        imagenet_testset.set_data_filename(trainParams.DatasetRootDir + g_ImageNet_LabelDir + g_ImageNet_TestLabelFile);
        imagenet_testset.set_use_percent(trainParams.PercentageTestingSamples);
        imagenet_testset.load();

        if (comm->am_world_master()) {
          cout << "Testing using " << (trainParams.PercentageTestingSamples*100) << "% of the testing data set, which is " << imagenet_testset.getNumData() << " samples." << endl;
        }

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
        greedy_layerwise_autoencoder* gla = new greedy_layerwise_autoencoder(trainParams.MBSize, comm, new objective_functions::mean_squared_error(comm), lfac, optimizer_fac);
        std::map<execution_mode, DataReader*> data_readers = {std::make_pair(execution_mode::training,&imagenet_trainset),
                                                              std::make_pair(execution_mode::validation, &imagenet_validation_set),
                                                              std::make_pair(execution_mode::testing, &imagenet_testset)};
        input_layer *input_layer = new input_layer_distributed_minibatch_parallel_io(data_layout::MODEL_PARALLEL, comm, parallel_io, (int) trainParams.MBSize, data_readers);
        gla->add(input_layer);
        gla->add("FullyConnected", data_layout::MODEL_PARALLEL, 10000, trainParams.ActivationType, weight_initialization::glorot_uniform, {new dropout(data_layout::MODEL_PARALLEL, comm, trainParams.DropOut)});
        gla->add("FullyConnected", data_layout::MODEL_PARALLEL, 5000, trainParams.ActivationType, weight_initialization::glorot_uniform, {new dropout(data_layout::MODEL_PARALLEL, comm,trainParams.DropOut)});
        gla->add("FullyConnected", data_layout::MODEL_PARALLEL, 2000, trainParams.ActivationType, weight_initialization::glorot_uniform, {new dropout(data_layout::MODEL_PARALLEL, comm,trainParams.DropOut)});
        gla->add("FullyConnected", data_layout::MODEL_PARALLEL, 1000, trainParams.ActivationType, weight_initialization::glorot_uniform, {new dropout(data_layout::MODEL_PARALLEL, comm,trainParams.DropOut)});
        gla->add("FullyConnected", data_layout::MODEL_PARALLEL, 500, trainParams.ActivationType, weight_initialization::glorot_uniform, {new dropout(data_layout::MODEL_PARALLEL, comm,trainParams.DropOut)});

        gla->setup();

        // set checkpoint directory and checkpoint interval
        // @TODO: add to lbann_proto
        gla->set_checkpoint_dir(trainParams.ParameterDir);
        gla->set_checkpoint_epochs(trainParams.CkptEpochs);
        gla->set_checkpoint_steps(trainParams.CkptSteps);
        gla->set_checkpoint_secs(trainParams.CkptSecs);

        // restart model from checkpoint if we have one
        gla->restartShared();

        if (comm->am_world_master()) {
	        cout << "Layer initialized:" << endl;
                for (uint n = 0; n < gla->get_layers().size(); n++)
                  cout << "\tLayer[" << n << "]: " << gla->get_layers()[n]->NumNeurons << endl;
            cout << endl;

	     cout << "Parameter settings:" << endl;
            cout << "\tBlock size: " << perfParams.BlockSize << endl;
            cout << "\tEpochs: " << trainParams.EpochCount << endl;
            cout << "\tMini-batch size: " << trainParams.MBSize << endl;
            cout << "\tLearning rate: " << trainParams.LearnRate << endl;
            cout << "\tEpoch count: " << trainParams.EpochCount << endl << endl;
            if(perfParams.MaxParIOSize == 0) {
              cout << "\tMax Parallel I/O Fetch: " << grid.Size() << " (Limited to # Processes)" << endl;
            }else {
              cout << "\tMax Parallel I/O Fetch: " << perfParams.MaxParIOSize << endl;
            }
            cout << "\tDataset: " << trainParams.DatasetRootDir << endl;
        }




        mpi::Barrier(grid.Comm());

        gla->train(trainParams.EpochCount);

        delete gla;
    }
    catch (lbann_exception& e) { lbann_report_exception(e, comm); }
    catch (exception& e) { ReportException(e); } /// Elemental exceptions

    // free all resources by El and MPI
    Finalize();

    return 0;
}
