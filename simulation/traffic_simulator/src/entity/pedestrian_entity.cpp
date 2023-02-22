// Copyright 2015 TIER IV, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <quaternion_operation/quaternion_operation.h>

#include <algorithm>
#include <memory>
#include <string>
#include <traffic_simulator/entity/pedestrian_entity.hpp>
#include <vector>

namespace traffic_simulator
{
namespace entity
{
PedestrianEntity::PedestrianEntity(
  const std::string & name,
  const traffic_simulator::entity_status::CanonicalizedEntityStatus & entity_status,
  const std::shared_ptr<hdmap_utils::HdMapUtils> & hdmap_utils_ptr,
  const traffic_simulator_msgs::msg::PedestrianParameters & parameters,
  const std::string & plugin_name)
: EntityBase(name, entity_status, hdmap_utils_ptr),
  plugin_name(plugin_name),
  loader_(pluginlib::ClassLoader<entity_behavior::BehaviorPluginBase>(
    "traffic_simulator", "entity_behavior::BehaviorPluginBase")),
  behavior_plugin_ptr_(loader_.createSharedInstance(plugin_name)),
  route_planner_(hdmap_utils_ptr_)
{
  behavior_plugin_ptr_->configure(rclcpp::get_logger(name));
  behavior_plugin_ptr_->setPedestrianParameters(parameters);
  behavior_plugin_ptr_->setDebugMarker({});
  behavior_plugin_ptr_->setBehaviorParameter(traffic_simulator_msgs::msg::BehaviorParameter());
  behavior_plugin_ptr_->setHdMapUtils(hdmap_utils_ptr_);
}

void PedestrianEntity::appendDebugMarker(visualization_msgs::msg::MarkerArray & marker_array)
{
  const auto marker = behavior_plugin_ptr_->getDebugMarker();
  std::copy(marker.begin(), marker.end(), std::back_inserter(marker_array.markers));
}

void PedestrianEntity::requestAssignRoute(
  const std::vector<traffic_simulator::lanelet_pose::CanonicalizedLaneletPose> & waypoints)
{
  behavior_plugin_ptr_->setRequest(behavior::Request::FOLLOW_LANE);
  if (status_.lanelet_pose_valid) {
    route_planner_.setWaypoints(waypoints);
  }
}

void PedestrianEntity::requestAssignRoute(const std::vector<geometry_msgs::msg::Pose> & waypoints)
{
  std::vector<traffic_simulator::lanelet_pose::CanonicalizedLaneletPose> route;
  for (const auto & waypoint : waypoints) {
    const auto lanelet_waypoint =
      hdmap_utils_ptr_->toLaneletPose(waypoint, getStatus().bounding_box, true);
    if (lanelet_waypoint) {
      route.emplace_back(traffic_simulator::lanelet_pose::CanonicalizedLaneletPose(
        lanelet_waypoint.get(), hdmap_utils_ptr_));
    } else {
      THROW_SEMANTIC_ERROR("Waypoint of pedestrian entity should be on lane.");
    }
  }
  requestAssignRoute(route);
}

std::string PedestrianEntity::getCurrentAction() const
{
  if (!npc_logic_started_) {
    return "waiting";
  }
  return behavior_plugin_ptr_->getCurrentAction();
}

auto PedestrianEntity::getDefaultDynamicConstraints() const
  -> const traffic_simulator_msgs::msg::DynamicConstraints &
{
  static auto default_dynamic_constraints = traffic_simulator_msgs::msg::DynamicConstraints();
  default_dynamic_constraints.max_acceleration = 1.0;
  default_dynamic_constraints.max_acceleration_rate = 1.0;
  default_dynamic_constraints.max_deceleration = 1.0;
  default_dynamic_constraints.max_deceleration_rate = 1.0;
  return default_dynamic_constraints;
}

std::vector<std::int64_t> PedestrianEntity::getRouteLanelets(double horizon)
{
  if (status_.lanelet_pose_valid) {
    return route_planner_.getRouteLanelets(
      traffic_simulator::lanelet_pose::CanonicalizedLaneletPose(
        status_.lanelet_pose, hdmap_utils_ptr_),
      horizon);
  } else {
    return {};
  }
}

auto PedestrianEntity::getObstacle() -> boost::optional<traffic_simulator_msgs::msg::Obstacle>
{
  return boost::none;
}

auto PedestrianEntity::getGoalPoses() -> std::vector<traffic_simulator_msgs::msg::LaneletPose>
{
  return route_planner_.getGoalPoses();
}

const traffic_simulator_msgs::msg::WaypointsArray PedestrianEntity::getWaypoints()
{
  return traffic_simulator_msgs::msg::WaypointsArray();
}

void PedestrianEntity::requestWalkStraight()
{
  behavior_plugin_ptr_->setRequest(behavior::Request::WALK_STRAIGHT);
}

void PedestrianEntity::requestAcquirePosition(
  const traffic_simulator::lanelet_pose::CanonicalizedLaneletPose & lanelet_pose)
{
  behavior_plugin_ptr_->setRequest(behavior::Request::FOLLOW_LANE);
  if (status_.lanelet_pose_valid) {
    route_planner_.setWaypoints({lanelet_pose});
  }
}

void PedestrianEntity::requestAcquirePosition(const geometry_msgs::msg::Pose & map_pose)
{
  if (const auto lanelet_pose =
        hdmap_utils_ptr_->toLaneletPose(map_pose, getStatus().bounding_box, true);
      lanelet_pose) {
    requestAcquirePosition(traffic_simulator::lanelet_pose::CanonicalizedLaneletPose(
      lanelet_pose.get(), hdmap_utils_ptr_));
  } else {
    THROW_SEMANTIC_ERROR("Goal of the pedestrian entity should be on lane.");
  }
}

void PedestrianEntity::cancelRequest()
{
  behavior_plugin_ptr_->setRequest(behavior::Request::NONE);
  route_planner_.cancelRoute();
}

auto PedestrianEntity::getEntityTypename() const -> const std::string &
{
  static const std::string result = "PedestrianEntity";
  return result;
}

void PedestrianEntity::setTrafficLightManager(
  const std::shared_ptr<traffic_simulator::TrafficLightManagerBase> & ptr)
{
  EntityBase::setTrafficLightManager(ptr);
  behavior_plugin_ptr_->setTrafficLightManager(traffic_light_manager_);
}

auto PedestrianEntity::getBehaviorParameter() const
  -> traffic_simulator_msgs::msg::BehaviorParameter
{
  return behavior_plugin_ptr_->getBehaviorParameter();
}

void PedestrianEntity::setBehaviorParameter(
  const traffic_simulator_msgs::msg::BehaviorParameter & behavior_parameter)
{
  behavior_plugin_ptr_->setBehaviorParameter(behavior_parameter);
}

void PedestrianEntity::setAccelerationLimit(double acceleration)
{
  if (acceleration <= 0.0) {
    THROW_SEMANTIC_ERROR("Acceleration limit should be over zero.");
  }
  auto behavior_parameter = getBehaviorParameter();
  behavior_parameter.dynamic_constraints.max_acceleration = acceleration;
  setBehaviorParameter(behavior_parameter);
}

void PedestrianEntity::setAccelerationRateLimit(double acceleration_rate)
{
  if (acceleration_rate <= 0.0) {
    THROW_SEMANTIC_ERROR("Acceleration rate limit should be over zero.");
  }
  auto behavior_parameter = getBehaviorParameter();
  behavior_parameter.dynamic_constraints.max_acceleration_rate = acceleration_rate;
  setBehaviorParameter(behavior_parameter);
}

void PedestrianEntity::setDecelerationLimit(double deceleration)
{
  if (deceleration <= 0.0) {
    THROW_SEMANTIC_ERROR("Deceleration limit should be over zero.");
  }
  auto behavior_parameter = getBehaviorParameter();
  behavior_parameter.dynamic_constraints.max_deceleration = deceleration;
  setBehaviorParameter(behavior_parameter);
}

void PedestrianEntity::setDecelerationRateLimit(double deceleration_rate)
{
  if (deceleration_rate <= 0.0) {
    THROW_SEMANTIC_ERROR("Deceleration rate limit should be over zero.");
  }
  auto behavior_parameter = getBehaviorParameter();
  behavior_parameter.dynamic_constraints.max_deceleration_rate = deceleration_rate;
  setBehaviorParameter(behavior_parameter);
}

void PedestrianEntity::onUpdate(double current_time, double step_time)
{
  EntityBase::onUpdate(current_time, step_time);
  if (npc_logic_started_) {
    behavior_plugin_ptr_->setOtherEntityStatus(other_status_);
    behavior_plugin_ptr_->setEntityTypeList(entity_type_list_);
    behavior_plugin_ptr_->setEntityStatus(status_);
    behavior_plugin_ptr_->setTargetSpeed(target_speed_);
    behavior_plugin_ptr_->setRouteLanelets(getRouteLanelets());
    behavior_plugin_ptr_->update(current_time, step_time);
    auto status_updated = behavior_plugin_ptr_->getUpdatedStatus();
    if (status_updated.lanelet_pose_valid) {
      auto following_lanelets =
        hdmap_utils_ptr_->getFollowingLanelets(status_updated.lanelet_pose.lanelet_id);
      auto l = hdmap_utils_ptr_->getLaneletLength(status_updated.lanelet_pose.lanelet_id);
      if (following_lanelets.size() == 1 && l <= status_updated.lanelet_pose.s) {
        stopAtEndOfRoad();
        return;
      }
    }

    setStatus(traffic_simulator::entity_status::CanonicalizedEntityStatus(
      status_updated, hdmap_utils_ptr_));
    updateStandStillDuration(step_time);
    updateTraveledDistance(step_time);
  } else {
    updateEntityStatusTimestamp(current_time);
  }
  EntityBase::onPostUpdate(current_time, step_time);
}
}  // namespace entity
}  // namespace traffic_simulator
