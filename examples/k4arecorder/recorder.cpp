#include "recorder.h"
#include <ctime>
#include <atomic>
#include <iostream>
#include <algorithm>

#include <k4a/k4a.h>
#include <k4arecord/record.h>
#include <k4ainternal/common.h>

// call k4a_device_close on every failed CHECK
#define CHECK(x, device)                                                                                               \
    {                                                                                                                  \
        auto retval = (x);                                                                                             \
        if (retval)                                                                                                    \
        {                                                                                                              \
            std::cerr << "Runtime error: " << #x << " returned " << retval << std::endl;                               \
            k4a_device_close(device);                                                                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    }

constexpr uint64_t operator"" _s(unsigned long long x)
{
    return x * 1000000000;
}

constexpr uint64_t operator"" _ms(unsigned long long x)
{
    return x * 1000000;
}

std::atomic_bool exiting(false);

int do_recording(uint8_t device_index,
                 char *recording_filename,
                 int recording_length,
                 k4a_device_configuration_t *device_config,
                 bool record_imu)
{
    const uint32_t installed_devices = k4a_device_get_installed_count();
    if (device_index >= installed_devices)
    {
        std::cerr << "Device not found." << std::endl;
        return 1;
    }

    k4a_device_t device;
    if (K4A_FAILED(k4a_device_open(device_index, &device)))
    {
        std::cerr << "Runtime error: k4a_device_open() failed " << std::endl;
    }

    char serial_number_buffer[256];
    size_t serial_number_buffer_size = sizeof(serial_number_buffer);
    CHECK(k4a_device_get_serialnum(device, serial_number_buffer, &serial_number_buffer_size), device);

    std::cout << "Device serial number: " << serial_number_buffer << std::endl;

    k4a_hardware_version_t version_info;
    CHECK(k4a_device_get_version(device, &version_info), device);

    std::cout << "Device version: " << (version_info.firmware_build == K4A_FIRMWARE_BUILD_RELEASE ? "Rel" : "Dbg")
              << "; C: " << version_info.rgb.major << "." << version_info.rgb.minor << "." << version_info.rgb.iteration
              << "; D: " << version_info.depth.major << "." << version_info.depth.minor << "."
              << version_info.depth.iteration << "[" << version_info.depth_sensor.major << "."
              << version_info.depth_sensor.minor << "]"
              << "; A: " << version_info.audio.major << "." << version_info.audio.minor << "."
              << version_info.audio.iteration << std::endl;

    uint32_t camera_fps = k4a_convert_fps_to_uint(device_config->camera_fps);

    if (camera_fps <= 0 || (device_config->color_resolution == K4A_COLOR_RESOLUTION_OFF &&
                            device_config->depth_mode == K4A_DEPTH_MODE_OFF))
    {
        std::cerr << "Either the color or depth modes must be enabled to record." << std::endl;
        return 1;
    }

    CHECK(k4a_device_start_cameras(device, device_config), device);
    if (record_imu)
    {
        CHECK(k4a_device_start_imu(device), device);
    }

    std::cout << "Device started" << std::endl;

    k4a_record_t recording;
    if (K4A_FAILED(k4a_record_create(recording_filename, device, *device_config, &recording)))
    {
        std::cerr << "Unable to create recording file: " << recording_filename << std::endl;
        return 1;
    }

    if (record_imu)
    {
        CHECK(k4a_record_add_imu_track(recording), device);
    }
    CHECK(k4a_record_write_header(recording), device);

    // Wait for the first capture before starting recording.
    k4a_capture_t capture;
    int32_t timeout_ms_for_first_capture = 60000;
    if (device_config->wired_sync_mode == K4A_WIRED_SYNC_MODE_SUBORDINATE)
    {
        timeout_ms_for_first_capture = 360000;
        std::cout << "[subordinate mode] Waiting for signal from master" << std::endl;
    }
    k4a_wait_result_t result = k4a_device_get_capture(device, &capture, timeout_ms_for_first_capture);

    if (result != K4A_WAIT_RESULT_SUCCEEDED)
    {
        std::cerr << "Runtime error: k4a_device_get_capture() returned " << result << std::endl;
        return 1;
    }
    k4a_capture_release(capture);

    std::cout << "Started recording" << std::endl;
    if (recording_length <= 0)
    {
        std::cout << "Press Ctrl-C to stop recording." << std::endl;
    }

    clock_t recording_start = clock();
    int32_t timeout_ms = 1000 / camera_fps;
    do
    {
        result = k4a_device_get_capture(device, &capture, timeout_ms);
        if (result == K4A_WAIT_RESULT_TIMEOUT)
        {
            continue;
        }
        else if (result != K4A_WAIT_RESULT_SUCCEEDED)
        {
            std::cerr << "Runtime error: k4a_device_get_capture() returned " << result << std::endl;
            break;
        }
        CHECK(k4a_record_write_capture(recording, capture), device);
        k4a_capture_release(capture);

        if (record_imu)
        {
            do
            {
                k4a_imu_sample_t sample;
                result = k4a_device_get_imu_sample(device, &sample, 0);
                if (result == K4A_WAIT_RESULT_TIMEOUT)
                {
                    break;
                }
                else if (result != K4A_WAIT_RESULT_SUCCEEDED)
                {
                    std::cerr << "Runtime error: k4a_imu_get_sample() returned " << result << std::endl;
                    break;
                }
                k4a_result_t write_result = k4a_record_write_imu_sample(recording, sample);
                if (K4A_FAILED(write_result))
                {
                    std::cerr << "Runtime error: k4a_record_write_imu_sample() returned " << write_result << std::endl;
                    break;
                }
            } while (!exiting && result != K4A_WAIT_RESULT_FAILED &&
                     (recording_length < 0 || (clock() - recording_start < recording_length * CLOCKS_PER_SEC)));
        }
    } while (!exiting && result != K4A_WAIT_RESULT_FAILED &&
             (recording_length < 0 || (clock() - recording_start < recording_length * CLOCKS_PER_SEC)));

    std::cout << "Stopping recording..." << std::endl;
    // TODO: BUG 19412496, BUG 19475311 - If references are held, stop command will hang.
    if (record_imu)
    {
        k4a_device_stop_imu(device);
    }
    k4a_device_stop_cameras(device);

    std::cout << "Saving recording..." << std::endl;
    CHECK(k4a_record_flush(recording), device);
    k4a_record_close(recording);

    std::cout << "Done" << std::endl;

    k4a_device_close(device);

    return 0;
}