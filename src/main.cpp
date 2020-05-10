#include "audio_client.h"
#include <nanogui/nanogui.h>
#include <string>
#include <fmt/core.h>
#include <stb_vorbis.c>
#include <memory>
#include <iostream>
#include <soundio/soundio.h>
#include "WaveShader.h"

#define showGUI 1

static std::unique_ptr<Area[]> channels;
static WaveFormCanvas *wave_canvas = nullptr;

static void write_callback(int num_samples, int num_areas, Area* areas) 
{
  for(int i = 0; i < num_areas; ++i)
    while(areas[i].ptr < areas[i].end){
      if(channels[i].ptr < channels[i].end) {
        *(areas[i]++) = *(channels[i]++);
      } else {
        *(areas[i]++) = 0.0;
      }
    }
}

void convert_short2float(short *data, float *f_data, int size) 
{
  short *start = data;
  short *end = data+size;
  while(start < end) {
    *f_data++ = (float)(*(start++)) / INT16_MAX;
  }
}

void playfile(const char *filename) 
{
  fmt::print("Loading {} file\n", filename);

  int num_channels, sample_rate;
  short* data_;
  int num_samples = stb_vorbis_decode_filename(filename, &num_channels, &sample_rate, &data_);
  if(num_samples <= 0) {
    fmt::print("Failed to load file {}", filename);
    throw "Failed to load file";
  }
  std::unique_ptr<short[]> data(data_);

  fmt::print("num_channels: {}\nsamples_rate: {}\nnum_samples: {}\n", num_channels, sample_rate, num_samples);
  fmt::print("duration: {}\n", (double)(num_samples / sample_rate));

  auto data_size = num_samples * num_channels;
  float* f_data = new float[data_size];
  convert_short2float(data.get(), f_data, data_size);

  channels = std::make_unique<Area[]>(num_channels);
  for (int i = 0; i < num_channels; ++i) {
    channels[i] = Area(f_data + i, num_samples + i, num_channels);
  }

    // auto wave_form = new WaveFormCanvas(screen, channels.get());
    // wave_form->set_position({0, 200});
    // wave_form->set_background_color({100, 0, 100, 255});
    // wave_form->set_fixed_size({10, 10});
    // wave_canvas = wave_form;
    wave_canvas->setArea(channels.get());

  init_audio_client(sample_rate, [](int, int, Area*){}, write_callback);
}



int main(int argc, char** argv) {
  
#if showGUI
  nanogui::init();
  {
    using namespace nanogui;

    nanogui::ref<nanogui::Screen> screen = new Screen(Vector2i(600, 480), "DeEsser");
    screen->set_background({100,100,100,255});

    auto wave_form = new WaveFormCanvas(screen, nullptr);
    wave_form->set_position({0, 0});
    wave_form->set_background_color({100, 100, 100, 255});
    wave_form->set_fixed_size({600, 00});
    wave_canvas = wave_form;

    screen->set_visible(true);
    screen->perform_layout();

    playfile("assets/rhodes.ogg");

    mainloop(1 / 60.f * 1000);
  }
  nanogui::shutdown();
#endif

  return -1;
}