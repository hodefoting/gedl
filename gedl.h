#if PLAN

rename to gcut ? which can mean GEGL cut, goat cut or gnome cut

bugs
  bounding box of preview/proxy not working properly
  huge video files cause thumtrack overflow, vertical instead of horizontal
  might not have same scanline problem.
  clipped left most clip cannot be scrubbed
  audio glitches, gegl ff-load / ff-save should perhaps round all audio-frame
    counts up to a fixed amount, use correct start .. and drop frames when
    assembling

features
  rewrite gedl-ui.c in lua
  add support for other frame sources.
     image
     video
     op-chain - or is it enough to permit opchain on empty source?

  annotations
  trimming by mouse / dragging clips around by mouse
  implement overlaying of audio from wav / mp3 files
  re-enable cross-fades for video and audio
  templates - for both clips and filters - filters that can be chained
  create thumbnails as well as thumbtracks for clips
     related: also write a cache for proxy.. when heavily filtered it is neccesary, frames from here can form thumbtrack
  pcm cache / thumb cache would almost be...
  detect locked or crashed ui, kill and respawn
  gaps in timeline (will be implemented as blank clips - but ui can be different)
  better video file import
    insert videos from the commandline
    ui for picking clips in current folder, possibly clib-db separate from video-project
    clip database done on demand - files that appear in the timeline get enrolled, as well as files thatappearinthe working dir.

refactor
   make each clip not have a loader but have a pool of loaders, that can be pre-seeded with right paths for upcoming clips during playback
   make active_clip not be shared between renderer and ui, perhaps not even serialize it?

#endif

#ifndef GEDL_H
#define GEDL_H

#include <gio/gio.h>

typedef struct _GeglEDL GeglEDL;
typedef struct _Clip    Clip;

void gedl_set_use_proxies (GeglEDL *edl, int use_proxies);
int         gegl_make_thumb_video (GeglEDL *edl, const char *path, const char *thumb_path);
char *gedl_make_proxy_path (GeglEDL *edl, const char *clip_path);
const char *compute_cache_path    (const char *path);

#define CACHE_TRY_SIMPLE    (1<<0)
#define CACHE_TRY_MIX       (1<<1)
#define CACHE_TRY_FILTERED  (1<<2)
#define CACHE_TRY_ALL       (CACHE_TRY_SIMPLE | CACHE_TRY_FILTERED | CACHE_TRY_MIX)
#define CACHE_MAKE_FILTERED (1<<3)
#define CACHE_MAKE_SIMPLE   (1<<4)
#define CACHE_MAKE_MIX      (1<<5)
#define CACHE_MAKE_ALL      (CACHE_MAKE_SIMPLE|CACHE_MAKE_MIX|CACHE_MAKE_FILTERED)

enum {
  GEDL_UI_MODE_FULL = 0,
  GEDL_UI_MODE_TIMELINE = 1,
  GEDL_UI_MODE_NONE = 2,
  GEDL_UI_MODE_PART = 3,
};

#define GEDL_LAST_UI_MODE 2

GeglEDL    *gedl_new                (void);
void        gedl_free               (GeglEDL    *edl);
void        gedl_set_fps            (GeglEDL    *edl,
                                     double      fps);
double      gedl_get_fps            (GeglEDL    *edl);
int         gedl_get_duration       (GeglEDL    *edl);
double      gedl_get_time           (GeglEDL    *edl);
void        gedl_parse_line         (GeglEDL    *edl, const char *line);
GeglEDL    *gedl_new_from_path      (const char *path);
void        gedl_load_path          (GeglEDL    *edl, const char *path);
void        gedl_save_path          (GeglEDL    *edl, const char *path);
GeglAudioFragment  *gedl_get_audio  (GeglEDL    *edl);
Clip       *gedl_get_clip           (GeglEDL *edl, int frame, int *clip_frame_no);

GeglBuffer *gedl_get_buffer         (GeglEDL    *edl);

void        gedl_set_frame          (GeglEDL    *edl, int frame);
void        gedl_set_time           (GeglEDL    *edl, double seconds);
int         gedl_get_frame          (GeglEDL    *edl);
const char *gedl_get_clip_path      (GeglEDL    *edl);
int         gedl_get_clip_frame_no  (GeglEDL    *edl);
char       *gedl_serialise          (GeglEDL    *edl);

void        gedl_set_range          (GeglEDL    *edl, int start_frame, int end_frame);
void        gedl_get_range          (GeglEDL    *edl,
                                     int        *start_frame,
                                     int        *end_frame);

void        gedl_set_selection      (GeglEDL    *edl, int start_frame, int end_frame);
void        gedl_get_selection      (GeglEDL    *edl,
                                     int        *start_frame,
                                     int        *end_frame);
char       *gedl_make_thumb_path    (GeglEDL    *edl, const char *clip_path);
guchar *gedl_get_cache_bitmap (GeglEDL *edl, int *length_ret);
void rig_frame (GeglEDL *edl, int frame_no);

Clip  *clip_new               (GeglEDL *edl);
void   clip_free              (Clip *clip);
const char *clip_get_path     (Clip *clip);
void   clip_set_path          (Clip *clip, const char *path);
int    clip_get_start         (Clip *clip);
int    clip_get_end           (Clip *clip);
int    clip_get_frames        (Clip *clip);
int    clip_is_static_source  (Clip *clip);
void   clip_set_start         (Clip *clip, int start);
void   clip_set_end           (Clip *clip, int end);
void   clip_set_range         (Clip *clip, int start, int end);
void   clip_fetch_audio       (Clip *clip);
void   clip_set_full          (Clip *clip, const char *path, int start, int end);
Clip  *clip_new_full          (GeglEDL *edl, const char *path, int start, int end);
void   clip_set_frame_no      (Clip *clip, int frame_no);
Clip * edl_get_clip_for_frame (GeglEDL *edl, int frame);
void   gedl_make_proxies      (GeglEDL *edl);
void gedl_get_video_info (const char *path, int *duration, double *fps);

#define SPLIT_VER  0.8

extern char *gedl_binary_path;

/*********/

typedef struct SourceClip
{
  char  *path;
  int    start;
  int    end;
  char  *title;
  int    duration;
  int    editing;
  char  *filter_graph; /* chain of gegl filters */
} SourceClip;

struct _Clip
{
  char  *path;  /*path to media file */
  int    start; /*frame number starting with 0 */
  int    end;   /*last frame, inclusive fro single frame, make equal to start */
  char  *title;
  int    duration;
  int    editing;
  char  *filter_graph; /* chain of gegl filters */
  /* to here Clip must match start of SourceClip */
  GeglEDL *edl;

  double fps;
  int    fade_out; /* the main control for fading in.. */
  int    fade_in;  /* implied by previous clip fading */
  int    fade_pad_start;
  int    fade_pad_end;
  int    static_source;

  int    abs_start;

  const char        *clip_path;
  GeglNode          *gegl;
  int                clip_frame_no;
  GeglBuffer        *buffer;
  GeglAudioFragment *audio;
  GeglNode          *loader;
  GeglNode          *proxy_loader;
  GeglNode          *store_buf;
  char              *cached_filter_graph;

  GMutex             mutex;
};

/*
typedef struct SourceVid
{
  char *path;
  int   clip_count;
  SourceClip clips[30];
} SourceVid;
*/

struct _GeglEDL
{
  GFileMonitor *monitor;
  char       *path;
  char       *parent_path;
  GList      *clip_db;
  GList      *clips;
  int         frame; /* render thread, frame_no is ui side */
  double      fps;
  GeglBuffer *buffer;
  GeglNode   *gegl;
  int         playing;
  int         width;
  int         height;
  GeglNode   *cache_loader;
  int         cache_flags;
  int         selection_start;
  int         selection_end;
  int         range_start;
  int         range_end;
  const char *output_path;
  const char *video_codec;
  const char *audio_codec;
  int         proxy_width;
  int         proxy_height;
  int         video_width;
  int         video_height;
  int         video_size_default;
  int         video_bufsize;
  int         video_bitrate;
  int         video_tolerance;
  int         audio_bitrate;
  int         audio_samplerate;
  int         fade_duration;
  int         frame_no;
  int         source_frame_no;
  int         use_proxies;
  int         framedrop;
  int         ui_mode;

  GeglNode   *nop_raw;
  GeglNode   *nop_transformed;
  GeglNode   *nop_raw2;
  GeglNode   *nop_transformed2;
  GeglNode   *load_buf;
  GeglNode   *result, *encode, *crop, *scale_size, *opacity,
             *load_buf2, *crop2, *scale_size2, *over;
  GeglNode   *store_buf;
  GeglNode   *source_store_buf;
  GeglNode   *cached_result;
  double      scale;
  double      t0;
  Clip       *active_clip;
  SourceClip *active_source;

  void       *mrg;

  char       *clip_query;
  int         clip_query_edited;

  int         filter_edited;

} _GeglEDL;


#endif
