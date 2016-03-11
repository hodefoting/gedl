#include <string.h>
#include <stdio.h>
#include <gegl.h>
#include <gexiv2/gexiv2.h>
#include <gegl-audio-fragment.h>

#if 0
  1 - video with audio fragment output (without, already kind of works with ff-save and frame-%06d.png patterns..
  3 - caches / proxies

for the main timeline have:

rendered results as full size JPGs
chunks of 320x240 encoded mp4 video files of 5s that contains render of video, from proxy
chunks of 320x240 encoded mp4 video files of 5s that contains render of video, from full size jpgs

for each video have:
  iconographer slice
  small 320x240 encoded mp4 video of all videos
  image full size jpg with audio extract
  160x120 size jpg with audio extract

#endif
/* GEGL edit decision list - a digital video cutter and splicer */
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

Clip *clip_new (void)
{
  Clip *clip = g_malloc0 (sizeof (Clip));
  clip->gegl = gegl_node_new ();
  clip->loader = gegl_node_new_child (clip->gegl, "operation", "gegl:ff-load", NULL);
  clip->store_buf = gegl_node_new_child (clip->gegl, "operation", "gegl:buffer-sink", "buffer", &clip->buffer, NULL);
  gegl_node_link_many (clip->loader, clip->store_buf, NULL);
  g_mutex_init (&clip->mutex);

  return clip;
}
void clip_free (Clip *clip)
{
  if (clip->path)
    g_free (clip->path);
  clip->path = NULL;

  if (clip->buffer)
    g_object_unref (clip->buffer);
  clip->buffer = NULL;

  if (clip->gegl)
    g_object_unref (clip->gegl);
  clip->gegl = NULL;
  g_mutex_clear (&clip->mutex);
  g_free (clip);
}
const char *clip_get_path (Clip *clip)
{
  return clip->path;
}
void clip_set_path (Clip *clip, const char *path)
{
  if (clip->path)
    g_free (clip->path);
  clip->path = g_strdup (path);
  gegl_node_set (clip->loader, "path", path, NULL);
}
int clip_get_start (Clip *clip)
{
  return clip->start;
}
int clip_get_end (Clip *clip)
{
  return clip->end;
}
int clip_get_frames (Clip *clip)
{
  int frames = clip_get_end (clip) - clip_get_start (clip) + 1;
  if (frames < 0) frames = 0;
  return frames;
}

void clip_prepare_for_playback (Clip *clip)
{
  if (g_mutex_trylock (&clip->mutex))
  {
    int start = clip->start - clip->fade_pad_start;
    if (start < 0) start = 0;

    gegl_node_set (clip->loader, "frame", start, NULL);
    gegl_node_process (clip->loader);
    g_mutex_unlock (&clip->mutex);
  }
}

void clip_set_start (Clip *clip, int start)
{
  clip->start = start;

  //clip_prepare_for_playback (clip);
}
void clip_set_end (Clip *clip, int end)
{
  clip->end = end;
}
void clip_set_range (Clip *clip, int start, int end)
{
  clip_set_start (clip, start);
  clip_set_end (clip, end);
}
void clip_set_full (Clip *clip, const char *path, int start, int end)
{
  clip_set_path (clip, path);
  clip_set_range (clip, start, end);
}
Clip *clip_new_full (const char *path, int start, int end)
{
  Clip *clip = clip_new ();
  clip_set_full (clip, path, start, end);
  return clip;
}
void clip_fade_set (Clip *clip, int do_fade_out)
{
  /*  should cancel any computations due to fade when cancelling it, and add them when fade is set 
   */
}

void gegl_create_chain (char **ops, GeglNode *start, GeglNode *proxy);
static void gedl_create_chain (GeglEDL *edl, GeglNode *op_start, GeglNode *op_end, const char *filter_source)
{
  gchar **argv = NULL;
  gint    argc = 0;
  g_shell_parse_argv (filter_source, &argc, &argv, NULL);
  if (argv)
  {
    gegl_create_chain (argv, op_start, op_end);
    g_strfreev (argv);
  }
}

#define CACHE_FORMAT "jpg"

#include <stdlib.h>

GeglEDL *gedl_new           (void)
{
  GeglEDL *edl = g_malloc0(sizeof (GeglEDL));
  system ("mkdir .gedl 2>/dev/null");  /* XXX: create cache dir */
  edl->gegl = gegl_node_new ();
  edl->cache_flags = CACHE_TRY_ALL | CACHE_MAKE_ALL;
  edl->cache_flags = 0;
  edl->cache_flags = CACHE_TRY_ALL;
  edl->selection_start = 23;
  edl->selection_end = 42;


  edl->cache_loader = gegl_node_new_child (edl->gegl, "operation", "gegl:"  CACHE_FORMAT  "-load", NULL);

  edl->output_path      = DEFAULT_output_path;
  edl->video_codec      = DEFAULT_video_codec;
  edl->audio_codec      = DEFAULT_audio_codec;
  edl->video_width      = DEFAULT_video_width;
  edl->video_height     = DEFAULT_video_height;
  edl->video_size_default = 1;
  edl->video_bufsize    = DEFAULT_video_bufsize;
  edl->video_bitrate    = DEFAULT_video_bitrate;
  edl->video_tolerance  = DEFAULT_video_tolerance;;
  edl->audio_bitrate    = DEFAULT_audio_bitrate;
  edl->audio_samplerate = DEFAULT_audio_samplerate;
  edl->fade_duration    = DEFAULT_fade_duration;
  edl->frame_no = 0;

  edl->scale = 1.0;

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
 
  g_object_unref (edl->gegl);
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

const char *edl_path = "input.edl";

#if 1
GeglEDL *edl;
GeglEDL *edl2;
#endif


void frob_fade (GeglEDL *edl, Clip *clip);

static void rig_filters (GeglEDL *edl, Clip *clip, Clip *clip2, int frame_no)
{
  /* create filter graph if a secondary clip is in use */
  if (edl->mix != 0.0 && clip2 && clip2->filter_graph)
    {
      if (clip2->cached_filter_graph &&
          !strcmp(clip2->cached_filter_graph,
                  clip2->filter_graph))
        {
          /* reuse previous filter graph (when proeprty tweening arrives, should compute frame
                                          for tweening) */
        }
      else
       {
         remove_in_betweens (edl->nop_raw2, edl->nop_transformed2);
         gedl_create_chain (edl, edl->nop_raw2, edl->nop_transformed2,
                            clip2->filter_graph /*,
                            clip2->clip_frame_no - clip2->end,
                            clip2->end - clip2->start*/);

         if (clip2->cached_filter_graph)
           g_free (clip2->cached_filter_graph);
         clip2->cached_filter_graph = g_strdup (clip2->filter_graph);
       }
    }
   else
    {
      remove_in_betweens (edl->nop_raw2, edl->nop_transformed2);
      if (clip2)
      {
        if (clip2->cached_filter_graph)
          g_free (clip2->cached_filter_graph);
        clip2->cached_filter_graph = NULL;
      }
      gegl_node_link_many (edl->nop_raw2, edl->nop_transformed2, NULL);
    }
}

void gedl_set_frame         (GeglEDL *edl, int    frame)
{
  GList *l;
  int clip_start = 0;
  if ((edl->frame) == frame && (frame != 0))
  {
    fprintf (stderr, "already done!\n");
    return;
  }
  edl->frame = frame;
  edl->mix = 0.0;

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int clip_frames = clip_get_frames (clip);

    clip->clip_frame_no = 0;

    clip->abs_start = clip_start;
    if (frame - clip_start < clip_frames)
    {
      /* found right clip */
      const char *clip_path = clip_get_path (clip);
      Clip *clip2 = NULL; 
      gchar *frame_recipe;
      GChecksum *hash;
      gchar *cache_path;
      int was_cached =0 ;
     
      edl->clip = clip;

      clip->clip_frame_no = (frame - clip_start) + clip_get_start (clip);
  
      if (!clip->is_image)
        {
          gegl_node_set (clip->loader, "frame", clip->clip_frame_no, NULL);
        }

      frob_fade (edl, clip);
      
      if (clip->fade_out && (clip->clip_frame_no > (clip->end - clip->fade_pad_end)) && l->next)
        {
          clip2 = l->next->data;
          clip2->clip_frame_no = 0;

          edl->clip2 = clip2;
          edl->mix = (clip->end - clip->clip_frame_no) * 1.0 / clip->fade_pad_end;
          edl->mix = (1.0 - edl->mix) / 2.0;

#if 1
      //  if (edl->source[1]->clip_path == NULL || strcmp (clip_get_path (clip2), edl->source[1]->clip_path))
           {
             gegl_node_set (clip2->loader, "operation", clip2->is_image?"gegl:load":"gegl:ff-load", "path", clip2->clip_path, NULL);
	     clip2->clip_frame_no = clip2->start - ( clip->end - clip->clip_frame_no);
           }
#endif
	     if (!clip2->is_image)
           {
	         gegl_node_set (clip2->loader, "frame", clip2->clip_frame_no, NULL);
           }
        }
      else if (clip->fade_in && (clip->clip_frame_no - clip->start < clip->fade_pad_start))
        {
          clip2 = l->prev->data;
          edl->mix = (clip->fade_pad_start - (clip->clip_frame_no - clip->start) ) * 1.0 / clip->fade_pad_start;
           edl->mix /= 2.0;

#if 1
      //     if (edl->source[1]->clip_path == NULL || strcmp (clip_get_path (clip2), edl->source[1]->clip_path))
             {
               gegl_node_set (clip2->loader, "operation", clip2->is_image?"gegl:load":"gegl:ff-load", "path", clip2->clip_path, NULL);
	           clip2->clip_frame_no = clip2->end + (clip->clip_frame_no - clip->start);

             }
#endif
	       if (!clip2->is_image) 
	         gegl_node_set (clip2->loader, "frame", clip2->clip_frame_no, NULL);
        }
      else
        {
          edl->mix = 0.0;
        }

      /* this is both where we can keep filter graphs, and do more global
       * cache short circuiting, this would leave the cross fading to still have
       * to happen on he fly.. along with audio mix
       */

      if (clip->filter_graph)
        {
          if (clip->cached_filter_graph &&
                !strcmp(clip->cached_filter_graph,
                        clip->filter_graph))
            {
              /* reuse previous filter graph (should probably set frame for tweening?  */
            }
          else
            {
              remove_in_betweens (edl->nop_raw, edl->nop_transformed);
              gedl_create_chain (edl, edl->nop_raw, edl->nop_transformed, clip->filter_graph /*, clip->clip_frame_no - clip->end, clip->end - clip->start */);
                if (clip->cached_filter_graph)
                  g_free (clip->cached_filter_graph);
                clip->cached_filter_graph = g_strdup (clip->filter_graph);
            }
         }
       else
         {
           remove_in_betweens (edl->nop_raw, edl->nop_transformed);
           if (clip->cached_filter_graph)
             g_free (clip->cached_filter_graph);
           clip->cached_filter_graph = NULL;
           gegl_node_link_many (edl->nop_raw, edl->nop_transformed, NULL);
         }

       rig_filters (edl, clip, clip2, frame);

        /**********************************************************************/

        frame_recipe = g_strdup_printf ("%s: %s %s %i %s %s %i %s %ix%i %f",
          "gedl-pre-3", clip_path, gedl_get_clip_path (edl), gedl_get_clip_frame_no (edl), clip->filter_graph,
                        //gedl_get_clip2_path (edl), gedl_get_clip2_frame_no (edl), clip2->filter_graph,
                        "aaa", 3, "bbb",
                        edl->video_width, edl->video_height, 
                        edl->mix);

        hash = g_checksum_new (G_CHECKSUM_MD5);
        g_checksum_update (hash, (void*)frame_recipe, -1);
        cache_path  = g_strdup_printf (".gedl/%s", g_checksum_get_string(hash));

        /*************************************************************************/

        if (g_file_test (cache_path, G_FILE_TEST_IS_REGULAR) && (edl->cache_flags & CACHE_TRY_ALL))
          {
            was_cached = 1;
            gegl_node_set (edl->cache_loader, "path", cache_path, NULL);
            gegl_node_link_many (edl->cache_loader, edl->result, NULL);
#if 1
            if (!clip->audio)
              clip->audio = gegl_audio_fragment_new (44100, 2, 0, 4000);
            gegl_meta_get_audio (cache_path, clip->audio);
#endif
          }
        else
          {
            g_mutex_lock (&clip->mutex);
            gegl_node_process (clip->store_buf);
            if (edl->mix != 0.0 && clip2)
              {
                gegl_node_process (clip2->store_buf);
              }
            if (clip->audio)
              {
                g_object_unref (clip->audio);
                clip->audio = NULL;
              }
             if (clip2 && clip2->audio)
              {
                g_object_unref (clip2->audio);
                clip2->audio = NULL;
              }

            if (clip->is_image)
              clip->audio = NULL;
            else
              gegl_node_get (clip->loader, "audio", &clip->audio, NULL);
            if (edl->mix != 0.0 && clip2)
              {
               /* directly mix the audio from the secondary into the primary, with proportional weighting of samples
                */
                if (clip2->is_image)
                  {
                    clip2->audio = NULL;
                  }
                else
                  {
                    int i, c;
                    gegl_node_get (clip2->loader, "audio", &clip2->audio, NULL);

                    for (c = 0; c < gegl_audio_fragment_get_channels (clip->audio); c++)
                      for (i = 0; i < MIN(gegl_audio_fragment_get_sample_count (clip->audio),
                              gegl_audio_fragment_get_sample_count (clip2->audio)); i++)
                        clip->audio->data[c][i] =
                          clip->audio->data[c][i] * (1.0-edl->mix) +
                          clip2->audio->data[c][i] * edl->mix;
                   }
               }

          /* write cached render of this frame */
          if (edl->cache_flags & CACHE_MAKE_ALL)
            {
              gchar *cache_path = g_strdup_printf (".gedl/%s~", g_checksum_get_string(hash));
              gchar *cache_path_final = g_strdup_printf (".gedl/%s", g_checksum_get_string(hash));
              if (!g_file_test (cache_path, G_FILE_TEST_IS_REGULAR) &&
                  !g_file_test (cache_path_final, G_FILE_TEST_IS_REGULAR))
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
              g_free (cache_path_final);
            }
            g_mutex_unlock (&clip->mutex);
          }

      g_checksum_free (hash);
      g_free (frame_recipe);

	  if (!was_cached)
      {
	    if (edl->mix != 0.0)
	    {
	       gegl_node_set (edl->load_buf2, "buffer", gedl_get_buffer2 (edl), NULL);
	       gegl_node_connect_to (edl->over, "output", edl->result, "input");
	       gegl_node_set (edl->opacity, "value", edl->mix, NULL);
	    }
	    else
	    {
	       gegl_node_connect_to (edl->crop, "output", edl->result, "input");
	    }
      }
      return;
    }
    clip_start += clip_frames;
  }
  gegl_node_connect_to (edl->crop, "output", edl->result, "input");
  //edl->source[0]->clip_path = "unknown";
  //edl->source[0]->clip_frame_no = 0;
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
GeglAudioFragment *gedl_get_audio  (GeglEDL *edl)
{
  return edl->clip->audio;
}
GeglBuffer *gedl_get_buffer (GeglEDL *edl)
{
  return edl->clip->buffer;
}
GeglBuffer *gedl_get_buffer2 (GeglEDL *edl)
{
  return edl->clip2->buffer?edl->clip2->buffer:edl->clip->buffer;
}
double gedl_get_mix (GeglEDL *edl)
{
  return edl->mix;
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

void frob_fade (GeglEDL *edl, Clip *clip)
{
        return;
  if (!clip->is_image)
  {
    if (clip->end == 0)
       clip->end = clip->duration;

    if (clip->fade_out)
    {
       clip->fade_pad_end = edl->fade_duration/2;
       if (clip->end + clip->fade_pad_end > clip->duration)
         {
           int delta = clip->end + clip->fade_pad_end - clip->duration;
           clip->end -= delta;
         }
    }
    else {
       clip->fade_pad_end = 0;
    }
    if (clip->fade_in)
    {
       clip->fade_pad_start = edl->fade_duration/2;
       if (clip->start - clip->fade_pad_start < 0)
         {
           int delta = clip->fade_pad_start - clip->start;
           clip->start += delta;
         }
    }
    else
    {
       clip->fade_pad_start = 0;
    }
  }
}

void gedl_parse_clip (GeglEDL *edl, const char *line)
{
  int start = 0; int end = 0;
  const char *rest = NULL;
  char path[1024];
  if (line[0] == '#' ||
      line[1] == '#' ||
      strlen (line) < 4)
    return;

  if (strstr (line, "--"))
    rest = strstr (line, "--") + 2;

  if (rest) while (*rest == ' ')rest++;

  sscanf (line, "%s %i %i", path, &start, &end);
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
      sclip->end= end;
      if (rest)
        sclip->title = g_strdup (rest);
    }
  /* todo: probe video file for length if any of arguments are nont present as 
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
     if (!strcmp (key, "output-path"))       edl->output_path = g_strdup (value);
     if (!strcmp (key, "video-codec"))       edl->video_codec = g_strdup (value);
     if (!strcmp (key, "audio-codec"))       edl->audio_codec = g_strdup (value);
     if (!strcmp (key, "audio-sample-rate")) edl->audio_samplerate = g_strtod (value, NULL);
     if (!strcmp (key, "video-bufsize"))     edl->video_bufsize = g_strtod (value, NULL);
     if (!strcmp (key, "video-bitrate"))     edl->video_bitrate = g_strtod (value, NULL);
     if (!strcmp (key, "audio-bitrate"))     edl->audio_bitrate = g_strtod (value, NULL);
     if (!strcmp (key, "video-width"))       edl->video_width = g_strtod (value, NULL);
     if (!strcmp (key, "video-height"))      edl->video_height = g_strtod (value, NULL);
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

      if (g_str_has_suffix (path, ".png") ||
          g_str_has_suffix (path, ".jpg") ||
          g_str_has_suffix (path, ".exr") ||
          g_str_has_suffix (path, ".JPG"))
       {
         clip = clip_new_full (path, start, end);
         clip->is_image = 1;
         edl->clips = g_list_append (edl->clips, clip);
       }
      else
       {
	 if ((start == 0 && end == 0))
           ff_probe = 1;

         clip = clip_new_full (path, start, end);
         edl->clips = g_list_append (edl->clips, clip);
       }
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

     if (ff_probe && !clip->is_image)
       {
	     GeglNode *gegl = gegl_node_new ();
	     GeglNode *probe = gegl_node_new_child (gegl, "operation",
                          "gegl:ff-load", "path", clip->path, NULL);
	     gegl_node_process (probe);

	     gegl_node_get (probe, "frames", &clip->duration, NULL);
	     gegl_node_get (probe, "frame-rate", &clip->fps, NULL);
         g_object_unref (gegl);
         frob_fade (edl, clip);

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
    }
  /* todo: probe video file for length if any of arguments are nont present as 
           integers.. alloving full clips and clips with mm:ss.nn timestamps,
   */
}

GeglEDL *gedl_new_from_string (const char *string);
GeglEDL *gedl_new_from_string (const char *string)
{
  GString *line = g_string_new ("");
  GeglEDL *edl = gedl_new ();
  int clips_done = 0;

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
       }
       else
       {
         if (line->str[0] == '-' &&
             line->str[1] == '-' &&
             line->str[2] == '-')
           clips_done = 1;
         else
           gedl_parse_line (edl, line->str);
       }
       g_string_assign (line, "");
       break;
      default: g_string_append_c (line, *p);
       break;
    }
  }
  g_string_free (line, TRUE);

  if ((edl->video_width == 0 || edl->video_height == 0) && edl->clips)
    {
      Clip *clip = edl->clips->data;
      GeglNode *gegl = gegl_node_new ();
      GeglRectangle rect;
      GeglNode *probe = gegl_node_new_child (gegl, "operation", "gegl:ff-load", "path", clip->path, NULL);
      gegl_node_process (probe);
      rect = gegl_node_get_bounding_box (probe);
      edl->video_width = rect.width;
      edl->video_height = rect.height;
      g_object_unref (gegl);
    }
  gedl_set_size (edl, edl->video_width, edl->video_height);

  return edl;
}

#if 0
void gedl_load_path (GeglEDL *edl, const char *path)
{
  gchar *string = NULL;

  if (edl->path)
    g_free (edl->path);
  edl->path = g_strdup (path);
  edl->video_width = DEFAULT_video_width;
  edl->video_height = DEFAULT_video_height;

  FILE *file = fopen (path, "r");
  if (file)
    {
       char line [4096];
       while (fgets (line, sizeof (line), file))
         gedl_parse_line (edl, line);
       fclose (file);
    }
}
#endif

void gedl_save_path (GeglEDL *edl, const char *path)
{
  char *serialized;
  if (g_str_has_suffix (path, ".mp4") ||
      g_str_has_suffix (path, ".MP4") ||
      g_str_has_suffix (path, ".mkv") ||
      g_str_has_suffix (path, ".MKV") ||
      g_str_has_suffix (path, ".OGV") ||
      g_str_has_suffix (path, ".AVI") ||
      g_str_has_suffix (path, ".ogv") ||
      g_str_has_suffix (path, ".avi"))
    return;
  FILE *file = fopen (path, "w");
  if (!file)
    return;

  serialized = gedl_serialise (edl);
  if (serialized)
  {
    fprintf (file, "%s\n", serialized);
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
      GeglNode *probe = gegl_node_new_child (gegl, "operation", "gegl:ff-load", "path", clip->path, NULL);
      gegl_node_process (probe);
      rect = gegl_node_get_bounding_box (probe);
      edl->video_width = rect.width;
      edl->video_height = rect.height;
      g_object_unref (gegl);
    }
}

GeglEDL *gedl_new_from_path (const char *path)
{
  GeglEDL *edl = NULL;
  gchar *string = NULL;

  g_file_get_contents (path, &string, NULL, NULL);
  if (string)
  {
    edl = gedl_new_from_string (string);
    g_free (string);
    gedl_update_video_size (edl);
    gedl_set_size (edl, edl->video_width, edl->video_height);
    if (!edl->path)
      edl->path = g_strdup (path);
  }

  return edl;
}
const char *gedl_get_clip_path (GeglEDL *edl)
{
  return edl->clip->clip_path;
}
int gedl_get_clip_frame_no    (GeglEDL *edl)
{
  return edl->clip->clip_frame_no;
}
const char *gedl_get_clip2_path          (GeglEDL *edl)
{
  return edl->clip2->clip_path;
}
int gedl_get_clip2_frame_no      (GeglEDL *edl)
{
  return edl->clip2->clip_frame_no;
}
static void setup (GeglEDL *edl)
{
  edl->gegl = gegl_node_new ();
  edl->result = gegl_node_new_child    (edl->gegl, "operation", "gegl:nop", NULL);
  edl->load_buf = gegl_node_new_child  (edl->gegl, "operation", "gegl:buffer-source", NULL);
  edl->load_buf2 = gegl_node_new_child (edl->gegl, "operation", "gegl:buffer-source", NULL);
  edl->crop = gegl_node_new_child      (edl->gegl, "operation", "gegl:crop", "x", 0.0, "y", 0.0, "width", 1.0 * edl->width,
                                         "height", 1.0 * edl->height, NULL);
  edl->crop2 = gegl_node_new_child     (edl->gegl, "operation", "gegl:crop", "x", 0.0, "y", 0.0, "width", 1.0 * edl->width,
                                         "height", 1.0 * edl->height, NULL);
  edl->over = gegl_node_new_child      (edl->gegl, "operation", "gegl:over", NULL);
  edl->nop_raw = gegl_node_new_child   (edl->gegl, "operation", "gegl:nop", NULL);
  edl->nop_raw2 = gegl_node_new_child  (edl->gegl, "operation", "gegl:nop", NULL);
  edl->nop_transformed = gegl_node_new_child (edl->gegl, "operation", "gegl:nop", NULL);
  edl->nop_transformed2 = gegl_node_new_child (edl->gegl, "operation", "gegl:nop", NULL);
  edl->opacity = gegl_node_new_child (edl->gegl, "operation", "gegl:opacity", NULL);
  edl->scale_size = gegl_node_new_child (edl->gegl, "operation", "gegl:scale-size",
                                          "x", 1.0 * edl->width,
                                          "y", 1.0 * edl->height, NULL);
  edl->scale_size2 = gegl_node_new_child (edl->gegl, "operation", "gegl:scale-size",
                                    "x", 1.0 * edl->width,
                                    "y", 1.0 * edl->height, NULL);
  edl->encode = gegl_node_new_child (edl->gegl, "operation", "gegl:ff-save",
                                      "path",           edl->output_path,
                                      "frame-rate",     gedl_get_fps (edl),
                                      "video-bit-rate", edl->video_bitrate,
                                      "video-bufsize",  edl->video_bufsize,
                                      "audio-bit-rate", edl->audio_bitrate,
                                      "audio-codec",    edl->audio_codec,
                                      "video-codec",    edl->video_codec,
                                      NULL);
  gegl_node_link_many (edl->result, edl->encode, NULL); 
  gegl_node_link_many (edl->load_buf, edl->scale_size, edl->nop_raw, edl->nop_transformed, edl->crop, NULL); 
  gegl_node_link_many (edl->load_buf2, edl->scale_size2, edl->nop_raw2, edl->nop_transformed2, edl->opacity, edl->crop2,  NULL); 

  gegl_node_connect_to (edl->nop_raw, "output", edl->nop_transformed, "input");
  gegl_node_connect_to (edl->nop_raw2, "output", edl->nop_transformed2, "input");
  gegl_node_connect_to (edl->crop2, "output", edl->over, "aux");
  gegl_node_connect_to (edl->crop, "output", edl->over, "input");
  gegl_node_connect_to (edl->over, "output", edl->result, "input");
}
void rig_frame (GeglEDL *edl, int frame_no)
{
  if (edl->frame == frame_no)
    return;
  gedl_set_frame (edl, frame_no);

  gegl_node_set (edl->load_buf, "buffer", gedl_get_buffer (edl), NULL);
  gegl_node_set (edl->encode, "audio", gedl_get_audio (edl), NULL);
}

int skip_encode = 0;
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
    if (!skip_encode)
      gegl_node_process (edl->encode);
    fprintf (stderr, "\r%1.2f%% %04d / %04d    ",
     100.0 * (frame_no-edl->range_start) * 1.0 / (edl->range_end - edl->range_start),
     frame_no, edl->range_end);
  }
}

int gegl_make_thumb_video (const char *path, const char *thumb_path)
{
  int tot_frames;
  GString *str = g_string_new ("");
  g_string_append_printf (str, "video-bitrate=100\n\noutput-path=%s\nvideo-width=320\nvideo-height=240\n\n%s\n", thumb_path, path);
  edl = gedl_new_from_string (str->str);
  setup (edl);
  tot_frames = gedl_get_duration (edl);
  if (edl->range_end == 0)
    edl->range_end = tot_frames-1;
  process_frames (edl);
  teardown ();
  g_string_free (str, TRUE);
  return 0;
}

static GThread *thread;

int gedl_ui_main (GeglEDL *edl, GeglEDL *edl2);

static gpointer preloader (gpointer data)
{
  GeglEDL *edl = data;
  GList *l;
  while (1)
  {
    for (l= edl->clips; l; l= l->next)
    {
      Clip *clip = l->data;

      if (clip != edl->clip) // && clip!= edl->clip2)
      /* XXX: should add a mutex to clip to avoid doing it in parallell,.. */
        clip_prepare_for_playback (clip);
    }
    g_usleep (5000);
  }
  return NULL;
}

int main (int argc, char **argv)
{
  int tot_frames;
  init (argc, argv);
  if (!argv[1])
  {
    fprintf (stderr, "usage: %s <input.edl> [output.mp4]\n", argv[0]);
    return -1;
  }

  setenv ("GEGL_USE_OPENCL", "no", 1);
  setenv ("GEGL_MIPMAP_RENDERING", "1", 1);

  if (argv[1] && argv[2] && argv[3] && !strcmp (argv[1], "--make-proxy"))
     return gegl_make_thumb_video (argv[2], argv[3]);

  edl_path = argv[1];

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
                  "gegl:ff-load", "path", edl_path, NULL);
	gegl_node_process (probe);

	gegl_node_get (probe, "frames", &duration, NULL);
	gegl_node_get (probe, "frame-rate", &fps, NULL);
    g_object_unref (gegl);

    sprintf (str, "%s 0 %i\n", edl_path, duration);
    edl = gedl_new_from_string (str);
    edl->path = g_strdup (edl_path);
    edl2 = gedl_new_from_string (str);
    edl2->path = g_strdup (edl_path);
  }
  else
  {
    edl = gedl_new_from_path (edl_path);
    edl2 = gedl_new_from_path (edl_path);
  }
  if (argv[2] && argv[2][0]!='-')
    edl->output_path = argv[2];
  setup (edl);
  setup (edl2);


  for (int i = 1; argv[i]; i++)
    if (!strcmp (argv[i], "-c"))
       skip_encode = 1;
    else
    if (!strcmp (argv[i], "-ui"))
    {
      if (0)
        thread = g_thread_new ("renderer", preloader, edl);
      return gedl_ui_main (edl, edl2);
    }

  tot_frames  = gedl_get_duration (edl);
  if (edl->range_end == 0)
    edl->range_end = tot_frames-1;
  process_frames (edl);
  teardown ();
  return 0;
}

char *gedl_serialise (GeglEDL *edl)
{
  GList *l;
  char *ret;
  GString *ser = g_string_new ("");

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
  if (edl->selection_start != DEFAULT_selection_start)
    g_string_append_printf (ser, "selection-start=%i\n",  edl->selection_start);
  if (edl->selection_end != DEFAULT_selection_end)
    g_string_append_printf (ser, "selection-end=%i\n",  edl->selection_end);

  if (edl->range_start != DEFAULT_range_start)
    g_string_append_printf (ser, "range-start=%i\n",  edl->range_start);
  if (edl->range_end != DEFAULT_range_end)
    g_string_append_printf (ser, "range-end=%i\n", edl->range_end);

  if (edl->scale != 1.0)//DEFAULT_range_end)
    g_string_append_printf (ser, "frame-scale=%f\n", edl->scale);
  if (edl->t0 != 1.0)//DEFAULT_range_end)
    g_string_append_printf (ser, "t0=%f\n", edl->t0);
  g_string_append_printf (ser, "frame-no=%i\n", edl->frame_no);
  
  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    g_string_append_printf (ser, "%s %d %d%s%s%s%s\n", clip->path, clip->start, clip->end,
        clip->fade_out?" [fade]":"", 
        (edl->active_clip == clip)?" [active]":"", 
        clip->filter_graph?" -- ":"",clip->filter_graph?clip->filter_graph:"");
 
  }
  g_string_append_printf (ser, "-----\n");
  for (l = edl->clip_db; l; l = l->next)
  {
    SourceClip *clip = l->data;
    g_string_append_printf (ser, "%s %d %d%s%s%s\n", clip->path, clip->start, clip->end,
        (edl->active_source == clip)?" [active]":"", 
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
  gexiv2_metadata_free (e2m);
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
  gexiv2_metadata_free (e2m);
}

void        gedl_set_selection      (GeglEDL    *edl, int start_frame, int end_frame)
{
  edl->selection_start = start_frame;
  edl->selection_end   = end_frame;
}

void        gedl_get_selection      (GeglEDL    *edl,
                                     int        *start_frame,
                                     int        *end_frame)
{
  if (start_frame)
    *start_frame = edl->selection_start;
  if (end_frame)
    *end_frame = edl->selection_end;
}

void        gedl_set_range      (GeglEDL    *edl, int start_frame, int end_frame)
{
  edl->range_start = start_frame;
  edl->range_end   = end_frame;
}

void        gedl_get_range      (GeglEDL    *edl,
                                 int        *start_frame,
                                 int        *end_frame)
{
  if (start_frame)
    *start_frame = edl->range_start;
  if (end_frame)
    *end_frame = edl->range_end;
}


void gegl_create_chain (char **ops, GeglNode *start, GeglNode *proxy)
{
  GeglNode   *iter[10] = {start, NULL};
  GeglNode   *new = NULL;
  gchar     **arg = ops;
  int         level = 0;
  char       *level_op[10];
  char       *level_pad[10];
  GHashTable *ht = NULL;

  level_op[level] = *arg;
 
  ht = g_hash_table_new (g_str_hash, g_str_equal);

  while (*arg)
    {
      if (strchr (*arg, ']'))
      {
        level--;
        gegl_node_connect_to (iter[level+1], "output", iter[level], level_pad[level]);
      }
      else
      {
      if (strchr (*arg, '=')) /* contains = sign, must be a property assignment */
      {
        char *match= strchr (*arg, '=');
        if (match[1] == '[')
        {
          char *pad = g_strdup (*arg);
          char *value = strchr (pad, '=') + 1;
          value[-1] = '\0';
          level_pad[level]=(void*)g_intern_string(pad);
          g_free (pad);
          level++;

          iter[level]=NULL;
          level_op[level]=NULL;
          level_pad[level]=NULL;
        }
        else
        {
          GType target_type = G_TYPE_INT;
          GValue gvalue={0,};
          char *key = g_strdup (*arg);
          char *value = strchr (key, '=') + 1;
          value[-1] = '\0';

          if (!strcmp (key, "id"))
          {
            g_hash_table_insert (ht, (void*)g_intern_string (value), iter[level]);
          }
          else if (!strcmp (key, "ref"))
          {
            if (g_hash_table_lookup (ht, g_intern_string (value)))
              iter[level] = g_hash_table_lookup (ht, g_intern_string (value));
            else
              g_warning ("unknown id '%s'", value);
          }
          else
          {
            unsigned int n_props = 0;
            GParamSpec **pspecs;
            int i;

            pspecs = gegl_operation_list_properties (level_op[level], &n_props);
            for (i = 0; i < n_props; i++)
            {
              if (!strcmp (pspecs[i]->name, key))
                target_type = pspecs[i]->value_type;
            }
            if (target_type == G_TYPE_DOUBLE || target_type == G_TYPE_FLOAT)
              {
                double val = g_strtod (value, NULL);
                gegl_node_set (iter[level], key, val, NULL);
              }
            else if (target_type == G_TYPE_BOOLEAN) {
            if (!strcmp (value, "true") || !strcmp (value, "TRUE") ||
                !strcmp (value, "YES") || !strcmp (value, "yes") ||
                !strcmp (value, "y") || !strcmp (value, "Y") ||
                !strcmp (value, "1") || !strcmp (value, "on"))
              {
                gegl_node_set (iter[level], key, TRUE, NULL);
              }
            else
              {
                gegl_node_set (iter[level], key, FALSE, NULL);
              }
            }
            else if (target_type == G_TYPE_INT)
            {
              int val = g_strtod (value, NULL);
              gegl_node_set (iter[level], key, val, NULL);
            }
            else if (target_type == GEGL_TYPE_COLOR)
            {
              GeglColor *color = g_object_new (GEGL_TYPE_COLOR,
                                               "string", value, NULL);
              gegl_node_set (iter[level], key, color, NULL);
            }
            else if (g_type_is_a (target_type, G_TYPE_ENUM))
            {
              GEnumClass *eclass = g_type_class_peek (target_type);
              GEnumValue *evalue = g_enum_get_value_by_nick (eclass, value);
              if (evalue)
                {
                  gegl_node_set (new, key, evalue->value, NULL);
                }
              else
                {
              /* warn, but try to get a valid nick out of the old-style
               * value name
               */
                gchar *nick;
                gchar *c;
                g_printerr ("gedl (param_set %s): enum %s has no value '%s'\n",
                            key,
                            g_type_name (target_type),
                            value);
                nick = g_strdup (value);
                for (c = nick; *c; c++)
                  {
                    *c = g_ascii_tolower (*c);
                    if (*c == ' ')
                      *c = '-';
                  }
                evalue = g_enum_get_value_by_nick (eclass, nick);
                if (evalue)
                  gegl_node_set (iter[level], key, evalue->value, NULL);
                g_free (nick);
              }
            }
            else
            {
              GValue gvalue_transformed={0,};
              g_value_init (&gvalue, G_TYPE_STRING);
              g_value_set_string (&gvalue, value);
              g_value_init (&gvalue_transformed, target_type);
              g_value_transform (&gvalue, &gvalue_transformed);
              gegl_node_set_property (iter[level], key, &gvalue_transformed);
              g_value_unset (&gvalue);
              g_value_unset (&gvalue_transformed);
            }
          }
          g_free (key);
        }
      }
      else
      {
        if (strchr (*arg, ':')) /* contains : is a non-prefixed operation */
          {
            level_op[level] = *arg;
          }
          else /* default to gegl: as prefix if no : specified */
          {
            char temp[1024];
            snprintf (temp, 1023, "gegl:%s", *arg);
            level_op[level] = (void*)g_intern_string (temp);
          }
        new = gegl_node_new_child (gegl_node_get_parent (proxy), "operation",
                        level_op[level], NULL);

        if (iter[level])
          gegl_node_link_many (iter[level], new, proxy, NULL);
        else
          gegl_node_link_many (new, proxy, NULL);
        iter[level] = new;
      }
      }
      arg++;
    }

  g_hash_table_unref (ht);
  gegl_node_link_many (iter[level], proxy, NULL);
}
