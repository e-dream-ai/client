#ifndef _CPU_USAGE_MAC_H_
#define _CPU_USAGE_MAC_H_

#include <sys/resource.h>

#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

// IOAccelerator class name for GPU matching
#ifndef kIOAcceleratorClassName
#define kIOAcceleratorClassName "IOAccelerator"
#endif

class ESCpuUsage
{
    Base::CTimer m_Timer;
    double m_LastCPUCheckTime;
    double m_LastGPUCheckTime;

    double m_LastESTime;
    int64_t m_LastGPUHardwareWaitTime;
    int m_LastGpuUsage;

    processor_info_array_t m_lastProcessorInfo;
    mach_msg_type_number_t m_lastNumProcessorInfo;
    natural_t m_numProcessors;

  public:
    ESCpuUsage() : m_LastCPUCheckTime(0), m_LastGPUCheckTime(0), 
                   m_LastGPUHardwareWaitTime(-1), m_LastGpuUsage(-1), m_numProcessors(1)
    {
        m_Timer.Reset();
        m_LastCPUCheckTime = m_Timer.Time();

        struct rusage r_usage;
        if (!getrusage(RUSAGE_SELF, &r_usage))
        {

            m_LastESTime = (double)r_usage.ru_utime.tv_sec +
                           (double)r_usage.ru_utime.tv_usec * 1e-6;

            m_LastESTime += (double)r_usage.ru_stime.tv_sec +
                            (double)r_usage.ru_stime.tv_usec * 1e-6;
        }

        processor_info_array_t processorInfo;
        mach_msg_type_number_t numProcessorInfo;
        natural_t numProcessors = 0U;

        kern_return_t err = host_processor_info(
            mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numProcessors,
            &processorInfo, &numProcessorInfo);
        if (err == KERN_SUCCESS)
        {
            m_lastProcessorInfo = processorInfo;
            m_lastNumProcessorInfo = numProcessorInfo;
            m_numProcessors = numProcessors;
        }
        else
        {
            m_lastProcessorInfo = NULL;
            m_lastNumProcessorInfo = 0;
        }
    }

    virtual ~ESCpuUsage()
    {
        if (m_lastProcessorInfo)
        {
            size_t lastProcessorInfoSize =
                sizeof(integer_t) * m_lastNumProcessorInfo;
            vm_deallocate(mach_task_self(), (vm_address_t)m_lastProcessorInfo,
                          lastProcessorInfoSize);
        }
    }

    // Get this app's CPU usage and total system CPU usage as percentage of total CPU capacity
    // Both values are 0-100% of all cores combined
    bool GetAppCpuUsage(int& _appCpu, int& _totalCpu)
    {
        struct rusage r_usage;

        double newtime = m_Timer.Time();

        double period = newtime - m_LastCPUCheckTime;
        if (period > 1)  // Only update every 1 second
        {
            // Get this app's CPU usage
            if (!getrusage(RUSAGE_SELF, &r_usage))
            {
                double utime = (double)r_usage.ru_utime.tv_sec +
                               (double)r_usage.ru_utime.tv_usec * 1e-6;

                utime += (double)r_usage.ru_stime.tv_sec +
                         (double)r_usage.ru_stime.tv_usec * 1e-6;

                // Calculate CPU usage as percentage of total CPU capacity (all cores)
                // Divide by number of processors to get 0-100% range
                _appCpu = int((utime - m_LastESTime) * 100. / period / m_numProcessors);

                m_LastESTime = utime;
                _appCpu = ::Base::Math::Clamped(_appCpu, 0, 100);
            }

            // Get total system CPU usage
            processor_info_array_t processorInfo;
            mach_msg_type_number_t numProcessorInfo;
            natural_t numProcessors = 0U;

            kern_return_t err = host_processor_info(
                mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numProcessors,
                &processorInfo, &numProcessorInfo);
            if (err == KERN_SUCCESS)
            {
                float accInUse = 0.0f, accTotal = 0.0f;

                for (unsigned i = 0U; i < numProcessors; ++i)
                {
                    float inUse, total;

                    if (m_lastProcessorInfo)
                    {
                        inUse = ((processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] -
                                  m_lastProcessorInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER]) +
                                 (processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] -
                                  m_lastProcessorInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM]) +
                                 (processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE] -
                                  m_lastProcessorInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE]));

                        total = inUse + (processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE] -
                                        m_lastProcessorInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE]);
                    }
                    else
                    {
                        inUse = processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] +
                                processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] +
                                processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE];
                        total = inUse + processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE];
                    }

                    accInUse += inUse;
                    accTotal += total;
                }

                if (accTotal > 0)
                {
                    _totalCpu = static_cast<int>((double)accInUse * 100. / (double)accTotal);
                    _totalCpu = ::Base::Math::Clamped(_totalCpu, 0, 100);
                }

                m_numProcessors = numProcessors;

                if (m_lastProcessorInfo)
                {
                    size_t lastProcessorInfoSize = sizeof(integer_t) * m_lastNumProcessorInfo;
                    vm_deallocate(mach_task_self(), (vm_address_t)m_lastProcessorInfo, lastProcessorInfoSize);
                }

                m_lastProcessorInfo = processorInfo;
                m_lastNumProcessorInfo = numProcessorInfo;
            }

            m_LastCPUCheckTime = newtime;
            return true;
        }

        return false;  // No update needed yet
    }
    
    // Get the number of CPU cores
    int GetNumCores() const
    {
        return (int)m_numProcessors;
    }

    // Get GPU utilization using IOKit (returns -1 if not available)
    // Based on SwiftyGPU approach
    int GetGpuUsage()
    {
        double newtime = m_Timer.Time();
        double period = newtime - m_LastGPUCheckTime;
        
        // Only update every 1 second to reduce jitter
        if (period < 1.0 && m_LastGpuUsage >= 0)
        {
            return m_LastGpuUsage;
        }
        
        int gpuUsage = -1;
        
        // Match IOAccelerator devices (GPU) using the class name constant
        CFMutableDictionaryRef matchDict = IOServiceMatching(kIOAcceleratorClassName);
        if (!matchDict)
            return m_LastGpuUsage;
            
        io_iterator_t iterator;
        if (IOServiceGetMatchingServices(kIOMasterPortDefault, matchDict, &iterator) != kIOReturnSuccess)
            return m_LastGpuUsage;
            
        io_service_t service;
        while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL)
        {
            CFMutableDictionaryRef properties = NULL;
            if (IORegistryEntryCreateCFProperties(service, &properties, kCFAllocatorDefault, 0) == kIOReturnSuccess)
            {
                // Look for PerformanceStatistics dictionary
                CFDictionaryRef perfStats = (CFDictionaryRef)CFDictionaryGetValue(properties, CFSTR("PerformanceStatistics"));
                if (perfStats)
                {
                    // Try "Device Utilization %" first (Intel integrated GPUs, some discrete)
                    CFNumberRef utilizationRef = (CFNumberRef)CFDictionaryGetValue(perfStats, CFSTR("Device Utilization %"));
                    if (utilizationRef && CFGetTypeID(utilizationRef) == CFNumberGetTypeID())
                    {
                        int64_t value = 0;
                        if (CFNumberGetValue(utilizationRef, kCFNumberSInt64Type, &value))
                        {
                            gpuUsage = (int)value;
                        }
                    }
                    
                    // Fallback to hardwareWaitTime (cumulative nanoseconds counter)
                    // Need to calculate delta between samples
                    if (gpuUsage < 0)
                    {
                        CFNumberRef waitTimeRef = (CFNumberRef)CFDictionaryGetValue(perfStats, CFSTR("hardwareWaitTime"));
                        if (waitTimeRef && CFGetTypeID(waitTimeRef) == CFNumberGetTypeID())
                        {
                            int64_t currentWaitTime = 0;
                            if (CFNumberGetValue(waitTimeRef, kCFNumberSInt64Type, &currentWaitTime))
                            {
                                if (m_LastGPUHardwareWaitTime >= 0 && period > 0)
                                {
                                    // Calculate delta in nanoseconds
                                    int64_t deltaWaitTime = currentWaitTime - m_LastGPUHardwareWaitTime;
                                    // Convert period to nanoseconds
                                    int64_t periodNs = (int64_t)(period * 1000000000.0);
                                    // Calculate percentage: (busy time / total time) * 100
                                    if (periodNs > 0)
                                    {
                                        gpuUsage = (int)((deltaWaitTime * 100) / periodNs);
                                    }
                                }
                                m_LastGPUHardwareWaitTime = currentWaitTime;
                            }
                        }
                    }
                    
                    if (gpuUsage >= 0)
                    {
                        gpuUsage = ::Base::Math::Clamped(gpuUsage, 0, 100);
                    }
                }
                CFRelease(properties);
            }
            IOObjectRelease(service);
            
            // Only use the first GPU we find with valid data
            if (gpuUsage >= 0)
                break;
        }
        
        IOObjectRelease(iterator);
        
        if (gpuUsage >= 0)
        {
            m_LastGpuUsage = gpuUsage;
            m_LastGPUCheckTime = newtime;
        }
        
        return m_LastGpuUsage;
    }

    // Legacy method for compatibility - still works but we don't need _total anymore
    bool GetCpuUsage(int& _total, int& _es)
    {
        struct rusage r_usage;

        double newtime = m_Timer.Time();

        double period = newtime - m_LastCPUCheckTime;
        if (period > 0.)
        {
            if (!getrusage(RUSAGE_SELF, &r_usage))
            {

                double utime = (double)r_usage.ru_utime.tv_sec +
                               (double)r_usage.ru_utime.tv_usec * 1e-6;

                utime += (double)r_usage.ru_stime.tv_sec +
                         (double)r_usage.ru_stime.tv_usec * 1e-6;

                _es = int((utime - m_LastESTime) * 100. / period);

                m_LastESTime = utime;
            }

            processor_info_array_t processorInfo;
            mach_msg_type_number_t numProcessorInfo;
            natural_t numProcessors = 0U;

            kern_return_t err = host_processor_info(
                mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numProcessors,
                &processorInfo, &numProcessorInfo);
            if (err == KERN_SUCCESS)
            {
                float accInUse = 0.0f, accTotal = 0.0f;

                for (unsigned i = 0U; i < numProcessors; ++i)
                {
                    float inUse, total;

                    if (m_lastProcessorInfo)
                    {
                        inUse = ((processorInfo[(CPU_STATE_MAX * i) +
                                                CPU_STATE_USER] -
                                  m_lastProcessorInfo[(CPU_STATE_MAX * i) +
                                                      CPU_STATE_USER]) +
                                 (processorInfo[(CPU_STATE_MAX * i) +
                                                CPU_STATE_SYSTEM] -
                                  m_lastProcessorInfo[(CPU_STATE_MAX * i) +
                                                      CPU_STATE_SYSTEM]) +
                                 (processorInfo[(CPU_STATE_MAX * i) +
                                                CPU_STATE_NICE] -
                                  m_lastProcessorInfo[(CPU_STATE_MAX * i) +
                                                      CPU_STATE_NICE]));

                        total =
                            inUse + (processorInfo[(CPU_STATE_MAX * i) +
                                                   CPU_STATE_IDLE] -
                                     m_lastProcessorInfo[(CPU_STATE_MAX * i) +
                                                         CPU_STATE_IDLE]);
                    }
                    else
                    {
                        inUse =
                            processorInfo[(CPU_STATE_MAX * i) +
                                          CPU_STATE_USER] +
                            processorInfo[(CPU_STATE_MAX * i) +
                                          CPU_STATE_SYSTEM] +
                            processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE];
                        total =
                            inUse +
                            processorInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE];
                    }

                    accInUse += inUse;
                    accTotal += total;
                }

                _total = static_cast<int>((double)accInUse * 100. /
                                          (double)accTotal);

                _es /= numProcessors;
                m_numProcessors = numProcessors;

                if (m_lastProcessorInfo)
                {
                    size_t lastProcessorInfoSize =
                        sizeof(integer_t) * m_lastNumProcessorInfo;
                    vm_deallocate(mach_task_self(),
                                  (vm_address_t)m_lastProcessorInfo,
                                  lastProcessorInfoSize);
                }

                m_lastProcessorInfo = processorInfo;
                m_lastNumProcessorInfo = numProcessorInfo;
            }
        }
        else
        {
            _es = 0;
            _total = 0;
        }

        _es = ::Base::Math::Clamped(_es, 0, 100);
        _total = ::Base::Math::Clamped(_total, 0, 100);

        m_LastCPUCheckTime = newtime;

        return true;
    }
};

#endif
