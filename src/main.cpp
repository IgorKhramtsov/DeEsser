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
static int sample_rate;
static nanogui::Label *audio_stats;
static bool play_flag = false;

static void write_callback(int num_samples, int num_areas, Area* areas) 
{
  wave_canvas->set_progress(channels[0].end - channels[0].ptr);

  for(int i = 0; i < num_areas; ++i)
    while(areas[i].ptr < areas[i].end){
      if(play_flag && channels[i].ptr < channels[i].end) {
        *(areas[i]++) = *(channels[i]++);
      } else {
        play_flag = false;
        *(areas[i]++) = 0.0;
      }
    }
  
  if(channels[0].ptr == channels[0].end && play_flag == false){
    channels[0].ptr = channels[0].start;
    channels[1].ptr = channels[1].start;
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

void loadfile(const char *filename) 
{
  if(filename == nullptr || *filename == '\0') return;

  fmt::print("Loading {} file\n", filename);

  int num_channels;
  short* data_;
  int num_samples = stb_vorbis_decode_filename(filename, &num_channels, &sample_rate, &data_);
  if(num_samples <= 0) {
    fmt::print("Failed to load file {}", filename);
    throw "Failed to load file";
  }
  std::unique_ptr<short[]> data(data_);

  fmt::print("num_channels: {}\nsamples_rate: {}\nnum_samples: {}\n", num_channels, sample_rate, num_samples);
  fmt::print("duration: {}\n", (double)(num_samples / sample_rate));
  audio_stats->set_caption(fmt::format("num_channels: {}\n\
    samples_rate: {}\n\
    num_samples: {}\n\
    duration: {}\n",
    num_channels, sample_rate, num_samples, (double)(num_samples / sample_rate)));

  auto data_size = num_samples * num_channels;
  float* f_data = new float[data_size];
  convert_short2float(data.get(), f_data, data_size);

  channels = std::make_unique<Area[]>(num_channels);
  for (int i = 0; i < num_channels; ++i) {
    channels[i] = Area(f_data + i, num_samples + i, num_channels);
  }
  wave_canvas->setArea(channels.get());

  init_audio_client(sample_rate, [](int, int, Area*){}, write_callback);
}



int main(int argc, char** argv) {
  
#if showGUI
  nanogui::init();
  {
    using namespace nanogui;

    nanogui::ref<nanogui::Screen> screen = new Screen(Vector2i(600, 480), "DeEsser");
    
    screen->set_background({30, 30, 30,255});

    constexpr int stats_width = 200;
    constexpr int first_column_height = 200;
    auto wave_form = new WaveFormCanvas(screen, nullptr);
    wave_form->set_position({stats_width + 10, 10});
    wave_form->set_background_color({30, 30, 30, 255});
    const std::function<void(Vector2i)> callback = [wave_form, stats_width, first_column_height](Vector2i vec){
      wave_form->set_size({vec.x() - stats_width - 20, first_column_height});
      };
    screen->set_resize_callback(callback);
    wave_canvas = wave_form;

    auto first_layout = new Widget(screen);
    first_layout->set_layout(new BoxLayout(Orientation::Vertical,
                                       Alignment::Middle, 0, 6));
    audio_stats = new Label(first_layout, "");
    audio_stats->set_fixed_size({stats_width, first_column_height / 2});
    

    auto b_load = new Button(first_layout, "Load");
    b_load->set_fixed_size({75, 30});
    b_load->set_callback([&] {
        loadfile(file_dialog({ {"ogg", "Open GNU G"} }, false).c_str());
    });
    auto b_play = new Button(first_layout, "Play");
    b_play->set_fixed_size({75, 30});
    b_play->set_callback([&] {
      play_flag = !play_flag; 
      b_play->set_caption(!play_flag ? "Play" : "Pause");
      } );

    first_layout->set_position({10,10});
    first_layout->set_fixed_size({stats_width, first_column_height});
    first_layout->set_visible(true);

    screen->set_visible(true);
    screen->perform_layout();


    mainloop(1 / 60.f * 1000);
  }
  nanogui::shutdown();
#endif

  return -1;
}