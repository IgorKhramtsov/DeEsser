#include "audio_client.h"
#include <nanogui/nanogui.h>
#include <string>
#include <fmt/core.h>
#include <stb_vorbis.c>
#include <memory>
#include <iostream>
#include <soundio/soundio.h>
#include "WaveShader.h"
#include "audio_processing.h"

#define showGUI 1

static std::unique_ptr<Area[]> channels;
static std::unique_ptr<Area[]> channels_processed;
static WaveFormCanvas *wave_canvas = nullptr;
static WaveFormCanvas *processed_canvas = nullptr;
static nanogui::Label *audio_stats;
static bool play_flag = false;
static int played_track = 0;

static void write_callback(int num_samples, int num_areas, Area* areas) 
{
  auto playable_channels = (played_track == 0) ? channels.get() : channels_processed.get();
  auto canvas = (played_track == 0) ? wave_canvas : processed_canvas;
  canvas->set_progress((playable_channels[0].end - playable_channels[0].ptr) / playable_channels[0].step);

  for(int i = 0; i < num_areas; ++i)
    while(areas[i].ptr < areas[i].end){
      if(play_flag && playable_channels[i].ptr < playable_channels[i].end) {
        *(areas[i]++) = *(playable_channels[i]++);
      } else {
        play_flag = false;
        *(areas[i]++) = 0.0;
      }
    }
  
  if(playable_channels[0].ptr == playable_channels[0].end && play_flag == false){
    playable_channels[0].ptr = playable_channels[0].start;
    playable_channels[1].ptr = playable_channels[1].start;
  }
}

void convert_short2float(short *data, float *f_data, int size) 
{
  short *start = data;
  short *end = data + size;
  while(start < end) {
    *f_data++ = (float)(*(start++)) / INT16_MAX;
  }
}

// Transform 1 area (all samples from 1 channel) to spectr and back
void process(Area in_data, Area out_data) 
{
  constexpr int buffer_size = 16384;

  // fill out_data by buffer of buffer_size while have in_data
  while(in_data.ptr < in_data.end) {
    auto complex = std::make_unique<Complex[]>(buffer_size);
    auto complex_it = complex.get();
    auto complex_end = complex.get() + buffer_size;
    while(complex_it < complex_end) {
      if (in_data.ptr < in_data.end)
        *(complex_it++) = *(in_data++);
      else // if in_data was not aliquot to buffer_size - fill with zeros
        *(complex_it++) = 0.;
    }

    auto complex_arr = CArray(complex.get(), buffer_size);

    fft_opt(complex_arr);
    ifft(complex_arr);

    // fill transformed data into out buffer
    int i = 0;
    while(out_data.ptr < out_data.end && i < buffer_size) {
        *(out_data++) = complex_arr[i++].real();
    }
  }
}

void loadfile(const char *filename) 
{
  if(filename == nullptr || *filename == '\0') return;
  fmt::print("Loading {} file\n", filename);

  int num_channels, sample_rate;
  short* data_;
  int num_samples = stb_vorbis_decode_filename(filename, &num_channels, &sample_rate, &data_);
  if(num_samples <= 0) {
    throw "Failed to load file";
  }
  std::unique_ptr<short[]> data(data_);
  auto data_size = num_samples * num_channels;
  float* f_data = new float[data_size]; // TODO: unmanaged memory. (this memory used to play file)
  convert_short2float(data.get(), f_data, data_size);

  audio_stats->set_caption(fmt::format(
    "num_channels: {}\n\
     samples_rate: {}smp/s\n\
     num_samples: {}\n\
     duration: {}s\n",
    num_channels, sample_rate, num_samples, (double)(num_samples / sample_rate)));


  /// channels data stored in ordered way
  /// l r l r l r l r ...
  /// so we have one large data buffer and step between data from the same channel is num_channels
  channels = std::make_unique<Area[]>(num_channels);
  for (int i = 0; i < num_channels; ++i) {
    channels[i] = Area(f_data + i, num_samples + i, num_channels);
  }
  wave_canvas->setArea(channels.get());

  float* processed_data = new float[data_size];
  channels_processed = std::make_unique<Area[]>(num_channels);
  for (int i = 0; i < num_channels; ++i) {
    channels_processed[i] = Area(processed_data + i, num_samples + i, num_channels);
  }

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
    
    auto wave_form2 = new WaveFormCanvas(screen, nullptr);
    wave_form2->set_position({stats_width + 10, 10 + 10 + first_column_height});
    wave_form2->set_background_color({30, 30, 30, 255});
    const std::function<void(Vector2i)> callback = [wave_form, wave_form2, stats_width, first_column_height](Vector2i vec){
      wave_form->set_size({vec.x() - stats_width - 20, first_column_height});
      wave_form2->set_size({vec.x() - stats_width - 20, first_column_height});
      };
    screen->set_resize_callback(callback);

    wave_canvas = wave_form;
    processed_canvas = wave_form2;

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
      played_track = 0;
      b_play->set_caption(!play_flag ? "Play" : "Pause");
      } );

    first_layout->set_position({10,10});
    first_layout->set_fixed_size({stats_width, first_column_height});
    first_layout->set_visible(true);

    auto second_layout = new Widget(screen);
    second_layout->set_layout(new BoxLayout(Orientation::Vertical,
                                    Alignment::Middle, 0, 6));

    auto b_process = new Button(second_layout, "Process");
    b_process->set_fixed_size({75, 30});
    b_process->set_callback([&] {
      for (int i = 0; i < channels[0].step; ++i)
        process(channels[i], channels_processed[i]);
      processed_canvas->setArea(channels_processed.get());
    });
    auto b_play_s = new Button(second_layout, "Play");
    b_play_s->set_fixed_size({75, 30});
    b_play_s->set_callback([&] {
      play_flag = !play_flag;
      played_track = 1;
      b_play_s->set_caption(!play_flag ? "Play" : "Pause");
      } );
    
    second_layout->set_position({10, first_column_height + 10 + 10});
    second_layout->set_fixed_size({stats_width, first_column_height});
    second_layout->set_visible(true);


    screen->set_visible(true);
    screen->perform_layout();
    mainloop(1 / 60.f * 1000);
  }
  nanogui::shutdown();
#endif

  return -1;
}