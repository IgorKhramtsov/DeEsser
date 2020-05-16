#include "audio_client.h"
#include <nanogui/nanogui.h>
#include <string>
#include <fmt/core.h>
#include <stb_vorbis.c>
#include <vorbis/vorbisenc.h>
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
void convert_float2char(float *data, short *s_data, int size) 
{
  auto *start = data;
  auto *end = data + size;
  while(start < end) {
    *s_data++ = (short)((*(start++)) * INT16_MAX);
  }
}

float getAvg(Area data) 
{
  float summ = 0.f;
  int count = data.size() / 2;
  while(data.ptr < data.end - 2) {
    auto a = *data++;
    auto b = *data++;
    summ += std::abs(b - a);
  }
  return summ / count;
}

// TODO: Coefficients of this filter should be precalculated
//        and stored in memory as array
void filter(CArray &arr, int buffer_size) 
{
    float freq;
    double coeff = -1.;
    for (int i = 1; i < buffer_size / 2; ++i) {
      freq = 44100.0f * i / buffer_size;
      double thresh = 12500;
      if (freq < thresh / 10)
        coeff = 0.5;
      else if (freq>=thresh)
        coeff = 10 * thresh / freq;
      else
        coeff = 1 + 9 * std::pow(freq / thresh, 3);

      arr[i]._Val[0] /= coeff;
      arr[i]._Val[1] /= coeff;

      arr[buffer_size - i]._Val[0] /= coeff;
      arr[buffer_size - i]._Val[1] /= coeff;
    }
}

/// Transform 1 area (all samples from 1 channel) fft -> filter -> ifft
/// Probably we lose first 1/3 buffer size of input signal,
///  can be solved by zeroing padding it from the begin
void process(Area in_data, Area out_data) 
{
  constexpr int buffer_size = 4096;
  float avg = getAvg(in_data);

  // fill out_data by buffer of buffer_size while have in_data
  while(in_data.ptr < in_data.end + buffer_size / 2 && out_data.ptr != out_data.end) {
    auto complex = std::make_unique<Complex[]>(buffer_size);
    auto complex_it = complex.get();
    auto complex_end = complex.get() + buffer_size;

    // Move window back to 2/3
    if(in_data.ptr != in_data.start)
      in_data -= ( 2 * buffer_size / 3);

    while(complex_it < complex_end) {
      if (in_data.ptr < in_data.end)
        *(complex_it++) = *(in_data++);
      else // if in_data was not aliquot to buffer_size - fill with zeros
        *(complex_it++) = 0.;
    }

    auto complex_arr = CArray(complex.get(), buffer_size);
    fft_opt(complex_arr);
    filter(complex_arr, buffer_size);
    ifft(complex_arr);

    // fill middle 1/3 of filtered data to output 
    int i = 0;
    while(out_data.ptr < out_data.end && i < buffer_size / 3) {
        *(out_data++) = (float)(complex_arr[buffer_size / 3 + i++].real());
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
  wave_canvas->set_avg(getAvg(channels[0]));
  wave_canvas->setArea(channels.get());

  float* processed_data = new float[data_size];
  // memcpy(processed_data, f_data, sizeof(float) * data_size);
  channels_processed = std::make_unique<Area[]>(num_channels);
  for (int i = 0; i < num_channels; ++i) {
    channels_processed[i] = Area(processed_data + i, num_samples + i, num_channels);
  }

  init_audio_client(sample_rate, [](int, int, Area*){}, write_callback);
}

void savefile() 
{
  ogg_stream_state os; /* take physical pages, weld into a logical
                          stream of packets */
  ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
  ogg_packet       op; /* one raw packet of data for decode */
  vorbis_info      vi; // struct that stores all the static vorbis bitstream settings 
  vorbis_comment   vc; /* struct that stores all the user comments */

  vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
  vorbis_block     vb; /* local working space for packet->PCM decode */

  // std::ofstream out("assets/out.ogg");
  FILE * out;
  out = fopen("out.ogg", "wb");

  vorbis_info_init(&vi);
  int ret=vorbis_encode_init_vbr(&vi,2,44100,0.1f);
  if(ret)
    throw "cant save";

  /* add a comment */
  vorbis_comment_init(&vc);
  vorbis_comment_add_tag(&vc,"ENCODER","encoder_example.c");

  /* set up the analysis state and auxiliary encoding storage */
  vorbis_analysis_init(&vd,&vi);
  vorbis_block_init(&vd,&vb);

  /* set up our packet->stream encoder */
  /* pick a random serial number; that way we can more likely build
     chained streams just by concatenation */
  int eos = 0;
  srand(time(NULL));
  ogg_stream_init(&os,rand());

  {
    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;

    vorbis_analysis_headerout(&vd,&vc,&header,&header_comm,&header_code);
    ogg_stream_packetin(&os,&header); /* automatically placed in its own
                                         page */
    ogg_stream_packetin(&os,&header_comm);
    ogg_stream_packetin(&os,&header_code);

    /* This ensures the actual
     * audio data will start on a new page, as per spec
     */
    while(!eos){
      int result=ogg_stream_flush(&os,&og);
      if(result==0)break;
      fwrite(og.header,1,og.header_len,out);
      fwrite(og.body,1,og.body_len,out);
    }

  }

  while(!eos){
    long i;
    // long bytes=fread(readbuffer,1,1024*4,stdin); /* stereo hardwired here */
    long bytes = (channels_processed[0].end - channels_processed[0].ptr) / channels_processed[0].step;
    bytes = std::min(bytes, 1024l);
    

    if(bytes==0){
      /* end of file.  this can be done implicitly in the mainline,
          but it's easier to see here in non-clever fashion.
          Tell the library we're at end of stream so that it can handle
          the last frame and mark end of stream in the output properly */
      vorbis_analysis_wrote(&vd,0);

    }else{
      /* data to encode */

      /* expose the buffer to submit data */
      float **buffer=vorbis_analysis_buffer(&vd,1024);

      /* uninterleave samples */
      for(i=0;i<bytes;i++){
        buffer[0][i] = (*channels_processed[0]++);
        buffer[1][i] = (*channels_processed[1]++);
        // buffer[0][i]=((readbuffer[i*4+1]<<8)|
                      // (0x00ff&(int)readbuffer[i*4]))/32768.f;
        // buffer[1][i]=((readbuffer[i*4+3]<<8)|
        //               (0x00ff&(int)readbuffer[i*4+2]))/32768.f;
      }

      /* tell the library how much we actually submitted */
      vorbis_analysis_wrote(&vd,i);
    }
      /* vorbis does some data preanalysis, then divvies up blocks for
        more involved (potentially parallel) processing.  Get a single
        block for encoding now */
    while(vorbis_analysis_blockout(&vd,&vb)==1){

      /* analysis, assume we want to use bitrate management */
      vorbis_analysis(&vb,NULL);
      vorbis_bitrate_addblock(&vb);

      while(vorbis_bitrate_flushpacket(&vd,&op)){

        /* weld the packet into the bitstream */
        ogg_stream_packetin(&os,&op);

        /* write out pages (if any) */
        while(!eos){
          int result=ogg_stream_pageout(&os,&og);
          if(result==0)break;
          fwrite(og.header,1,og.header_len,out);
          fwrite(og.body,1,og.body_len,out);

          /* this could be set above, but for illustrative purposes, I do
              it here (to show that vorbis does know where the stream ends) */

          if(ogg_page_eos(&og))eos=1;
        }
      }
    }
  }

  /* clean up and exit.  vorbis_info_clear() must be called last */

  ogg_stream_clear(&os);
  vorbis_block_clear(&vb);
  vorbis_dsp_clear(&vd);
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);

  fmt::print("saved to file out.ogg\n");
  std::fclose(out);
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
        loadfile(file_dialog({ {"ogg", "Vorbis ogg"} }, false).c_str());
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
      savefile();
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