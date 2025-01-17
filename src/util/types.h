#ifndef UTIL_TYPES_H_
#define UTIL_TYPES_H_

#include <stdint.h>

#include <Eigen/Core>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>

#include "util/alignment.h"

#ifdef _MSC_VER
#if _MSC_VER >= 1600
#include <cstdint>
#else
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#endif
#elif __GNUC__ >= 3
#include <cstdint>
#endif

// Define non-copyable or non-movable classes.
#define NON_COPYABLE(class_name)          \
  class_name(class_name const&) = delete; \
  void operator=(class_name const& obj) = delete;
#define NON_MOVABLE(class_name) class_name(class_name&&) = delete;

#include <Eigen/Core>

namespace Eigen {

typedef Eigen::Matrix<float, 3, 4> Matrix3x4f;
typedef Eigen::Matrix<double, 3, 4> Matrix3x4d;
typedef Eigen::Matrix<uint8_t, 3, 1> Vector3ub;
typedef Eigen::Matrix<uint8_t, 4, 1> Vector4ub;
typedef Eigen::Matrix<double, 6, 1> Vector6d;

}  // namespace Eigen

namespace gopt {

////////////////////////////////////////////////////////////////////////////////
// Index types, determines the maximum number of objects.
////////////////////////////////////////////////////////////////////////////////

// Unique identifier for cameras.
typedef uint32_t camera_t;

// Unique identifier for images.
typedef uint32_t image_t;

// Each image pair gets a unique ID, see `Database::ImagePairToPairId`.
typedef uint64_t image_pair_t;

typedef std::pair<image_t, image_t> ImagePair;
typedef std::pair<std::string, std::string> ImageNamePair;
typedef std::tuple<image_t, image_t, image_t> ImageIdTriplet;

// Values for invalid identifiers or indices.
const camera_t kInvalidCameraId = std::numeric_limits<camera_t>::max();
const image_t kInvalidImageId = std::numeric_limits<image_t>::max();
const image_pair_t kInvalidImagePairId =
    std::numeric_limits<image_pair_t>::max();

// A struct to hold match and projection data between two views. It is assumed
// that the first view is at the origin with an identity rotation.
struct TwoViewGeometry {
  TwoViewGeometry()
      : rotation_2(Eigen::Vector3d::Zero()),
        translation_2(Eigen::Vector3d::Zero()),
        visibility_score(1) {}

  Eigen::Vector3d rotation_2;
  Eigen::Vector3d translation_2;

  // The visibility score is computed based on the inlier features from 2-view
  // geometry estimation. This score is similar to the number of verified
  // matches, but has a spatial weighting to encourage good coverage of the
  // image by the inliers. The visibility score here is the sum of the
  // visibility scores for each image.
  int visibility_score = 1;
};

}  // namespace gopt

// This file provides specializations of the templated hash function for
// custom types. These are used for comparison in unordered sets/maps.
namespace std {

// Hash function specialization for uint32_t pairs, e.g., image_t or camera_t.
template <>
struct hash<std::pair<uint32_t, uint32_t>> {
  std::size_t operator()(const std::pair<uint32_t, uint32_t>& p) const {
    const uint64_t s = (static_cast<uint64_t>(p.first) << 32) +
                       static_cast<uint64_t>(p.second);
    return std::hash<uint64_t>()(s);
  }
};

}  // namespace std

#endif  // UTIL_TYPES_H_
