#pragma once

#ifdef BUILDING_TN_EXTENSION
#define TN_EXTERN __declspec(dllexport)
#include "v8.h"
#else
#define TN_EXTERN __declspec(dllimport)
#include "v8/v8.h"
#endif

namespace titan {
class TN_EXTERN NodeBindings {
public:
  static NodeBindings& getInstance();

  void initializeContext(v8::Local<v8::Context> context);
  void update();

private:
  NodeBindings();
  ~NodeBindings();

  struct PImpl;
  std::unique_ptr<PImpl> m_impl;  ///< private implementation data
};
}  // namespace titan
