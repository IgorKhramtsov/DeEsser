//
//  Created by Bartholomew Joyce on 27/03/2018.
//  Copyright © 2018 Bartholomew Joyce All rights reserved.
//  https://github.com/bartjoyce/bmjap
//

#pragma once

#include "Area.h"
#include <functional>

typedef std::function<void(int num_samples, int num_areas, Area*)> WriteCallback;
typedef std::function<void(int num_samples, int num_areas, Area*)> ReadCallback;
int init_audio_client(int sample_rate, ReadCallback read_callback, WriteCallback write_callback);
void destroy_audio_client();