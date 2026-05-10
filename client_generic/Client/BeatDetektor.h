#pragma once

/*
 * BeatDetektor.h
 *
 * BeatDetektor - CubicFX Visualizer Beat Detection & Analysis Algorithm
 *
 * Copyright (c) 2009 Charles J. Cliffe.
 * MIT License — http://opensource.org/licenses/MIT
 */

#include <map>
#include <vector>
#include <math.h>

#define BD_DETECTION_RANGES 128
#define BD_DETECTION_RATE 12.0
#define BD_DETECTION_FACTOR 0.925

#define BD_QUALITY_TOLERANCE 0.96
#define BD_QUALITY_DECAY 0.95
#define BD_QUALITY_REWARD 7.0
#define BD_QUALITY_STEP 0.1
#define BD_FINISH_LINE 60.0
#define BD_MINIMUM_CONTRIBUTIONS 6

class BeatDetektor
{
public:
    float BPM_MIN;
    float BPM_MAX;

    BeatDetektor *src;

    float current_bpm;
    float winning_bpm;
    float winning_bpm_lo;
    float win_val;
    int   win_bpm_int;
    float win_val_lo;
    int   win_bpm_int_lo;

    float bpm_predict;

    bool  is_erratic;
    float bpm_offset;
    float last_timer;
    float last_update;
    float total_time;

    float bpm_timer;
    int   beat_counter;
    int   half_counter;
    int   quarter_counter;
    float detection_factor;
    float quality_reward, quality_decay, detection_rate, finish_line;
    int   minimum_contributions;
    float quality_total, quality_avg, ma_quality_lo, ma_quality_total, ma_quality_avg, maa_quality_avg;

    float a_freq_range[BD_DETECTION_RANGES];
    float ma_freq_range[BD_DETECTION_RANGES];
    float maa_freq_range[BD_DETECTION_RANGES];
    float last_detection[BD_DETECTION_RANGES];

    float ma_bpm_range[BD_DETECTION_RANGES];
    float maa_bpm_range[BD_DETECTION_RANGES];

    float detection_quality[BD_DETECTION_RANGES];

    bool detection[BD_DETECTION_RANGES];

    std::map<int,float> bpm_contest;
    std::map<int,float> bpm_contest_lo;

    BeatDetektor(float BPM_MIN_in = 100.0f, float BPM_MAX_in = 200.0f, BeatDetektor *link_src = nullptr) :
        current_bpm(0.0f),
        winning_bpm(0.0f),
        winning_bpm_lo(0.0f),
        win_val(0.0f),
        win_bpm_int(0),
        win_val_lo(0.0f),
        win_bpm_int_lo(0),
        bpm_predict(0.0f),
        is_erratic(false),
        bpm_offset(0.0f),
        last_timer(0.0f),
        last_update(0.0f),
        total_time(0.0f),
        bpm_timer(0.0f),
        beat_counter(0),
        half_counter(0),
        quarter_counter(0),
        src(link_src),
        quality_reward(BD_QUALITY_REWARD),
        detection_rate(BD_DETECTION_RATE),
        finish_line(BD_FINISH_LINE),
        minimum_contributions(BD_MINIMUM_CONTRIBUTIONS),
        detection_factor(BD_DETECTION_FACTOR),
        quality_total(1.0f),
        quality_avg(1.0f),
        quality_decay(BD_QUALITY_DECAY),
        ma_quality_avg(0.001f),
        ma_quality_lo(1.0f),
        ma_quality_total(1.0f)
    {
        BPM_MIN = BPM_MIN_in;
        BPM_MAX = BPM_MAX_in;
        reset();
    }

    void reset(bool reset_freq = true)
    {
        for (int i = 0; i < BD_DETECTION_RANGES; i++)
        {
            ma_bpm_range[i]  = maa_bpm_range[i] =
                60.0f / (float)(BPM_MIN + 5) +
                ((60.0f / (float)(BPM_MAX - 5) - 60.0f / (float)(BPM_MIN + 5)) *
                 ((float)i / (float)BD_DETECTION_RANGES));
            if (reset_freq)
                a_freq_range[i] = ma_freq_range[i] = maa_freq_range[i] = 0.0f;
            last_detection[i]    = 0.0f;
            detection_quality[i] = 0.0f;
            detection[i]         = false;
        }
        total_time = 0.0f;
        maa_quality_avg = 500.0f;
        bpm_offset = bpm_timer = last_update = last_timer =
            winning_bpm = current_bpm = win_val = 0.0f;
        win_bpm_int = 0;
        bpm_contest.clear();
        bpm_contest_lo.clear();
    }

    void process(float timer_seconds, std::vector<float> &fft_data);
};
