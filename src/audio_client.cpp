//
//  Created by Bartholomew Joyce on 27/03/2018.
//  Copyright © 2018 Bartholomew Joyce All rights reserved.
//  https://github.com/bartjoyce/bmjap
//

#include "audio_client.h"

#include <soundio/soundio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <memory>

static struct SoundIo* soundio;
static struct SoundIoDevice* indevice;
static struct SoundIoDevice* outdevice;
static struct SoundIoInStream* instream;
static struct SoundIoOutStream* outstream;

static ReadCallback  app_read_callback;
static WriteCallback app_write_callback;

constexpr int BUFFER_SIZE = 128;

static void write_sample_s16ne(char* ptr, float sample) {
    static double multiplier = ((double)INT16_MAX - (double)INT16_MIN) / 2.0;
    int16_t* buf = (int16_t*)ptr;
    *buf = sample * multiplier;
}

static void write_sample_s32ne(char* ptr, float sample) {
    static double multiplier = ((double)INT32_MAX - (double)INT32_MIN) / 2.0;
    int32_t* buf = (int32_t *)ptr;
    *buf = sample * multiplier;
}

static void write_sample_float32ne(char* ptr, float sample) {
    float* buf = (float*)ptr;
    *buf = sample;
}

static void write_sample_float64ne(char* ptr, float sample) {
    double* buf = (double*)ptr;
    *buf = (double)sample;
}

static float read_sample_s16ne(char* ptr) {
    static double multiplier = ((double)INT16_MAX - (double)INT16_MIN) / 2.0;
    double sample = *(int16_t*)ptr;
    return sample / multiplier;
}

static float read_sample_s32ne(char* ptr) {
    static double multiplier = ((double)INT32_MAX - (double)INT32_MIN) / 2.0;
    double sample = *(int32_t*)ptr;
    return sample / multiplier;
}

static float read_sample_float32ne(char* ptr) {
    return *(float*)ptr;
}

static float read_sample_float64ne(char* ptr) {
    return *(double*)ptr;
}

static float (*read_sample)(char* ptr);
static void read_callback(SoundIoInStream* instream, int frame_count_min, int frame_count_max) {
    struct SoundIoChannelArea* areas;
    int err;
    
    int frames_left = frame_count_max;

    for (;;) {
        int frame_count = frames_left;
        if ((err = soundio_instream_begin_read(instream, &areas, &frame_count))) {
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count) {
            break;
        }

        int num_channels = instream->layout.channel_count;

        // Prepare the buffers for the app_read_callback
        std::unique_ptr<Area[]> app_buffer_areas = std::make_unique<Area[]>(num_channels);

        if (instream->format == SoundIoFormatFloat32NE) {
            // No conversion is necessary, give the areas directly to the app
            for (int i = 0; i < num_channels; ++i) {
                app_buffer_areas[i] = Area((float*)areas[i].ptr, frame_count, areas[i].step / sizeof(float));
            }

        } else {
            // Conversion is necessary
            static float* app_buffer = NULL;
            static size_t app_buffer_size = 0;

            size_t required_size = frame_count * num_channels;

            // Allocate the app buffer
            if (!app_buffer || app_buffer_size < required_size) {
                if (app_buffer) {
                    delete[] app_buffer;
                }
                app_buffer = new float[required_size];
                app_buffer_size = required_size;
            }

            // Copy over all the samples
            float* app_ptr = app_buffer;
            for (int i = 0; i < frame_count; ++i) {
                for (int j = 0; j < num_channels; ++j) {
                    *app_ptr++ = read_sample(areas[j].ptr);
                    areas[j].ptr += areas[j].step;
                }
            }

            // Create the app areas
            for (int i = 0; i < num_channels; ++i) {
                app_buffer_areas[i] = Area(app_buffer + i, frame_count, num_channels);
            }

        }

        // Call the user_read_callback with the buffers
        app_read_callback(frame_count, num_channels, app_buffer_areas.get());

        if ((err = soundio_instream_end_read(instream))) {
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
        if (frames_left <= 0) {
            break;
        }

    }
}

static void overflow_callback(SoundIoInStream* instream) {
    static int count = 0;
    fprintf(stderr, "underflow %d\n", count++);
}

static void (*write_sample)(char* ptr, float sample);
static void write_callback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max) {
    struct SoundIoChannelArea* areas;
    int err;

    int frames_left = frame_count_max;

    for (;;) {
        int frame_count = frames_left;
        if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count) {
            break;
        }

        int num_channels = outstream->layout.channel_count;
        
        // Prepare the buffers for the app_write_callback
        auto app_buffer_areas = std::make_unique<Area[]>(num_channels);

        if (outstream->format == SoundIoFormatFloat32NE) {
            // Our stream format is 32bit, so its a float
            // No conversion is necessary, give the areas directly to the app
            for (int i = 0; i < num_channels; ++i) {
                app_buffer_areas[i] = Area((float*)(areas[i].ptr), frame_count, areas[i].step / sizeof(float));
            }
            
            // Call the user_write_callback with the buffers
            app_write_callback(frame_count, num_channels, app_buffer_areas.get());
        } else 
        {
            // Otherwise its not 32 and not a float respectively
            // Conversion is necessary, create a custom buffer for the app to fill
            static float* app_buffer = NULL;
            static size_t app_buffer_size = 0;
            
            size_t required_size = frame_count * num_channels;
            
            // Allocate the app buffer
            if (!app_buffer || app_buffer_size < required_size) {
                if (app_buffer) {
                    delete[] app_buffer;
                }
                app_buffer = new float[required_size];
                app_buffer_size = required_size;
            }
            
            // Create the app areas
            for (int i = 0; i < num_channels; ++i) {
                app_buffer_areas[i] = Area(app_buffer + i, frame_count, num_channels);
            }
            
            // Call the user_read_callback with the buffers
            app_write_callback(frame_count, num_channels, app_buffer_areas.get());
            
            // Copy back all the samples
            float* app_ptr = app_buffer;
            for (int i = 0; i < frame_count; ++i) {
                for (int j = 0; j < num_channels; ++j) {
                    write_sample(areas[j].ptr, *app_ptr++);
                    areas[j].ptr += areas[j].step;
                }
            }

        }

        if ((err = soundio_outstream_end_write(outstream))) {
            if (err == SoundIoErrorUnderflow)
                return;
            fprintf(stderr, "unrecoverable stream error: %s\n", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
        if (frames_left <= 0) {
            break;
        }
    }
}

static void underflow_callback(SoundIoOutStream *outstream) {
    static int count = 0;
    fprintf(stderr, "underflow %d\n", count++);
}

int init_audio_client(int sample_rate, ReadCallback read_callback_, WriteCallback write_callback_) {
    app_read_callback = std::move(read_callback_);
    app_write_callback = std::move(write_callback_);

    char *stream_name = NULL;
    double latency = (double)BUFFER_SIZE / sample_rate;

    soundio = soundio_create();
    if (!soundio) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    int err = soundio_connect(soundio);

    if (err) {
        fprintf(stderr, "Unable to connect to backend: %s\n", soundio_strerror(err));
        return 1;
    }

    fprintf(stderr, "Backend: %s\n", soundio_backend_name(soundio->current_backend));

    soundio_flush_events(soundio);

    // Setup the input device
    {
        int device_index = soundio_default_input_device_index(soundio);
        if (device_index < 0) {
            fprintf(stderr, "Input device not found\n");
            return 1;
        }
        SoundIoDevice* device = soundio_get_input_device(soundio, device_index);
        if (!device) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }

        fprintf(stderr, "Input device: %s\n", device->name);

        if (device->probe_error) {
            fprintf(stderr, "Cannot probe device: %s\n", soundio_strerror(device->probe_error));
            return 1;
        }

        instream = soundio_instream_create(device);
        if (!instream) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }

        instream->read_callback = read_callback;
        instream->overflow_callback = overflow_callback;
        instream->name = stream_name;
        instream->software_latency = latency;
        instream->sample_rate = sample_rate;

        if (soundio_device_supports_format(device, SoundIoFormatFloat32NE)) {
            instream->format = SoundIoFormatFloat32NE;
            read_sample = read_sample_float32ne;
        } else if (soundio_device_supports_format(device, SoundIoFormatFloat64NE)) {
            instream->format = SoundIoFormatFloat64NE;
            read_sample = read_sample_float64ne;
        } else if (soundio_device_supports_format(device, SoundIoFormatS32NE)) {
            instream->format = SoundIoFormatS32NE;
            read_sample = read_sample_s32ne;
        } else if (soundio_device_supports_format(device, SoundIoFormatS16NE)) {
            instream->format = SoundIoFormatS16NE;
            read_sample = read_sample_s16ne;
        } else {
            fprintf(stderr, "No suitable device format available.\n");
            return 1;
        }

        if ((err = soundio_instream_open(instream))) {
            fprintf(stderr, "unable to open device: %s", soundio_strerror(err));
        }

        fprintf(stderr, "Input software latency: %f\n", instream->software_latency);

        if (instream->layout_error) {
            fprintf(stderr, "unable to set channel layout: %s\n", soundio_strerror(instream->layout_error));
        }

        if ((err = soundio_instream_start(instream))) {
            fprintf(stderr, "unable to start device: %s\n", soundio_strerror(err));
        }

        indevice = device;
    }

    // Setup the output device
    {
        int device_index = soundio_default_output_device_index(soundio);
        if (device_index < 0) {
            fprintf(stderr, "Output device not found\n");
            return 1;
        }
        SoundIoDevice* device = soundio_get_output_device(soundio, device_index);
        if (!device) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }

        fprintf(stderr, "Output device: %s\n", device->name);

        if (device->probe_error) {
            fprintf(stderr, "Cannot probe device: %s\n", soundio_strerror(device->probe_error));
            return 1;
        }

        outstream = soundio_outstream_create(device);
        if (!outstream) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        
        outstream->write_callback = write_callback;
        outstream->underflow_callback = underflow_callback;
        outstream->name = stream_name;
        outstream->software_latency = latency;
        outstream->sample_rate = sample_rate;
        
        if (soundio_device_supports_format(device, SoundIoFormatFloat32NE)) {
            outstream->format = SoundIoFormatFloat32NE;
            write_sample = write_sample_float32ne;
        } else if (soundio_device_supports_format(device, SoundIoFormatFloat64NE)) {
            outstream->format = SoundIoFormatFloat64NE;
            write_sample = write_sample_float64ne;
        } else if (soundio_device_supports_format(device, SoundIoFormatS32NE)) {
            outstream->format = SoundIoFormatS32NE;
            write_sample = write_sample_s32ne;
        } else if (soundio_device_supports_format(device, SoundIoFormatS16NE)) {
            outstream->format = SoundIoFormatS16NE;
            write_sample = write_sample_s16ne;
        } else {
            fprintf(stderr, "No suitable device format available.\n");
            return 1;
        }

        if ((err = soundio_outstream_open(outstream))) {
            fprintf(stderr, "unable to open device: %s", soundio_strerror(err));
        }

        fprintf(stderr, "Input software latency: %f\n", outstream->software_latency);

        if (outstream->layout_error) {
            fprintf(stderr, "unable to set channel layout: %s\n", soundio_strerror(outstream->layout_error));
        }

        if ((err = soundio_outstream_start(outstream))) {
            fprintf(stderr, "unable to start device: %s\n", soundio_strerror(err));
        }

        outdevice = device;
    }

    return 0;
}



void destroy_audio_client() {
    soundio_instream_destroy(instream);
    soundio_outstream_destroy(outstream);
    soundio_device_unref(indevice);
    soundio_device_unref(outdevice);
    soundio_destroy(soundio);
}
