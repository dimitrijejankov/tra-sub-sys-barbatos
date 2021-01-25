#pragma once

#include "../tensor/tensor_factory.h"
#include "../ud_functions/builtin_functions.h"

namespace bbts {

// given a bunch of implementations find the udf that works
class udf_matcher {

public:

  // the matches
  udf_matcher(std::vector<ud_impl_ptr_t> &impls) : _impls(impls) {}

  // try to find a match for the via the input strings
  // TODO this is most likely going to be slow but for now it is fine
  auto findMatch(const std::vector<std::string> &inputs,
                 const std::vector<std::string> &outputs,
                 bool require_gpu = false) {

    // go through each implementation
    for(const auto &f : _impls) {

      // check the ud is gpu
      if (f->is_gpu != require_gpu) {
        continue;
      }

      // match the input types
      for (size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i] != f->inputTypes[i]) { continue; }
      }

      // match the output types
      for (size_t i = 0; i < outputs.size(); ++i) {
        if (outputs[i] != f->outputTypes[i]) { continue; }
      }

      return f.get();
    }

    // return null
    return static_cast<ud_impl_t*>(nullptr);
  }

 private:

  // the implementations we want to match to
  std::vector<ud_impl_ptr_t> &_impls;
};
using udf_matcher_ptr = std::unique_ptr<udf_matcher>;

//
class udf_manager {
public:

  // initializes the UD manager
  udf_manager(tensor_factory_ptr_t _tensor_factory) : _tensor_factory(_tensor_factory) {

    /// 1. matrix summation
    register_udf(get_matrix_add_udf());

    /// 1.1 add the dense implementation
    register_udf_impl(std::make_unique<dense_matrix_add_t>());

    /// 2. matrix multiplication
    register_udf(get_matrix_mult_udf());

    /// 2.1 add the dense implementation
    register_udf_impl(std::make_unique<dense_matrix_mult_t>());

    /// 3. matrix multiplication
    register_udf(get_matrix_uniform_udf());

    /// 3.1 add the dense implementation
    register_udf_impl(std::make_unique<dense_uniform_t>());
  }

  // registers a udf with the system
  ud_id_t register_udf(ud_func_ptr_t _fun) {

    // check if the udf already exists?
    ud_id_t id = _udfs.size();
    auto it = _udfs_name_to_id.find(_fun->ud_name);
    if(it != _udfs_name_to_id.end()) {
      return -1;
    }

    // set the impl_id to the udf
    _fun->id = id;

    // store the function so that it is registered
    _udfs_name_to_id[_fun->ud_name] = id;
    _udfs.emplace_back(std::move(_fun));

    // return the impl_id
    return id;
  }

  // registers the implementation for a udf with the system
  ud_impl_id_t register_udf_impl(ud_impl_ptr_t _impl) {

    // check if udf is registered.
    auto it = _udfs_name_to_id.find(_impl->ud_name);
    if(it == _udfs_name_to_id.end()) {
      return {-1, -1};
    }

    // check if the number of arguments matches
    if(_impl->inputTypes.size() != _udfs[it->second]->num_in &&
       _impl->outputTypes.size() != _udfs[it->second]->num_out) {
      return {-1, -1};
    }

    // return the impl_id
    return _udfs[it->second]->add_impl(std::move(_impl));
  }

  // returns a matcher for the given ud function that will be used to figure out
  udf_matcher_ptr get_matcher_for(const std::string &ud_name) {

    // check if udf is registered.
    auto it = _udfs_name_to_id.find(ud_name);
    if(it == _udfs_name_to_id.end()) {
      return nullptr;
    }

    // make a matcher with the given implementations
    return std::make_unique<udf_matcher>(_udfs[it->second]->impls);
  }

  // return the implementation
  ud_impl_t* get_fn_impl(ud_impl_id_t _id) {

     // check if we have the ud function
    if(_id.ud_id >= _udfs.size() ||
       _id.impl_id >= _udfs[_id.ud_id]->impls.size()) {
      return nullptr;
    }

    // return the function
    return _udfs[_id.ud_id]->impls[_id.impl_id].get();
  }

private:

  // we use this to grab types from strings
  tensor_factory_ptr_t _tensor_factory;

  // all the registered udfs
  std::vector<ud_func_ptr_t> _udfs;

  // maps the udf name to the impl_id
  std::unordered_map<std::string, ud_id_t> _udfs_name_to_id;
};

using udf_manager_ptr = std::shared_ptr<udf_manager>;

}
