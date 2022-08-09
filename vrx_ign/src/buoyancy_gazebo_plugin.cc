/*
 * Copyright (C) 2015 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <functional>
#include <string>
#include <ignition/common/Profiler.hh>
#include <ignition/plugin/Register.hh>
#include <sdf/sdf.hh>

#include "buoyancy_gazebo_plugin.hh"

using namespace ignition;
using namespace vrx;

//////////////////////////////////////////////////
BuoyancyObject::BuoyancyObject()
  : linkId(-1),
    linkName(""),
    pose(0, 0, 0, 0, 0, 0),
    mass(0.0),
    shape(nullptr)
{
}

///////////////////////////////////////////////////
BuoyancyObject::BuoyancyObject(BuoyancyObject &&obj) noexcept // NOLINT
  : linkId(obj.linkId),
    linkName(obj.linkName),
    pose(obj.pose),
    mass(obj.mass),
    shape(std::move(obj.shape))
{
}

///////////////////////////////////////////////////
void BuoyancyObject::Load(const gazebo::Entity &_entity,
  const std::shared_ptr<const sdf::Element> &_sdf,
  ignition::gazebo::EntityComponentManager &_ecm)
{
  // parse link
  if (_sdf->HasElement("link_name"))
  {
    gazebo::Model model(_entity);
    this->linkName = _sdf->Get<std::string>("link_name");
    this->link = gazebo::Link(model.LinkByName(_ecm, linkName));
    if (!this->link.Valid(_ecm))
    {
      ignerr << "Could not find link named [" << this->linkName
           << "] in model" << std::endl;
      throw ParseException("link_name", "invalid link name");
    }
    this->linkId = this->link.Entity();
  }
  else
  {
    throw ParseException("link_name", "missing element");
  }

  // parse pose (optional)
  if (_sdf->HasElement("pose"))
  {
    this->pose = _sdf->Get<ignition::math::Pose3d>("pose");
  }

  // parse geometry
  if (_sdf->HasElement("geometry"))
  {
    auto ptr = const_cast<sdf::Element *>(_sdf.get());
    sdf::ElementPtr geometry = ptr->GetElement("geometry");
    try
    {
      this->shape = std::move(ShapeVolume::makeShape(geometry));
    }
    catch(...)
    {
      throw;
    }
  }
  else
  {
    throw ParseException("geometry", "missing element");
  }
}

//////////////////////////////////////////////////
std::string BuoyancyObject::Disp()
{
  std::stringstream ss;
  ss << "Buoyancy object\n"
      << "\tlink: " << this->linkName << "[" << this->linkId << "]\n"
      << "\tpose: " << this->pose << '\n'
      << "\tgeometry " << this->shape->Display() << '\n'
      << "\tmass " << this->mass;
  return ss.str();
}

//////////////////////////////////////////////////
 Buoyancy:: Buoyancy()
  : System()
{
}

// /////////////////////////////////////////////////
// BuoyancyPlugin::BuoyancyPlugin()
//   : fluidDensity(997),
//     fluidLevel(0.0),
//     linearDrag(0.0),
//     angularDrag(0.0),
//     waveModelName(""),
//     lastSimTime(0.0)
// {
// }

//////////////////////////////////////////////////
void Buoyancy::Configure(const gazebo::Entity &_entity,
    const std::shared_ptr<const sdf::Element> &_sdf,
    gazebo::EntityComponentManager &_ecm,
    gazebo::EventManager &/*_eventMgr*/)
{
  // Capture the wave model
  // if (_sdf->HasElement("wave_model"))
  // {
  //   this->waveModelName = _sdf->Get<std::string>("wave_model");
  // }
  // this->waveParams = nullptr;

  // Wavefield
  this->wavefield.Load(_sdf);

  if (_sdf->HasElement("fluid_density"))
  {
    this->fluidDensity = _sdf->Get<double>("fluid_density");
  }
  if (_sdf->HasElement("fluid_level"))
  {
    this->fluidLevel = _sdf->Get<double>("fluid_level");
  }
  if (_sdf->HasElement("linear_drag"))
  {
    this->linearDrag = _sdf->Get<double>("linear_drag");
  }
  if (_sdf->HasElement("angular_drag"))
  {
    this->angularDrag = _sdf->Get<double>("angular_drag");
  }

  if (_sdf->HasElement("buoyancy"))
  {
    ignmsg << "Found buoyancy element(s), looking at each element..."
           << std::endl;
    auto ptr = const_cast<sdf::Element *>(_sdf.get());
    for (sdf::ElementPtr buoyancyElem = ptr->GetElement("buoyancy");
        buoyancyElem;
        buoyancyElem = buoyancyElem->GetNextElement("buoyancy"))
    {
      try
      {
        BuoyancyObject buoyObj = BuoyancyObject();
        buoyObj.Load(_entity, buoyancyElem, _ecm);

        // add link to linkMap if it is not in the map
        if (this->linkMap.find(buoyObj.linkId) == this->linkMap.end())
        {
          gazebo::Model model(_entity);
          gazebo::Link objLink = gazebo::Link(model.LinkByName(_ecm, buoyObj.linkName));
          if (!objLink.Valid(_ecm))
          {
            ignerr << "Could not find link named [" << buoyObj.linkName
                 << "] in model" << std::endl;
            throw ParseException("link_name", "invalid link name");
          }

          this->linkMap[buoyObj.linkId] = objLink;
          // initialize link height
          // if (!this->waveModelName.empty())
          // {
          //   this->linkHeights[linkMap[buoyObj.linkId]] = this->fluidLevel;
          // }
          // else
          // {
            //this->linkHeights[linkMap[buoyObj.linkId]] = 0;
          // }
          // initialize link height velocity
          //this->linkHeightDots[linkMap[buoyObj.linkId]] = 0;
        }

        // get mass
        buoyObj.mass = this->linkMap[buoyObj.linkId]->GetInertial()->Mass();

        // add buoyancy object to list and display stats
        ignmsg << buoyObj.Disp() << std::endl;
        buoyancyObjects.push_back(std::move(buoyObj));
      }
      catch(const std::exception& e)
      {
        ignwarn << e.what() << std::endl;
      }
    }
  }

  // // Initialize sim time memory
  // #if GAZEBO_MAJOR_VERSION >= 8
  //   this->lastSimTime =  this->world->SimTime().Double();
  // #else
  //   this->lastSimTime =  this->world->GetSimTime().Double();
  // #endif
}

//////////////////////////////////////////////////
void Buoyancy::PreUpdate(const gazebo::UpdateInfo &_info,
    gazebo::EntityComponentManager &_ecm)
{
  IGN_PROFILE("Buoyancy::PreUpdate");


}

// /////////////////////////////////////////////////
// void BuoyancyPlugin::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf)
// {
//   GZ_ASSERT(_model != nullptr, "Received NULL model pointer");
//   GZ_ASSERT(_sdf != nullptr, "Received NULL SDF pointer");

//   // Capture the model and world pointers.
//   this->model = _model;
//   this->world = this->model->GetWorld();

//   // Capture the wave model
//   if (_sdf->HasElement("wave_model"))
//   {
//     this->waveModelName = _sdf->Get<std::string>("wave_model");
//   }
//   this->waveParams = nullptr;

//   if (_sdf->HasElement("fluid_density"))
//   {
//     this->fluidDensity = _sdf->Get<double>("fluid_density");
//   }
//   if (_sdf->HasElement("fluid_level"))
//   {
//     this->fluidLevel = _sdf->Get<double>("fluid_level");
//   }
//   if (_sdf->HasElement("linear_drag"))
//   {
//     this->linearDrag = _sdf->Get<double>("linear_drag");
//   }
//   if (_sdf->HasElement("angular_drag"))
//   {
//     this->angularDrag = _sdf->Get<double>("angular_drag");
//   }

//   if (_sdf->HasElement("buoyancy"))
//   {
//     gzmsg << "Found buoyancy element(s), looking at each element..."
//           << std::endl;
//     for (sdf::ElementPtr buoyancyElem = _sdf->GetElement("buoyancy");
//         buoyancyElem;
//         buoyancyElem = buoyancyElem->GetNextElement("buoyancy"))
//     {
//       try
//       {
//         BuoyancyObject buoyObj = BuoyancyObject();
//         buoyObj.Load(_model, buoyancyElem);

//         // add link to linkMap if it is not in the map
//         if (this->linkMap.find(buoyObj.linkId) == this->linkMap.end())
//         {
//           this->linkMap[buoyObj.linkId] = _model->GetLink(buoyObj.linkName);
//           // initialize link height
//           if (!this->waveModelName.empty())
//           {
//             this->linkHeights[linkMap[buoyObj.linkId]] = this->fluidLevel;
//           }
//           else
//           {
//             this->linkHeights[linkMap[buoyObj.linkId]] = 0;
//           }
//           // initialize link height velocity
//           this->linkHeightDots[linkMap[buoyObj.linkId]] = 0;
//         }

//         // get mass
//         #if GAZEBO_MAJOR_VERSION >= 8
//           buoyObj.mass = this->linkMap[buoyObj.linkId]->GetInertial()->Mass();
//         #else
//           buoyObj.mass =
//               this->linkMap[buoyObj.linkId]->GetInertial()->GetMass();
//         #endif

//         // add buoyancy object to list and display stats
//         gzmsg << buoyObj.Disp() << std::endl;
//         buoyancyObjects.push_back(std::move(buoyObj));
//       }
//       catch(const std::exception& e)
//       {
//         gzwarn << e.what() << std::endl;
//       }
//     }
//   }

//   // Initialize sim time memory
//   #if GAZEBO_MAJOR_VERSION >= 8
//     this->lastSimTime =  this->world->SimTime().Double();
//   #else
//     this->lastSimTime =  this->world->GetSimTime().Double();
//   #endif
// }

// /////////////////////////////////////////////////
// void BuoyancyPlugin::Init()
// {
//   this->updateConnection = event::Events::ConnectWorldUpdateBegin(
//       std::bind(&BuoyancyPlugin::OnUpdate, this));
// }

// /////////////////////////////////////////////////
// void BuoyancyPlugin::OnUpdate()
// {
//   // update wave height if wave model specified
//   if (!this->waveModelName.empty())
//   {
//     // If we haven't yet, retrieve the wave parameters from ocean model plugin.
//     if (!this->waveParams)
//     {
//       gzmsg << "usv_gazebo_dynamics_plugin: waveParams is null. "
//             << "Trying to get wave parameters from ocean model" << std::endl;
//       this->waveParams = WavefieldModelPlugin::GetWaveParams(
//           this->world, this->waveModelName);
//     }

//     #if GAZEBO_MAJOR_VERSION >= 8
//       double simTime = this->world->SimTime().Double();
//     #else
//       double simTime = this->world->GetSimTime().Double();
//     #endif

//     double dt = simTime - this->lastSimTime;
//     this->lastSimTime = simTime;

//     // get wave height for each link
//     for (auto& link : this->linkMap)
//     {
//       auto linkPtr = link.second;
//       #if GAZEBO_MAJOR_VERSION >= 8
//         ignition::math::Pose3d linkFrame = linkPtr->WorldPose();
//       #else
//         ignition::math::Pose3d linkFrame = linkPtr->GetWorldPose().Ign();
//       #endif

//       // Compute the wave displacement at the centre of the link frame.
//       // Wave field height at the link, relative to the mean water level.
//       double waveHeight = WavefieldSampler::ComputeDepthSimply(
//           *waveParams, linkFrame.Pos(), simTime);

//       this->linkHeightDots[linkPtr] =
//           (waveHeight - this->linkHeights[linkPtr]) / dt;
//       this->linkHeights[linkPtr] = waveHeight;
//     }
//   }

//   for (auto& buoyancyObj : this->buoyancyObjects)
//   {
//     auto link = this->linkMap[buoyancyObj.linkId];
//     #if GAZEBO_MAJOR_VERSION >= 8
//         ignition::math::Pose3d linkFrame = link->WorldPose();
//     #else
//         ignition::math::Pose3d linkFrame = link->GetWorldPose().Ign();
//     #endif
//     linkFrame = linkFrame * buoyancyObj.pose;

//     auto submergedVolume = buoyancyObj.shape->CalculateVolume(linkFrame,
//         this->linkHeights[link] + this->fluidLevel);

//     GZ_ASSERT(submergedVolume.volume >= 0,
//         "Non-positive volume found in volume properties!");

//     // calculate buoyancy and drag forces
//     if (submergedVolume.volume > 1e-6)
//     {
//       // By Archimedes' principle,
//       // buoyancy = -(mass*gravity)*fluid_density/object_density
//       // object_density = mass/volume, so the mass term cancels.
//       ignition::math::Vector3d buoyancy = -this->fluidDensity
//           * submergedVolume.volume * model->GetWorld()->Gravity();

//       #if GAZEBO_MAJOR_VERSION >= 8
//         ignition::math::Vector3d linVel = link->WorldLinearVel();
//         ignition::math::Vector3d angVel = link->RelativeAngularVel();
//       #else
//         ignition::math::Vector3d linVel= link->GetWorldLinearVel().Ign();
//         ignition::math::Vector3d angVel = link->GetRelativeAngularVel().Ign();
//       #endif

//       // partial mass = total_mass * submerged_vol / total_vol
//       float partialMass = buoyancyObj.mass * submergedVolume.volume
//           / buoyancyObj.shape->volume;

//       // drag (based on Exact Buoyancy for Polyhedra by Eric Catto)
//       // linear drag
//       ignition::math::Vector3d relVel =
//           ignition::math::Vector3d(0, 0, this->linkHeightDots[link]) - linVel;
//       ignition::math::Vector3d dragForce = linearDrag * partialMass * relVel;
//       buoyancy += dragForce;
//       if (buoyancy.Z() < 0.0)
//       {
//         buoyancy.Z() = 0.0;
//       }
//       // apply force
//       link->AddForceAtWorldPosition(buoyancy, submergedVolume.centroid);

//       // drag torque
//       double averageLength2 = ::pow(buoyancyObj.shape->averageLength, 2);
//       ignition::math::Vector3d dragTorque = (-partialMass * angularDrag
//           * averageLength2) * angVel;
//       link->AddRelativeTorque(dragTorque);
//     }
//   }
// }

IGNITION_ADD_PLUGIN(Buoyancy,
                    gazebo::System,
                    Buoyancy::ISystemConfigure,
                    Buoyancy::ISystemPreUpdate)

IGNITION_ADD_PLUGIN_ALIAS(vrx::Buoyancy,
                          "vrx::Buoyancy")
