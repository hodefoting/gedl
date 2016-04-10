#ifndef GEDL_H
#define GEDL_H

typedef struct _GeglEDL GeglEDL;
typedef struct _Clip    Clip;

int         gegl_make_thumb_video (const char *path, const char *thumb_path);
const char *compute_cache_path    (const char *path);
void        gegl_make_video_cache (const char *path, const char *cache_path);

#define CACHE_TRY_SIMPLE    (1<<0)
#define CACHE_TRY_MIX       (1<<1)
#define CACHE_TRY_FILTERED  (1<<2)
#define CACHE_TRY_ALL       (CACHE_TRY_SIMPLE | CACHE_TRY_FILTERED | CACHE_TRY_MIX)
#define CACHE_MAKE_FILTERED (1<<3)
#define CACHE_MAKE_SIMPLE   (1<<4)
#define CACHE_MAKE_MIX      (1<<5)
#define CACHE_MAKE_ALL      (CACHE_MAKE_SIMPLE|CACHE_MAKE_MIX|CACHE_MAKE_FILTERED)

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
Clip       *gedl_get_clip (GeglEDL *edl, int frame, int *clip_frame_no);
GeglBuffer *gedl_get_buffer         (GeglEDL    *edl);
GeglBuffer *gedl_get_buffer2        (GeglEDL    *edl);
double gedl_get_mix                 (GeglEDL    *edl);
const char *gedl_get_clip2_path     (GeglEDL    *edl);
int         gedl_get_clip2_frame_no (GeglEDL    *edl);
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
void rig_frame (GeglEDL *edl, int frame_no);

/*********/

typedef struct SourceClip
{
  char *path;
  int   start;
  int   end;
  char *title;
  int   duration;
  int   editing;
} SourceClip;

struct _Clip
{
  char  *path;  /*path to media file */
  int    start; /*frame number starting with 0 */
  int    end;   /*last frame, inclusive fro single frame, make equal to start */
  char  *title;
  int    duration;
  int    editing;
  /* start of this must match start of clip */

  double fps;
  char   sha256sum[20]; /*< would also be the filename of thumbtrack */
  int    fade_out; /* the main control for fading in.. */
  int    fade_in;  /* implied by previous clip fading */
  int    fade_pad_start; 
  int    fade_pad_end;
  int    is_image;
  char  *filter_graph; /* chain of gegl filters */
  int    abs_start;

  const char        *clip_path;
  GeglNode          *gegl;
  int                clip_frame_no;
  GeglBuffer        *buffer;
  GeglAudioFragment *audio;
  GeglNode          *loader;
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
  char       *path;
  GList      *clip_db;
  GList      *clips;
  int         frame;
  double      fps;
  GeglBuffer *buffer;
  GeglNode   *gegl;
  int         width;
  int         height;
  double      mix;
  GeglNode   *cache_loader;
  int         cache_flags;
  Clip       *clip;
  Clip       *clip2;
  int         selection_start;
  int         selection_end;
  int         range_start;
  int         range_end;
  const char *output_path;
  const char *video_codec;
  const char *audio_codec;
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

  char       *script_hash;
} _GeglEDL;

Clip *clip_new            (void);
void  clip_free           (Clip *clip);
const char *clip_get_path (Clip *clip);
void  clip_set_path       (Clip *clip, const char *path);
int   clip_get_start      (Clip *clip);
int   clip_get_end        (Clip *clip);
int   clip_get_frames     (Clip *clip);
void  clip_set_start      (Clip *clip, int start);
void  clip_set_end        (Clip *clip, int end);
void  clip_set_range      (Clip *clip, int start, int end);
void  clip_set_full       (Clip *clip, const char *path, int start, int end);
Clip *clip_new_full       (const char *path, int start, int end);

#endif
