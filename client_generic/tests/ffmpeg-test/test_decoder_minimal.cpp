/**
 * Minimal ContentDecoder Test
 * 
 * This is a minimal test that directly uses FFmpeg to decode videos
 * and count frames, without using the full infinidream code.
 */

#include <iostream>
#include <string>
#include <vector>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

class MinimalDecoder {
private:
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    int videoStreamIndex = -1;
    
public:
    ~MinimalDecoder() {
        cleanup();
    }
    
    void cleanup() {
        if (codecContext) {
            avcodec_free_context(&codecContext);
        }
        if (formatContext) {
            avformat_close_input(&formatContext);
        }
    }
    
    bool open(const std::string& filename) {
        // Open input file
        if (avformat_open_input(&formatContext, filename.c_str(), nullptr, nullptr) < 0) {
            std::cerr << "Could not open file: " << filename << std::endl;
            return false;
        }
        
        // Find stream info
        if (avformat_find_stream_info(formatContext, nullptr) < 0) {
            std::cerr << "Could not find stream info" << std::endl;
            return false;
        }
        
        // Find video stream
        for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
            if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIndex = i;
                break;
            }
        }
        
        if (videoStreamIndex == -1) {
            std::cerr << "No video stream found" << std::endl;
            return false;
        }
        
        // Get codec
        const AVCodec* codec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
        if (!codec) {
            std::cerr << "Codec not found" << std::endl;
            return false;
        }
        
        // Allocate codec context
        codecContext = avcodec_alloc_context3(codec);
        if (!codecContext) {
            std::cerr << "Could not allocate codec context" << std::endl;
            return false;
        }
        
        // Copy codec parameters
        if (avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar) < 0) {
            std::cerr << "Could not copy codec parameters" << std::endl;
            return false;
        }
        
        // Open codec
        if (avcodec_open2(codecContext, codec, nullptr) < 0) {
            std::cerr << "Could not open codec" << std::endl;
            return false;
        }
        
        return true;
    }
    
    void printInfo() {
        std::cout << "Format: " << formatContext->iformat->name << std::endl;
        std::cout << "Duration: " << formatContext->duration / AV_TIME_BASE << " seconds" << std::endl;
        std::cout << "Bit rate: " << formatContext->bit_rate << std::endl;
        
        AVStream* stream = formatContext->streams[videoStreamIndex];
        std::cout << "Video stream index: " << videoStreamIndex << std::endl;
        std::cout << "Codec: " << avcodec_get_name(codecContext->codec_id) << std::endl;
        std::cout << "Frame rate: " << av_q2d(stream->avg_frame_rate) << std::endl;
        std::cout << "Resolution: " << codecContext->width << "x" << codecContext->height << std::endl;
        std::cout << "Stream nb_frames: " << stream->nb_frames << std::endl;
    }
    
    int64_t countFrames() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        
        int64_t frameCount = 0;
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        while (av_read_frame(formatContext, packet) >= 0) {
            if (packet->stream_index == videoStreamIndex) {
                // Send packet to decoder
                int ret = avcodec_send_packet(codecContext, packet);
                if (ret < 0) {
                    std::cerr << "Error sending packet to decoder" << std::endl;
                    av_packet_unref(packet);
                    continue;
                }
                
                // Receive decoded frames
                while (ret >= 0) {
                    ret = avcodec_receive_frame(codecContext, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        std::cerr << "Error receiving frame from decoder" << std::endl;
                        break;
                    }
                    
                    frameCount++;
                    
                    // Print first and last few frames
                    if (frameCount <= 5 || frameCount % 100 == 0) {
                        std::cout << "Frame " << frameCount << " decoded"
                                  << " (pts=" << frame->pts << ")" << std::endl;
                    }
                    
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(packet);
        }
        
        // Flush decoder
        avcodec_send_packet(codecContext, nullptr);
        int ret;
        while ((ret = avcodec_receive_frame(codecContext, frame)) >= 0) {
            frameCount++;
            std::cout << "Frame " << frameCount << " decoded (from flush)" << std::endl;
            av_frame_unref(frame);
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << "\nDecoding complete:" << std::endl;
        std::cout << "Total frames decoded: " << frameCount << std::endl;
        std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
        std::cout << "FPS: " << (frameCount * 1000.0 / duration.count()) << std::endl;
        
        av_frame_free(&frame);
        av_packet_free(&packet);
        
        return frameCount;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file> [video_file2...]" << std::endl;
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        std::cout << "\n=== Testing file: " << argv[i] << " ===" << std::endl;
        
        MinimalDecoder decoder;
        if (decoder.open(argv[i])) {
            decoder.printInfo();
            std::cout << std::endl;
            
            int64_t frameCount = decoder.countFrames();
            
            // Get expected frame count using ffprobe method
            std::string cmd = "ffprobe -v error -select_streams v:0 -count_packets "
                             "-show_entries stream=nb_read_packets -of csv=p=0 ";
            cmd += argv[i];
            
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buffer[128];
                if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    int64_t expectedFrames = std::stoll(buffer);
                    std::cout << "\nExpected frames (ffprobe): " << expectedFrames << std::endl;
                    std::cout << "Actual frames decoded: " << frameCount << std::endl;
                    if (expectedFrames != frameCount) {
                        std::cout << "WARNING: Frame count mismatch! Difference: " 
                                  << (frameCount - expectedFrames) << std::endl;
                    } else {
                        std::cout << "SUCCESS: Frame counts match!" << std::endl;
                    }
                }
                pclose(pipe);
            }
        }
    }
    
    return 0;
}