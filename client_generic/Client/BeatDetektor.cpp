/*
 * BeatDetektor.cpp
 *
 * BeatDetektor - CubicFX Visualizer Beat Detection & Analysis Algorithm
 *
 * Copyright (c) 2009 Charles J. Cliffe.
 * MIT License — http://opensource.org/licenses/MIT
 */

#include "BeatDetektor.h"

void BeatDetektor::process(float timer_seconds, std::vector<float> &fft_data)
{
    if (!last_timer) { last_timer = timer_seconds; return; }

    if (timer_seconds < last_timer) { reset(); return; }

    float timestamp = timer_seconds;

    last_update  = timer_seconds - last_timer;
    last_timer   = timer_seconds;
    total_time  += last_update;

    unsigned int range_step = (unsigned int)(fft_data.size() / BD_DETECTION_RANGES);
    if (range_step == 0) return;

    unsigned int range = 0;
    int   i, x;
    float v;

    float bpm_floor = 60.0f / BPM_MAX;
    float bpm_ceil  = 60.0f / BPM_MIN;

    if (current_bpm != current_bpm) current_bpm = 0.0f; // NaN guard

    for (x = 0; x < (int)fft_data.size() && range < BD_DETECTION_RANGES; x += range_step)
    {
        if (!src)
        {
            a_freq_range[range] = 0.0f;
            for (i = x; i < x + (int)range_step; i++)
            {
                v = fabs(fft_data[i]);
                a_freq_range[range] += v;
            }
            a_freq_range[range] /= range_step;

            ma_freq_range[range]  -= (ma_freq_range[range]  - a_freq_range[range])  * last_update * detection_rate;
            maa_freq_range[range] -= (maa_freq_range[range] - ma_freq_range[range]) * last_update * detection_rate;
        }
        else
        {
            a_freq_range[range]   = src->a_freq_range[range];
            ma_freq_range[range]  = src->ma_freq_range[range];
            maa_freq_range[range] = src->maa_freq_range[range];
        }

        bool det = (ma_freq_range[range] * detection_factor >= maa_freq_range[range]);

        if (ma_bpm_range[range]  > bpm_ceil)  ma_bpm_range[range]  = bpm_ceil;
        if (ma_bpm_range[range]  < bpm_floor) ma_bpm_range[range]  = bpm_floor;
        if (maa_bpm_range[range] > bpm_ceil)  maa_bpm_range[range] = bpm_ceil;
        if (maa_bpm_range[range] < bpm_floor) maa_bpm_range[range] = bpm_floor;

        bool rewarded = false;

        if (!detection[range] && det)
        {
            float trigger_gap = timestamp - last_detection[range];

#define REWARD_VALS 7
            float reward_tolerances[REWARD_VALS] = { 0.001f, 0.005f, 0.01f, 0.02f, 0.04f, 0.08f, 0.10f };
            float reward_multipliers[REWARD_VALS] = { 20.0f, 10.0f, 8.0f, 1.0f, 0.5f, 0.25f, 0.125f };

            if (trigger_gap < bpm_ceil && trigger_gap > bpm_floor)
            {
                for (i = 0; i < REWARD_VALS; i++)
                {
                    if (fabs(ma_bpm_range[range] - trigger_gap) < ma_bpm_range[range] * reward_tolerances[i])
                    {
                        detection_quality[range] += quality_reward * reward_multipliers[i];
                        rewarded = true;
                    }
                }
                if (rewarded)
                    last_detection[range] = timestamp;
            }
            else if (trigger_gap >= bpm_ceil)
            {
                trigger_gap /= 2.0f;
                if (trigger_gap < bpm_ceil && trigger_gap > bpm_floor)
                {
                    for (i = 0; i < REWARD_VALS; i++)
                    {
                        if (fabs(ma_bpm_range[range] - trigger_gap) < ma_bpm_range[range] * reward_tolerances[i])
                        {
                            detection_quality[range] += quality_reward * reward_multipliers[i];
                            rewarded = true;
                        }
                    }
                }
                if (!rewarded) trigger_gap *= 2.0f;
                last_detection[range] = timestamp;
            }

            float qmp = (detection_quality[range] / quality_avg) * BD_QUALITY_STEP;
            if (qmp > 1.0f) qmp = 1.0f;

            if (rewarded)
            {
                ma_bpm_range[range]  -= (ma_bpm_range[range]  - trigger_gap)         * qmp;
                maa_bpm_range[range] -= (maa_bpm_range[range] - ma_bpm_range[range]) * qmp;
            }
            else if (trigger_gap >= bpm_floor && trigger_gap <= bpm_ceil)
            {
                if (detection_quality[range] < quality_avg * BD_QUALITY_TOLERANCE && current_bpm)
                {
                    ma_bpm_range[range]  -= (ma_bpm_range[range]  - trigger_gap)         * BD_QUALITY_STEP;
                    maa_bpm_range[range] -= (maa_bpm_range[range] - ma_bpm_range[range]) * BD_QUALITY_STEP;
                }
                detection_quality[range] -= BD_QUALITY_STEP;
            }
            else if (trigger_gap >= bpm_ceil)
            {
                if (detection_quality[range] < quality_avg * BD_QUALITY_TOLERANCE && current_bpm)
                {
                    ma_bpm_range[range]  -= (ma_bpm_range[range]  - current_bpm)         * 0.5f;
                    maa_bpm_range[range] -= (maa_bpm_range[range] - ma_bpm_range[range]) * 0.5f;
                }
                detection_quality[range] -= quality_reward * BD_QUALITY_STEP;
            }
        }

        if ((!rewarded && timestamp - last_detection[range] > bpm_ceil) ||
            (det && fabs(ma_bpm_range[range] - current_bpm) > bpm_offset))
        {
            detection_quality[range] -= detection_quality[range] * BD_QUALITY_STEP * quality_decay * last_update;
        }

        if (detection_quality[range] <= 0.0f) detection_quality[range] = 0.001f;

        detection[range] = det;
        range++;
    }

    quality_total = 0.0f;
    float bpm_total = 0.0f;
    int   bpm_contributions = 0;

    for (x = 0; x < BD_DETECTION_RANGES; x++)
        quality_total += detection_quality[x];

    quality_avg = quality_total / (float)BD_DETECTION_RANGES;

    ma_quality_avg   += (quality_avg   - ma_quality_avg)   * last_update * detection_rate / 2.0f;
    maa_quality_avg  += (ma_quality_avg - maa_quality_avg) * last_update;
    ma_quality_total += (quality_total - ma_quality_total) * last_update * detection_rate / 2.0f;

    ma_quality_avg   -= 0.98f * ma_quality_avg   * last_update * 3.0f;

    if (ma_quality_total <= 0.0f) ma_quality_total = 1.0f;
    if (ma_quality_avg   <= 0.0f) ma_quality_avg   = 1.0f;

    float avg_bpm_offset  = 0.0f;
    float offset_test_bpm = current_bpm;
    std::map<int,float> draft;

    for (x = 0; x < BD_DETECTION_RANGES; x++)
    {
        if (detection_quality[x] * BD_QUALITY_TOLERANCE >= ma_quality_avg)
        {
            if (maa_bpm_range[x] < bpm_ceil && maa_bpm_range[x] > bpm_floor)
            {
                bpm_total += maa_bpm_range[x];

                float draft_float = roundf((60.0f / maa_bpm_range[x]) * 1000.0f);
                draft_float = (fabsf(ceilf(draft_float)  - (60.0f / current_bpm) * 1000.0f) <
                               fabsf(floorf(draft_float) - (60.0f / current_bpm) * 1000.0f))
                              ? ceilf(draft_float / 10.0f) : floorf(draft_float / 10.0f);
                int draft_int = (int)(draft_float / 10.0f);

                draft[draft_int] += (detection_quality[x] / quality_avg);
                bpm_contributions++;

                if (offset_test_bpm == 0.0f) offset_test_bpm = maa_bpm_range[x];
                else avg_bpm_offset += fabsf(offset_test_bpm - maa_bpm_range[x]);
            }
        }
    }

    bool has_prediction = (bpm_contributions >= minimum_contributions);

    if (has_prediction)
    {
        int   draft_winner = 0;
        float win_max      = 0.0f;

        for (auto &kv : draft)
        {
            if (kv.second > win_max)
            {
                win_max      = kv.second;
                draft_winner = kv.first;
            }
        }

        if (draft_winner != 0)
            bpm_predict = 60.0f / (float)(draft_winner / 10.0f);

        avg_bpm_offset /= (float)bpm_contributions;
        bpm_offset = avg_bpm_offset;

        if (!current_bpm) current_bpm = bpm_predict;

        if (current_bpm && bpm_predict)
            current_bpm -= (current_bpm - bpm_predict) * last_update;
        if (current_bpm != current_bpm || current_bpm < 0.0f) current_bpm = 0.0f;

        float contest_max = 0.0f;
        for (auto &kv : bpm_contest)
        {
            if (contest_max < kv.second) contest_max = kv.second;
            if (kv.second > BD_FINISH_LINE / 2.0f)
                bpm_contest_lo[(int)roundf((float)kv.first / 10.0f)] += (kv.second / 10.0f) * last_update;
        }

        if (contest_max > finish_line)
        {
            for (auto &kv : bpm_contest)
                kv.second = (kv.second / contest_max) * finish_line;
        }

        contest_max = 0.0f;
        for (auto &kv : bpm_contest_lo)
            if (contest_max < kv.second) contest_max = kv.second;

        if (contest_max > finish_line)
        {
            for (auto &kv : bpm_contest_lo)
                kv.second = (kv.second / contest_max) * finish_line;
        }

        for (auto &kv : bpm_contest)
            kv.second -= kv.second * (last_update / detection_rate);
        for (auto &kv : bpm_contest_lo)
            kv.second -= kv.second * (last_update / detection_rate);

        bpm_timer += last_update;

        int winner    = 0;
        int winner_lo = 0;

        if (bpm_timer > winning_bpm / 4.0f && current_bpm)
        {
            if (winning_bpm)
                while (bpm_timer > winning_bpm / 4.0f)
                    bpm_timer -= winning_bpm / 4.0f;

            quarter_counter++;
            half_counter  = quarter_counter / 2;
            beat_counter  = quarter_counter / 4;

            bpm_contest[(int)roundf((60.0f / current_bpm) * 10.0f)] += quality_reward;

            win_val = 0.0f;
            for (auto &kv : bpm_contest)
            {
                if (win_val < kv.second)
                {
                    winner  = kv.first;
                    win_val = kv.second;
                }
            }
            if (winner)
            {
                win_bpm_int  = winner;
                winning_bpm  = 60.0f / (float)(winner / 10.0f);
            }

            win_val_lo = 0.0f;
            for (auto &kv : bpm_contest_lo)
            {
                if (win_val_lo < kv.second)
                {
                    winner_lo  = kv.first;
                    win_val_lo = kv.second;
                }
            }
            if (winner_lo)
            {
                win_bpm_int_lo = winner_lo;
                winning_bpm_lo = 60.0f / (float)(winner_lo);
            }
        }
    }
}
