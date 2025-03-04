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

#ifndef TRAFFIC_SIMULATOR__ENTITY__ENTITY_MANAGER_HPP_
#define TRAFFIC_SIMULATOR__ENTITY__ENTITY_MANAGER_HPP_

#include <tf2/LinearMath/Quaternion.h>
#include <tf2_ros/static_transform_broadcaster.h>
#include <tf2_ros/transform_broadcaster.h>

#include <autoware_perception_msgs/msg/traffic_signal_array.hpp>
#include <memory>
#include <optional>
#include <rclcpp/node_interfaces/get_node_topics_interface.hpp>
#include <rclcpp/node_interfaces/node_topics_interface.hpp>
#include <rclcpp/rclcpp.hpp>
#include <scenario_simulator_exception/exception.hpp>
#include <stdexcept>
#include <string>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <traffic_simulator/api/configuration.hpp>
#include <traffic_simulator/data_type/lane_change.hpp>
#include <traffic_simulator/data_type/speed_change.hpp>
#include <traffic_simulator/entity/ego_entity.hpp>
#include <traffic_simulator/entity/entity_base.hpp>
#include <traffic_simulator/entity/misc_object_entity.hpp>
#include <traffic_simulator/entity/pedestrian_entity.hpp>
#include <traffic_simulator/entity/vehicle_entity.hpp>
#include <traffic_simulator/hdmap_utils/hdmap_utils.hpp>
#include <traffic_simulator/traffic/traffic_sink.hpp>
#include <traffic_simulator/traffic_lights/configurable_rate_updater.hpp>
#include <traffic_simulator/traffic_lights/traffic_light_marker_publisher.hpp>
#include <traffic_simulator/traffic_lights/traffic_light_publisher.hpp>
#include <traffic_simulator_msgs/msg/behavior_parameter.hpp>
#include <traffic_simulator_msgs/msg/bounding_box.hpp>
#include <traffic_simulator_msgs/msg/entity_status_with_trajectory_array.hpp>
#include <traffic_simulator_msgs/msg/vehicle_parameters.hpp>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <visualization_msgs/msg/marker_array.hpp>

/// @todo find some shared space for this function
template <typename T>
static auto getParameter(const std::string & name, T value = {})
{
  rclcpp::Node node{"get_parameter", "simulation"};

  node.declare_parameter<T>(name, value);
  node.get_parameter<T>(name, value);

  return value;
}

namespace traffic_simulator
{
namespace entity
{
class LaneletMarkerQoS : public rclcpp::QoS
{
public:
  explicit LaneletMarkerQoS(std::size_t depth = 1) : rclcpp::QoS(depth) { transient_local(); }
};

class EntityMarkerQoS : public rclcpp::QoS
{
public:
  explicit EntityMarkerQoS(std::size_t depth = 100) : rclcpp::QoS(depth) {}
};

class EntityManager
{
  Configuration configuration;

  std::shared_ptr<rclcpp::node_interfaces::NodeTopicsInterface> node_topics_interface;

  tf2_ros::StaticTransformBroadcaster broadcaster_;
  tf2_ros::TransformBroadcaster base_link_broadcaster_;

  const rclcpp::Clock::SharedPtr clock_ptr_;

  std::unordered_map<std::string, std::shared_ptr<traffic_simulator::entity::EntityBase>> entities_;

  double step_time_;

  double current_time_;

  bool npc_logic_started_;

  using EntityStatusWithTrajectoryArray =
    traffic_simulator_msgs::msg::EntityStatusWithTrajectoryArray;
  const rclcpp::Publisher<EntityStatusWithTrajectoryArray>::SharedPtr entity_status_array_pub_ptr_;

  using MarkerArray = visualization_msgs::msg::MarkerArray;
  const rclcpp::Publisher<MarkerArray>::SharedPtr lanelet_marker_pub_ptr_;

  const std::shared_ptr<hdmap_utils::HdMapUtils> hdmap_utils_ptr_;

  MarkerArray markers_raw_;

  const std::shared_ptr<TrafficLightManager> conventional_traffic_light_manager_ptr_;
  const std::shared_ptr<TrafficLightMarkerPublisher>
    conventional_traffic_light_marker_publisher_ptr_;

  const std::shared_ptr<TrafficLightManager> v2i_traffic_light_manager_ptr_;
  const std::shared_ptr<TrafficLightMarkerPublisher> v2i_traffic_light_marker_publisher_ptr_;
  const std::shared_ptr<TrafficLightPublisherBase> v2i_traffic_light_legacy_topic_publisher_ptr_;
  const std::shared_ptr<TrafficLightPublisherBase> v2i_traffic_light_publisher_ptr_;
  ConfigurableRateUpdater v2i_traffic_light_updater_, conventional_traffic_light_updater_;

public:
  template <typename Node>
  auto getOrigin(Node & node) const
  {
    geographic_msgs::msg::GeoPoint origin;
    {
      if (!node.has_parameter("origin_latitude")) {
        node.declare_parameter("origin_latitude", 0.0);
      }
      if (!node.has_parameter("origin_longitude")) {
        node.declare_parameter("origin_longitude", 0.0);
      }
      node.get_parameter("origin_latitude", origin.latitude);
      node.get_parameter("origin_longitude", origin.longitude);
    }

    return origin;
  }

  template <typename... Ts>
  auto makeV2ITrafficLightPublisher(Ts &&... xs) -> std::shared_ptr<TrafficLightPublisherBase>
  {
    if (const auto architecture_type =
          getParameter<std::string>("architecture_type", "awf/universe");
        architecture_type.find("awf/universe") != std::string::npos) {
      return std::make_shared<
        TrafficLightPublisher<autoware_perception_msgs::msg::TrafficSignalArray>>(
        std::forward<decltype(xs)>(xs)...);
    } else {
      throw common::SemanticError(
        "Unexpected architecture_type ", std::quoted(architecture_type),
        " given for V2I traffic lights simulation.");
    }
  }

  template <class NodeT, class AllocatorT = std::allocator<void>>
  explicit EntityManager(NodeT && node, const Configuration & configuration)
  : configuration(configuration),
    node_topics_interface(rclcpp::node_interfaces::get_node_topics_interface(node)),
    broadcaster_(node),
    base_link_broadcaster_(node),
    clock_ptr_(node->get_clock()),
    current_time_(std::numeric_limits<double>::quiet_NaN()),
    npc_logic_started_(false),
    entity_status_array_pub_ptr_(rclcpp::create_publisher<EntityStatusWithTrajectoryArray>(
      node, "entity/status", EntityMarkerQoS(),
      rclcpp::PublisherOptionsWithAllocator<AllocatorT>())),
    lanelet_marker_pub_ptr_(rclcpp::create_publisher<MarkerArray>(
      node, "lanelet/marker", LaneletMarkerQoS(),
      rclcpp::PublisherOptionsWithAllocator<AllocatorT>())),
    hdmap_utils_ptr_(std::make_shared<hdmap_utils::HdMapUtils>(
      configuration.lanelet2_map_path(), getOrigin(*node))),
    markers_raw_(hdmap_utils_ptr_->generateMarker()),
    conventional_traffic_light_manager_ptr_(
      std::make_shared<TrafficLightManager>(hdmap_utils_ptr_)),
    conventional_traffic_light_marker_publisher_ptr_(
      std::make_shared<TrafficLightMarkerPublisher>(conventional_traffic_light_manager_ptr_, node)),
    v2i_traffic_light_manager_ptr_(std::make_shared<TrafficLightManager>(hdmap_utils_ptr_)),
    v2i_traffic_light_marker_publisher_ptr_(
      std::make_shared<TrafficLightMarkerPublisher>(v2i_traffic_light_manager_ptr_, node)),
    v2i_traffic_light_legacy_topic_publisher_ptr_(
      makeV2ITrafficLightPublisher("/v2x/traffic_signals", node, hdmap_utils_ptr_)),
    v2i_traffic_light_publisher_ptr_(makeV2ITrafficLightPublisher(
      "/perception/traffic_light_recognition/external/traffic_signals", node, hdmap_utils_ptr_)),
    v2i_traffic_light_updater_(
      node,
      [this]() {
        v2i_traffic_light_marker_publisher_ptr_->publish();
        v2i_traffic_light_publisher_ptr_->publish(
          clock_ptr_->now(), v2i_traffic_light_manager_ptr_->generateUpdateTrafficLightsRequest());
        v2i_traffic_light_legacy_topic_publisher_ptr_->publish(
          clock_ptr_->now(), v2i_traffic_light_manager_ptr_->generateUpdateTrafficLightsRequest());
      }),
    conventional_traffic_light_updater_(
      node, [this]() { conventional_traffic_light_marker_publisher_ptr_->publish(); })
  {
    updateHdmapMarker();
  }

  ~EntityManager() = default;

public:
#define FORWARD_GETTER_TO_TRAFFIC_LIGHT_MANAGER(NAME)                 \
  template <typename... Ts>                                           \
  decltype(auto) getConventional##NAME(Ts &&... xs) const             \
  {                                                                   \
    return conventional_traffic_light_manager_ptr_->get##NAME(xs...); \
  }                                                                   \
  static_assert(true, "");                                            \
  template <typename... Ts>                                           \
  decltype(auto) getV2I##NAME(Ts &&... xs) const                      \
  {                                                                   \
    return v2i_traffic_light_manager_ptr_->get##NAME(xs...);          \
  }                                                                   \
  static_assert(true, "")

  FORWARD_GETTER_TO_TRAFFIC_LIGHT_MANAGER(TrafficLights);
  FORWARD_GETTER_TO_TRAFFIC_LIGHT_MANAGER(TrafficLight);

#undef FORWARD_GETTER_TO_TRAFFIC_LIGHT_MANAGER

  auto generateUpdateRequestForConventionalTrafficLights()
  {
    return conventional_traffic_light_manager_ptr_->generateUpdateTrafficLightsRequest();
  }

  auto resetConventionalTrafficLightPublishRate(double rate) -> void
  {
    conventional_traffic_light_updater_.resetUpdateRate(rate);
  }

  auto resetV2ITrafficLightPublishRate(double rate) -> void
  {
    v2i_traffic_light_updater_.resetUpdateRate(rate);
  }

  auto setConventionalTrafficLightConfidence(lanelet::Id id, double confidence) -> void
  {
    for (auto & traffic_light : conventional_traffic_light_manager_ptr_->getTrafficLights(id)) {
      traffic_light.get().confidence = confidence;
    }
  }

  // clang-format off
#define FORWARD_TO_HDMAP_UTILS(NAME)                                  \
  /*!                                                                 \
   @brief Forward to arguments to the HDMapUtils::NAME function.      \
   @return return value of the HDMapUtils::NAME function.             \
   @note This function was defined by FORWARD_TO_HDMAP_UTILS macro.   \
   */                                                                 \
  template <typename... Ts>                                           \
  decltype(auto) NAME(Ts &&... xs) const                              \
  {                                                                   \
    return hdmap_utils_ptr_->NAME(std::forward<decltype(xs)>(xs)...); \
  }                                                                   \
  static_assert(true, "")
  // clang-format on

  FORWARD_TO_HDMAP_UTILS(getLaneletLength);
  FORWARD_TO_HDMAP_UTILS(toLaneletPose);
  // FORWARD_TO_HDMAP_UTILS(toMapPose);

#undef FORWARD_TO_HDMAP_UTILS

  // clang-format off
#define FORWARD_TO_ENTITY(IDENTIFIER, ...)                                       \
  /*!                                                                            \
   @brief Forward to arguments to the EntityBase::IDENTIFIER function.           \
   @return return value of the EntityBase::IDENTIFIER function.                  \
   @note This function was defined by FORWARD_TO_ENTITY macro.    　  　　　　　   \
   */                                                                            \
  template <typename... Ts>                                                      \
  decltype(auto) IDENTIFIER(const std::string & name, Ts &&... xs) __VA_ARGS__   \
  try {                                                                          \
    return entities_.at(name)->IDENTIFIER(std::forward<decltype(xs)>(xs)...);    \
  } catch (const std::out_of_range &) {                                          \
    THROW_SEMANTIC_ERROR("entity : ", name, "does not exist");                   \
  }                                                                              \
  static_assert(true, "")
  // clang-format on

  FORWARD_TO_ENTITY(asFieldOperatorApplication, const);
  FORWARD_TO_ENTITY(fillLaneletPose, const);
  FORWARD_TO_ENTITY(get2DPolygon, const);
  FORWARD_TO_ENTITY(getBehaviorParameter, const);
  FORWARD_TO_ENTITY(getBoundingBox, const);
  FORWARD_TO_ENTITY(getCurrentAccel, const);
  FORWARD_TO_ENTITY(getCurrentAction, const);
  FORWARD_TO_ENTITY(getCurrentTwist, const);
  FORWARD_TO_ENTITY(getDefaultMatchingDistanceForLaneletPoseCalculation, const);
  FORWARD_TO_ENTITY(getEntityStatusBeforeUpdate, const);
  FORWARD_TO_ENTITY(getEntityType, const);
  FORWARD_TO_ENTITY(getEntityTypename, const);
  FORWARD_TO_ENTITY(getLaneletPose, const);
  FORWARD_TO_ENTITY(getLinearJerk, const);
  FORWARD_TO_ENTITY(getMapPose, const);
  FORWARD_TO_ENTITY(getMapPoseFromRelativePose, const);
  FORWARD_TO_ENTITY(getRouteLanelets, const);
  FORWARD_TO_ENTITY(getStandStillDuration, const);
  FORWARD_TO_ENTITY(getTraveledDistance, const);
  FORWARD_TO_ENTITY(laneMatchingSucceed, const);
  FORWARD_TO_ENTITY(activateOutOfRangeJob, );
  FORWARD_TO_ENTITY(cancelRequest, );
  FORWARD_TO_ENTITY(isControlledBySimulator, );
  FORWARD_TO_ENTITY(requestAcquirePosition, );
  FORWARD_TO_ENTITY(requestAssignRoute, );
  FORWARD_TO_ENTITY(requestFollowTrajectory, );
  FORWARD_TO_ENTITY(requestLaneChange, );
  FORWARD_TO_ENTITY(requestWalkStraight, );
  FORWARD_TO_ENTITY(setAcceleration, );
  FORWARD_TO_ENTITY(setAccelerationLimit, );
  FORWARD_TO_ENTITY(setAccelerationRateLimit, );
  FORWARD_TO_ENTITY(setBehaviorParameter, );
  FORWARD_TO_ENTITY(setControlledBySimulator, );
  FORWARD_TO_ENTITY(setDecelerationLimit, );
  FORWARD_TO_ENTITY(setDecelerationRateLimit, );
  FORWARD_TO_ENTITY(setLinearJerk, );
  FORWARD_TO_ENTITY(setLinearVelocity, );
  FORWARD_TO_ENTITY(setMapPose, );
  FORWARD_TO_ENTITY(setTwist, );
  FORWARD_TO_ENTITY(setVelocityLimit, );

#undef FORWARD_TO_ENTITY

  visualization_msgs::msg::MarkerArray makeDebugMarker() const;

  bool trafficLightsChanged();

  void requestSpeedChange(const std::string & name, double target_speed, bool continuous);

  void requestSpeedChange(
    const std::string & name, const double target_speed, const speed_change::Transition transition,
    const speed_change::Constraint constraint, const bool continuous);

  void requestSpeedChange(
    const std::string & name, const speed_change::RelativeTargetSpeed & target_speed,
    bool continuous);

  void requestSpeedChange(
    const std::string & name, const speed_change::RelativeTargetSpeed & target_speed,
    const speed_change::Transition transition, const speed_change::Constraint constraint,
    const bool continuous);

  auto updateNpcLogic(const std::string & name) -> const CanonicalizedEntityStatus &;

  void broadcastEntityTransform();

  void broadcastTransform(
    const geometry_msgs::msg::PoseStamped & pose, const bool static_transform = true);

  bool checkCollision(const std::string & name0, const std::string & name1);

  bool despawnEntity(const std::string & name);

  bool entityExists(const std::string & name);

  auto getCurrentTime() const noexcept -> double;

  auto getEntityNames() const -> const std::vector<std::string>;

  auto getEntity(const std::string & name) const
    -> std::shared_ptr<traffic_simulator::entity::EntityBase>;

  auto getEntityStatus(const std::string & name) const -> CanonicalizedEntityStatus;

  auto getHdmapUtils() -> const std::shared_ptr<hdmap_utils::HdMapUtils> &;

  auto getNumberOfEgo() const -> std::size_t;

  auto getObstacle(const std::string & name)
    -> std::optional<traffic_simulator_msgs::msg::Obstacle>;

  auto getPedestrianParameters(const std::string & name) const
    -> const traffic_simulator_msgs::msg::PedestrianParameters &;

  auto getStepTime() const noexcept -> double;

  auto getVehicleParameters(const std::string & name) const
    -> const traffic_simulator_msgs::msg::VehicleParameters &;

  auto getWaypoints(const std::string & name) -> traffic_simulator_msgs::msg::WaypointsArray;

  template <typename T>
  auto getGoalPoses(const std::string & name) -> std::vector<T>
  {
    if constexpr (std::is_same_v<std::decay_t<T>, CanonicalizedLaneletPose>) {
      if (not npc_logic_started_) {
        return {};
      } else {
        return entities_.at(name)->getGoalPoses();
      }
    } else {
      if (not npc_logic_started_) {
        return {};
      } else {
        std::vector<geometry_msgs::msg::Pose> poses;
        for (const auto & lanelet_pose : getGoalPoses<CanonicalizedLaneletPose>(name)) {
          poses.push_back(toMapPose(lanelet_pose));
        }
        return poses;
      }
    }
  }

  template <typename EntityType>
  bool is(const std::string & name) const
  {
    return dynamic_cast<EntityType const *>(entities_.at(name).get()) != nullptr;
  }

  bool isEgoSpawned() const;

  const std::string getEgoName() const;

  bool isInLanelet(const std::string & name, const lanelet::Id lanelet_id, const double tolerance);

  bool isStopping(const std::string & name) const;

  bool reachPosition(
    const std::string & name, const geometry_msgs::msg::Pose & target_pose,
    const double tolerance) const;
  bool reachPosition(
    const std::string & name, const CanonicalizedLaneletPose & lanelet_pose,
    const double tolerance) const;
  bool reachPosition(
    const std::string & name, const std::string & target_name, const double tolerance) const;

  void requestLaneChange(
    const std::string & name, const traffic_simulator::lane_change::Direction & direction);

  /**
   * @brief Reset behavior plugin of the target entity.
   * The internal behavior is to take over the various parameters and save them, then respawn the Entity and set the parameters.
   * @param name The name of the target entity.
   * @param behavior_plugin_name The name of the behavior plugin you want to set.
   * @sa traffic_simulator::entity::PedestrianEntity::BuiltinBehavior
   * @sa traffic_simulator::entity::VehicleEntity::BuiltinBehavior
   */
  void resetBehaviorPlugin(const std::string & name, const std::string & behavior_plugin_name);

  auto setEntityStatus(const std::string & name, const CanonicalizedEntityStatus &) -> void;

  void setVerbose(const bool verbose);

  template <typename Entity, typename Pose, typename Parameters, typename... Ts>
  auto spawnEntity(
    const std::string & name, const Pose & pose, const Parameters & parameters, Ts &&... xs)
  {
    auto makeEntityStatus = [&]() {
      EntityStatus entity_status;

      if constexpr (std::is_same_v<std::decay_t<Entity>, EgoEntity>) {
        if (auto iter = std::find_if(
              std::begin(entities_), std::end(entities_),
              [this](auto && each) { return is<EgoEntity>(each.first); });
            iter != std::end(entities_)) {
          THROW_SEMANTIC_ERROR("multi ego simulation does not support yet");
        } else {
          entity_status.type.type = traffic_simulator_msgs::msg::EntityType::EGO;
        }
      } else if constexpr (std::is_same_v<std::decay_t<Entity>, VehicleEntity>) {
        entity_status.type.type = traffic_simulator_msgs::msg::EntityType::VEHICLE;
      } else if constexpr (std::is_same_v<std::decay_t<Entity>, PedestrianEntity>) {
        entity_status.type.type = traffic_simulator_msgs::msg::EntityType::PEDESTRIAN;
      } else {
        entity_status.type.type = traffic_simulator_msgs::msg::EntityType::MISC_OBJECT;
      }

      entity_status.subtype = parameters.subtype;

      entity_status.time = getCurrentTime();

      entity_status.name = name;

      entity_status.bounding_box = parameters.bounding_box;

      entity_status.action_status = traffic_simulator_msgs::msg::ActionStatus();
      entity_status.action_status.current_action = "waiting for initialize";

      if constexpr (std::is_same_v<std::decay_t<Pose>, CanonicalizedLaneletPose>) {
        entity_status.pose = toMapPose(pose);
        entity_status.lanelet_pose = static_cast<LaneletPose>(pose);
        entity_status.lanelet_pose_valid = true;
      } else {
        /// @note If the entity is pedestrian or misc object, we have to consider matching to crosswalk lanelet.
        if (const auto lanelet_pose = toLaneletPose(
              pose, parameters.bounding_box,
              entity_status.type.type == traffic_simulator_msgs::msg::EntityType::PEDESTRIAN ||
                entity_status.type.type == traffic_simulator_msgs::msg::EntityType::MISC_OBJECT,
              [](const auto & parameters) {
                if constexpr (std::is_same_v<
                                std::decay_t<Parameters>,
                                traffic_simulator_msgs::msg::VehicleParameters>) {
                  return std::max(
                           parameters.axles.front_axle.track_width,
                           parameters.axles.rear_axle.track_width) *
                           0.5 +
                         1.0;
                } else {
                  return parameters.bounding_box.dimensions.y * 0.5 + 1.0;
                }
              }(parameters));
            lanelet_pose) {
          entity_status.lanelet_pose = *lanelet_pose;
          entity_status.lanelet_pose_valid = true;
          /// @note fix z, roll and pitch to fitting to the lanelet
          if (getParameter<bool>("consider_pose_by_road_slope", false)) {
            entity_status.pose = hdmap_utils_ptr_->toMapPose(*lanelet_pose).pose;
          } else {
            entity_status.pose = pose;
          }
        } else {
          entity_status.lanelet_pose_valid = false;
          entity_status.pose = pose;
        }
      }

      return CanonicalizedEntityStatus(entity_status, hdmap_utils_ptr_);
    };

    if (const auto [iter, success] = entities_.emplace(
          name, std::make_unique<Entity>(
                  name, makeEntityStatus(), hdmap_utils_ptr_, parameters,
                  std::forward<decltype(xs)>(xs)...));
        success) {
      // FIXME: this ignores V2I traffic lights
      iter->second->setTrafficLightManager(conventional_traffic_light_manager_ptr_);
      if (npc_logic_started_ && not is<EgoEntity>(name)) {
        iter->second->startNpcLogic();
      }
      return success;
    } else {
      THROW_SEMANTIC_ERROR("Entity ", std::quoted(name), " is already exists.");
    }
  }

  auto toMapPose(const CanonicalizedLaneletPose &) const -> const geometry_msgs::msg::Pose;

  template <typename MessageT, typename... Args>
  auto createPublisher(Args &&... args)
  {
    return rclcpp::create_publisher<MessageT>(node_topics_interface, std::forward<Args>(args)...);
  }

  template <typename MessageT, typename... Args>
  auto createSubscription(Args &&... args)
  {
    return rclcpp::create_subscription<MessageT>(
      node_topics_interface, std::forward<Args>(args)...);
  }

  void update(const double current_time, const double step_time);

  void updateHdmapMarker();

  void startNpcLogic();

  auto isNpcLogicStarted() const { return npc_logic_started_; }
};
}  // namespace entity
}  // namespace traffic_simulator

#endif  // TRAFFIC_SIMULATOR__ENTITY__ENTITY_MANAGER_HPP_
