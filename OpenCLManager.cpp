#include "OpenCLManager.hpp"

#include "Context.hpp"
#include "HelperFunctions.hpp"
#include <iostream>
#include <algorithm>
#include "HelperFunctions.hpp"

#if defined(__APPLE__) || defined(__MACOSX)
#include <OpenCL/cl_gl.h>
#include <OpenGL/OpenGL.h>
#else
#if _WIN32
#else
#include <GL/glx.h>
#include <CL/cl_gl.h>
#endif
#endif

namespace oul {

OpenCLManager* OpenCLManager::getInstance() {
    if (instance == NULL) {
        instance = new OpenCLManager();
    }
    return instance;
}

void OpenCLManager::shutdown() {
    //TODO make sure all opencl threads  are finished before returning

    delete instance;
    instance = NULL;
}

bool OpenCLManager::deviceHasOpenGLInteropCapability(const cl::Device &device) {
    // Get the cl_device_id of the device
    cl_device_id deviceID = device();
    // Get the platform of device
    cl::Platform platform = device.getInfo<CL_DEVICE_PLATFORM>();
    // Get all devices that are capable of OpenGL interop with this platform
    // Create properties for CL-GL context
#if defined(__APPLE__) || defined(__MACOSX)
    // Apple (untested)
    // TODO: create GL context for Apple
    cl_context_properties cps[] = {
        CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
        (cl_context_properties)CGLGetShareGroup(CGLGetCurrentContext()),
        0};

    return false;
#else
#ifdef _WIN32
    // Windows
    // TODO: create GL context for Windows
    cl_context_properties cps[] = {
        CL_GL_CONTEXT_KHR,
        (cl_context_properties)wglGetCurrentContext(),
        CL_WGL_HDC_KHR,
        (cl_context_properties)wglGetCurrentDC(),
        CL_CONTEXT_PLATFORM,
        (cl_context_properties)(platform)(),
        0
    };
    return false;
#else
    // Linux
    // Create a GL context using glX
    int sngBuf[] = { GLX_RGBA,
                     GLX_RED_SIZE, 1,
                     GLX_GREEN_SIZE, 1,
                     GLX_BLUE_SIZE, 1,
                     GLX_DEPTH_SIZE, 12,
                     None
    };

    // TODO: should probably free this stuff
    Display * display = XOpenDisplay(0);
    XVisualInfo* vi = glXChooseVisual(display, DefaultScreen(display), sngBuf);
    GLXContext gl2Context = glXCreateContext(display, vi, 0, GL_TRUE);

    if (gl2Context == NULL) {
        throw Exception(
                "Could not create a GL 2.1 context, please check your graphics drivers");
    }

    cl_context_properties * cps = createInteropContextProperties(platform, (cl_context_properties)gl2Context, (cl_context_properties)display);

    // check if any of these devices have the same cl_device_id as deviceID
    // Query which devices are associated with GL context
    cl_device_id cl_gl_device_ids[32];
    size_t returnSize = 0;
    clGetGLContextInfoKHR_fn glGetGLContextInfo_func = (clGetGLContextInfoKHR_fn) clGetExtensionFunctionAddress("clGetGLContextInfoKHR");
    glGetGLContextInfo_func(cps, CL_DEVICES_FOR_GL_CONTEXT_KHR, 32 * sizeof(cl_device_id), &cl_gl_device_ids, &returnSize);
    delete[] cps;

    reporter.report("There are " + oul::number(returnSize / sizeof(cl_device_id)) + " devices that can be associated with the GL context", oul::INFO);

    bool found = false;
    for (int i = 0; i < returnSize / sizeof(cl_device_id); i++) {
        cl::Device device2(cl_gl_device_ids[i]);
        if (deviceID == device2()) {
            found = true;
            break;
        }
    }
    return found;
#endif
#endif
}

bool OpenCLManager::devicePlatformMismatch(
        const cl::Device &device,
        const cl::Platform &platform) {
    std::string platformVendorStr = platform.getInfo<CL_PLATFORM_VENDOR>();
    DevicePlatform platformVendor = getDevicePlatform(platformVendorStr);

    std::string deviceVendorStr = device.getInfo<CL_DEVICE_VENDOR>();
    DevicePlatform deviceVendor = getDevicePlatform(deviceVendorStr);

    return platformVendor != deviceVendor;
}

typedef struct deviceAndScore {
        int score;
        cl::Device device;
} deviceAndScore;
bool compareScores(deviceAndScore a, deviceAndScore b) {
    return (a.score > b.score);
}
;
void OpenCLManager::sortDevicesAccordingToPreference(
        int numberOfPlatforms,
        int maxNumberOfDevices,
        std::vector<PlatformDevices> platformDevices,
        DevicePreference preference,
        std::vector<cl::Device> * sortedPlatformDevices,
        int * platformScores) {
    for (int i = 0; i < numberOfPlatforms; i++) {
        if (platformDevices[i].second.size() == 0)
            continue;
        // Go through each device and give it a score based on the preference
        std::vector<deviceAndScore> deviceScores;
        for (int j = 0; j < platformDevices[i].second.size(); j++) {
            cl::Device device = platformDevices[i].second[j];
            deviceAndScore das;
            das.device = device;
            switch (preference) {
            case DEVICE_PREFERENCE_NOT_CONNECTED_TO_SCREEN:
                if (!deviceHasOpenGLInteropCapability(device)) {
                    das.score = 1;
                } else {
                    das.score = 0;
                }
                break;
            case DEVICE_PREFERENCE_COMPUTE_UNITS:
                das.score = device.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>();
                break;
            case DEVICE_PREFERENCE_GLOBAL_MEMORY:
                das.score = device.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>()
                        / (1024 * 1024); // In MBs
                break;
            default:
                // Do nothing
                reporter.report("No valid preference selected.", oul::INFO);
                break;
            }
            reporter.report("The device "  +  device.getInfo<CL_DEVICE_NAME>() + " got a score of " + oul::number(das.score), oul::INFO);
            deviceScores.push_back(das);
        }

        // Sort devices according to the scores
        std::sort(deviceScores.begin(), deviceScores.end(), compareScores);

        // put devices back to vector and calculate scores
        int platformScore = 0;
        for (int j = 0; j < std::min(maxNumberOfDevices, (int) platformDevices[i].second.size()); j++) {
            sortedPlatformDevices[i].push_back(deviceScores[j].device);
            platformScore += deviceScores[j].score;
        }
        platformScores[i] = platformScore;

		cl::Platform platform = sortedPlatformDevices[i][0].getInfo<CL_DEVICE_PLATFORM>();
		reporter.report("The platform " + platform.getInfo<CL_PLATFORM_NAME>() + " got a score of " + oul::number(platformScore), oul::INFO);
    }
}

DevicePlatform OpenCLManager::getDevicePlatform(std::string platformVendor) {
    DevicePlatform retval;
    if (platformVendor.find("Advanced Micro Devices, Inc.") != std::string::npos) {
        retval = DEVICE_PLATFORM_AMD;
    } else if (platformVendor.find("Apple") != std::string::npos) {
        retval = DEVICE_PLATFORM_APPLE;
    } else if (platformVendor.find("Intel") != std::string::npos) {
        retval = DEVICE_PLATFORM_INTEL;
    } else if (platformVendor.find("NVIDIA") != std::string::npos) {
        retval = DEVICE_PLATFORM_NVIDIA;
    }
    return retval;
}

std::string OpenCLManager::getDevicePlatform(DevicePlatform devicePlatform) {
    std::string retval;
    switch (devicePlatform) {
    case DEVICE_PLATFORM_NVIDIA:
        retval = "NVIDIA";
        break;
    case DEVICE_PLATFORM_AMD:
        retval = "Advanced Micro Devices";
        break;
    case DEVICE_PLATFORM_INTEL:
        retval = "Intel";
        break;
    case DEVICE_PLATFORM_APPLE:
        retval = "Apple";
        break;
    case DEVICE_PLATFORM_ANY:
        break;
    }
    return retval;
}

std::vector<cl::Device> OpenCLManager::getDevicesForBestPlatform(
        const DeviceCriteria& deviceCriteria,
        std::vector<PlatformDevices> &platformDevices
        ) {

    bool * devicePlatformVendorMismatch = new bool[platformDevices.size()];
    for (int i = 0; i < platformDevices.size(); i++) {
        for (int j = 0; j < platformDevices[i].second.size(); j++) {
            // Check for a device-platform mismatch.
            // This happens for instance if we try to use the AMD platform on a Intel CPU
            // In this case, the Intel platform would be preferred.
            devicePlatformVendorMismatch[i] = devicePlatformMismatch(
                    platformDevices[i].second[j], platformDevices[i].first);
            if (devicePlatformVendorMismatch[i])
            	reporter.report("A device-platform mismatch was detected.", oul::INFO);
        }
    }

    std::vector<cl::Device>* sortedPlatformDevices = new std::vector<cl::Device>[platformDevices.size()];
    int* platformScores = new int[platformDevices.size()]();
    if (deviceCriteria.getDevicePreference() == DEVICE_PREFERENCE_NONE) {
        for(int i = 0; i < platformDevices.size(); i++) {
            sortedPlatformDevices[i] = platformDevices[i].second;
        }
    } else {
        sortDevicesAccordingToPreference(platformDevices.size(),
                deviceCriteria.getDeviceCountMaxCriteria(), platformDevices,
                deviceCriteria.getDevicePreference(), sortedPlatformDevices,
                platformScores);
    }
    // Now, finally, select the best platform and its devices by inspecting the platformDevices list
    int bestPlatform = -1;
    for (int i = 0; i < platformDevices.size(); i++) {
        if (platformDevices[i].second.size() > 0) {
            // Make sure the platform has some devices that has all criteria
            if (bestPlatform == -1) {
                bestPlatform = i;
            } else if (platformDevices[i].second.size()
                    >= deviceCriteria.getDeviceCountMinCriteria()) {
                // was enough devices found?
                // Check devicePlatformVendorMismatch
                if (devicePlatformVendorMismatch[bestPlatform] == true && devicePlatformVendorMismatch[i]
                        == false) {
                    bestPlatform = i;
                    // If there is not mismatch, choose the one with the best score
                } else if (platformScores[i] > platformScores[bestPlatform]) {
                    bestPlatform = i;
                }
            }
        }
    }
    delete[] devicePlatformVendorMismatch;

    std::vector<cl::Device> validDevices;
    if (bestPlatform == -1) {
        throw NoValidPlatformsException();
    } else {
        // Select the devices from the bestPlatform
        for (int i = 0; i < sortedPlatformDevices[bestPlatform].size(); i++) {
            validDevices.push_back(sortedPlatformDevices[bestPlatform][i]);
        }
		reporter.report("The platform " + platformDevices[bestPlatform].first.getInfo<CL_PLATFORM_NAME>() + " was selected as the best platform.", oul::INFO);
		reporter.report("A total of " + oul::number(sortedPlatformDevices[bestPlatform].size()) + " devices were selected for the context from this platform:", oul::INFO);

		for (int i = 0; i < validDevices.size(); i++) {
			reporter.report("Device " + oul::number(i) + ": " + validDevices[i].getInfo<CL_DEVICE_NAME>(), oul::INFO);
		}
    }
    delete[] sortedPlatformDevices;
    return validDevices;
}

std::vector<PlatformDevices> OpenCLManager::getDevices(
        const DeviceCriteria &deviceCriteria) {

    if (platforms.size() == 0)
        throw NoPlatformsInstalledException();

    reporter.report("Found " + oul::number(platforms.size()) + " OpenCL platforms.", oul::INFO);

    // First, get all the platforms that fit the platform criteria
    std::vector<cl::Platform> validPlatforms = this->getPlatforms(deviceCriteria.getPlatformCriteria());
    reporter.report(oul::number(validPlatforms.size()) + " platforms selected for inspection.", oul::INFO);

    // Create a vector of devices for each platform
    std::vector<PlatformDevices> platformDevices;
    for (int i = 0; i < validPlatforms.size(); i++) {
    	reporter.report("Platform " + oul::number(i) + ": " +  validPlatforms[i].getInfo<CL_PLATFORM_VENDOR>(), oul::INFO);

        // Next, get all devices of correct type for each of those platforms
        std::vector<cl::Device> devices;
        cl_device_type deviceType;
        if (deviceCriteria.getTypeCriteria() == DEVICE_TYPE_ANY) {
            deviceType = CL_DEVICE_TYPE_ALL;
            reporter.report("Looking for all types of devices.", oul::INFO);
        } else if (deviceCriteria.getTypeCriteria() == DEVICE_TYPE_GPU) {
            deviceType = CL_DEVICE_TYPE_GPU;
            reporter.report("Looking for GPU devices only.", oul::INFO);
        } else if (deviceCriteria.getTypeCriteria() == DEVICE_TYPE_CPU) {
            deviceType = CL_DEVICE_TYPE_CPU;
            reporter.report("Looking for CPU devices only.", oul::INFO);
        }
        try {
            validPlatforms[i].getDevices(deviceType, &devices);
        } catch (cl::Error &error) {
            // Do nothing?
        }
        reporter.report(oul::number(devices.size()) + " devices found for this platform.", oul::INFO);

        // Go through each device and see if they have the correct capabilities (if any)
        std::vector<cl::Device> acceptedDevices;
        for (int j = 0; j < devices.size(); j++) {
        	reporter.report("Inspecting device " + oul::number(j) + " with the name " + devices[j].getInfo<CL_DEVICE_NAME>(), oul::INFO);
            std::vector<DeviceCapability> capabilityCriteria = deviceCriteria.getCapabilityCriteria();
            bool accepted = true;
            for (int k = 0; k < capabilityCriteria.size(); k++) {
                if (capabilityCriteria[k] == DEVICE_CAPABILITY_OPENGL_INTEROP) {
                    if (!deviceHasOpenGLInteropCapability(devices[j]))
                        accepted = false;
                }
            }
            if (accepted) {
            	reporter.report("The device was accepted.", oul::INFO);
                acceptedDevices.push_back(devices[j]);
            }
        }
        if(acceptedDevices.size() > 0)
            platformDevices.push_back(std::make_pair(validPlatforms[i], acceptedDevices));
    }

    return platformDevices;
}

std::vector<cl::Platform> OpenCLManager::getPlatforms(
        oul::DevicePlatform platformCriteria) {

    std::vector<cl::Platform> retval;
    if (platformCriteria == DEVICE_PLATFORM_ANY) {
        retval = this->platforms;
    } else {
        // Find the correct platform and add to validPlatforms
        std::string find = getDevicePlatform(platformCriteria);
        for (int i = 0; i < platforms.size(); i++) {
            if (platforms[i].getInfo<CL_PLATFORM_VENDOR>().find(find) != std::string::npos) {
                retval.push_back(platforms[i]);
                break;
            }
        }
    }
    return retval;
}

OpenCLManager::OpenCLManager() {
    cl::Platform::get(&platforms);
}

Context OpenCLManager::createContext(
        std::vector<cl::Device> &devices,
        unsigned long * OpenGLContext,
        bool enableProfiling
        ) {
    return Context(devices, OpenGLContext, enableProfiling);
}

/**
 * This method parses program arguments into device criteria and returns a context.
 * If some arguments are not used, the criteria supplied in the defaultCriteria object are used.
 * Possible arguments are:
 * --device any|gpu|cpu
 * --platform any|amd|apple|intel|nvidia
 * --capability opengl-interop
 * --preference none|no-screen|compute-units|global-memory
 * --device-min-count x
 * --device-max-count x
 */
Context OpenCLManager::createContext(
        int argc,
        char** argv,
        DeviceCriteria &defaultCriteria) {

    for (int i = 1; i < argc - 1; i++) {
        std::string token = argv[i];
        std::string value = "";
        if (i + 1 < argc)
            value = argv[i + 1];
        if (token == "--device") {
            if (value == "any") {
                defaultCriteria.setTypeCriteria(DEVICE_TYPE_ANY);
            } else if (value == "gpu") {
                defaultCriteria.setTypeCriteria(DEVICE_TYPE_GPU);
            } else if (value == "cpu") {
                defaultCriteria.setTypeCriteria(DEVICE_TYPE_CPU);
            }
        } else if (token == "--platform") {
            if (value == "any") {
                defaultCriteria.setPlatformCriteria(DEVICE_PLATFORM_ANY);
            } else if (value == "amd") {
                defaultCriteria.setPlatformCriteria(DEVICE_PLATFORM_AMD);
            } else if (value == "apple") {
                defaultCriteria.setPlatformCriteria(DEVICE_PLATFORM_APPLE);
            } else if (value == "intel") {
                defaultCriteria.setPlatformCriteria(DEVICE_PLATFORM_INTEL);
            } else if (value == "nvidia") {
                defaultCriteria.setPlatformCriteria(DEVICE_PLATFORM_NVIDIA);
            }
        } else if (token == "--capability") {
            if (value == "opengl-interop") {
                defaultCriteria.setCapabilityCriteria(
                        DEVICE_CAPABILITY_OPENGL_INTEROP);
            }
        } else if (token == "--preference") {
            if (value == "none") {
                defaultCriteria.setDevicePreference(DEVICE_PREFERENCE_NONE);
            } else if (value == "no-screen") {
                defaultCriteria.setDevicePreference(
                        DEVICE_PREFERENCE_NOT_CONNECTED_TO_SCREEN);
            } else if (value == "compute-units") {
                defaultCriteria.setDevicePreference(
                        DEVICE_PREFERENCE_COMPUTE_UNITS);
            } else if (value == "global-memory") {
                defaultCriteria.setDevicePreference(
                        DEVICE_PREFERENCE_GLOBAL_MEMORY);
            }
        } else if (token == "--device-min-count") {
            unsigned int count = atoi(value.c_str());
            defaultCriteria.setDeviceCountCriteria(count,
                    defaultCriteria.getDeviceCountMaxCriteria());
        } else if (token == "--device-max-count") {
            unsigned int count = atoi(value.c_str());
            defaultCriteria.setDeviceCountCriteria(
                    defaultCriteria.getDeviceCountMinCriteria(), count);
        }
    }

    return createContext(defaultCriteria);

}

/**
 * This method finds a set of devices which satisfies the supplied device criteria and creates a context
 */
Context OpenCLManager::createContext(const DeviceCriteria &deviceCriteria, unsigned long * OpenGLContext, bool enableProfiling) {
    std::vector<PlatformDevices> platformDevices = getDevices(deviceCriteria);
    std::vector<cl::Device> validDevices = getDevicesForBestPlatform(deviceCriteria, platformDevices);

    return oul::Context(validDevices, OpenGLContext, enableProfiling);
}

ContextPtr OpenCLManager::createContextPtr(const DeviceCriteria &deviceCriteria, unsigned long * OpenGLContext, bool enableProfiling) {
    std::vector<PlatformDevices> platformDevices = getDevices(deviceCriteria);
    std::vector<cl::Device> validDevices = getDevicesForBestPlatform(deviceCriteria, platformDevices);

	return ContextPtr(new oul::Context(validDevices, OpenGLContext, enableProfiling));
}

OpenCLManager* opencl() {
    return OpenCLManager::getInstance();
}

OpenCLManager* OpenCLManager::instance = NULL;


Context OpenCLManager::createContext(cl::Device device, unsigned long * OpenGLContext, bool enableProfiling) {
    std::vector<cl::Device> deviceVector;
    deviceVector.push_back(device);
    return this->createContext(deviceVector, OpenGLContext, enableProfiling);
}

}                //namespace oul
