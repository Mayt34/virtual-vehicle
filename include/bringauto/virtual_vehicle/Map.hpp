#pragma once

#include <bringauto/osm/OsmiumHandler.hpp>
#include <string>



namespace bringauto::virtual_vehicle {
class Map {
public:
	/**
	 * Load points and routes from .osm file using libosmium
	 *
	 * @param filePath full path to .osm file
	 */
	void loadMapFromFile(const std::string &filePath);

	/**
	 * Get specific route from map
	 *
	 * @param routeName name of one of routes specified in .osm file
	 */
	std::shared_ptr<bringauto::osm::Route> getRoute(const std::string &routeName);

	void speedOverride(unsigned int speed);

private:
	std::vector<std::shared_ptr<bringauto::osm::Route>> routes_;
	std::vector<std::shared_ptr<bringauto::osm::Point>> points_;
};
}
