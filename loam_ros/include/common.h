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

#ifndef LOAM_ROS_COMMON_H
#define LOAM_ROS_COMMON_H

#include <fstream>

#include <Eigen/Dense>
#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <ecl/time/stopwatch.hpp>
#include <pcl/common/eigen.h>
#include <pcl/common/transforms.h>


namespace loam {


Eigen::Matrix4d getTransformFromQuaternion(const Eigen::Quaterniond q) {
  Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
  T.block<3,3>(0,0) = q.toRotationMatrix();
  return T;
}

// const Eigen::Quaterniond rot_loam(0.7071, -0.7071, 0, 0);
const Eigen::Quaterniond rot_loam(0.0005631, -0.0005631, 0.7071065, 0.7071065);
const Eigen::Matrix4d T_fix_loam = getTransformFromQuaternion(rot_loam.inverse());

/** \brief Construct a new point cloud message from the specified information and publish it via the given publisher.
 *
 * @tparam PointT the point type
 * @param publisher the publisher instance
 * @param cloud the cloud to publish
 * @param stamp the time stamp of the cloud message
 * @param frameID the message frame ID
 */
template <typename PointT>
inline void publishCloudMsg(ros::Publisher& publisher,
                            const pcl::PointCloud<PointT>& cloud,
                            const ros::Time& stamp,
                            std::string frameID) {
  sensor_msgs::PointCloud2 msg;
  pcl::toROSMsg(cloud, msg);
  msg.header.stamp = stamp;
  msg.header.frame_id = frameID;
  publisher.publish(msg);
}

// TODO doc
Eigen::Isometry3d convertOdometryToEigenIsometry(const nav_msgs::Odometry odom_msg) {
  const auto& orientation = odom_msg.pose.pose.orientation;
  const auto& position = odom_msg.pose.pose.position;

  Eigen::Quaterniond quat;
  quat.w() = orientation.w;
  quat.x() = orientation.x;
  quat.y() = orientation.y;
  quat.z() = orientation.z;

  Eigen::Isometry3d isometry = Eigen::Isometry3d::Identity();
  isometry.linear() = quat.toRotationMatrix();
  isometry.translation() = Eigen::Vector3d(position.x, position.y, position.z);
  
  return isometry;
}

// TODO doc
nav_msgs::Odometry convertEigenIsometryToOdometry(const std::string frame_id,
                                                  const Eigen::Isometry3d& odom,
                                                  const ros::Time stamp = ros::Time::now()) {
  nav_msgs::Odometry odom_msg;

  Eigen::Vector3d pos = odom.translation();
  Eigen::Quaterniond rot(odom.rotation());

  odom_msg.header.stamp = stamp;
  odom_msg.header.frame_id = frame_id;
  odom_msg.pose.pose.position.x = pos.x();
  odom_msg.pose.pose.position.y = pos.y();
  odom_msg.pose.pose.position.z = pos.z();
  odom_msg.pose.pose.orientation.x = rot.x();
  odom_msg.pose.pose.orientation.y = rot.y();
  odom_msg.pose.pose.orientation.z = rot.z();
  odom_msg.pose.pose.orientation.w = rot.w();

  return odom_msg;
}


} // end namespace loam

#endif // LOAM_COMMON_H
