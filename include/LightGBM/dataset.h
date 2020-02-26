/*!
 * Copyright (c) 2016 Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See LICENSE file in the project root for license information.
 */
#ifndef LIGHTGBM_DATASET_H_
#define LIGHTGBM_DATASET_H_

#include <LightGBM/config.h>
#include <LightGBM/feature_group.h>
#include <LightGBM/meta.h>
#include <LightGBM/utils/common.h>
#include <LightGBM/utils/openmp_wrapper.h>
#include <LightGBM/utils/random.h>
#include <LightGBM/utils/text_reader.h>

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace LightGBM {

/*! \brief forward declaration */
class DatasetLoader;
/*!
* \brief This class is used to store some meta(non-feature) data for training data,
*        e.g. labels, weights, initial scores, query level informations.
*
*        Some details:
*        1. Label, used for training.
*        2. Weights, weighs of records, optional
*        3. Query Boundaries, necessary for lambdarank.
*           The documents of i-th query is in [ query_boundaries[i], query_boundaries[i+1] )
*        4. Query Weights, auto calculate by weights and query_boundaries(if both of them are existed)
*           the weight for i-th query is sum(query_boundaries[i] , .., query_boundaries[i+1]) / (query_boundaries[i + 1] -  query_boundaries[i+1])
*        5. Initial score. optional. if existing, the model will boost from this score, otherwise will start from 0.
*/
class Metadata {
 public:
  /*!
  * \brief Null constructor
  */
  Metadata();
  /*!
  * \brief Initialization will load query level informations, since it is need for sampling data
  * \param data_filename Filename of data
  */
  void Init(const char* data_filename);
  /*!
  * \brief init as subset
  * \param metadata Filename of data
  * \param used_indices
  * \param num_used_indices
  */
  void Init(const Metadata& metadata, const data_size_t* used_indices, data_size_t num_used_indices);
  /*!
  * \brief Initial with binary memory
  * \param memory Pointer to memory
  */
  void LoadFromMemory(const void* memory);
  /*! \brief Destructor */
  ~Metadata();

  /*!
  * \brief Initial work, will allocate space for label, weight(if exists) and query(if exists)
  * \param num_data Number of training data
  * \param weight_idx Index of weight column, < 0 means doesn't exists
  * \param query_idx Index of query id column, < 0 means doesn't exists
  */
  void Init(data_size_t num_data, int weight_idx, int query_idx);

  /*!
  * \brief Partition label by used indices
  * \param used_indices Indices of local used
  */
  void PartitionLabel(const std::vector<data_size_t>& used_indices);

  /*!
  * \brief Partition meta data according to local used indices if need
  * \param num_all_data Number of total training data, including other machines' data on parallel learning
  * \param used_data_indices Indices of local used training data
  */
  void CheckOrPartition(data_size_t num_all_data,
                        const std::vector<data_size_t>& used_data_indices);

  void SetLabel(const label_t* label, data_size_t len);

  void SetWeights(const label_t* weights, data_size_t len);

  void SetQuery(const data_size_t* query, data_size_t len);

  /*!
  * \brief Set initial scores
  * \param init_score Initial scores, this class will manage memory for init_score.
  */
  void SetInitScore(const double* init_score, data_size_t len);


  /*!
  * \brief Save binary data to file
  * \param file File want to write
  */
  void SaveBinaryToFile(const VirtualFileWriter* writer) const;

  /*!
  * \brief Get sizes in byte of this object
  */
  size_t SizesInByte() const;

  /*!
  * \brief Get pointer of label
  * \return Pointer of label
  */
  inline const label_t* label() const { return label_.data(); }

  /*!
  * \brief Set label for one record
  * \param idx Index of this record
  * \param value Label value of this record
  */
  inline void SetLabelAt(data_size_t idx, label_t value) {
    label_[idx] = value;
  }

  /*!
  * \brief Set Weight for one record
  * \param idx Index of this record
  * \param value Weight value of this record
  */
  inline void SetWeightAt(data_size_t idx, label_t value) {
    weights_[idx] = value;
  }

  /*!
  * \brief Set Query Id for one record
  * \param idx Index of this record
  * \param value Query Id value of this record
  */
  inline void SetQueryAt(data_size_t idx, data_size_t value) {
    queries_[idx] = static_cast<data_size_t>(value);
  }

  /*!
  * \brief Get weights, if not exists, will return nullptr
  * \return Pointer of weights
  */
  inline const label_t* weights() const {
    if (!weights_.empty()) {
      return weights_.data();
    } else {
      return nullptr;
    }
  }

  /*!
  * \brief Get data boundaries on queries, if not exists, will return nullptr
  *        we assume data will order by query,
  *        the interval of [query_boundaris[i], query_boundaris[i+1])
  *        is the data indices for query i.
  * \return Pointer of data boundaries on queries
  */
  inline const data_size_t* query_boundaries() const {
    if (!query_boundaries_.empty()) {
      return query_boundaries_.data();
    } else {
      return nullptr;
    }
  }

  /*!
  * \brief Get Number of queries
  * \return Number of queries
  */
  inline data_size_t num_queries() const { return num_queries_; }

  /*!
  * \brief Get weights for queries, if not exists, will return nullptr
  * \return Pointer of weights for queries
  */
  inline const label_t* query_weights() const {
    if (!query_weights_.empty()) {
      return query_weights_.data();
    } else {
      return nullptr;
    }
  }

  /*!
  * \brief Get initial scores, if not exists, will return nullptr
  * \return Pointer of initial scores
  */
  inline const double* init_score() const {
    if (!init_score_.empty()) {
      return init_score_.data();
    } else {
      return nullptr;
    }
  }

  /*!
  * \brief Get size of initial scores
  */
  inline int64_t num_init_score() const { return num_init_score_; }

  /*! \brief Disable copy */
  Metadata& operator=(const Metadata&) = delete;
  /*! \brief Disable copy */
  Metadata(const Metadata&) = delete;

 private:
  /*! \brief Load initial scores from file */
  void LoadInitialScore();
  /*! \brief Load wights from file */
  void LoadWeights();
  /*! \brief Load query boundaries from file */
  void LoadQueryBoundaries();
  /*! \brief Load query wights */
  void LoadQueryWeights();
  /*! \brief Filename of current data */
  std::string data_filename_;
  /*! \brief Number of data */
  data_size_t num_data_;
  /*! \brief Number of weights, used to check correct weight file */
  data_size_t num_weights_;
  /*! \brief Label data */
  std::vector<label_t> label_;
  /*! \brief Weights data */
  std::vector<label_t> weights_;
  /*! \brief Query boundaries */
  std::vector<data_size_t> query_boundaries_;
  /*! \brief Query weights */
  std::vector<label_t> query_weights_;
  /*! \brief Number of querys */
  data_size_t num_queries_;
  /*! \brief Number of Initial score, used to check correct weight file */
  int64_t num_init_score_;
  /*! \brief Initial score */
  std::vector<double> init_score_;
  /*! \brief Queries data */
  std::vector<data_size_t> queries_;
  /*! \brief mutex for threading safe call */
  std::mutex mutex_;
  bool weight_load_from_file_;
  bool query_load_from_file_;
  bool init_score_load_from_file_;
};


/*! \brief Interface for Parser */
class Parser {
 public:
  /*! \brief virtual destructor */
  virtual ~Parser() {}

  /*!
  * \brief Parse one line with label
  * \param str One line record, string format, should end with '\0'
  * \param out_features Output columns, store in (column_idx, values)
  * \param out_label Label will store to this if exists
  */
  virtual void ParseOneLine(const char* str,
                            std::vector<std::pair<int, double>>* out_features, double* out_label) const = 0;

  virtual int NumFeatures() const = 0;

  /*!
  * \brief Create an object of parser, will auto choose the format depend on file
  * \param filename One Filename of data
  * \param num_features Pass num_features of this data file if you know, <=0 means don't know
  * \param label_idx index of label column
  * \return Object of parser
  */
  static Parser* CreateParser(const char* filename, bool header, int num_features, int label_idx);
};

struct TrainingTempState {
  std::vector<hist_t, Common::AlignmentAllocator<hist_t, kAlignedSize>>
      hist_buf;
  int num_bin_aligned;
  bool is_use_subcol = false;
  bool is_use_subrow = false;
  bool is_subrow_copied = false;
  const data_size_t* bagging_use_indices;
  data_size_t bagging_indices_cnt;
  std::unique_ptr<MultiValBin> multi_val_bin;
  std::unique_ptr<MultiValBin> multi_val_bin_subset;
  std::vector<uint32_t> hist_move_src;
  std::vector<uint32_t> hist_move_dest;
  std::vector<uint32_t> hist_move_size;

  void SetMultiValBin(MultiValBin* bin) {
    if (bin == nullptr) {
      return;
    }
    multi_val_bin.reset(bin);
    int num_threads = 1;
#pragma omp parallel
#pragma omp master
    { num_threads = omp_get_num_threads(); }
    num_bin_aligned =
        (bin->num_bin() + kAlignedSize - 1) / kAlignedSize * kAlignedSize;
    size_t new_size = static_cast<size_t>(num_bin_aligned) * 2 * num_threads;
    if (new_size > hist_buf.size()) {
      hist_buf.resize(static_cast<size_t>(num_bin_aligned) * 2 * num_threads);
    }
  }

  hist_t* TempBuf() {
    if (!is_use_subcol) {
      return nullptr;
    }
    return hist_buf.data() + hist_buf.size() - num_bin_aligned * 2;
  }

  void HistMove(const hist_t* src, hist_t* dest) {
    if (!is_use_subcol) {
      return;
    }
#pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(hist_move_src.size()); ++i) {
      std::copy_n(src + hist_move_src[i], hist_move_size[i],
                  dest + hist_move_dest[i]);
    }
  }
};

/*! \brief The main class of data set,
*          which are used to training or validation
*/
class Dataset {
 public:
  friend DatasetLoader;

  LIGHTGBM_EXPORT Dataset();

  LIGHTGBM_EXPORT Dataset(data_size_t num_data);

  void Construct(
    std::vector<std::unique_ptr<BinMapper>>* bin_mappers,
    int num_total_features,
    const std::vector<std::vector<double>>& forced_bins,
    int** sample_non_zero_indices,
    double** sample_values,
    const int* num_per_col,
    int num_sample_col,
    size_t total_sample_cnt,
    const Config& io_config);

  /*! \brief Destructor */
  LIGHTGBM_EXPORT ~Dataset();

  LIGHTGBM_EXPORT bool CheckAlign(const Dataset& other) const {
    if (num_features_ != other.num_features_) {
      return false;
    }
    if (num_total_features_ != other.num_total_features_) {
      return false;
    }
    if (label_idx_ != other.label_idx_) {
      return false;
    }
    for (int i = 0; i < num_features_; ++i) {
      if (!FeatureBinMapper(i)->CheckAlign(*(other.FeatureBinMapper(i)))) {
        return false;
      }
    }
    return true;
  }

  inline void FinishOneRow(int tid, data_size_t row_idx, const std::vector<bool>& is_feature_added) {
    if (is_finish_load_) { return; }
    for (auto fidx : feature_need_push_zeros_) {
      if (is_feature_added[fidx]) { continue; }
      const int group = feature2group_[fidx];
      const int sub_feature = feature2subfeature_[fidx];
      feature_groups_[group]->PushData(tid, sub_feature, row_idx, 0.0f);
    }
  }

  inline void PushOneRow(int tid, data_size_t row_idx, const std::vector<double>& feature_values) {
    if (is_finish_load_) { return; }
    for (size_t i = 0; i < feature_values.size() && i < static_cast<size_t>(num_total_features_); ++i) {
      int feature_idx = used_feature_map_[i];
      if (feature_idx >= 0) {
        const int group = feature2group_[feature_idx];
        const int sub_feature = feature2subfeature_[feature_idx];
        feature_groups_[group]->PushData(tid, sub_feature, row_idx, feature_values[i]);
      }
    }
  }

  inline void PushOneRow(int tid, data_size_t row_idx, const std::vector<std::pair<int, double>>& feature_values) {
    if (is_finish_load_) { return; }
    std::vector<bool> is_feature_added(num_features_, false);
    for (auto& inner_data : feature_values) {
      if (inner_data.first >= num_total_features_) { continue; }
      int feature_idx = used_feature_map_[inner_data.first];
      if (feature_idx >= 0) {
        is_feature_added[feature_idx] = true;
        const int group = feature2group_[feature_idx];
        const int sub_feature = feature2subfeature_[feature_idx];
        feature_groups_[group]->PushData(tid, sub_feature, row_idx, inner_data.second);
      }
    }
    FinishOneRow(tid, row_idx, is_feature_added);
  }

  inline void PushOneData(int tid, data_size_t row_idx, int group, int sub_feature, double value) {
    feature_groups_[group]->PushData(tid, sub_feature, row_idx, value);
  }

  inline int RealFeatureIndex(int fidx) const {
    return real_feature_idx_[fidx];
  }

  inline int InnerFeatureIndex(int col_idx) const {
    return used_feature_map_[col_idx];
  }
  inline int Feature2Group(int feature_idx) const {
    return feature2group_[feature_idx];
  }
  inline int Feture2SubFeature(int feature_idx) const {
    return feature2subfeature_[feature_idx];
  }
  inline uint64_t GroupBinBoundary(int group_idx) const {
    return group_bin_boundaries_[group_idx];
  }
  inline uint64_t NumTotalBin() const {
    return group_bin_boundaries_.back();
  }

  inline std::vector<int> ValidFeatureIndices() const {
    std::vector<int> ret;
    for (int i = 0; i < num_total_features_; ++i) {
      if (used_feature_map_[i] >= 0) {
        ret.push_back(i);
      }
    }
    return ret;
  }
  void ReSize(data_size_t num_data);

  void CopySubrow(const Dataset* fullset, const data_size_t* used_indices, data_size_t num_used_indices, bool need_meta_data);

  MultiValBin* GetMultiBinFromSparseFeatures() const;

  MultiValBin* GetMultiBinFromAllFeatures() const;

  TrainingTempState* TestMultiThreadingMethod(
    score_t* gradients, score_t* hessians,
    const std::vector<int8_t>& is_feature_used, bool is_constant_hessian,
    bool force_colwise, bool force_rowwise, bool* is_hist_col_wise) const;

  LIGHTGBM_EXPORT void FinishLoad();

  LIGHTGBM_EXPORT bool SetFloatField(const char* field_name, const float* field_data, data_size_t num_element);

  LIGHTGBM_EXPORT bool SetDoubleField(const char* field_name, const double* field_data, data_size_t num_element);

  LIGHTGBM_EXPORT bool SetIntField(const char* field_name, const int* field_data, data_size_t num_element);

  LIGHTGBM_EXPORT bool GetFloatField(const char* field_name, data_size_t* out_len, const float** out_ptr);

  LIGHTGBM_EXPORT bool GetDoubleField(const char* field_name, data_size_t* out_len, const double** out_ptr);

  LIGHTGBM_EXPORT bool GetIntField(const char* field_name, data_size_t* out_len, const int** out_ptr);

  /*!
  * \brief Save current dataset into binary file, will save to "filename.bin"
  */
  LIGHTGBM_EXPORT void SaveBinaryFile(const char* bin_filename);

  LIGHTGBM_EXPORT void DumpTextFile(const char* text_filename);

  LIGHTGBM_EXPORT void CopyFeatureMapperFrom(const Dataset* dataset);

  LIGHTGBM_EXPORT void CreateValid(const Dataset* dataset);

  void InitTrain(const std::vector<int8_t>& is_feature_used,
                 bool is_colwise,
                 TrainingTempState* temp_state) const;

  void ConstructHistograms(const std::vector<int8_t>& is_feature_used,
                           const data_size_t* data_indices,
                           data_size_t num_data, const score_t* gradients,
                           const score_t* hessians, score_t* ordered_gradients,
                           score_t* ordered_hessians, bool is_constant_hessian,
                           bool is_colwise, TrainingTempState* temp_state,
                           hist_t* histogram_data) const;

  void ConstructHistogramsMultiVal(const data_size_t* data_indices,
                                   data_size_t num_data,
                                   const score_t* gradients,
                                   const score_t* hessians,
                                   bool is_constant_hessian,
                                   TrainingTempState* temp_state,
                                   hist_t* histogram_data) const;

  void FixHistogram(int feature_idx, double sum_gradient, double sum_hessian, hist_t* data) const;

  inline data_size_t Split(int feature,
                           const uint32_t* threshold, int num_threshold,  bool default_left,
                           data_size_t* data_indices, data_size_t num_data,
                           data_size_t* lte_indices, data_size_t* gt_indices) const {
    const int group = feature2group_[feature];
    const int sub_feature = feature2subfeature_[feature];
    return feature_groups_[group]->Split(sub_feature, threshold, num_threshold, default_left, data_indices, num_data, lte_indices, gt_indices);
  }

  inline int SubFeatureBinOffset(int i) const {
    const int sub_feature = feature2subfeature_[i];
    if (sub_feature == 0) {
      return 1;
    } else {
      return 0;
    }
  }

  inline int FeatureNumBin(int i) const {
    const int group = feature2group_[i];
    const int sub_feature = feature2subfeature_[i];
    return feature_groups_[group]->bin_mappers_[sub_feature]->num_bin();
  }

  inline int FeatureGroupNumBin(int group) const {
    return feature_groups_[group]->num_total_bin_;
  }

  inline const BinMapper* FeatureBinMapper(int i) const {
    const int group = feature2group_[i];
    const int sub_feature = feature2subfeature_[i];
    return feature_groups_[group]->bin_mappers_[sub_feature].get();
  }

  inline const Bin* FeatureGroupBin(int group) const {
    return feature_groups_[group]->bin_data_.get();
  }

  inline BinIterator* FeatureIterator(int i) const {
    const int group = feature2group_[i];
    const int sub_feature = feature2subfeature_[i];
    return feature_groups_[group]->SubFeatureIterator(sub_feature);
  }

  inline BinIterator* FeatureGroupIterator(int group) const {
    return feature_groups_[group]->FeatureGroupIterator();
  }

  inline bool IsMultiGroup(int i) const {
    return feature_groups_[i]->is_multi_val_;
  }

  inline double RealThreshold(int i, uint32_t threshold) const {
    const int group = feature2group_[i];
    const int sub_feature = feature2subfeature_[i];
    return feature_groups_[group]->bin_mappers_[sub_feature]->BinToValue(threshold);
  }

  // given a real threshold, find the closest threshold bin
  inline uint32_t BinThreshold(int i, double threshold_double) const {
    const int group = feature2group_[i];
    const int sub_feature = feature2subfeature_[i];
    return feature_groups_[group]->bin_mappers_[sub_feature]->ValueToBin(threshold_double);
  }

  /*!
  * \brief Get meta data pointer
  * \return Pointer of meta data
  */
  inline const Metadata& metadata() const { return metadata_; }

  /*! \brief Get Number of used features */
  inline int num_features() const { return num_features_; }

  /*! \brief Get Number of feature groups */
  inline int num_feature_groups() const { return num_groups_;}

  /*! \brief Get Number of total features */
  inline int num_total_features() const { return num_total_features_; }

  /*! \brief Get the index of label column */
  inline int label_idx() const { return label_idx_; }

  /*! \brief Get names of current data set */
  inline const std::vector<std::string>& feature_names() const { return feature_names_; }

  inline void set_feature_names(const std::vector<std::string>& feature_names) {
    if (feature_names.size() != static_cast<size_t>(num_total_features_)) {
      Log::Fatal("Size of feature_names error, should equal with total number of features");
    }
    feature_names_ = std::vector<std::string>(feature_names);
    std::unordered_set<std::string> feature_name_set;
    // replace ' ' in feature_names with '_'
    bool spaceInFeatureName = false;
    for (auto& feature_name : feature_names_) {
      // check ascii
      if (!Common::CheckASCII(feature_name)) {
        Log::Fatal("Do not support non-ASCII characters in feature name.");
      }
      // check json
      if (!Common::CheckAllowedJSON(feature_name)) {
        Log::Fatal("Do not support special JSON characters in feature name.");
      }
      if (feature_name.find(' ') != std::string::npos) {
        spaceInFeatureName = true;
        std::replace(feature_name.begin(), feature_name.end(), ' ', '_');
      }
      if (feature_name_set.count(feature_name) > 0) {
        Log::Fatal("Feature (%s) appears more than one time.", feature_name.c_str());
      }
      feature_name_set.insert(feature_name);
    }
    if (spaceInFeatureName) {
      Log::Warning("Find whitespaces in feature_names, replace with underlines");
    }
  }

  inline std::vector<std::string> feature_infos() const {
    std::vector<std::string> bufs;
    for (int i = 0; i < num_total_features_; ++i) {
      int fidx = used_feature_map_[i];
      if (fidx < 0) {
        bufs.push_back("none");
      } else {
        const auto bin_mapper = FeatureBinMapper(fidx);
        bufs.push_back(bin_mapper->bin_info_string());
      }
    }
    return bufs;
  }

  /*! \brief Get Number of data */
  inline data_size_t num_data() const { return num_data_; }

  /*! \brief Disable copy */
  Dataset& operator=(const Dataset&) = delete;
  /*! \brief Disable copy */
  Dataset(const Dataset&) = delete;

  void AddFeaturesFrom(Dataset* other);

 private:
  std::string data_filename_;
  /*! \brief Store used features */
  std::vector<std::unique_ptr<FeatureGroup>> feature_groups_;
  /*! \brief Mapper from real feature index to used index*/
  std::vector<int> used_feature_map_;
  /*! \brief Number of used features*/
  int num_features_;
  /*! \brief Number of total features*/
  int num_total_features_;
  /*! \brief Number of total data*/
  data_size_t num_data_;
  /*! \brief Store some label level data*/
  Metadata metadata_;
  /*! \brief index of label column */
  int label_idx_ = 0;
  /*! \brief store feature names */
  std::vector<std::string> feature_names_;
  /*! \brief store feature names */
  static const char* binary_file_token;
  int num_groups_;
  std::vector<int> real_feature_idx_;
  std::vector<int> feature2group_;
  std::vector<int> feature2subfeature_;
  std::vector<uint64_t> group_bin_boundaries_;
  std::vector<int> group_feature_start_;
  std::vector<int> group_feature_cnt_;
  bool is_finish_load_;
  int max_bin_;
  std::vector<int32_t> max_bin_by_feature_;
  std::vector<std::vector<double>> forced_bin_bounds_;
  int bin_construct_sample_cnt_;
  int min_data_in_bin_;
  bool use_missing_;
  bool zero_as_missing_;
  std::vector<int> feature_need_push_zeros_;
};

}  // namespace LightGBM

#endif   // LightGBM_DATA_H_
