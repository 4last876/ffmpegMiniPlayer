extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}
#include <iostream>
#include <sstream>
#include <queue>
#include <mutex>
#include <algorithm>
#include <functional>
#include <thread>
#include <future>
#include <memory>
#include <optional>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_sdl_renderer.h"

class MediaEngine{
using FormatContext = std::shared_ptr<AVFormatContext>;
using CodecParameteres = AVCodecParameters*;
using CodecContext = std::shared_ptr<AVCodecContext>;

FormatContext current_formatContext;
CodecContext current_codec_context;
std::pair<int,CodecParameteres> current_video_steream;

std::unique_ptr<AVFrame,void(*)(AVFrame*)> current_frame {av_frame_alloc(),Delete_Frame};
std::unique_ptr<AVPacket,void(*)(AVPacket*)> current_packet {av_packet_alloc(),Delete_Packet};

bool is_end = false;

void initContext(std::string path){
FormatContext::element_type* local_formatContext = nullptr;  
avformat_open_input(&local_formatContext,path.c_str(),nullptr,nullptr);
current_formatContext.reset(local_formatContext,Deleter_FormatContext);
}

void initStreams(){
  avformat_find_stream_info(current_formatContext.get(), nullptr);
  int video_steream = av_find_best_stream(current_formatContext.get(),AVMediaType::AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
  CodecParameteres pLocalCodecParameters = current_formatContext->streams[video_steream]->codecpar;
  current_video_steream = std::make_pair(video_steream,pLocalCodecParameters);
}

void initCodec(){
  const AVCodec* Codec;
  CodecContext::element_type* pLocalCodecContext;
  Codec = avcodec_find_decoder(current_video_steream.second->codec_id);
  pLocalCodecContext = avcodec_alloc_context3(Codec);
  avcodec_parameters_to_context(pLocalCodecContext,current_video_steream.second);
  avcodec_open2(pLocalCodecContext,Codec,nullptr);
  current_codec_context.reset(pLocalCodecContext,Deleter_CodecContext);
}

  public:

constexpr static void Deleter_FormatContext(FormatContext::element_type* elem){
avformat_close_input(&elem);
}

constexpr static void Deleter_CodecContext(CodecContext::element_type* elem){
avcodec_free_context(&elem);
}

constexpr static void Delete_Frame(AVFrame* elem){
av_frame_free(&elem);
}

constexpr static void Delete_Packet(AVPacket* elem){
av_packet_free(&elem);
}


 void setVideo(std::string path){
   is_end = false;
   initContext(path);
   initStreams();
   initCodec();
  }

std::unique_ptr<AVPacket,void(*)(AVPacket*)> getPacket(){
if(av_read_frame(current_formatContext.get(),current_packet.get()) >= 0){
if(current_packet->stream_index == current_video_steream.first){
AVPacket* pk = av_packet_alloc();
av_packet_move_ref(pk,current_packet.get());
return std::unique_ptr<AVPacket,void(*)(AVPacket*)>(pk,Delete_Packet);
}else{
  av_packet_unref(current_packet.get());
}

}else{
    is_end = true;
    av_packet_unref(current_packet.get());
  }
  return {nullptr,Delete_Packet};
}

std::unique_ptr<AVFrame,void(*)(AVFrame*)> getFrame(AVPacket* current_packet){
if(int res = avcodec_send_packet(current_codec_context.get(), current_packet); res  >= 0){
     if((res = avcodec_receive_frame(current_codec_context.get(), current_frame.get())) >= 0){
      AVFrame* fr = av_frame_alloc();
      fr->format = current_frame->format;
      fr->width = current_frame->width;
      fr->height = current_frame->height;
      av_frame_get_buffer(fr, 0); 
      av_frame_copy(fr,current_frame.get());
      av_frame_copy_props(fr,current_frame.get()); 
      return std::unique_ptr<AVFrame, void(*)(AVFrame*)>(fr,Delete_Frame);
     }
   }
   return {nullptr,Delete_Frame};
}  

bool IsEnd() const {
  return is_end;
}
   
double timebase() const{
  float res = static_cast<float>(current_formatContext->streams[current_video_steream.first]->time_base.num) / static_cast<float>(current_formatContext->streams[current_video_steream.first]->time_base.den);
  std::cout << res << '\t'<< current_formatContext->streams[current_video_steream.first]->time_base.num << '\t' << current_formatContext->streams[current_video_steream.first]->time_base.den << std::endl;
  return res;
}

void forward(int n_seconds){

}
    
void backward(int n_seconds){

}

};

class QueuePacket {
    std::queue<AVPacket*> queue;
    std::mutex mtx;
    std::condition_variable cv;
    const size_t max_size = 100;

constexpr static void Delete_Packet(AVPacket* elem){
av_packet_free(&elem);
}

public:
    void pushPacket(AVPacket* pkt) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return queue.size() < max_size; });
        
        AVPacket* new_pkt = av_packet_alloc();
        av_packet_move_ref(new_pkt, pkt); 
        queue.push(new_pkt);
        
        cv.notify_one();
    }

    std::unique_ptr<AVPacket,void (*)(AVPacket*)> popPacket() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !queue.empty(); });
        
        AVPacket* pkt = queue.front();
        queue.pop();
        
        cv.notify_one();
        return {pkt,Delete_Packet};
    }

    bool is_empty(){
      return queue.empty();
    }
};

class QueuefFrame {
    std::mutex mtx;
    std::condition_variable cv;
    std::queue<AVFrame*> queue;
    const size_t max_size = 20;

constexpr static void Delete_Frame(AVFrame* elem){
av_frame_free(&elem);
}

public:
    void pushFrame(AVFrame* frm) {
      std::unique_lock lock (mtx);
      cv.wait(lock, [this](){return queue.size() < max_size;});

      queue.push(frm);

      cv.notify_one();
    }

    std::unique_ptr<AVFrame,void (*)(AVFrame*)> popFrame() {
      std::unique_lock lock (mtx);
      cv.wait(lock, [&](){return !queue.empty();});

      AVFrame* pfrm = queue.front();
      queue.pop();

      cv.notify_one();

      return {pfrm,Delete_Frame};
    }

    bool is_empty(){
      return queue.empty();
    }

};

class ManagerFrames{
std::shared_ptr<QueuePacket> queuePackets;
std::shared_ptr<QueuefFrame> QueueFrame;
std::shared_ptr<MediaEngine> mediaEngine;

public:
  ManagerFrames(std::shared_ptr<QueuePacket> queuePackets_,std::shared_ptr<QueuefFrame> QueueFrame_,std::shared_ptr<MediaEngine> mediaEngine_) : queuePackets(queuePackets_),QueueFrame(QueueFrame_),mediaEngine(mediaEngine_){

  }

  void thread_for_packets(bool &is_pause){
     while(!mediaEngine->IsEnd()){
      if(!is_pause){
        auto packet = mediaEngine->getPacket();
         if(packet){
          queuePackets->pushPacket(packet.get());
           }
         }
      }
  }

  void thread_for_frame(bool &is_pause){
     while(!mediaEngine->IsEnd() || !queuePackets->is_empty()){
      if(!is_pause){
        auto paket = queuePackets->popPacket();
        auto frame = mediaEngine->getFrame(paket.get());
        if(frame){
        QueueFrame->pushFrame(frame.release());
          }
       }
     }
  }

};

class IVideoPlayer {
public:
    virtual ~IVideoPlayer() = default;
    virtual void setVideo(const std::string& path) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void forward(int frameIndex) = 0;
    virtual void backward(int frameIndex) = 0;
    virtual std::unique_ptr<AVFrame,void (*)(AVFrame*)> getFrame()  = 0;
    virtual bool isEnd() const = 0;
    virtual float timebase() const = 0;
    virtual std::string c() const = 0;
};

class VideoPlayer : public IVideoPlayer{
std::shared_ptr<QueuePacket> queuePackets = std::make_shared<QueuePacket>();
std::shared_ptr<QueuefFrame> QueueFrame = std::make_shared<QueuefFrame>();
std::shared_ptr<MediaEngine> mediaEngine = std::make_shared<MediaEngine>();
ManagerFrames frameManagers;
bool is_pause = false;
bool is_end = false;

constexpr static void Delete_Frame(AVFrame* elem){
av_frame_free(&elem);
}

  public:
 
  VideoPlayer() : frameManagers(queuePackets,QueueFrame,mediaEngine){

  }

     void setVideo(const std::string& path){
      mediaEngine->setVideo(path);
     }

     void play(){
    {
      auto funcPackets = std::bind(&ManagerFrames::thread_for_packets,&frameManagers,std::placeholders::_1);
      std::thread ad(funcPackets,std::ref(is_pause));
      ad.detach();
    }

    {
      auto funcFrame = std::bind(&ManagerFrames::thread_for_frame,&frameManagers,std::placeholders::_1);
      std::thread ad(funcFrame,std::ref(is_pause));
      ad.detach();
    }
    
  }

     void pause(){
         is_pause = !is_pause;
     }

     void stop(){
        is_end = true;
     }

    void forward(int n_second){
    }
    
    void backward(int n_second){
    }

     bool isEnd() const {
        return  is_end || mediaEngine->IsEnd();
     }

     float timebase() const {
      return mediaEngine->timebase();
     }

       std::unique_ptr<AVFrame,void (*)(AVFrame*)> getFrame(){
        if(!is_pause)
        return QueueFrame->popFrame();
        return {nullptr,Delete_Frame};
     }

     std::string getState() const {

     }

};


class FontInit{
float font_scale = 1;
float font_size = 20;
  public:

   void initFont(struct nk_context* ctx, std::string path){
        struct nk_font_atlas *atlas;
        struct nk_font_config config = nk_font_config(font_size);
        config.oversample_h = 1;
        config.oversample_v = 1;
        config.range = nk_font_cyrillic_glyph_ranges();
        
   
        nk_sdl_font_stash_begin(&atlas);
        struct nk_font *font = nk_font_atlas_add_from_file(atlas,path.c_str(), font_size, &config);
        nk_sdl_font_stash_end();
        nk_style_set_font(ctx, &font->handle); 

        font->handle.height /= font_scale;
        nk_style_set_font(ctx, &font->handle);
    }
};

class VideoTexture{
std::shared_ptr<SDL_Texture> texture;

constexpr static void DeleterTexture(SDL_Texture* text){
SDL_DestroyTexture(text);
}
  public:

  void setDataTexture(SDL_Renderer* render, AVFrame* frame){
    if(!texture)
    texture.reset(SDL_CreateTexture(render,SDL_PIXELFORMAT_YV12,SDL_TEXTUREACCESS_STREAMING,frame->width,frame->height),DeleterTexture);
    SDL_UpdateYUVTexture(texture.get(),nullptr,frame->data[0],frame->linesize[0],frame->data[1],frame->linesize[1],frame->data[2],frame->linesize[2]);
}
  
SDL_Texture* get(){
  return texture.get();
}
};


class IWindowRender{
  public:
  virtual ~IWindowRender(){} 
  virtual  void run() = 0;
};

class SDLWindowRender : public IWindowRender{
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 650;

bool quit = false;
bool running = false;

std::shared_ptr<SDL_Renderer> render;
std::shared_ptr<SDL_Window> win;
std::shared_ptr<VideoPlayer> playvideo = std::make_shared<VideoPlayer>();
FontInit font;
VideoTexture videoTexture;

struct nk_context *ctx;
struct nk_command_buffer *canvas;
struct nk_colorf bg;

constexpr static void DeleterRend(SDL_Renderer* render){
SDL_DestroyRenderer(render);
}

constexpr static void DeleterWin(SDL_Window* win){
SDL_DestroyWindow(win);
}

constexpr static void DeleterTexture(SDL_Texture* text){
SDL_DestroyTexture(text);
}

void show(){
  if (nk_begin(ctx, "Demo", nk_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT),
            NK_WINDOW_BORDER))
        {
            enum {EASY, HARD};
            static int op = EASY;
            static int property = 20;

            nk_layout_row_static(ctx, 30, 80, 1);
            nk_label(ctx,"lolzs",0);
            nk_layout_row_dynamic(ctx,500, 1);
            if(videoTexture.get() != nullptr){
            struct nk_image img = nk_image_ptr(videoTexture.get());
            nk_image(ctx,img);
            nk_layout_row_dynamic(ctx,30, 1);
             if(nk_button_label(ctx,"||")){
              playvideo->pause();
                }
            }
        }
        nk_end(ctx);
    }

    void renders(){
    SDL_SetRenderDrawColor(render.get(), 0xFF, 0xFF, 0xFF, 0xFF );
    SDL_RenderClear(render.get());
    nk_sdl_render(NK_ANTI_ALIASING_ON);
    SDL_RenderPresent(render.get());
    }

    void update(){
        SDL_Event evt;
        nk_input_begin(ctx);
        while (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_QUIT) running = !running;
            if(evt.key.keysym.sym  == SDLK_SPACE){playvideo->pause();}
            nk_sdl_handle_event(&evt);
        }
        nk_sdl_handle_grab(); 
        nk_input_end(ctx);
    }

public:

SDLWindowRender(){
    if(SDL_Init(SDL_INIT_VIDEO) < 0){
        std::cout << "Error: " << SDL_GetError() << std::endl;
    }

    win.reset(SDL_CreateWindow("Love",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,SCREEN_WIDTH,SCREEN_HEIGHT,SDL_WINDOW_SHOWN),DeleterWin);
    if(win.get() == nullptr){
        std::cout << "Error: " << SDL_GetError() << std::endl;
    }

    render.reset(SDL_CreateRenderer(win.get(),-1,SDL_RENDERER_ACCELERATED),DeleterRend);

    if(render.get() == nullptr){
        std::cout << "Error: " << SDL_GetError() << std::endl;
      } 

      ctx = nk_sdl_init(win.get(), render.get());
      font.initFont(ctx,"font.ttf");
    }

void run() override{
  playvideo->setVideo("path");
  playvideo->play();

  float last_frame_time = 0; 
  float timebase = playvideo->timebase();

  auto timepoit_start = std::chrono::system_clock::now();
              while(!running){
                  if(!playvideo->isEnd()){
      auto timepoit_last = std::chrono::system_clock::now();
      if(auto frame = playvideo->getFrame(); frame != nullptr){
       float first_frame_time = 0;
       float different_frame_time = 0;
       first_frame_time = frame->pts * timebase;

       auto timepoit_now = std::chrono::system_clock::now();
       std::chrono::duration<double,std::micro> different_time = timepoit_now - timepoit_last;
       std::chrono::duration<double,std::micro> different_time_start_program = timepoit_now - timepoit_start;

       different_frame_time = ((first_frame_time - last_frame_time) * 1000000);

       if((first_frame_time * 1000000) < different_time_start_program.count()){
           std::cout << "A\t" <<(first_frame_time * 1000) << std::endl;
           videoTexture.setDataTexture(render.get(),frame.get());
       }else{
           std::cout << "B\t" << std::chrono::duration_cast<std::chrono::milliseconds>(different_time_start_program).count() << std::endl;
           std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>(different_frame_time - different_time.count())));
           videoTexture.setDataTexture(render.get(),frame.get());
       }

       last_frame_time = first_frame_time;
                      }
          }
      show();
      renders();
      update();
    }
 }

};


int main(){
  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
  SDL_Init(SDL_INIT_VIDEO);
  SDLWindowRender wind;
  wind.run();
  SDL_Quit();
}
