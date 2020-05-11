DeEsser
====================

Audio processing to get rid of excessive prominence of sibilant consonants.

## Build

```
git clone --recursive git@github.com:crataegus27/DeEsser
cd DeEsser
mkdir build
cd build 
cmake ..
make # your build command dependent on platform
```

## Dependencies

 - [nanogui](https://github.com/mitsuba-renderer/nanogui)
 - [libsoundio](https://github.com/andrewrk/libsoundio)
 - [fmt](https://github.com/fmtlib/fmt)
 - [stb_vorbis](http://nothings.org/stb_vorbis/)

 ## Progress

 1. [X] Project initialization (cmake)
 2. [X] Loading and decoding ogg audio file from disk (stb_vorbis)
 3. [X] Streaming audio data to speakers through (libsoundio)
 4. [X] Rendering data like waveform, audio specs, playback control (nanogui)
 5. [ ] Audio processing, filtering ...
 6. [ ] ...