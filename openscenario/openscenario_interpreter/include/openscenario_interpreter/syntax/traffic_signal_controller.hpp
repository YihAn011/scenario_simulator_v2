// Copyright 2015-2020 Tier IV, Inc. All rights reserved.
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

#ifndef OPENSCENARIO_INTERPRETER__SYNTAX__TRAFFIC_SIGNAL_CONTROLLER_HPP_
#define OPENSCENARIO_INTERPRETER__SYNTAX__TRAFFIC_SIGNAL_CONTROLLER_HPP_

#include <openscenario_interpreter/syntax/double.hpp>
#include <openscenario_interpreter/syntax/phase.hpp>
#include <openscenario_interpreter/syntax/string.hpp>

namespace openscenario_interpreter
{
inline namespace syntax
{
/* ---- TrafficSignalController ------------------------------------------------
 *
 *  <xsd:complexType name="TrafficSignalController">
 *    <xsd:sequence>
 *      <xsd:element name="Phase" minOccurs="0" maxOccurs="unbounded" type="Phase"/>
 *    </xsd:sequence>
 *    <xsd:attribute name="name" type="String" use="required"/>
 *    <xsd:attribute name="delay" type="Double" use="optional"/>
 *    <xsd:attribute name="reference" type="String" use="optional"/>
 *  </xsd:complexType>
 *
 * -------------------------------------------------------------------------- */
struct TrafficSignalController
{
  /* ---- NOTE -----------------------------------------------------------------
   *
   *  ID of the traffic signal controller in the road network.
   *
   *  In the TierIV OpenSCENARIO implementation, it is the Lanelet ID (positive
   *  integer) of the traffic light.
   *
   * ------------------------------------------------------------------------ */
  const String name;

  /* ---- NOTE -----------------------------------------------------------------
   *
   *  The delay to the controller in the reference property. A controller
   *  having a delay to another one means that its first phase virtually starts
   *  delaytime seconds after the start of the reference's first phase. This
   *  can be used to define a progressive signal system, but only makes sense,
   *  if the total times of all connected controllers are the same. If delay is
   *  set, reference is required. Unit: s; Range: [0..inf[.
   *
   * ------------------------------------------------------------------------ */
  const Double delay;

  /* ---- NOTE -----------------------------------------------------------------
   *
   *  A reference (ID) to the connected controller in the road network. If
   *  reference is set, a delay is required.
   *
   * ------------------------------------------------------------------------ */
  const String reference;

  /* ---- NOTE -----------------------------------------------------------------
   *
   *  Phases of a TrafficSignalController.
   *
   * ------------------------------------------------------------------------ */
  const std::vector<Phase> phases;

  template <typename Node, typename Scope>
  explicit TrafficSignalController(const Node & node, Scope & outer_scope)
  : name(readAttribute<String>("name", node, outer_scope)),
    delay(readAttribute<Double>("delay", node, outer_scope, Double())),
    reference(readAttribute<String>("reference", node, outer_scope, String())),
    phases(readElements<Phase, 0>("Phase", node, outer_scope))
  {
  }

  auto evaluate() const
  {
    // setTrafficLightColorPhase()
    return unspecified;
  }
};
}  // namespace syntax
}  // namespace openscenario_interpreter

#endif  // OPENSCENARIO_INTERPRETER__SYNTAX__TRAFFIC_SIGNAL_CONTROLLER_HPP_
