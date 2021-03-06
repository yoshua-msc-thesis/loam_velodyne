// Copyright 2013, Ji Zhang, Carnegie Mellon University
// Further contributions copyright (c) 2016, Southwest Research Institute
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// This is an implementation of the algorithm described in the following paper:
//   J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
//     Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.

#include "loam_velodyne/CtRot2DScanRegistration.h"
#include "math_utils.h"

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/filters/voxel_grid.h>


namespace loam {


CtRot2DScanRegistration::CtRot2DScanRegistration(const RegistrationParams& config)
    : ScanRegistration(config),
      _systemDelay(SYSTEM_DELAY),
      _laserRotDir(1)
{

};



bool CtRot2DScanRegistration::setup(ros::NodeHandle& node,
                                  ros::NodeHandle& privateNode)
{
  if (!ScanRegistration::setup(node, privateNode)) {
    return false;
  }

  // fetch scan mapping params
  std::string lidarName;

  if (privateNode.getParam("lidar", lidarName))
  {
    if (lidarName == "ct_2d") {
      ROS_INFO("Using continuous-rotation 2D laser scanner");
    } 

    if (!privateNode.hasParam("scanPeriod")) {
      _config.scanPeriod = 0.1;
      ROS_INFO("Set scanPeriod: %f", _config.scanPeriod);
    }
  }

  // subscribe to input cloud topic
  _subLaserCloud = node.subscribe<sensor_msgs::PointCloud2>
      ("/sync_scan_cloud_filtered", 2, &CtRot2DScanRegistration::handleCloudMessage, this);

  return true;
}



void CtRot2DScanRegistration::handleCloudMessage(const sensor_msgs::PointCloud2ConstPtr &laserCloudMsg)
{
  if (_systemDelay > 0) {
    _systemDelay--;
    return;
  }

  // fetch new input cloud
  pcl::PointCloud<pcl::PointXYZ> laserCloudIn;
  pcl::fromROSMsg(*laserCloudMsg, laserCloudIn);

  process(laserCloudIn, laserCloudMsg->header.stamp);
}



void CtRot2DScanRegistration::process(const pcl::PointCloud<pcl::PointXYZ>& laserCloudIn,
                                    const ros::Time& scanTime)
{
  size_t cloudSize = laserCloudIn.size();

  // original loam_continous
  pcl::PointXYZ laserPointFirst = laserCloudIn.points[0];
  pcl::PointXYZ laserPointLast = laserCloudIn.points[cloudSize - 1];

  float rangeFirst = calcPointDistance(laserPointFirst);
  laserPointFirst.x /= rangeFirst;
  laserPointFirst.y /= rangeFirst;
  laserPointFirst.z /= rangeFirst;

  float rangeLast = calcPointDistance(laserPointLast);
  laserPointLast.x /= rangeLast;
  laserPointLast.y /= rangeLast;
  laserPointLast.z /= rangeLast;

  float laserAngle = atan2(laserPointLast.x - laserPointFirst.x,
                           laserPointLast.y - laserPointFirst.y);

  bool newSweep = false;
  if (laserAngle * _laserRotDir < 0 &&
      scanTime.toSec() - _sweepStart.toSec() > _config.scanPeriod) {
    // ROS_WARN("######################    NEW SWEEP!    ######################");
    _laserRotDir *= -1;
    newSweep = true;
  }

  // reset internal buffers and set IMU start state based on current scan time
  reset(scanTime, newSweep);

  pcl::PointXYZI point;
  pcl::PointCloud<pcl::PointXYZI> laserCloudScan;

  // extract valid points from input cloud
  for (int i = 0; i < cloudSize; i++) {
    point.x = laserCloudIn[i].y;
    point.y = laserCloudIn[i].z;
    point.z = laserCloudIn[i].x;

    // skip NaN and INF valued points
    if (!pcl_isfinite(point.x) ||
        !pcl_isfinite(point.y) ||
        !pcl_isfinite(point.z)) {
      continue;
    }

    // skip zero valued points
    if (point.x * point.x + point.y * point.y + point.z * point.z < 0.0001) {
      continue;
    }

    // calculate relative scan time based on point orientation
    float relTime = 0;

    // project point to the start of the sweep using corresponding IMU data
    if (hasIMUData()) {
      setIMUTransformFor(relTime);
      transformToStartIMU(point);
    }

    laserCloudScan.push_back(point);
  }

  _laserCloud = laserCloudScan;

  // extract features
  extractFeatures();

  // publish result
  publishResult();
}


void CtRot2DScanRegistration::extractFeatures()
{
  size_t cloudSize = _laserCloud.size();
  size_t startPoints[4] = {5,
                           6 + int((cloudSize - 10) / 4.0),
                           6 + int((cloudSize - 10) / 2.0),
                           6 + int(3 * (cloudSize - 10) / 4.0)};
  size_t endPoints[4] = {5 + int((cloudSize - 10) / 4.0),
                         5 + int((cloudSize - 10) / 2.0),
                         5 + int(3 * (cloudSize - 10) / 4.0),
                         cloudSize - 6};

  // extract features from individual scans
  pcl::PointCloud<pcl::PointXYZI>::Ptr surfPointsLessFlatScan(new pcl::PointCloud<pcl::PointXYZI>);

  // reset scan buffers. Exclude invalid points
  setScanBuffersFor(0, cloudSize);

  // extract features from equally sized scan regions
  for (size_t i = 0; i < _config.nFeatureRegions; i++) {
    size_t sp = startPoints[i];
    size_t ep = endPoints[i];
    size_t regionSize = ep - sp;

    // reset region buffers
    setRegionBuffersFor(sp, ep-1);

    // extract corner features
    int largestPickedNum = 0;
    for (size_t j = regionSize-1; j > 0 && largestPickedNum < _config.maxCornerLessSharp; j--) {
      size_t scanIdx = _regionSortIndices[j];
      size_t regionIdx = scanIdx - sp;

      if (_scanNeighborPicked[scanIdx] == 0 &&
          _regionCurvature[regionIdx] > _config.surfaceCurvatureThreshold) {

        largestPickedNum++;
        if (largestPickedNum <= _config.maxCornerSharp) {
          _regionLabel[regionIdx] = CORNER_SHARP;
          _cornerPointsSharp.push_back(_laserCloud[scanIdx]);
        } else if (largestPickedNum <= _config.maxCornerSharp*10) {
          _regionLabel[regionIdx] = CORNER_LESS_SHARP;
          _cornerPointsLessSharp.push_back(_laserCloud[scanIdx]);
        }
        else {
          break;
        }
        
        markAsPicked(scanIdx, scanIdx);
      }
    }

    // extract flat surface features
    int smallestPickedNum = 0;
    for (size_t j = 0; j < regionSize && smallestPickedNum < _config.maxSurfaceFlat; j++) {
      size_t scanIdx = _regionSortIndices[j];
      size_t regionIdx = scanIdx - sp;

      if (_scanNeighborPicked[scanIdx] == 0 &&
          _regionCurvature[regionIdx] < _config.surfaceCurvatureThreshold) {

        smallestPickedNum++;
        _regionLabel[regionIdx] = SURFACE_FLAT;
        _surfacePointsFlat.push_back(_laserCloud[scanIdx]);

        markAsPicked(scanIdx, scanIdx);
      }
    }

    // extract less flat surface features
    for (size_t j = 0; j < regionSize; j++) {
      if (_regionLabel[j] <= SURFACE_LESS_FLAT) {
        surfPointsLessFlatScan->push_back(_laserCloud[sp + j]);
      }
    }
  }

  // down size less flat surface point cloud of current scan
  pcl::PointCloud<pcl::PointXYZI> surfPointsLessFlatScanDS;
  pcl::VoxelGrid<pcl::PointXYZI> downSizeFilter;
  downSizeFilter.setInputCloud(surfPointsLessFlatScan);
  downSizeFilter.setLeafSize(_config.lessFlatFilterSize, _config.lessFlatFilterSize, _config.lessFlatFilterSize);
  downSizeFilter.filter(surfPointsLessFlatScanDS);

  _surfacePointsLessFlat += surfPointsLessFlatScanDS;
}




} // end namespace loam
