#ifndef RUNTIMEMEASUREMENTMANAGER_HPP_
#define RUNTIMEMEASUREMENTMANAGER_HPP_

#include "openclutilitylibrary_export.h"

#include <string>
#include <map>
#include "CL/OpenCL.hpp"
#include "RuntimeMeasurement.hpp"

namespace oul {

class OPENCLUTILITYLIBRARY_EXPORT RuntimeMeasurementsManager {

public:
	RuntimeMeasurementsManager();

	void enable();
	void disable();
	bool isEnabled();

	void startCLTimer(std::string name, cl::CommandQueue queue);
	void stopCLTimer(std::string name, cl::CommandQueue queue);

	void startRegularTimer(std::string name);
	void stopRegularTimer(std::string name);

	void startNumberedCLTimer(std::string name, cl::CommandQueue queue);
	void stopNumberedCLTimer(std::string name, cl::CommandQueue queue);

	void startNumberedRegularTimer(std::string name);
	void stopNumberedRegularTimer(std::string name);

	RuntimeMeasurement getTiming(std::string name);

	void print(std::string name);
	void printAll();

private:
	bool enabled;
	std::map<std::string, RuntimeMeasurementPtr> timings;
	std::map<std::string, unsigned int> numberings;
	std::map<std::string, cl::Event> startEvents;
};

typedef boost::shared_ptr<class RuntimeMeasurementsManager> RuntimeMeasurementsManagerPtr;

} //namespace oul

#endif /* RUNTIMEMEASUREMENTMANAGER_HPP_ */
