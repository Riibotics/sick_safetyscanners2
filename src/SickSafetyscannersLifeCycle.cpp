// this is for emacs file handling -*- mode: c++; indent-tabs-mode: nil -*-

// -- BEGIN LICENSE BLOCK ----------------------------------------------

/*!
*  Copyright (C) 2020, SICK AG, Waldkirch
*  Copyright (C) 2020, FZI Forschungszentrum Informatik, Karlsruhe, Germany
*
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.

*/

// -- END LICENSE BLOCK ------------------------------------------------

//----------------------------------------------------------------------
/*!
 * \file SickSafetyscannersLifeCycle.cpp
 *
 * \authors  Soma gallai<soma.gallai@cm-robotics.com>  Erwin Lejeune
 * <erwin.lejeune@cm-robotics.com> \date    2021-05-27
 */
//----------------------------------------------------------------------

#include <sick_safetyscanners2/SickSafetyscannersLifeCycle.hpp>

namespace sick {

SickSafetyscannersLifeCycle::SickSafetyscannersLifeCycle(const rclcpp::NodeOptions& options):
    rclcpp_lifecycle::LifecycleNode("SickSafetyscannersLifecycle", options) {
  RCLCPP_INFO(this->get_logger(), "Initializing SickSafetyscannersLifeCycle ");
  // read parameters!
  initializeParameters(*this);
  loadParameters(*this);
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
SickSafetyscannersLifeCycle::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(this->get_logger(), "on_configure()...");

  // init publishers and services
  // set QOS profile for publishers explicitly
  auto qos = rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_sensor_data), rmw_qos_profile_sensor_data).keep_last(1);
  m_laser_scan_publisher =
      this->create_publisher<sensor_msgs::msg::LaserScan>("scan", qos);
  m_extended_laser_scan_publisher = this->create_publisher<
      sick_safetyscanners2_interfaces::msg::ExtendedLaserScan>("extended_scan",
                                                               qos);
  m_output_paths_publisher =
      this->create_publisher<sick_safetyscanners2_interfaces::msg::OutputPaths>(
          "output_paths", qos);
  m_raw_data_publisher = this->create_publisher<
      sick_safetyscanners2_interfaces::msg::RawMicroScanData>("raw_data", qos);

  m_field_data_service =
      this->create_service<sick_safetyscanners2_interfaces::srv::FieldData>(
          "field_data",
          std::bind(&SickSafetyscannersLifeCycle::getFieldData, this,
                    std::placeholders::_1, std::placeholders::_2));

  // Dynamic Parameter Change client
  m_param_callback = add_on_set_parameters_callback(
      std::bind(&SickSafetyscannersLifeCycle::parametersCallback, this,
                std::placeholders::_1));

  setupCommunication(std::bind(&SickSafetyscannersLifeCycle::receiveUDPPaket,
                               this, std::placeholders::_1));

  RCLCPP_INFO(this->get_logger(), "Node Configured");

  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::
      CallbackReturn::SUCCESS;
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
SickSafetyscannersLifeCycle::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(this->get_logger(), "on_activate()...");

  m_laser_scan_publisher->on_activate();
  m_extended_laser_scan_publisher->on_activate();
  m_output_paths_publisher->on_activate();
  m_raw_data_publisher->on_activate();

  startCommunication(this, m_laser_scan_publisher);

  RCLCPP_INFO(this->get_logger(), "Node activated, device is running...");

  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::
      CallbackReturn::SUCCESS;
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
SickSafetyscannersLifeCycle::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(this->get_logger(), "on_deactivate()...");

  stopCommunication();

  m_laser_scan_publisher->on_deactivate();
  m_extended_laser_scan_publisher->on_deactivate();
  m_output_paths_publisher->on_deactivate();
  m_raw_data_publisher->on_deactivate();

  RCLCPP_INFO(this->get_logger(), "Node deactivated, device stopped...");

  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::
      CallbackReturn::SUCCESS;
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
SickSafetyscannersLifeCycle::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(this->get_logger(), "on_cleanup()...");
  m_laser_scan_publisher.reset();
  m_extended_laser_scan_publisher.reset();
  m_output_paths_publisher.reset();
  m_raw_data_publisher.reset();

  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::
      CallbackReturn::SUCCESS;
}
rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
SickSafetyscannersLifeCycle::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(this->get_logger(), "on_shutdown()...");
  m_laser_scan_publisher.reset();
  m_extended_laser_scan_publisher.reset();
  m_output_paths_publisher.reset();
  m_raw_data_publisher.reset();

  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::
      CallbackReturn::SUCCESS;
}

void SickSafetyscannersLifeCycle::receiveUDPPaket(
    const sick::datastructure::Data &data) {
  if (!m_config.m_msg_creator) {
    RCLCPP_WARN(get_logger(), "Received UDP packet before all objects were "
                              "instantiated, ignoring this packet.");
    return;
  }

  if (!data.getMeasurementDataPtr()->isEmpty() &&
      !data.getDerivedValuesPtr()->isEmpty()) {
    if(m_diagnosed_laser_scan_publisher->getPublisher()->get_subscription_count() > 0){
      auto scan = m_config.m_msg_creator->createLaserScanMsg(data, this->now());
      m_diagnosed_laser_scan_publisher->publish(std::move(scan));
    }
    
    if(m_extended_laser_scan_publisher->get_subscription_count() > 0){
      auto extended_scan =
          m_config.m_msg_creator->createExtendedLaserScanMsg(data, this->now());
      m_extended_laser_scan_publisher->publish(std::move(extended_scan));
    }
    if(m_output_paths_publisher->get_subscription_count() > 0){
      auto output_paths = m_config.m_msg_creator->createOutputPathsMsg(data);
      m_output_paths_publisher->publish(std::move(output_paths));
    }
  }

  auto raw_msg = m_config.m_msg_creator->createRawDataMsg(data);
  m_last_raw_msg = *raw_msg;
  if(m_raw_data_publisher->get_subscription_count() > 0) {
    m_raw_data_publisher->publish(std::move(raw_msg));
  }
}
} // namespace sick

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(sick::SickSafetyscannersLifeCycle)