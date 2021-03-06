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
// lbann_data_reader_mnist .hpp .cpp - DataReader class for MNIST dataset
////////////////////////////////////////////////////////////////////////////////

#include "lbann/data_readers/lbann_data_reader_mnist.hpp"
#include <stdio.h>

using namespace std;
using namespace El;

inline void __swapEndianInt(unsigned int& ui)
{
  ui = ((ui >> 24) | ((ui<<8) & 0x00FF0000) | ((ui>>8) & 0x0000FF00) | (ui << 24));
}



lbann::DataReader_MNIST::DataReader_MNIST(int batchSize, bool shuffle)
  : DataReader(batchSize, shuffle)
  //, lbann_image_preprocessor()
{
  m_image_width = 28;
  m_image_height = 28;
  m_num_labels = 10;
}

lbann::DataReader_MNIST::DataReader_MNIST(int batchSize)
  : DataReader_MNIST(batchSize, true) {}

/*
lbann::DataReader_MNIST::DataReader_MNIST(const DataReader_MNIST& source)
  : DataReader((const DataReader&) source),
    //lbann_image_preprocessor((const lbann_image_preprocessor&) source),
    m_image_width(source.m_image_width), m_image_height(source.m_image_height),
    m_num_labels(source.m_num_labels)
{
  // No need to deallocate data on a copy constuctor

  clone_image_data(source);
}
*/

lbann::DataReader_MNIST::~DataReader_MNIST()
{
  //this->free();
}

int lbann::DataReader_MNIST::fetch_data(Mat& X)
{
  if(!DataReader::position_valid()) {
    stringstream err;
    err << __FILE__<<" "<<__LINE__<< " :: MNIST data reader load error: !position_valid";
    throw lbann_exception(err.str());
  }

  int pixelcount = m_image_width * m_image_height;
  int current_batch_size = getBatchSize();

  int n = 0;
  std::vector<float> pixels(pixelcount);
  for (n = CurrentPos; n < CurrentPos + current_batch_size; n++) {
    if (n >= (int)ShuffledIndices.size())
      break;

    int k = n - CurrentPos;
    int index = ShuffledIndices[n];
    vector<unsigned char> &tmp = m_image_data[index];

    for (int p = 0; p < pixelcount; p++) {
      X.Set(p, k, tmp[p+1]);
    }

    auto pixel_col = X(IR(0, X.Height()), IR(k, k + 1));
    augment(pixel_col, m_image_height, m_image_width, 1);
    normalize(pixel_col, 1);
  }

  return (n - CurrentPos);
}

int lbann::DataReader_MNIST::fetch_label(Mat& Y)
{
  if(!DataReader::position_valid()) {
    stringstream err;
    err << __FILE__<<" "<<__LINE__<< " :: MNIST data reader load error: !position_valid";
    throw lbann_exception(err.str());
  }

  int current_batch_size = getBatchSize();
  int n = 0;
  for (n = CurrentPos; n < CurrentPos + current_batch_size; n++) {
    if (n >= (int)ShuffledIndices.size())
      break;

    int k = n - CurrentPos;
    int index = ShuffledIndices[n];
    unsigned char label = m_image_data[index][0];

    Y.Set(label, k, 1);
  }

  return (n - CurrentPos);
}


/*
void lbann::DataReader_MNIST::free()
{
  for (size_t n = 0; n < m_image_data.size(); n++) {
    unsigned char* data = m_image_data[n];
    delete [] data;
  }
  m_image_data.clear();
}
*/

// Assignment operator
#if 0
lbann::DataReader_MNIST& lbann::DataReader_MNIST::operator=(const DataReader_MNIST& source)
{
  // check for self-assignment
  if (this == &source)
    return *this;

  // Call the parent operator= function
  DataReader::operator=(source);

  // first we need to deallocate any data that this data reader is holding!
  for (size_t n = 0; n < m_image_data.size(); n++) {
    unsigned char* data = m_image_data[n];
    delete [] data;
  }
  m_image_data.clear();

  this->m_image_width = source.m_image_width;
  this->m_image_height = source.m_image_height;
  this->m_num_labels = source.m_num_labels;
  this->m_num_labels = source.m_num_labels;

  clone_image_data(source);
  return *this;
}
#endif

/*
void lbann::DataReader_MNIST::clone_image_data(const DataReader_MNIST& source)
{
  // m_image_data has pointers, so we need to deep copy them
  for (size_t n = 0; n < source.m_image_data.size(); n++) {
    unsigned char* data = new unsigned char[1 + m_image_width * m_image_height];
    unsigned char* src_data = source.m_image_data[n];

    for (size_t i = 0; i < 1 + m_image_width * m_image_height; i++) {
      data[i] = src_data[i];
    }
    m_image_data.push_back(data);
  }
  return;
}
*/

//===================================================

void lbann::DataReader_MNIST::load()
{
  //this->free();
  m_image_data.clear();

  string FileDir = get_file_dir();
  string ImageFile = get_data_filename();
  string LabelFile = get_label_filename();

  // set filepath
  string imagepath = FileDir + __DIR_DELIMITER + ImageFile;
  string labelpath = FileDir + __DIR_DELIMITER + LabelFile;

  // read labels
  FILE* fplbl = fopen(labelpath.c_str(), "rb");
  if (!fplbl) {
    stringstream err;
    err << __FILE__<<" "<<__LINE__<< " :: MNIST data reader: failed to open file: " << labelpath;
    throw lbann_exception(err.str());
  }

  int magicnum1, numitems1;
  fread(&magicnum1, 4, 1, fplbl);
  fread(&numitems1, 4, 1, fplbl);
  __swapEndianInt((unsigned int&)magicnum1);
  __swapEndianInt((unsigned int&)numitems1);

  // read images
  FILE* fpimg = fopen(imagepath.c_str(), "rb");
  if (!fpimg) {
    stringstream err;
    err << __FILE__<<" "<<__LINE__<< " :: MNIST data reader: failed to open file: " << imagepath;
    throw lbann_exception(err.str());
  }

  int magicnum2, numitems2, imgwidth, imgheight;
  fread(&magicnum2, 4, 1, fpimg);
  fread(&numitems2, 4, 1, fpimg);
  fread(&imgwidth, 4, 1, fpimg);
  fread(&imgheight, 4, 1, fpimg);
  __swapEndianInt((unsigned int&)magicnum2);
  __swapEndianInt((unsigned int&)numitems2);
  __swapEndianInt((unsigned int&)imgwidth);
  __swapEndianInt((unsigned int&)imgheight);

  if (numitems1 != numitems2) {
    fclose(fplbl);
    fclose(fpimg);
    stringstream err;
    err << __FILE__<<" "<<__LINE__<< " :: MNIST data reader: numitems1 != numitems2";
    throw lbann_exception(err.str());
  }

  // set to array
  m_image_data.resize(numitems1);
  for (int n = 0; n < numitems1; n++) {
    m_image_data[n].resize(1+(imgwidth * imgheight));
    fread(&m_image_data[n][0], 1, 1, fplbl);
    fread(&m_image_data[n][1], imgwidth * imgheight, 1, fpimg);
  }
  fclose(fpimg);
  fclose(fplbl);

  // reset indices
  ShuffledIndices.clear();
  ShuffledIndices.resize(m_image_data.size());
  for (size_t n = 0; n < ShuffledIndices.size(); n++) {
    ShuffledIndices[n] = n;
  }

  select_subset_of_data();
}

