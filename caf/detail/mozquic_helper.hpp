//
// Created by boss on 8/11/18.
//
#include <iostream>
#include "MozQuic.h"

#ifndef MOZQUIC_EXAMPLE_MOZQUIC_HELPER_H
#define MOZQUIC_EXAMPLE_MOZQUIC_HELPER_H

#define CHECK_MOZQUIC_ERR(err, msg) \
if (err) {\
  switch(err) { \
    case MOZQUIC_ERR_GENERAL: \
      std::cerr << msg << ": MOZQUIC_ERR_GENERAL" << std::endl;\
      break; \
    case MOZQUIC_ERR_INVALID: \
      std::cerr << msg << ": MOZQUIC_ERR_INVALID" << std::endl;\
      break; \
    case MOZQUIC_ERR_MEMORY:\
      std::cerr << msg << ": MOZQUIC_ERR_MEMORY" << std::endl;\
      break; \
    case MOZQUIC_ERR_IO:\
      std::cerr << msg << ": MOZQUIC_ERR_IO" << std::endl;\
      break; \
    case MOZQUIC_ERR_CRYPTO:\
      std::cerr << msg << ": MOZQUIC_ERR_CRYPTO" << std::endl;\
      break; \
    case MOZQUIC_ERR_VERSION:\
      std::cerr << msg << ": MOZQUIC_ERR_VERSION" << std::endl;\
      break; \
    case MOZQUIC_ERR_ALREADY_FINISHED:\
      std::cerr << msg << ": MOZQUIC_ERR_ALREADY_FINISHED" << std::endl;\
      break; \
    case MOZQUIC_ERR_DEFERRED:\
      std::cerr << msg << ": MOZQUIC_ERR_DEFERRED" << std::endl;\
      break; \
    default: \
      std::cerr << msg << ": UNRECOGNIZED MOZQUIC_ERR" << std::endl;\
      break; \
  }\
  exit(err);\
}

#endif //MOZQUIC_EXAMPLE_MOZQUIC_HELPER_H
