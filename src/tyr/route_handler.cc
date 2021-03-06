#include "tyr/route_handler.h"

#include <stdexcept>
#include <ostream>
#include <valhalla/proto/trippath.pb.h>
#include <valhalla/proto/tripdirections.pb.h>
#include <valhalla/proto/directions_options.pb.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/python/str.hpp>
#include <boost/python/extract.hpp>
#include <valhalla/baldr/graphreader.h>
#include <valhalla/baldr/pathlocation.h>
#include <valhalla/loki/search.h>
#include <valhalla/sif/costfactory.h>
#include <valhalla/sif/autocost.h>
#include <valhalla/sif/bicyclecost.h>
#include <valhalla/sif/pedestriancost.h>
#include <valhalla/thor/pathalgorithm.h>
#include <valhalla/baldr/graphid.h>
#include <valhalla/thor/trippathbuilder.h>
#include <valhalla/odin/directionsbuilder.h>
#include <boost/algorithm/string/replace.hpp>

#include "tyr/json.h"


namespace {

/*
OSRM output looks like this:
{
    "hint_data": {
        "locations": [
            "_____38_SADaFQQAKwEAABEAAAAAAAAAdgAAAFfLwga4tW0C4P6W-wAARAA",
            "fzhIAP____8wFAQA1AAAAC8BAAAAAAAAAAAAAP____9Uu20CGAiX-wAAAAA"
        ],
        "checksum": 2875622111
    },
    "route_name": [ "West 26th Street", "Madison Avenue" ],
    "via_indices": [ 0, 9 ],
    "found_alternative": false,
    "route_summary": {
        "end_point": "West 29th Street",
        "start_point": "West 26th Street",
        "total_time": 145,
        "total_distance": 878
    },
    "via_points": [ [ 40.744377, -73.990433 ], [40.745811, -73.988075 ] ],
    "route_instructions": [
        [ "10", "West 26th Street", 216, 0, 52, "215m", "SE", 118 ],
        [ "1", "East 26th Street", 153, 2, 29, "153m", "SE", 120 ],
        [ "7", "Madison Avenue", 237, 3, 25, "236m", "NE", 29 ],
        [ "7", "East 29th Street", 155, 6, 29, "154m", "NW", 299 ],
        [ "1", "West 29th Street", 118, 7, 21, "117m", "NW", 299 ],
        [ "15", "", 0, 8, 0, "0m", "N", 0 ]
    ],
    "route_geometry": "ozyulA~p_clCfc@ywApTar@li@ybBqe@c[ue@e[ue@i[ci@dcB}^rkA",
    "status_message": "Found route between points",
    "status": 0
}
*/
using namespace valhalla::tyr;
using namespace std;

json::ArrayPtr route_name(const valhalla::odin::TripDirections& trip_directions){
  auto route_name = json::array({});
  if(trip_directions.maneuver_size() > 0) {
    if(trip_directions.maneuver(0).street_name_size() > 0) {
      route_name->push_back(trip_directions.maneuver(0).street_name(0));
    }
    if(trip_directions.maneuver(trip_directions.maneuver_size() - 1).street_name_size() > 0) {
      route_name->push_back(trip_directions.maneuver(trip_directions.maneuver_size() - 1).street_name(0));
    }
  }
  return route_name;
}

json::ArrayPtr via_indices(const valhalla::odin::TripDirections& trip_directions){
  auto via_indices = json::array({});
  if(trip_directions.maneuver_size() > 0) {
    via_indices->push_back(static_cast<uint64_t>(0));
    via_indices->push_back(static_cast<uint64_t>(trip_directions.maneuver_size() - 1));
  }
  return via_indices;
}

json::MapPtr route_summary(const valhalla::odin::TripDirections& trip_directions){
  auto route_summary = json::map({});
  if(trip_directions.maneuver_size() > 0) {
    if(trip_directions.maneuver(0).street_name_size() > 0)
      route_summary->emplace("start_point", trip_directions.maneuver(0).street_name(0));
    else
      route_summary->emplace("start_point", string(""));
    if(trip_directions.maneuver(trip_directions.maneuver_size() - 1).street_name_size() > 0)
      route_summary->emplace("end_point", trip_directions.maneuver(trip_directions.maneuver_size() - 1).street_name(0));
    else
      route_summary->emplace("end_point", string(""));
  }
  uint64_t seconds = 0, meters = 0;
  for(const auto& maneuver : trip_directions.maneuver()) {
    meters += static_cast<uint64_t>(maneuver.length() * 1000.f);
    seconds += static_cast<uint64_t>(maneuver.time());
  }
  route_summary->emplace("total_time", seconds);
  route_summary->emplace("total_distance", meters);
  return route_summary;
}

json::ArrayPtr via_points(const valhalla::odin::TripPath& trip_path){
  auto via_points = json::array({});
  for(const auto& location : trip_path.location()) {
    via_points->emplace_back(json::array({json::fp_t{location.ll().lat(),6}, json::fp_t{location.ll().lng(),6}}));
  }
  return via_points;
}

const std::unordered_map<int, std::string> maneuver_type = {
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kNone),             "0" },//NoTurn = 0,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kContinue),         "1" },//GoStraight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kBecomes),          "1" },//GoStraight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kRampStraight),     "1" },//GoStraight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kStayStraight),     "1" },//GoStraight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kMerge),            "1" },//GoStraight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kFerryEnter),       "1" },//GoStraight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kFerryExit),        "1" },//GoStraight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kSlightRight),      "2" },//TurnSlightRight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kRight),            "3" },//TurnRight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kRampRight),        "3" },//TurnRight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kExitRight),        "3" },//TurnRight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kStayRight),        "3" },//TurnRight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kSharpRight),       "4" },//TurnSharpRight,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kUturnLeft),        "5" },//UTurn,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kUturnRight),       "5" },//UTurn,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kSharpLeft),        "6" },//TurnSharpLeft,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kLeft),             "7" },//TurnLeft,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kRampLeft),         "7" },//TurnLeft,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kExitLeft),         "7" },//TurnLeft,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kStayLeft),         "7" },//TurnLeft,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kSlightLeft),       "8" },//TurnSlightLeft,
    //{ static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_k),               "9" },//ReachViaLocation,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kRoundaboutEnter),  "11" },//EnterRoundAbout,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kRoundaboutExit),   "12" },//LeaveRoundAbout,
    //{ static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_k),               "13" },//StayOnRoundAbout,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kStart),            "14" },//StartAtEndOfStreet,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kStartRight),       "14" },//StartAtEndOfStreet,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kStartLeft),        "14" },//StartAtEndOfStreet,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kDestination),      "15" },//ReachedYourDestination,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kDestinationRight), "15" },//ReachedYourDestination,
    { static_cast<int>(valhalla::odin::TripDirections_Maneuver_Type_kDestinationLeft),  "15" },//ReachedYourDestination,
    //{ static_cast<int>valhalla::odin::TripDirections_Maneuver_Type_k),                "16" },//EnterAgainstAllowedDirection,
    //{ static_cast<int>valhalla::odin::TripDirections_Maneuver_Type_k),                "17" },//LeaveAgainstAllowedDirection
};

const std::unordered_map<int, std::string> cardinal_direction_string = {
  { static_cast<int>(valhalla::odin::TripDirections_Maneuver_CardinalDirection_kNorth),     "N" },
  { static_cast<int>(valhalla::odin::TripDirections_Maneuver_CardinalDirection_kNorthEast), "NE" },
  { static_cast<int>(valhalla::odin::TripDirections_Maneuver_CardinalDirection_kEast),      "E" },
  { static_cast<int>(valhalla::odin::TripDirections_Maneuver_CardinalDirection_kSouthEast), "SE" },
  { static_cast<int>(valhalla::odin::TripDirections_Maneuver_CardinalDirection_kSouth),     "S" },
  { static_cast<int>(valhalla::odin::TripDirections_Maneuver_CardinalDirection_kSouthWest), "SW" },
  { static_cast<int>(valhalla::odin::TripDirections_Maneuver_CardinalDirection_kWest),      "W" },
  { static_cast<int>(valhalla::odin::TripDirections_Maneuver_CardinalDirection_kNorthWest), "NW" }
};

json::ArrayPtr route_instructions(const valhalla::odin::TripDirections& trip_directions){
  auto route_instructions = json::array({});
  for(const auto& maneuver : trip_directions.maneuver()) {
    //if we dont know the type of maneuver then skip it
    auto maneuver_text = maneuver_type.find(static_cast<int>(maneuver.type()));
    if(maneuver_text == maneuver_type.end())
      continue;

    //length
    std::ostringstream length;
    length << static_cast<uint64_t>(maneuver.length()*1000.f) << "m";

    //json
    route_instructions->emplace_back(json::array({
      maneuver_text->second, //maneuver type
      (maneuver.street_name_size() ? maneuver.street_name(0) : string("")), //street name
      static_cast<uint64_t>(maneuver.length() * 1000.f), //length in meters
      static_cast<uint64_t>(maneuver.begin_shape_index()), //index in the shape
      static_cast<uint64_t>(maneuver.time()), //time in seconds
      length.str(), //length as a string with a unit suffix
      cardinal_direction_string.find(static_cast<int>(maneuver.begin_cardinal_direction()))->second, // one of: N S E W NW NE SW SE
      static_cast<uint64_t>(maneuver.begin_heading())
    }));
  }
  return route_instructions;
}

void serialize(const valhalla::odin::TripPath& trip_path,
  const valhalla::odin::TripDirections& trip_directions,
  std::ostringstream& stream) {

  //TODO: worry about multipoint routes


  //build up the json object
  auto json = json::map
  ({
    {"hint_data", json::map
      ({
        {"locations", json::array({ string(""), string("") })}, //TODO: are these internal ids?
        {"checksum", static_cast<uint64_t>(0)} //TODO: what is this exactly?
      })
    },
    {"route_name", route_name(trip_directions)}, //TODO: list of all of the streets or just the via points?
    {"via_indices", via_indices(trip_directions)}, //maneuver index
    {"found_alternative", static_cast<bool>(false)}, //no alt route support
    {"route_summary", route_summary(trip_directions)}, //start/end name, total time/distance
    {"via_points", via_points(trip_path)}, //array of lat,lng pairs
    {"route_instructions", route_instructions(trip_directions)}, //array of maneuvers
    {"route_geometry", trip_path.shape()}, //polyline encoded shape
    {"status_message", string("Found route between points")}, //found route between points OR cannot find route between points
    {"status", static_cast<uint64_t>(0)} //0 success or 207 no route
  });

  //serialize it
  stream << *json;
}

}

namespace valhalla {
namespace tyr {

RouteHandler::RouteHandler(const boost::property_tree::ptree& config, const boost::property_tree::ptree& request)
    : Handler(config, request) {
  //we require locations
  try {
    for(const auto& loc : request.get_child("loc"))
      locations_.emplace_back(std::move(baldr::Location::FromCsv(loc.second.get_value<std::string>())));
    if(locations_.size() < 2)
      throw;
  }
  catch(...) {
    throw std::runtime_error("insufficiently specified required parameter `loc'");
  }

  // Parse out the type of route
  std::string costing;
  try {
    costing = request.get<std::string>("costing");
  }
  catch(...) {
    throw std::runtime_error("No edge/node costing provided");
  }

  // Register edge/node costing methods
  valhalla::sif::CostFactory<valhalla::sif::DynamicCost> factory;
  factory.Register("auto", valhalla::sif::CreateAutoCost);
  factory.Register("auto_shorter", valhalla::sif::CreateAutoShorterCost);
  factory.Register("bicycle", valhalla::sif::CreateBicycleCost);
  factory.Register("pedestrian", valhalla::sif::CreatePedestrianCost);

  // Get the costing method. Get the base options from the config
  boost::property_tree::ptree config_costing = config.get_child("costing_options." + costing);
  cost_ = factory.Create(costing, config_costing);

  // Get the config for the graph reader
  reader_.reset(new valhalla::baldr::GraphReader(config.get_child("mjolnir.hierarchy")));
}

RouteHandler::~RouteHandler() {

}

std::string RouteHandler::Action() {
  //where to
  valhalla::baldr::PathLocation origin = valhalla::loki::Search(locations_[0], *reader_, cost_->GetFilter());
  valhalla::baldr::PathLocation destination = valhalla::loki::Search(locations_[1], *reader_, cost_->GetFilter());

  //get the path
  valhalla::thor::PathAlgorithm path_algorithm;
  std::vector<valhalla::thor::PathInfo> path = path_algorithm.GetBestPath(
              origin, destination, *reader_, cost_);

  // Check if failure.
  if (path.size() == 0) {
    // If costing allows multiple passes - relax the hierarchy limits.
    // TODO - configuration control of # of passes and relaxation factor
    if (cost_->AllowMultiPass()) {
      uint32_t n = 0;
      while (path.size() == 0 && n++ < 4) {
        path_algorithm.Clear();
        cost_->RelaxHierarchyLimits(4.0f);
        path = path_algorithm.GetBestPath(origin, destination, *reader_, cost_);
      }
      if (path.size() == 0) {
        throw std::runtime_error("Route failed after 4 passes");
      }
    } else {
      throw std::runtime_error("Route failure");
    }
  }

  //get some pbf
  valhalla::odin::TripPath trip_path = valhalla::thor::TripPathBuilder::Build(
              *reader_, path, origin, destination);
  path_algorithm.Clear();

  //get some annotated instructions
  valhalla::odin::DirectionsBuilder directions_builder;
  valhalla::odin::DirectionsOptions directions_options;
  valhalla::odin::TripDirections trip_directions = directions_builder.Build(
      directions_options, trip_path);

  //make some json
  std::ostringstream stream;
  if(jsonp_)
    stream << *jsonp_ << '(';
  serialize(trip_path, trip_directions, stream);
  if(jsonp_)
    stream << ')';
  return stream.str();
}


}
}
