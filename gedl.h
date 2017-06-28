#if PLAN

  bugs
    !!audio glitches, gegl ff-load / ff-save should perhaps round all audio-frame
      counts up to a fixed amount, use correct start .. and drop frames when
      assembling
    huge video files cause (cairo) thumtrack overflow, vertical also has this problem - how to split?

  features
    rewrite gedl-ui.c in lua and call it gcut
    using edl files as clip sources
    support for other timecodes, mm:ss:ff and s
    ui for adding/editing annotations, setting variables for
      interpolation in annotations? -- maybe good for subtitles?
    global filter(s) permitting different chains to apply over many clips
    trimming by mouse / dragging clips around by mouse
    implement overlaying of audio from wav / mp3 files - similar to annotations/global filters..
    templates - for both clips and filters - filters that can be chained
    detect locked or crashed ui, kill and respawn
    gaps in timeline (will be implemented as blank clips - but ui could be different)
    insert videos from the commandline

#endif

#define CACHE_FORMAT "jpg"
#define GEDL_SAMPLER   GEGL_SAMPLER_NEAREST

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

void        gedl_set_frame          (GeglEDL    *edl, int frame);
void        gedl_set_time           (GeglEDL    *edl, double seconds);
int         gedl_get_frame          (GeglEDL    *edl);
char       *gedl_serialize          (GeglEDL    *edl);

void        gedl_set_range          (GeglEDL    *edl, int start_frame, int end_frame);
void        gedl_get_range          (GeglEDL    *edl,
                                     int        *start_frame,
                                     int        *end_frame);

void        gedl_set_selection      (GeglEDL    *edl, int start_frame, int end_frame);
void        gedl_get_selection      (GeglEDL    *edl,
                                     int        *start_frame,
                                     int        *end_frame);
char       *gedl_make_thumb_path    (GeglEDL    *edl, const char *clip_path);
guchar     *gedl_get_cache_bitmap   (GeglEDL *edl, int *length_ret);


Clip  *clip_new               (GeglEDL *edl);
void   clip_free              (Clip *clip);
const char *clip_get_path     (Clip *clip);
void   clip_set_path          (Clip *clip, const char *path);
int    clip_get_start         (Clip *clip);
int    clip_get_end           (Clip *clip);
int    clip_get_frames        (Clip *clip);
void   clip_set_start         (Clip *clip, int start);
void   clip_set_end           (Clip *clip, int end);
void   clip_set_range         (Clip *clip, int start, int end);
int    clip_is_static_source  (Clip *clip);
gchar *clip_get_frame_hash    (Clip *clip, int clip_frame_no);


void   clip_fetch_audio       (Clip *clip);
void   clip_set_full          (Clip *clip, const char *path, int start, int end);
Clip  *clip_new_full          (GeglEDL *edl, const char *path, int start, int end);

//void   clip_set_frame_no      (Clip *clip, int frame_no);
void clip_render_frame (Clip *clip, int clip_frame_no);


Clip * edl_get_clip_for_frame (GeglEDL *edl, int frame);
void   gedl_make_proxies      (GeglEDL *edl);
void gedl_get_video_info (const char *path, int *duration, double *fps);
void gegl_meta_set_audio (const char        *path,
                          GeglAudioFragment *audio);
void gegl_meta_get_audio (const char        *path,
                          GeglAudioFragment *audio);

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
  int    fade; /* the main control for fading in.. */
  int    static_source;
  int    is_chain;
  int    is_meta;

  int    abs_start;

  const char        *clip_path;
  GeglNode          *gegl;
  GeglAudioFragment *audio;
  GeglNode          *chain_loader;
  GeglNode          *full_loader;
  GeglNode          *proxy_loader;
  GeglNode          *loader; /* nop that one of the prior is linked to */

  GeglNode          *nop_scaled;
  GeglNode          *nop_crop;
  GeglNode          *nop_store_buf;

  GMutex             mutex;
};

struct _GeglEDL
{
  GFileMonitor *monitor;
  char         *path;
  char         *parent_path;
  GList        *clip_db;
  GList        *clips;
  int           frame; /* render thread, frame_no is ui side */
  double        fps;
  GeglBuffer   *buffer;
  GeglBuffer   *buffer_copy_temp;
  GeglBuffer   *buffer_copy;
  GMutex        buffer_copy_mutex;
  GeglNode     *cached_result;
  GeglNode     *gegl;
  int           playing;
  int           width;
  int           height;
  GeglNode     *cache_loader;
  int           cache_flags;
  int           selection_start;
  int           selection_end;
  int           range_start;
  int           range_end;
  const char   *output_path;
  const char   *video_codec;
  const char   *audio_codec;
  int           proxy_width;
  int           proxy_height;
  int           video_width;
  int           video_height;
  int           video_size_default;
  int           video_bufsize;
  int           video_bitrate;
  int           video_tolerance;
  int           audio_bitrate;
  int           audio_samplerate;
  int           frame_no;
  int           source_frame_no;
  int           use_proxies;
  int           framedrop;
  int           ui_mode;

  GeglNode     *result;
  GeglNode     *store_final_buf;
  GeglNode     *mix;

  GeglNode     *encode;
  double        scale;
  double        t0;
  Clip         *active_clip;

  void         *mrg;

  char       *clip_query;
  int         clip_query_edited;
  int         filter_edited;
} _GeglEDL;

void update_size (GeglEDL *edl, Clip *clip);
void remove_in_betweens (GeglNode *nop_scaled, GeglNode *nop_filtered);
int  is_connected (GeglNode *a, GeglNode *b);
void gedl_update_buffer (GeglEDL *edl);

#endif
