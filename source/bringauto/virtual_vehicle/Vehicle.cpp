#include <bringauto/virtual_vehicle/Vehicle.hpp>
#include <bringauto/common_utils/CommonUtils.hpp>

#include <bringauto/logging/Logger.hpp>



namespace bringauto::virtual_vehicle {
void Vehicle::initialize() {
	route_->prepareRoute();
	updateVehicleState(communication::Status::IDLE);
	com_->initializeConnection();
	setNextPosition();
}

void Vehicle::drive() {
	while(!globalContext_->ioContext.stopped()) {
		nextEvent();
	}
}

void Vehicle::nextEvent() {
	switch(state_) {
		case communication::Status::IDLE:
			handleIdleEvent();
			break;
		case communication::Status::DRIVE:
			handleDriveEvent();
			break;
		case communication::Status::IN_STOP:
			handleInStopEvent();
			break;
		case communication::Status::OBSTACLE:
			handleObstacleEvent();
		case communication::Status::ERROR:
			handleErrorEvent();
			break;
	}
	request();
}

void Vehicle::handleIdleEvent() {
	std::this_thread::sleep_for(std::chrono::milliseconds(globalContext_->settings->messagePeriodMs));
}

void Vehicle::handleDriveEvent() {
	if(checkForStop()) { return; }

	if(driveMillisecondLeft_ < globalContext_->settings->messagePeriodMs) {
		std::this_thread::sleep_for(std::chrono::milliseconds(driveMillisecondLeft_));
	} else {
		std::this_thread::sleep_for(std::chrono::milliseconds(globalContext_->settings->messagePeriodMs));
	}
	driveMillisecondLeft_ -= globalContext_->settings->messagePeriodMs;

	if(driveMillisecondLeft_ <= 0) {
		setNextPosition();
	}
}

void Vehicle::handleInStopEvent() {
	if(inStopMillisecondsLeft_ > globalContext_->settings->messagePeriodMs) {
		std::this_thread::sleep_for(std::chrono::milliseconds(globalContext_->settings->messagePeriodMs));
	} else {
		std::this_thread::sleep_for(std::chrono::milliseconds(inStopMillisecondsLeft_));
	}
	inStopMillisecondsLeft_ -= globalContext_->settings->messagePeriodMs;
	if(inStopMillisecondsLeft_ < 0) {
		inStopMillisecondsLeft_ = 0;
	}
}

void Vehicle::handleObstacleEvent() {
	std::this_thread::sleep_for(std::chrono::milliseconds(globalContext_->settings->messagePeriodMs));
	logging::Logger::logWarning("Cars state is obstacle, this state is not supported.");
}

void Vehicle::handleErrorEvent() {
	std::this_thread::sleep_for(std::chrono::milliseconds(globalContext_->settings->messagePeriodMs));
	logging::Logger::logWarning("Car is in error state.");
}

void Vehicle::setNextPosition() {
	actualPosition_ = route_->getPosition();
	route_->setNextPosition();
	nextPosition_ = route_->getPosition();

	actualSpeed_ = state_ == communication::Status::DRIVE ? actualPosition_->getSpeedInMetersPerSecond() : 0;
	driveMillisecondLeft_ = common_utils::CommonUtils::timeToDriveInMilliseconds(
			common_utils::CommonUtils::calculateDistanceInMeters(actualPosition_, nextPosition_),
			actualSpeed_);

	if(state_ == communication::Status::DRIVE) {
		bringauto::logging::Logger::logInfo(
				"Distance to drive: {:.2f}m, time to get there: {:.2f}s",
				common_utils::CommonUtils::calculateDistanceInMeters(actualPosition_, nextPosition_),
				(double)driveMillisecondLeft_/1000);
	}
}

void Vehicle::request() {
	communication::Status status { actualPosition_->getLongitude(), actualPosition_->getLatitude(),
								   actualSpeed_, state_, nextStopName_ };
	std::stringstream is;
	is << status;
	logging::Logger::logInfo("Sending status {}", is.str());
	com_->makeRequest(status);
	evaluateCommand();
}

void Vehicle::evaluateCommand() {
	auto command = com_->getCommand();
#ifdef STATE_SMURF
	settings::StateSmurfDefinition::changeToState(globalContext_->transitions, command.action);
#endif

	if(command.stops.empty()) {
		updateVehicleState(communication::Status::IDLE);
		return;
	}

	if(mission_ != command.stops) {
		if(!route_->areStopsPresent(mission_)) {
			logging::Logger::logWarning(
					"Received stopNames are not on route, stopNames will be completely ignored {}",
					common_utils::CommonUtils::constructMissionString(mission_));
			mission_.clear();
			missionValidity_ = false;
			return;
		} else {
			missionValidity_ = true;
		}
		mission_ = command.stops;
	}

	switch(state_) {
		case communication::Status::IDLE:
			if(command.action == communication::Command::START) {
				updateVehicleState(communication::Status::DRIVE);
			} else {
				updateVehicleState(communication::Status::IDLE);
			}
			break;
		case communication::Status::DRIVE:
			if(command.action == communication::Command::STOP) {
				updateVehicleState(communication::Status::IDLE);
			} else {
				updateVehicleState(communication::Status::DRIVE);
			}
			break;
		case communication::Status::IN_STOP:
			if(command.action == communication::Command::START) {
				if(inStopMillisecondsLeft_ == 0) {
					if(mission_.empty()) {
						updateVehicleState(communication::Status::IDLE);
					} else {
						updateVehicleState(communication::Status::DRIVE);
					}
				} else {
					updateVehicleState(communication::Status::IN_STOP);
				}
			} else {
				updateVehicleState(communication::Status::IDLE);
			}
			break;
		case communication::Status::OBSTACLE:
		case communication::Status::ERROR:
			break;
	}

	if(state_ != communication::Status::IN_STOP) {
		nextStopName_ = mission_.front();
	}
}

int Vehicle::checkForStop() {
	if(actualPosition_->isStop() && actualPosition_->getName() == nextStopName_) {
		updateVehicleState(communication::Status::State::IN_STOP);
		bringauto::logging::Logger::logInfo("Car have arrived at the stop {}", nextStopName_);
		return true;
	}
	return false;
}

void Vehicle::updateVehicleState(communication::Status::State state) {
	if(!missionValidity_) {
		state_ = bringauto::communication::Status::State::ERROR;
		return;
	}
#ifdef STATE_SMURF
	settings::StateSmurfDefinition::changeToState(globalContext_->transitions, state);
#endif
	if(state_ == state) {
		return;
	}
	state_ = state;
	switch(state_) {
		case communication::Status::IDLE:
			nextStopName_.clear();
			actualSpeed_ = 0;
			break;
		case communication::Status::DRIVE:
			actualSpeed_ = actualPosition_->getSpeedInMetersPerSecond();
			driveMillisecondLeft_ = common_utils::CommonUtils::timeToDriveInMilliseconds(
					common_utils::CommonUtils::calculateDistanceInMeters(actualPosition_, nextPosition_), actualSpeed_);
			break;
		case communication::Status::IN_STOP:
			actualSpeed_ = 0;
			inStopMillisecondsLeft_ = globalContext_->settings->stopWaitTime*1000;
			break;
		case communication::Status::OBSTACLE:
		case communication::Status::ERROR:
			break;
	}
}
}