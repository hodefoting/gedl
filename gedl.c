#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <gegl.h>
#include <gexiv2/gexiv2.h>
#include <gegl-audio-fragment.h>


#define GEDL_SAMPLER   GEGL_SAMPLER_NEAREST

/* GEGL edit decision list - a digital video cutter and splicer */

/* take a string and expand {t=v i t=v t=v }  to numeric or string
   value. Having it that way.. makes it hard to keep parts of graph,.
   unless graph is kept when constructing... and values are filled in
   if topology of graphs match..
 */

#include "gedl.h"

void
gegl_meta_set_audio (const char        *path,
                     GeglAudioFragment *audio);
void
gegl_meta_get_audio (const char        *path,
                     GeglAudioFragment *audio);

#define DEFAULT_output_path      "output.mp4"
#define DEFAULT_video_codec      "auto"
#define DEFAULT_audio_codec      "auto"
#define DEFAULT_video_width       0
#define DEFAULT_video_height      0
#define DEFAULT_proxy_width       0
#define DEFAULT_proxy_height      0
#define DEFAULT_video_bufsize     0
#define DEFAULT_video_bitrate     256
#define DEFAULT_video_tolerance   -1
#define DEFAULT_audio_bitrate     64
#define DEFAULT_audio_samplerate  64
#define DEFAULT_fade_duration     20
#define DEFAULT_frame_start       0
#define DEFAULT_frame_end         0
#define DEFAULT_selection_start   0
#define DEFAULT_selection_end     0
#define DEFAULT_range_start       0
#define DEFAULT_range_end         0
#define DEFAULT_framedrop         0

char *gedl_binary_path = NULL;

static char *escaped_base_path (GeglEDL *edl, const char *clip_path)
{
  char *path0= g_strdup (clip_path);
  char *path = path0;
  int i;
  char *ret = 0;
  if (!strncmp (path, edl->parent_path, strlen(edl->parent_path)))
      path += strlen (edl->parent_path);
  for (i = 0; path[i];i ++)
  {
    switch (path[i]){
      case '/':
      case ' ':
      case '\'':
      case '#':
      case '%':
      case '?':
      case '*':
        path[i]='_';
        break;
    }
  }
  ret = g_strdup (path);
  g_free (path0);
  return ret;
}

char *gedl_make_thumb_path (GeglEDL *edl, const char *clip_path)
{
  gchar *ret;
  gchar *path = escaped_base_path (edl, clip_path);
  ret = g_strdup_printf ("%s.gedl/thumb/%s.png", edl->parent_path, path); // XXX: should escape relative/absolute path instead of basename - or add bit of its hash
  g_free (path);
  return ret;
}

char *gedl_make_proxy_path (GeglEDL *edl, const char *clip_path)
{
  gchar *ret;
  gchar *path = escaped_base_path (edl, clip_path);
  ret = g_strdup_printf ("%s.gedl/proxy/%s-%ix%i.mp4", edl->parent_path, path, edl->proxy_width, edl->proxy_height);
  g_free (path);
  return ret;
}

#define CACHE_FORMAT "jpg"

#include <stdlib.h>

GeglEDL *gedl_new           (void)
{
  GeglRectangle roi = {0,0,1024, 1024};
  GeglEDL *edl = g_malloc0(sizeof (GeglEDL));


  edl->gegl = gegl_node_new ();
  edl->cache_flags = 0;
  edl->cache_flags = CACHE_TRY_ALL;
  //edl->cache_flags = CACHE_TRY_ALL | CACHE_MAKE_ALL;
  edl->selection_start = 23;
  edl->selection_end = 42;

  edl->cache_loader = gegl_node_new_child (edl->gegl, "operation", "gegl:"  CACHE_FORMAT  "-load", NULL);

  edl->output_path      = DEFAULT_output_path;
  edl->video_codec      = DEFAULT_video_codec;
  edl->audio_codec      = DEFAULT_audio_codec;
  edl->video_width      = DEFAULT_video_width;
  edl->video_height     = DEFAULT_video_height;
  edl->proxy_width      = DEFAULT_proxy_width;
  edl->proxy_height     = DEFAULT_proxy_height;
  edl->video_size_default = 1;
  edl->video_bufsize    = DEFAULT_video_bufsize;
  edl->video_bitrate    = DEFAULT_video_bitrate;
  edl->video_tolerance  = DEFAULT_video_tolerance;;
  edl->audio_bitrate    = DEFAULT_audio_bitrate;
  edl->audio_samplerate = DEFAULT_audio_samplerate;
  edl->fade_duration    = DEFAULT_fade_duration;
  edl->framedrop        = DEFAULT_framedrop;
  edl->frame_no         = 0;  /* frame-no in ui shell */
  edl->frame = -1;            /* frame-no in renderer thread */
  edl->scale = 1.0;

  edl->buffer = gegl_buffer_new (&roi, babl_format ("R'G'B'A u8"));

  edl->clip_query = strdup ("");
  edl->use_proxies = 0;

  return edl;
}

void gedl_set_size (GeglEDL *edl, int width, int height);
void gedl_set_size (GeglEDL *edl, int width, int height)
{
  edl->width = width;
  edl->height = height;
}

void     gedl_free          (GeglEDL *edl)
{
  while (edl->clips)
  {
    clip_free (edl->clips->data);
    edl->clips = g_list_remove (edl->clips, edl->clips->data);
  }
  if (edl->path)
    g_free (edl->path);
  if (edl->parent_path)
    g_free (edl->parent_path);

  g_object_unref (edl->gegl);
  g_object_unref (edl->buffer);
  g_free (edl);
}

void remove_in_betweens (GeglNode *nop_raw, GeglNode *nop_transformed);
void remove_in_betweens (GeglNode *nop_raw, GeglNode *nop_transformed)
{
 GeglNode *iter = nop_raw;
 GList *collect = NULL;
 while (iter && iter != nop_transformed)
 {
   GeglNode **nodes = NULL;
   int count = gegl_node_get_consumers (iter, "output", &nodes, NULL);
   if (count) iter = nodes[0];
   g_free (nodes);
   if (iter && iter != nop_transformed)
     collect = g_list_append (collect, iter);
 }
 while (collect)
 {
    g_object_unref (collect->data);
    collect = g_list_remove (collect, collect->data);
 }
}

/* XXX: oops global state */


#if 1
GeglEDL *edl;
#endif


Clip *gedl_get_clip (GeglEDL *edl, int frame, int *clip_frame_no)
{
  GList *l;
  int clip_start = 0;

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int clip_frames = clip_get_frames (clip);

    if (frame - clip_start < clip_frames)
    {
      /* found right clip */
      if (clip_frame_no)
       *clip_frame_no = (frame - clip_start) + clip_get_start (clip);
      return clip;
    }
    clip_start += clip_frames;
  }
  return NULL;
}

int cache_hits = 0;
int cache_misses = 0;

static void update_size (GeglEDL *edl);
void gedl_set_use_proxies (GeglEDL *edl, int use_proxies)
{
  int frame;
  edl->use_proxies = use_proxies;

  if (edl->use_proxies)
    gedl_set_size (edl, edl->proxy_width, edl->proxy_height);
  else
    gedl_set_size (edl, edl->video_width, edl->video_height);

  frame = edl->frame;
  if (frame > 0)
  {
    edl->frame--;
    gedl_set_frame (edl, frame);
  }

  update_size (edl);
}
int do_encode = 1;

/* computes the hash of a given rendered frame - without altering
 * any state
 */
gchar *gedl_get_frame_hash (GeglEDL *edl, int frame)
{
  GList *l;
  int clip_start = 0;

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int clip_frames = clip_get_frames (clip);

    if (frame - clip_start < clip_frames)
    {
      gchar *frame_recipe;
      GChecksum *hash;
      int is_static_source = clip_is_static_source (clip);

      int clip_frame_no = (frame - clip_start) + clip_get_start (clip);

      frame_recipe = g_strdup_printf ("%s: %s %i %s %ix%i",
          "gedl-pre-4",
          clip_get_path (clip),
          clip->filter_graph || (!is_static_source) ? clip_frame_no : 0,
          clip->filter_graph,
          edl->video_width,
          edl->video_height);

      hash = g_checksum_new (G_CHECKSUM_MD5);
      g_checksum_update (hash, (void*)frame_recipe, -1);
      char *ret = g_strdup (g_checksum_get_string(hash));

      g_checksum_free (hash);
      g_free (frame_recipe);

      return ret;
    }
    clip_start += clip_frames;
  }
  return NULL;
}

/*  calling this causes gedl to rig up its graphs for providing/rendering this frame
 */
int got_cached = 0;

void gedl_set_frame (GeglEDL *edl, int frame)
{
  GList *l;
  int clip_start = 0;
  int use_proxies = edl->use_proxies;

  if ((edl->frame) == frame && (frame != 0))
  {
    fprintf (stderr, "already done!\n");
    return;
  }
  got_cached = 0;

  edl->frame = frame;

  char *frame_hash = gedl_get_frame_hash (edl, frame);
  char *cache_path  = g_strdup_printf ("%s.gedl/cache/%s", edl->parent_path, frame_hash);
  g_free (frame_hash);
  if (g_file_test (cache_path, G_FILE_TEST_IS_REGULAR) &&
      (edl->cache_flags & CACHE_TRY_ALL))
  {
    Clip *clip = NULL;
    gegl_node_set (edl->cache_loader, "path", cache_path, NULL);
    gegl_node_link_many (edl->cache_loader, edl->result, NULL);
    while (!clip)
      clip = edl->clip = edl_get_clip_for_frame (edl, edl->frame);
    if (clip->audio)
      {
        g_object_unref (clip->audio);
        clip->audio = NULL;
      }
    if (!clip->audio)
      clip->audio = gegl_audio_fragment_new (44100, 2, 0, 44100);
    gegl_meta_get_audio (cache_path, clip->audio);
    gegl_node_process (edl->store_buf);
    g_free (cache_path);
    got_cached = 1;
    return;
  }

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int clip_frames = clip_get_frames (clip);

    clip->clip_frame_no = 0;

    clip->abs_start = clip_start;

    if (frame - clip_start < clip_frames)
    {
      int clip_frame_no = (frame - clip_start) + clip_get_start (clip);

      edl->clip = clip;

      remove_in_betweens (edl->nop_raw, edl->nop_transformed);

      gegl_node_set (edl->nop_raw, "operation", "gegl:scale-size-keepaspect",
                                     "y", 0.0, //
                                     "x", 1.0 * edl->width,
                                     "sampler", use_proxies?GEDL_SAMPLER:GEGL_SAMPLER_CUBIC,
                                     NULL);

      gegl_node_link_many (edl->nop_raw, edl->nop_transformed, NULL);

      if (clip->filter_graph)
        {
           GError *error = NULL;
           gegl_create_chain (clip->filter_graph, edl->nop_raw, edl->nop_transformed, clip_frame_no - clip->start, edl->height, NULL, &error);
           if (error)
             {
               /* should set error string */
               fprintf (stderr, "%s\n", error->message);
               g_error_free (error);
             }
         }
      /**********************************************************************/

      clip_set_frame_no (clip, clip_frame_no);
      gegl_node_connect_to (edl->crop, "output", edl->result, "input");

      g_mutex_lock (&clip->mutex);

      if (!clip_is_static_source (clip) || clip->buffer == NULL)
        gegl_node_process (clip->store_buf);
      gegl_node_set (edl->load_buf, "buffer", clip->buffer, NULL);
      gegl_node_process (edl->store_buf);

     clip_fetch_audio (clip);

      /* write cached render of this frame */
      if (!strstr (clip->path, ".gedl/cache") && (!use_proxies))
        {
          gchar *cache_path_final = cache_path;
          gchar *cache_path       = g_strdup_printf ("%s~", cache_path_final);

          if (!g_file_test (cache_path_final, G_FILE_TEST_IS_REGULAR) && !edl->playing)
            {
              GeglNode *save_graph = gegl_node_new ();
              GeglNode *save;
              save = gegl_node_new_child (save_graph,
                          "operation", "gegl:" CACHE_FORMAT "-save",
                          "path", cache_path,
                          NULL);
              if (!strcmp (CACHE_FORMAT, "png"))
              {
                gegl_node_set (save, "bitdepth", 8, NULL);
              }
              gegl_node_link_many (edl->result, save, NULL);
              gegl_node_process (save);
              if (clip->audio)
                gegl_meta_set_audio (cache_path, clip->audio);
              rename (cache_path, cache_path_final);
              g_object_unref (save_graph);
            }
          g_free (cache_path);
        }
      g_mutex_unlock (&clip->mutex);
      g_free (cache_path);

      return;
    }
    clip_start += clip_frames;
  }
  g_free (cache_path);
}

void gedl_set_time (GeglEDL *edl, double seconds)
{
  gedl_set_frame (edl, seconds * edl->fps);
}

void gedl_set_fps (GeglEDL *edl, double fps)
{
  edl->fps = fps;
}
double gedl_get_fps (GeglEDL *edl)
{
  return edl->fps;
}
int    gedl_get_frame (GeglEDL *edl)
{
  return edl->frame;
}
double gedl_get_time (GeglEDL *edl)
{
  return edl->frame / edl->fps;
}
GeglAudioFragment *gedl_get_audio (GeglEDL *edl)
{
  return edl->clip?edl->clip->audio:NULL;
}
GeglBuffer *gedl_get_buffer (GeglEDL *edl)
{
  return edl->clip?edl->clip->buffer:NULL;
}

void gedl_get_video_info (const char *path, int *duration, double *fps)
{
  GeglNode *gegl = gegl_node_new ();
  GeglNode *probe = gegl_node_new_child (gegl, "operation",
                          "gegl:ff-load", "path", path, NULL);
  gegl_node_process (probe);

  if (duration)
  gegl_node_get (probe, "frames", duration, NULL);
  if (fps)
  gegl_node_get (probe, "frame-rate", fps, NULL);
  g_object_unref (gegl);
}

int    gedl_get_duration (GeglEDL *edl)
{
  int count = 0;
  GList *l;
  for (l = edl->clips; l; l = l->next)
  {
    ((Clip*)(l->data))->abs_start = count;
    count += clip_get_frames (l->data);
  }
  return count;
}
#include <string.h>

void gedl_parse_clip (GeglEDL *edl, const char *line)
{
  int start = 0; int end = 0; int duration = 0;
  const char *rest = NULL;
  char path[1024];
  if (line[0] == '#' ||
      line[1] == '#' ||
      strlen (line) < 4)
    return;

  if (strstr (line, "--"))
    rest = strstr (line, "--") + 2;

  if (rest) while (*rest == ' ')rest++;

  sscanf (line, "%s %i %i %i", path, &start, &end, &duration);
  if (strlen (path) > 3)
    {
      SourceClip *sclip = g_new0 (SourceClip, 1);
      edl->clip_db = g_list_append (edl->clip_db, sclip);
      if (strstr (line, "[active]"))
       {
         edl->active_source = sclip;
       }
      sclip->path = g_strdup (path);
      sclip->start = start;
      sclip->end = end;
      sclip->duration = duration;
      if (rest)
        sclip->title = g_strdup (rest);
    }
  /* todo: probe video file for length if any of arguments are not present as
           integers.. alloving full clips and clips with mm:ss.nn timestamps,
   */
}

void gedl_parse_line (GeglEDL *edl, const char *line)
{
  int start = 0; int end = 0;
  const char *rest = NULL;
  char path[1024];
  if (line[0] == '#' ||
      line[1] == '#' ||
      strlen (line) < 4)
    return;

  if (strchr (line, '=') && !strstr(line, "--"))
   {
     char *key = g_strdup (line);
     char *value = strchr (key, '=') + 1;
     value[-1]='\0';

     while (value[strlen(value)-1]==' ' ||
            value[strlen(value)-1]=='\n')
            value[strlen(value)-1]='\0';
     if (!strcmp (key, "fade-duration"))     edl->fade_duration = g_strtod (value, NULL);
     if (!strcmp (key, "fps"))               gedl_set_fps (edl, g_strtod (value, NULL));
     if (!strcmp (key, "framedrop"))         edl->framedrop     = g_strtod (value, NULL);
     if (!strcmp (key, "output-path"))       edl->output_path = g_strdup (value);
     if (!strcmp (key, "video-codec"))       edl->video_codec = g_strdup (value);
     if (!strcmp (key, "audio-codec"))       edl->audio_codec = g_strdup (value);
     if (!strcmp (key, "audio-sample-rate")) edl->audio_samplerate = g_strtod (value, NULL);
     if (!strcmp (key, "video-bufsize"))     edl->video_bufsize = g_strtod (value, NULL);
     if (!strcmp (key, "video-bitrate"))     edl->video_bitrate = g_strtod (value, NULL);
     if (!strcmp (key, "audio-bitrate"))     edl->audio_bitrate = g_strtod (value, NULL);
     if (!strcmp (key, "video-width"))       edl->video_width = g_strtod (value, NULL);
     if (!strcmp (key, "video-height"))      edl->video_height = g_strtod (value, NULL);
     if (!strcmp (key, "proxy-width"))       edl->proxy_width = g_strtod (value, NULL);
     if (!strcmp (key, "proxy-height"))      edl->proxy_height = g_strtod (value, NULL);
     if (!strcmp (key, "frame-start"))       edl->range_start = g_strtod (value, NULL);
     if (!strcmp (key, "frame-end"))         edl->range_end = g_strtod (value, NULL);
     if (!strcmp (key, "selection-start"))   edl->selection_start = g_strtod (value, NULL);
     if (!strcmp (key, "selection-end"))     edl->selection_end = g_strtod (value, NULL);
     if (!strcmp (key, "range-start"))       edl->range_start = g_strtod (value, NULL);
     if (!strcmp (key, "range-end"))         edl->range_end = g_strtod (value, NULL);
     if (!strcmp (key, "frame-no"))          edl->frame_no = g_strtod (value, NULL);
     if (!strcmp (key, "frame-scale"))       edl->scale = g_strtod (value, NULL);
     if (!strcmp (key, "t0"))                edl->t0 = g_strtod (value, NULL);

     g_free (key);
     return;
   }
  if (strstr (line, "--"))
    rest = strstr (line, "--") + 2;
  if (rest) while (*rest == ' ')rest++;

  sscanf (line, "%s %i %i", path, &start, &end);
  if (strlen (path) > 3)
    {
      Clip *clip = NULL;
      int ff_probe = 0;


     clip = clip_new_full (edl, path, start, end);
     if (!clip_is_static_source (clip) &&
         (start == 0 && end == 0))
       ff_probe = 1;
     edl->clips = g_list_append (edl->clips, clip);
     if (strstr (line, "[fade]"))
       {
         clip->fade_out = TRUE;
         ff_probe = 1;
       }
     if (strstr (line, "[active]"))
       {
         edl->active_clip = clip;
       }
     {
       GList *self = g_list_find (edl->clips, clip);
       GList *prev = self?self->prev: NULL;
       Clip *prev_clip = prev?prev->data:NULL;
       if (prev_clip && prev_clip->fade_out)
       {
         clip->fade_in = 1;
         ff_probe = 1;
       }
       else
       {
         clip->fade_in = 0;
       }
     }

     if (clip == edl->clips->data)
     {
       ff_probe = 1;
     }

     if (ff_probe && !clip_is_static_source (clip))
       {
         gedl_get_video_info (clip->path, &clip->duration, &clip->fps);

         if (edl->fps == 0.0)
         {
           gedl_set_fps (edl, clip->fps);
         }
       }

     if (rest)
       {
         clip->filter_graph = g_strdup (rest);
         while (clip->filter_graph[strlen(clip->filter_graph)-1]==' ' ||
                clip->filter_graph[strlen(clip->filter_graph)-1]=='\n')
                clip->filter_graph[strlen(clip->filter_graph)-1]='\0';
       }

     if (clip->end == 0)
     {
        clip->end = clip->duration;
     }
    }
  /* todo: probe video file for length if any of arguments are nont present as 
           integers.. alloving full clips and clips with mm:ss.nn timestamps,
   */
}

#include <string.h>

void gedl_update_video_size (GeglEDL *edl);
GeglEDL *gedl_new_from_string (const char *string, const char *parent_path);
GeglEDL *gedl_new_from_string (const char *string, const char *parent_path)
{
  GString *line = g_string_new ("");
  GeglEDL *edl = gedl_new ();
  int clips_done = 0;
  int newlines = 0;
  edl->parent_path = g_strdup (parent_path);

  for (const char *p = string; p==string || *(p-1); p++)
  {
    switch (*p)
    {
      case 0:
      case '\n':
       if (clips_done)
       {
         if (line->len > 2)
           gedl_parse_clip (edl, line->str);
         g_string_assign (line, "");
       }
       else
       {
         if (line->str[0] == '-' &&
             line->str[1] == '-' &&
             line->str[2] == '-')
         {
           clips_done = 1;
           g_string_assign (line, "");
         }
         else
         {
           if (*p == 0)
           {
             newlines = 2;
           }
           else if (*p == '\n')
           {
             newlines ++;
           }
           else
           {
             newlines = 0;
           }
           if (strchr (line->str, '='))
             newlines = 3;

           if (newlines >= 2)
           {
             gedl_parse_line (edl, line->str);
             g_string_assign (line, "");
           }
           else
             g_string_append_c (line, *p);
         }
       }
       break;
      default: g_string_append_c (line, *p);
       break;
    }
  }
  g_string_free (line, TRUE);

  gedl_update_video_size (edl);
  gedl_set_use_proxies (edl, edl->use_proxies);

  return edl;
}

void gedl_save_path (GeglEDL *edl, const char *path)
{
  char *serialized;
  serialized = gedl_serialise (edl);

  if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
  {
     char backup_path[4096];
     struct tm *tim;
     char *old_contents = NULL;

     g_file_get_contents (path, &old_contents, NULL, NULL);
     if (old_contents)
     {
       char *oc, *s;

       oc = strstr (old_contents, "\n\n");
       s  = strstr (serialized, "\n\n");
       if (oc && s && !strcmp (oc, s))
       {
         g_free (old_contents);
         return;
       }
       g_free (old_contents);
     }

     sprintf (backup_path, "%s.gedl/history/%s-", edl->parent_path, basename(edl->path));

     time_t now = time(NULL);
     tim = gmtime(&now);

     strftime(backup_path + strlen(backup_path), sizeof(backup_path)-strlen(backup_path), "%Y%m%d_%H%M%S", tim);
     rename (path, backup_path);
  }

  FILE *file = fopen (path, "w");
  if (!file)
  {
    g_free (serialized);
    return;
  }

  if (serialized)
  {
    fprintf (file, "%s", serialized);
    g_free (serialized);
  }
  fclose (file);
}

void gedl_update_video_size (GeglEDL *edl)
{
  if ((edl->video_width == 0 || edl->video_height == 0) && edl->clips)
    {
      Clip *clip = edl->clips->data;
      GeglNode *gegl = gegl_node_new ();
      GeglRectangle rect;
      // XXX: is ff-load good for pngs and jpgs as well?
      GeglNode *probe;
      probe = gegl_node_new_child (gegl, "operation", "gegl:ff-load", "path", clip->path, NULL);
      gegl_node_process (probe);
      rect = gegl_node_get_bounding_box (probe);
      edl->video_width = rect.width;
      edl->video_height = rect.height;
      g_object_unref (gegl);
    }
  if ((edl->proxy_width <= 0) && edl->video_width)
  {
    edl->proxy_width = 320;
  }
  if ((edl->proxy_height <= 0) && edl->video_width)
  {
    edl->proxy_height = edl->proxy_width * (1.0 * edl->video_height / edl->video_width);
  }
}

static void generate_gedl_dir (GeglEDL *edl)
{
  char *tmp = g_strdup_printf ("cd %s; mkdir .gedl 2>/dev/null ; mkdir .gedl/cache 2>/dev/null mkdir .gedl/proxy 2>/dev/null mkdir .gedl/thumb 2>/dev/null ; mkdir .gedl/video 2>/dev/null; mkdir .gedl/history 2>/dev/null", edl->parent_path);
  system (tmp);
  g_free (tmp);
}

static GTimer *  timer            = NULL;
static guint     timeout_id       = 0;
static gdouble   throttle         = 4.0;

void gedl_reread (GeglEDL *edl)
{
  GeglEDL *new_edl = gedl_new_from_path (edl->path);
  GList *l;

  /* swap clips */
  l = edl->clips;
  edl->clips = new_edl->clips;
  new_edl->clips = l;
  edl->active_clip = NULL; // XXX: better to resolve?

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    clip->edl = edl;
  }

  for (l = new_edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    clip->edl = new_edl;
  }

  gedl_free (new_edl);
}

static gboolean timeout (gpointer user_data)
{
  GeglEDL *edl = user_data;
  gedl_reread (edl);
  // system (user_data);
  g_timer_start (timer);
  timeout_id = 0;
  return FALSE;
}


static void file_changed (GFileMonitor     *monitor,
                          GFile            *file,
                          GFile            *other_file,
                          GFileMonitorEvent event_type,
                          GeglEDL          *edl)
{
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED)
    {
          if (!timeout_id)
            {
              gdouble elapsed = g_timer_elapsed (timer, NULL);
              gdouble wait    = throttle - elapsed;

              if (wait <= 0.0)
                wait = 0.0;

              timeout_id = g_timeout_add (wait * 1000, timeout, edl);
            }
    }
}

void
gedl_monitor_start (GeglEDL *edl)
{
  if (!edl->path)
    return;
  /* save to make sure file exists */
  gedl_save_path (edl, edl->path);
  /* start monitor */
  timer = g_timer_new ();
  edl->monitor = g_file_monitor_file (g_file_new_for_path (edl->path),
                                      G_FILE_MONITOR_NONE,
                                      NULL, NULL);
  g_signal_connect (edl->monitor, "changed", G_CALLBACK (file_changed), edl);
}

GeglEDL *gedl_new_from_path (const char *path)
{
  GeglEDL *edl = NULL;
  gchar *string = NULL;

  g_file_get_contents (path, &string, NULL, NULL);
  if (string)
  {
    char *rpath = realpath (path, NULL);
    char *parent = g_strdup (rpath);
    strrchr(parent, '/')[1]='\0';

      edl = gedl_new_from_string (string, parent);

    g_free (parent);
    g_free (string);
    if (!edl->path)
      edl->path = g_strdup (realpath (path, NULL)); // XXX: leak
  }
  else
  {
    char *parent = NULL;
    if (path[0] == '/')
    {
      parent = strdup (path);
      strrchr(parent, '/')[1]='\0';
    }
    else
    {
      parent = g_malloc0 (PATH_MAX);
      getcwd (parent, PATH_MAX);
    }
    edl = gedl_new_from_string ("", parent);
    if (!edl->path)
    {
      if (path[0] == '/')
      {
        edl->path = g_strdup (path);
      }
      else
      {
        edl->path = g_strdup_printf ("%s/%s", parent, basename (path));
      }
    }
    g_free (parent);
  }
  generate_gedl_dir (edl);

  return edl;
}
const char *gedl_get_clip_path (GeglEDL *edl)
{
  return edl->clip?edl->clip->clip_path:"";
}
int gedl_get_clip_frame_no    (GeglEDL *edl)
{
  return edl->clip?edl->clip->clip_frame_no:0;
}
static void update_size (GeglEDL *edl)
{
  if (!edl->crop)
    return;
  gegl_node_set (edl->crop, "width", 1.0 * edl->width,
                            "height", 1.0 * edl->height,
                            NULL);
  gegl_node_set (edl->crop2, "width", 1.0 * edl->width,
                             "height", 1.0 * edl->height,
                             NULL);
  gegl_node_set (edl->nop_raw, "x", 1.0 * edl->width,
                             "y", 0.0,
                             NULL);
  gegl_node_set (edl->scale_size2, "x", 1.0 * edl->width,
                             "y", 1.0 * edl->height,
                             NULL);
}
static void setup (GeglEDL *edl)
{
  edl->result = gegl_node_new_child    (edl->gegl, "operation", "gegl:nop", NULL);
  edl->load_buf = gegl_node_new_child  (edl->gegl, "operation", "gegl:buffer-source", NULL);
  edl->load_buf2 = gegl_node_new_child (edl->gegl, "operation", "gegl:buffer-source", NULL);
  edl->crop = gegl_node_new_child      (edl->gegl, "operation", "gegl:crop", "x", 0.0, "y", 0.0, "width", 1.0 * edl->width,
                                         "height", 1.0 * edl->height, NULL);
  edl->crop2 = gegl_node_new_child     (edl->gegl, "operation", "gegl:crop", "x", 0.0, "y", 0.0, "width", 1.0 * edl->width,
                                         "height", 1.0 * edl->height, NULL);
  edl->over = gegl_node_new_child      (edl->gegl, "operation", "gegl:over", NULL);
  edl->nop_raw = gegl_node_new_child (edl->gegl, "operation", "gegl:scale-size-keepaspect",
                                          "y", 0.0, //
                                          "x", 1.0 * edl->width,
                                          "sampler", GEDL_SAMPLER,
                                          NULL);

  edl->nop_raw2 = gegl_node_new_child  (edl->gegl, "operation", "gegl:nop", NULL);
  edl->nop_transformed = gegl_node_new_child (edl->gegl, "operation", "gegl:nop", NULL);
  edl->nop_transformed2 = gegl_node_new_child (edl->gegl, "operation", "gegl:nop", NULL);
  edl->opacity = gegl_node_new_child (edl->gegl, "operation", "gegl:opacity", NULL);
  edl->scale_size = gegl_node_new_child (edl->gegl, "operation", "gegl:nop", NULL);
  /*"operation", "gegl:scale-size-keepaspect",
                                          "y", 0.0, //
                                          "x", 1.0 * edl->width, NULL);*/
  edl->scale_size2 = gegl_node_new_child (edl->gegl, "operation", "gegl:scale-size-keepaspect",
                                    "x", 1.0 * edl->width,
                                    "y", 1.0 * edl->height,
                                    "sampler", GEDL_SAMPLER,
                                    NULL);
  edl->encode = gegl_node_new_child (edl->gegl, "operation", "gegl:ff-save",
                                      "path",           edl->output_path,
                                      "frame-rate",     gedl_get_fps (edl),
                                      "video-bit-rate", edl->video_bitrate,
                                      "video-bufsize",  edl->video_bufsize,
                                      "audio-bit-rate", edl->audio_bitrate,
                                      "audio-codec",    edl->audio_codec,
                                      "video-codec",    edl->video_codec,
                                      NULL);
  edl->cached_result = gegl_node_new_child (edl->gegl, "operation", "gegl:buffer-source", "buffer", edl->buffer, NULL);
  edl->store_buf = gegl_node_new_child (edl->gegl, "operation", "gegl:write-buffer", "buffer", edl->buffer, NULL);
  edl->source_store_buf = gegl_node_new_child (edl->gegl, "operation", "gegl:write-buffer", "buffer", edl->buffer, NULL);

  gegl_node_link_many (edl->result, edl->encode, NULL);
  gegl_node_link_many (edl->load_buf, edl->scale_size, edl->nop_raw, edl->nop_transformed, edl->crop, NULL);
  gegl_node_link_many (edl->load_buf2, edl->scale_size2, edl->nop_raw2, edl->nop_transformed2, edl->opacity, edl->crop2,  NULL);
  gegl_node_connect_to (edl->result, "output", edl->store_buf, "input");
  gegl_node_connect_to (edl->nop_raw, "output", edl->nop_transformed, "input");
  gegl_node_connect_to (edl->nop_raw2, "output", edl->nop_transformed2, "input");
  gegl_node_connect_to (edl->crop2, "output", edl->over, "aux");
  gegl_node_connect_to (edl->crop, "output", edl->over, "input");
  gegl_node_connect_to (edl->over, "output", edl->result, "input");
  update_size (edl);
}
void rig_frame (GeglEDL *edl, int frame_no)
{
  if (edl->frame == frame_no)
    return;
  gedl_set_frame (edl, frame_no);

  if (do_encode)
    gegl_node_set (edl->encode, "audio", gedl_get_audio (edl), NULL);
}

static void teardown (void)
{
  gedl_free (edl);
}
static void init (int argc, char **argv)
{
  gegl_init (&argc, &argv);
  g_object_set (gegl_config (),
                "application-license", "GPL3",
                NULL);
}

static void process_frames (GeglEDL *edl)
{
  int frame_no;
  for (frame_no = edl->range_start; frame_no <= edl->range_end; frame_no++)
  {
    edl->frame_no = frame_no;
    rig_frame (edl, edl->frame_no);

    fprintf (stdout, "\r%1.2f%% %04d / %04d   ",
     100.0 * (frame_no-edl->range_start) * 1.0 / (edl->range_end - edl->range_start),
     frame_no, edl->range_end);

    if (do_encode)
    {
      gegl_node_process (edl->encode);
    }
    fflush (0);
  }
  fprintf (stdout, "\n");
}

void nop_handler(int sig)
{
}

static int stop_cacher = 0;

void handler1(int sig)
{
  stop_cacher = 1;
}

static int cacheno = 0;
static int cachecount = 2;

static inline int this_cacher (int frame_no)
{
  if (frame_no % cachecount == cacheno) return 1;
  return 0;
}

static void process_frames_cache (GeglEDL *edl)
{
  int frame_no = edl->frame_no;
  int frame_start = edl->frame_no;
  int duration;
  signal(SIGUSR2, handler1);
  duration = gedl_get_duration (edl);

  /* XXX: should probably do first frame of each clip - since
          these are used for quick keyboard navigation of the
          project
   */
  GList *l;
  int clip_start = 0;

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int clip_frames = clip_get_frames (clip);
    edl->frame_no = clip_start;
    if (this_cacher (edl->frame_no))
    {
      rig_frame (edl, edl->frame_no);
    }

    clip_start += clip_frames;
    if (stop_cacher)
      return;
  }

  for (frame_no = frame_start - 3; frame_no < duration; frame_no++)
  {
    edl->frame_no = frame_no;
    if (this_cacher (edl->frame_no))
      rig_frame (edl, edl->frame_no);
    if (stop_cacher)
      return;
  }
  for (frame_no = 0; frame_no < frame_start; frame_no++)
  {
    edl->frame_no = frame_no;
    if (this_cacher (edl->frame_no))
      rig_frame (edl, edl->frame_no);
    if (stop_cacher)
      return;
  }
}

static inline void set_bit (guchar *bitmap, int no)
{
  bitmap[no/8] |= (1 << (no % 8));
}

guchar *gedl_get_cache_bitmap (GeglEDL *edl, int *length_ret)
{
  int duration = gedl_get_duration (edl);
  int frame_no;
  int length = (duration / 8) + 1;
  guchar *ret = g_malloc0 (length);

  if (length_ret)
    *length_ret = length;

  for (frame_no = 0; frame_no < duration; frame_no++)
  {
    const gchar *hash = gedl_get_frame_hash (edl, frame_no);
    gchar *path = g_strdup_printf ("%s.gedl/cache/%s", edl->parent_path, hash);
    if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
      set_bit (ret, frame_no);
    g_free (path);
  }

  return ret;
}

static void process_frames_cache_stat (GeglEDL *edl)
{
  int frame_no = edl->frame_no;
  int duration;
  signal(SIGUSR2, handler1);
  duration = gedl_get_duration (edl);

  /* XXX: should probably do first frame of each clip - since
          these are used for quick keyboard navigation of the
          project
   */

  for (frame_no = 0; frame_no < duration; frame_no++)
  {
    const gchar *hash = gedl_get_frame_hash (edl, frame_no);
    gchar *path = g_strdup_printf ("%s.gedl/cache/%s", edl->parent_path, hash);
    if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
      fprintf (stdout, "%i ", frame_no);
    //  fprintf (stdout, ". %i %s\n", frame_no, hash);
   // else
    //  fprintf (stdout, "  %i %s\n", frame_no, hash);
    g_free (path);
  }
}

int gegl_make_thumb_image (GeglEDL *edl, const char *path, const char *icon_path)
{
  GString *str = g_string_new ("");

  g_string_assign (str, "");
  g_string_append_printf (str, "%s iconographer -p -h -f 'mid-col 96 audio' %s -a %s",
  //g_string_append_printf (str, "iconographer -p -h -f 'thumb 96' %s -a %s",
                          gedl_binary_path, path, icon_path);
  system (str->str);

  g_string_free (str, TRUE);

  return 0;
}

int gegl_make_thumb_video (GeglEDL *edl, const char *path, const char *thumb_path)
{
  GString *str = g_string_new ("");

  g_string_assign (str, "");
  g_string_append_printf (str, "ffmpeg -y -i %s -vf scale=%ix%i %s", path, edl->proxy_width, edl->proxy_height, thumb_path);
  system (str->str);
  g_string_free (str, TRUE);

  return 0;
#if 0  // much slower and worse for fps/audio than ffmpeg method for creating thumbs
  int tot_frames; //
  g_string_append_printf (str, "video-bitrate=100\n\noutput-path=%s\nvideo-width=256\nvideo-height=144\n\n%s\n", thumb_path, path);
  edl = gedl_new_from_string (str->str);
  setup (edl);
  tot_frames = gedl_get_duration (edl);

  if (edl->range_end == 0)
    edl->range_end = tot_frames-1;
  process_frames (edl);
  teardown ();
  g_string_free (str, TRUE);
  return 0;
#endif
}


int gedl_ui_main (GeglEDL *edl);

int gegl_make_thumb_video (GeglEDL *edl, const char *path, const char *thumb_path);
void gedl_make_proxies (GeglEDL *edl)
{
  GList *l;
  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    char *proxy_path = gedl_make_proxy_path (edl, clip->path);
    char *thumb_path = gedl_make_thumb_path (edl, clip->path);
    if (!g_file_test (proxy_path, G_FILE_TEST_IS_REGULAR))
       gegl_make_thumb_video (edl, clip->path, proxy_path);
    if (!g_file_test (thumb_path, G_FILE_TEST_IS_REGULAR))
       gegl_make_thumb_image(edl, proxy_path, thumb_path);
    g_free (proxy_path);
    g_free (thumb_path);
  }
}

gint iconographer_main (gint    argc, gchar **argv);

int main (int argc, char **argv)
{
  const char *edl_path = "input.edl";
  int tot_frames;

  gedl_binary_path = realpath (argv[0], NULL);

  if (argv[1] && !strcmp (argv[1], "iconographer"))
  {
    argv[1] = argv[0];
    return iconographer_main (argc-1, argv + 1);
  }

  setenv ("GEGL_USE_OPENCL", "no", 1);
  setenv ("GEGL_MIPMAP_RENDERING", "1", 1);

  init (argc, argv);
  if (!argv[1])
  {
    fprintf (stderr, "usage: %s <project.edl>\n", argv[0]);
    return -1;
  }

  edl_path = argv[1]; //realpath (argv[1], NULL);

  if (g_str_has_suffix (edl_path, ".mp4") ||
      g_str_has_suffix (edl_path, ".ogv") ||
      g_str_has_suffix (edl_path, ".mkv") ||
      g_str_has_suffix (edl_path, ".MKV") ||
      g_str_has_suffix (edl_path, ".avi") ||
      g_str_has_suffix (edl_path, ".MP4") ||
      g_str_has_suffix (edl_path, ".OGV") ||
      g_str_has_suffix (edl_path, ".AVI"))
  {
    char str[1024];
    int duration;
    double fps;
    GeglNode *gegl = gegl_node_new ();
    GeglNode *probe = gegl_node_new_child (gegl, "operation",
                                           "gegl:ff-load", "path", edl_path,
                                           NULL);
    gegl_node_process (probe);

    gegl_node_get (probe, "frames", &duration, NULL);
    gegl_node_get (probe, "frame-rate", &fps, NULL);
    g_object_unref (gegl);

    sprintf (str, "%s 0 %i\n", edl_path, duration);
    {
      char * path = realpath (edl_path, NULL); 
      char * rpath = g_strdup_printf ("%s.edl", path);
      char * parent = g_strdup (rpath);
      strrchr(parent, '/')[1]='\0';
      edl = gedl_new_from_string (str, parent);
      g_free (parent);
      edl->path = rpath;
      free (path);
    }
    generate_gedl_dir (edl);
  }
  else
  {
    edl = gedl_new_from_path (edl_path);
  }

  chdir (edl->parent_path); /* we try as good as we can to deal with absolute
                               paths correctly,  */
/*
  if (argv[2] && argv[2][0]!='-')
    edl->output_path = argv[2];
    */
  setup (edl);

  {
#define RUNMODE_UI     0
#define RUNMODE_RENDER 1
#define RUNMODE_CACHE  2
#define RUNMODE_CACHE_STAT  3
    int runmode = RUNMODE_UI;
    for (int i = 0; argv[i]; i++)
    {
      if (!strcmp (argv[i], "render")) runmode = RUNMODE_RENDER;
      if (!strcmp (argv[i], "cachestat")) runmode = RUNMODE_CACHE_STAT;
      if (!strcmp (argv[i], "cache"))
      {
        runmode = RUNMODE_CACHE;
        if (argv[i+1])
        {
          cacheno = atoi (argv[i+1]);
          if (argv[i+2])
            cachecount = atoi (argv[i+2]);
        }
      }
    }

    switch (runmode)
    {
      case RUNMODE_UI:

        signal(SIGUSR2, nop_handler);

        gedl_monitor_start (edl);

        return gedl_ui_main (edl);
      case RUNMODE_RENDER:
        tot_frames  = gedl_get_duration (edl);
        if (edl->range_end == 0)
          edl->range_end = tot_frames-1;
        process_frames (edl);
        teardown ();
        return 0;
      case RUNMODE_CACHE:
        do_encode  = 0;
        tot_frames  = gedl_get_duration (edl);
        if (edl->range_end == 0)
          edl->range_end = tot_frames-1;
        process_frames_cache (edl);
        teardown ();
        return 0;
      case RUNMODE_CACHE_STAT:
        do_encode  = 0;
        process_frames_cache_stat (edl);
        teardown ();
        return 0;
     }
  }
  return -1;
}

char *gedl_serialise (GeglEDL *edl)
{
  GList *l;
  char *ret;
  GString *ser = g_string_new ("");


  if (edl->proxy_width != DEFAULT_proxy_width)
    g_string_append_printf (ser, "proxy-width=%i\n",  edl->proxy_width);
  if (edl->proxy_height != DEFAULT_proxy_height)
    g_string_append_printf (ser, "proxy-height=%i\n", edl->proxy_height);
  if (edl->framedrop != DEFAULT_framedrop)
    g_string_append_printf (ser, "framedrop=%i\n", edl->framedrop);

  if (strcmp(edl->output_path, DEFAULT_output_path))
    g_string_append_printf (ser, "output-path=%s\n", edl->output_path);
  if (strcmp(edl->video_codec, DEFAULT_video_codec))
    g_string_append_printf (ser, "video-codec=%s\n", edl->video_codec);
  if (strcmp(edl->audio_codec, DEFAULT_audio_codec))
    g_string_append_printf (ser, "audio-codec=%s\n", edl->audio_codec);
  if (edl->video_width != DEFAULT_video_width)
    g_string_append_printf (ser, "video-width=%i\n",  edl->video_width);
  if (edl->video_height != DEFAULT_video_height)
    g_string_append_printf (ser, "video-height=%i\n", edl->video_height);
  if (edl->video_bufsize != DEFAULT_video_bufsize)
    g_string_append_printf (ser, "video-bufsize=%i\n", edl->video_bufsize);
  if (edl->video_bitrate != DEFAULT_video_bitrate)
    g_string_append_printf (ser, "video-bitrate=%i\n",  edl->video_bitrate);
  if (edl->video_tolerance != DEFAULT_video_tolerance)
    g_string_append_printf (ser, "video-tolerance=%i\n", edl->video_tolerance);
  if (edl->audio_bitrate != DEFAULT_audio_bitrate)
    g_string_append_printf (ser, "audio-bitrate=%i\n",  edl->audio_bitrate);
  if (edl->audio_samplerate != DEFAULT_audio_samplerate)
    g_string_append_printf (ser, "audio-samplerate=%i\n",  edl->audio_samplerate);
  if (edl->fade_duration != DEFAULT_fade_duration)
    g_string_append_printf (ser, "fade-duration=%i\n",  edl->fade_duration);

  g_string_append_printf (ser, "fps=%f\n", gedl_get_fps (edl));

  if (edl->range_start != DEFAULT_range_start)
    g_string_append_printf (ser, "range-start=%i\n",  edl->range_start);
  if (edl->range_end != DEFAULT_range_end)
    g_string_append_printf (ser, "range-end=%i\n", edl->range_end);

  if (edl->selection_start != DEFAULT_selection_start)
    g_string_append_printf (ser, "selection-start=%i\n",  edl->selection_start);
  if (edl->selection_end != DEFAULT_selection_end)
    g_string_append_printf (ser, "selection-end=%i\n",  edl->selection_end);
  if (edl->scale != 1.0)
    g_string_append_printf (ser, "frame-scale=%f\n", edl->scale);
  if (edl->t0 != 1.0)
    g_string_append_printf (ser, "t0=%f\n", edl->t0);

  g_string_append_printf (ser, "frame-no=%i\n", edl->frame_no);
  g_string_append_printf (ser, "\n");

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    gchar *path = clip->path;
    if (!strncmp (path, edl->parent_path, strlen(edl->parent_path)))
      path += strlen (edl->parent_path);
    g_string_append_printf (ser, "%s %d %d%s%s%s%s%s\n", path, clip->start, clip->end,
        clip->fade_out?" [fade]":"",
        "", //(edl->active_clip == clip)?" [active]":"",
        clip->filter_graph?" -- ":"",clip->filter_graph?clip->filter_graph:"",
        clip->filter_graph?"\n":"\n");
  }
  g_string_append_printf (ser, "-----\n");
  for (l = edl->clip_db; l; l = l->next)
  {
    SourceClip *clip = l->data;
    g_string_append_printf (ser, "%s %d %d %d%s%s%s\n", clip->path, clip->start, clip->end, clip->duration,
        "", //(edl->active_source == clip)?" [active]":"",
        clip->title?" -- ":"",clip->title?clip->title:"");
  }
  ret=ser->str;
  g_string_free (ser, FALSE);
  return ret;
}

void
gegl_meta_set_audio (const char        *path,
                     GeglAudioFragment *audio)
{
  GError *error = NULL;
  GExiv2Metadata *e2m = gexiv2_metadata_new ();
  gexiv2_metadata_open_path (e2m, path, &error);
  if (error)
  {
    g_warning ("%s", error->message);
  }
  else
  {
    int i, c;
    GString *str = g_string_new ("");
    int sample_count = gegl_audio_fragment_get_sample_count (audio);
    int channels = gegl_audio_fragment_get_channels (audio);
    if (gexiv2_metadata_has_tag (e2m, "Xmp.xmp.GEGL"))
      gexiv2_metadata_clear_tag (e2m, "Xmp.xmp.GEGL");

    g_string_append_printf (str, "%i %i %i %i",
                              gegl_audio_fragment_get_sample_rate (audio),
                              gegl_audio_fragment_get_channels (audio),
                              gegl_audio_fragment_get_channel_layout (audio),
                              gegl_audio_fragment_get_sample_count (audio));

    for (i = 0; i < sample_count; i++)
      for (c = 0; c < channels; c++)
        g_string_append_printf (str, " %0.5f", audio->data[c][i]);

    gexiv2_metadata_set_tag_string (e2m, "Xmp.xmp.GeglAudio", str->str);
    gexiv2_metadata_save_file (e2m, path, &error);
    if (error)
      g_warning ("%s", error->message);
    g_string_free (str, TRUE);
  }
  g_object_unref (e2m);
}

void
gegl_meta_get_audio (const char        *path,
                     GeglAudioFragment *audio)
{
  GError *error = NULL;
  GExiv2Metadata *e2m = gexiv2_metadata_new ();
  gexiv2_metadata_open_path (e2m, path, &error);
  if (!error)
  {
    GString *word = g_string_new ("");
    gchar *p;
    gchar *ret = gexiv2_metadata_get_tag_string (e2m, "Xmp.xmp.GeglAudio");
    int element_no = 0;
    int channels = 2;
    int max_samples = 2000;

    if (ret)
    for (p = ret; p==ret || p[-1] != '\0'; p++)
    {
      switch (p[0])
      {
        case '\0':case ' ':
        if (word->len > 0)
         {
           switch (element_no++)
           {
             case 0:
               gegl_audio_fragment_set_sample_rate (audio, g_strtod (word->str, NULL));
               break;
             case 1:
               channels = g_strtod (word->str, NULL);
               gegl_audio_fragment_set_channels (audio, channels);
               break;
             case 2:
               gegl_audio_fragment_set_channel_layout (audio, g_strtod (word->str, NULL));
               break;
             case 3:
               gegl_audio_fragment_set_sample_count (audio, g_strtod (word->str, NULL));
               break;
             default:
               {
                  int sample_no = element_no - 4;
                  int channel_no = sample_no % channels;
                  sample_no/=2;
                  if (sample_no < max_samples)
                  audio->data[channel_no][sample_no] = g_strtod (word->str, NULL);
               }
               break;
           }
         }
        g_string_assign (word, "");
        break;
        default:
        g_string_append_c (word, p[0]);
        break;
      }
    }
    g_string_free (word, TRUE);
    g_free (ret);
  }
  else
    g_warning ("%s", error->message);
  g_object_unref (e2m);
}

void gedl_set_selection (GeglEDL *edl, int start_frame, int end_frame)
{
  edl->selection_start = start_frame;
  edl->selection_end   = end_frame;
}

void gedl_get_selection (GeglEDL *edl,
                         int     *start_frame,
                         int     *end_frame)
{
  if (start_frame)
    *start_frame = edl->selection_start;
  if (end_frame)
    *end_frame = edl->selection_end;
}

void gedl_set_range (GeglEDL *edl, int start_frame, int end_frame)
{
  edl->range_start = start_frame;
  edl->range_end   = end_frame;
}

void gedl_get_range (GeglEDL *edl,
                     int     *start_frame,
                     int     *end_frame)
{
  if (start_frame)
    *start_frame = edl->range_start;
  if (end_frame)
    *end_frame = edl->range_end;
}


Clip * edl_get_clip_for_frame (GeglEDL *edl, int frame)
{
  GList *l;
  int t = 0;
  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    if (frame >= t && frame < t + clip_get_frames (clip))
    {
      return clip;
    }
    t += clip_get_frames (clip);
  }
  return NULL;
}
