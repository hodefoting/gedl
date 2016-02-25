#ifndef GEDL_H
#define GEDL_H

typedef struct _GeglEDL GeglEDL;

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
int         gedl_get_frames         (GeglEDL    *edl);
double      gedl_get_time           (GeglEDL    *edl);
void        gedl_parse_line         (GeglEDL    *edl, const char *line);
GeglEDL    *gedl_new_from_path      (const char *path);
void        gedl_load_path          (GeglEDL *edl, const char *path);
void        gedl_save_path          (GeglEDL *edl, const char *path);
GeglAudioFragment  *gedl_get_audio  (GeglEDL    *edl);
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

int         gedl_get_render_complexity (GeglEDL *edl, int frame);
/* Make it possibly to decide whether we render preview or final frame.. */

/*********/
typedef struct Clip
{

  char  *path;  /*path to media file */
  double fps;
  int    duration;
  char   sha256sum[20]; /*< would also be the filename of thumbtrack */
  int    start; /*frame number starting with 0 */
  int    end;   /*last frame, inclusive fro single frame, make equal to start */
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
} Clip;

struct _GeglEDL
{
  char       *path;
  GList      *clip_db;
  GList      *clips;
  int         frame;
  double      fps;
  GeglNode   *gegl;
  int         width;
  int         height;
  double      mix;
  GeglNode   *cache_loader;
  int         cache_flags;

  //Clip       *source[2];


  Clip *clip;
  Clip *clip2;
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
