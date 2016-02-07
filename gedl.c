#include <string.h>
#include <stdio.h>
#include <gegl.h>
#include <gexiv2/gexiv2.h>
#include <gegl-audio-fragment.h>

#if 0
  1 - video with audio fragment output (without, already kind of works with ff-save and frame-%06d.png patterns..
  2 - mrg based ui
  3 - caches / proxies

for the main timeline have:

rendered results as full size JPGs
chunks of 320x240 encoded mp4 video files of 5s that contains render of video, from proxy
chunks of 320x240 encoded mp4 video files of 5s that contains render of video, from full size jpgs

for each video have:
  small 320x240 encoded mp4 video of all videos
  image full size jpg with audio extract
  160x120 size jpg with audio extract
  iconographer slice

#endif
/* GEGL edit decision list - a digital video cutter and splicer */
#include "gedl.h"

GeglNode *nop_raw;
GeglNode *nop_transformed;
GeglNode *nop_raw2;
GeglNode *nop_transformed2;
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


const char *output_path      = DEFAULT_output_path;
const char *video_codec      = DEFAULT_video_codec;
const char *audio_codec      = DEFAULT_audio_codec;
int         video_width      = DEFAULT_video_width;
int         video_height     = DEFAULT_video_height;
int         video_size_default = 1;
int         video_bufsize    = DEFAULT_video_bufsize;
int         video_bitrate    = DEFAULT_video_bitrate;
int         video_tolerance  = DEFAULT_video_tolerance;;
int         audio_bitrate    = DEFAULT_audio_bitrate;
int         audio_samplerate = DEFAULT_audio_samplerate;
int         fade_duration    = DEFAULT_fade_duration;
int         frame_start      = DEFAULT_frame_start;
int         frame_end        = DEFAULT_frame_end;

Clip *clip_new (void)
{
  Clip *clip = g_malloc0 (sizeof (Clip));
  return clip;
}
void clip_free (Clip *clip)
{
  if (clip->path)
    g_free (clip->path);
  clip->path = NULL;
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
void clip_set_start (Clip *clip, int start)
{
  clip->start = start;
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

static void gedl_create_chain (GeglEDL *edl, GeglNode *op_start, GeglNode *op_end, const char *filter_source, int frame, int tot_frames)
{
  const char *p;
  GString *op_name = g_string_new ("");
  char *cur_op = NULL;
  GString *op_attr = g_string_new ("");
  GeglNode *iter = op_start;
  for (p = filter_source; *p; p++)
  {
     g_string_append_c (op_name, p[0]);
     if (p[1] == ' ' || p[1]=='\n' || p[1] == '\0') {
       if (op_name->len > 2)
       {
         if (strchr (op_name->str, '='))
         {
           GType target_type = G_TYPE_INT;
           GValue gvalue={0,};
           GValue gvalue_transformed={0,};
           char *key = g_strdup (op_name->str);
           char *value = strchr (key, '=') + 1;
           value[-1]='\0';
           {
             unsigned int n_props;
             GParamSpec **pspecs = gegl_operation_list_properties (cur_op, &n_props);
             int i;
             for (i = 0; i < n_props; i++)
             {
               if (!strcmp (pspecs[i]->name, key))
                 target_type = pspecs[i]->value_type;
             }
           }
	   if (target_type == G_TYPE_DOUBLE ||
               target_type == G_TYPE_FLOAT)
           {
             double val = g_strtod (value, NULL);
             gegl_node_set (iter, key, val, NULL);
           }
	   else if (target_type == G_TYPE_BOOLEAN)
           {

          if (!strcmp (value, "true") ||
              !strcmp (value, "TRUE") ||
              !strcmp (value, "YES") ||
              !strcmp (value, "yes") ||
              !strcmp (value, "y") ||
              !strcmp (value, "Y") ||
              !strcmp (value, "1") ||
              !strcmp (value, "on"))
            {
              gegl_node_set (iter, key, TRUE, NULL);
            }
          else
            {
              gegl_node_set (iter, key, FALSE, NULL);
            }
           }
	   else if (target_type == G_TYPE_INT)
           {
             int val = g_strtod (value, NULL);
             gegl_node_set (iter, key, val, NULL);
           }
           else
           {
             g_value_init (&gvalue, G_TYPE_STRING);
             g_value_set_string (&gvalue, value);
             g_value_init (&gvalue_transformed, target_type);
             g_value_transform (&gvalue, &gvalue_transformed);
             gegl_node_set_property (iter, key, &gvalue_transformed);
             g_value_unset (&gvalue);
             g_value_unset (&gvalue_transformed);
           }
           g_free (key);
         }
         else if (strchr (op_name->str, ':'))
         {
           GeglNode *node = gegl_node_new_child (edl->gegl, "operation", op_name->str, NULL);
           gegl_node_link_many (iter, node, NULL);
           iter = node;
           if (cur_op) g_free (cur_op);
           cur_op = g_strdup (op_name->str);
         }
       }
       g_string_assign (op_name, "");
       if (p[1])
         p++;
     } 
  }
  gegl_node_link_many (iter, op_end, NULL);
  g_string_free (op_name, TRUE);
  g_string_free (op_attr, TRUE);
}

GeglEDL *gedl_new           (void)
{
  GeglEDL *edl = g_malloc0(sizeof (GeglEDL));
  int s;
  edl->gegl = gegl_node_new ();
  edl->cache_flags = CACHE_TRY_ALL | CACHE_MAKE_ALL;

  edl->cache_loader = gegl_node_new_child (edl->gegl, "operation", "gegl:jpg-load", NULL);
  for (s = 0; s < 2; s++)
   {
     edl->source[s].loader = gegl_node_new_child (edl->gegl, "operation", "gegl:ff-load", NULL);
     edl->source[s].store_buf = gegl_node_new_child (edl->gegl, "operation", "gegl:buffer-sink", "buffer", &edl->source[s].buffer, NULL);
     gegl_node_link_many (edl->source[s].loader, edl->source[s].store_buf, NULL);
   }

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
  int s;
  for (s = 0; s < 2; s++)
  if (edl->source[s].buffer)
    g_object_unref (edl->source[s].buffer);
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

const char *edl_path = "input.edl";
GeglEDL *edl;
GeglNode *gegl, *load_buf, *result, *encode, *crop, *scale_size, *opacity,
                *load_buf2, *crop2, *scale_size2, *over;
void frob_fade (Clip *clip);

/* also take stat-ing of cache status into account */
int gedl_get_render_complexity (GeglEDL *edl, int frame)
{
  if (edl->frame == frame)
    return 0;
  if (edl->frame + 1 == frame)
    return 1;
  if (edl->frame <= frame &&
      (frame - edl->frame) < 16)
    return 2;
  return 3;
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

  edl->source[1].clip_path     = "unknown";
  edl->source[1].clip_frame_no = 0;
  edl->source[1].filter_graph  = NULL;
  edl->source[0].clip_path     = "unknown";
  edl->source[0].filter_graph  = NULL;
  edl->source[0].clip_frame_no = 0;
  edl->mix = 0.0;

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int clip_frames = clip_get_frames (clip);

    clip->abs_start = clip_start;
    if (frame - clip_start < clip_frames)
    {
      const char *clip_path = clip_get_path (clip);
      Clip *clip2 = NULL; 
      gchar *frame_recipe;
      GChecksum *hash;
      gchar *cache_path;
      int was_cached =0 ;
     
      edl->source[0].filter_graph = clip->filter_graph;

      if (edl->source[0].clip_path == NULL || strcmp (clip_path, edl->source[0].clip_path))
      {
        edl->source[0].clip_path = clip_get_path (clip);

        gegl_node_set (edl->source[0].loader, "operation", clip->is_image?"gegl:load":"gegl:ff-load",
                                    "path", clip_get_path (clip), NULL);
      }

      edl->source[0].clip_frame_no = (frame - clip_start) + clip_get_start (clip);
  
      if (!clip->is_image)
        gegl_node_set (edl->source[0].loader, "frame", edl->source[0].clip_frame_no, NULL);

      for (int s = 0; s < 2; s ++)
      if (edl->source[s].buffer) {
        g_object_unref (edl->source[s].buffer);
        edl->source[s].buffer = NULL;
      }
      frob_fade (clip);
      if (clip->fade_out && 
        (edl->source[0].clip_frame_no > (clip->end - clip->fade_pad_end)) && l->next)
        {
          clip2 = l->next->data;
          edl->mix = (clip->end - edl->source[0].clip_frame_no) * 1.0 / clip->fade_pad_end;
          edl->mix = (1.0 - edl->mix) / 2.0;
          edl->source[1].filter_graph = clip2->filter_graph;
          if (edl->source[1].clip_path == NULL || strcmp (clip_get_path (clip2), edl->source[1].clip_path))
           {
             edl->source[1].clip_path = clip_get_path (clip2);
             gegl_node_set (edl->source[1].loader, "operation", clip2->is_image?"gegl:load":"gegl:ff-load", "path", edl->source[1].clip_path, NULL);
	     edl->source[1].clip_frame_no = clip2->start - ( clip->end - edl->source[0].clip_frame_no);
           }
	   if (!clip2->is_image)
	     gegl_node_set (edl->source[1].loader, "frame", edl->source[1].clip_frame_no, NULL);
        }
        else 
if (clip->fade_in &&
            (edl->source[0].clip_frame_no - clip->start < clip->fade_pad_start))
        {
           clip2 = l->prev->data;
           edl->mix = (clip->fade_pad_start - (edl->source[0].clip_frame_no - clip->start) ) * 1.0 / clip->fade_pad_start;
           edl->mix /= 2.0;
           edl->source[1].filter_graph = clip2->filter_graph;

           if (edl->source[1].clip_path == NULL || strcmp (clip_get_path (clip2), edl->source[1].clip_path))
           {
             edl->source[1].clip_path = clip_get_path (clip2);
             gegl_node_set (edl->source[1].loader, "operation", clip2->is_image?"gegl:load":"gegl:ff-load", "path", edl->source[1].clip_path, NULL);
	     edl->source[1].clip_frame_no = clip2->end + (edl->source[0].clip_frame_no - clip->start);

           }
	   if (!clip2->is_image) 
	     gegl_node_set (edl->source[1].loader, "frame", edl->source[1].clip_frame_no, NULL);
        }
        else
        {
           edl->mix = 0.0;
        }

        /** this is both where we can keep filter graphs, and do more global
         ** cache short circuiting, this would leave the cross fading to still have
         ** to happen on he fly.. along with audio mix
         **/

        if (edl->source[0].filter_graph)
         {
            if (edl->source[0].cached_filter_graph &&
                !strcmp(edl->source[0].cached_filter_graph,
                        edl->source[0].filter_graph))
            {
            }
            else
            {
            remove_in_betweens (nop_raw, nop_transformed);
            gedl_create_chain (edl, nop_raw, nop_transformed, edl->source[0].filter_graph, edl->source[0].clip_frame_no - clip->end, clip->end - clip->start);
              if (edl->source[0].cached_filter_graph)
                g_free (edl->source[0].cached_filter_graph);
              edl->source[0].cached_filter_graph = g_strdup (edl->source[0].filter_graph);
            }
         }
        else
         {
            remove_in_betweens (nop_raw, nop_transformed);
            if (edl->source[0].cached_filter_graph)
              g_free (edl->source[0].cached_filter_graph);
            edl->source[0].cached_filter_graph = NULL;
            gegl_node_link_many (nop_raw, nop_transformed, NULL);
         }

        if (edl->mix != 0.0 && edl->source[1].filter_graph)
         {
            if (edl->source[1].cached_filter_graph &&
                !strcmp(edl->source[1].cached_filter_graph,
                        edl->source[1].filter_graph))
            {
            }
            else
            {
         
            remove_in_betweens (nop_raw2, nop_transformed2);
            gedl_create_chain (edl, nop_raw2, nop_transformed2, edl->source[1].filter_graph, edl->source[1].clip_frame_no - clip2->end, clip2->end - clip2->start);
              if (edl->source[1].cached_filter_graph)
                g_free (edl->source[1].cached_filter_graph);
              edl->source[1].cached_filter_graph = g_strdup (edl->source[1].filter_graph);
            }
         }
        else
         {
            remove_in_betweens (nop_raw2, nop_transformed2);
            if (edl->source[1].cached_filter_graph)
              g_free (edl->source[1].cached_filter_graph);
            edl->source[1].cached_filter_graph = NULL;
            gegl_node_link_many (nop_raw2, nop_transformed2, NULL);
         }

        frame_recipe = g_strdup_printf ("%s: %s %i %s %s %i %s %ix%i %f",
          "gedl-pre-3", gedl_get_clip_path (edl), gedl_get_clip_frame_no (edl), edl->source[0].filter_graph,
                        gedl_get_clip2_path (edl), gedl_get_clip2_frame_no (edl), edl->source[1].filter_graph,
                        video_width, video_height, 
                        edl->mix);

        hash = g_checksum_new (G_CHECKSUM_MD5);
        g_checksum_update (hash, (void*)frame_recipe, -1);
        cache_path  = g_strdup_printf ("/tmp/gedl/%s", g_checksum_get_string(hash));

        if (g_file_test (cache_path, G_FILE_TEST_IS_REGULAR) && (edl->cache_flags & CACHE_TRY_ALL))
          {
            was_cached = 1;
            gegl_node_set (edl->cache_loader, "path", cache_path, NULL);
            gegl_node_link_many (edl->cache_loader, result, NULL);
            if (!edl->source[0].audio)
              edl->source[0].audio = gegl_audio_fragment_new (44100, 2, 0, 4000);
            gegl_meta_get_audio (cache_path, edl->source[0].audio);
          }
        else
          {

        gegl_node_process (edl->source[0].store_buf);
        if (edl->mix != 0.0)
        {
          gegl_node_process (edl->source[1].store_buf);
        }
        if (edl->source[0].audio)
        {
          g_object_unref (edl->source[0].audio);
          edl->source[0].audio = NULL;
        }
        if (edl->source[1].audio)
        {
          g_object_unref (edl->source[1].audio);
          edl->source[1].audio = NULL;
        }

        if (clip->is_image)
         edl->source[0].audio = NULL;
        else
          gegl_node_get (edl->source[0].loader, "audio", &edl->source[0].audio, NULL);
        if (edl->mix != 0.0)
        {
          /* directly mix the audio from the secondary into the primary, with proportional weighting
           * of samples
           */
          if (clip2->is_image)
            edl->source[1].audio = NULL;
          else
          {
            int i, c;
            gegl_node_get (edl->source[1].loader, "audio", &edl->source[1].audio, NULL);

            for (c = 0; c < gegl_audio_fragment_get_channels (edl->source[0].audio); c++)
            for (i = 0; i < MIN(gegl_audio_fragment_get_sample_count (edl->source[0].audio),
                            gegl_audio_fragment_get_sample_count (edl->source[1].audio)); i++)
            edl->source[0].audio->data[c][i] =
               edl->source[0].audio->data[c][i] * (1.0-edl->mix) +
               edl->source[1].audio->data[c][i] * edl->mix;
          }
        }

          /* write cached render of this frame */
          if (edl->cache_flags & CACHE_MAKE_ALL){ 
            gchar *cache_path = g_strdup_printf ("/tmp/gedl/%s~", g_checksum_get_string(hash));
            gchar *cache_path_final = g_strdup_printf ("/tmp/gedl/%s", g_checksum_get_string(hash));
            if (!g_file_test (cache_path, G_FILE_TEST_IS_REGULAR) &&
                !g_file_test (cache_path_final, G_FILE_TEST_IS_REGULAR))
            {
            GeglNode *save_graph = gegl_node_new ();
            GeglNode *save;
            save = gegl_node_new_child (save_graph, "operation", "gegl:jpg-save", "path", cache_path, NULL);
            gegl_node_link_many (result, save, NULL);
            gegl_node_process (save);
            if (edl->source[0].audio)
              gegl_meta_set_audio (cache_path, edl->source[0].audio);
              rename (cache_path, cache_path_final);
              g_object_unref (save_graph);
            }
            g_free (cache_path);
            g_free (cache_path_final);
          }
        }

          g_checksum_free (hash);
         g_free (frame_recipe);


	if (!was_cached){
	    if (edl->mix != 0.0)
	    {
	       gegl_node_set (load_buf2, "buffer", gedl_get_buffer2 (edl), NULL);
	       gegl_node_connect_to (over, "output", result, "input");
	       gegl_node_set (opacity, "value", edl->mix, NULL);
	    }
	    else
	    {
	       gegl_node_connect_to (crop, "output", result, "input");
	    }
    }

      return;
    }
    clip_start += clip_frames;
  }
  gegl_node_connect_to (crop, "output", result, "input");
  edl->source[0].clip_path = "unknown";
  edl->source[0].clip_frame_no = 0;
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
  return edl->source[0].audio;
}
GeglBuffer *gedl_get_buffer (GeglEDL *edl)
{
  return edl->source[0].buffer;
}
GeglBuffer *gedl_get_buffer2 (GeglEDL *edl)
{
  return edl->source[1].buffer?edl->source[1].buffer:edl->source[0].buffer;
}
double gedl_get_mix (GeglEDL *edl)
{
  return edl->mix;
}

int    gedl_get_frames (GeglEDL *edl)
{
  int count = 0;
  GList *l;
  for (l = edl->clips; l; l = l->next)
  {
    count += clip_get_frames (l->data);
  }
  return count; 
}
#include <string.h>

void frob_fade (Clip *clip)
{
  if (!clip->is_image)
  {
    if (clip->end == 0)
       clip->end = clip->duration;

    if (clip->fade_out)
    {
       clip->fade_pad_end = fade_duration/2;
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
       clip->fade_pad_start = fade_duration/2;
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
     if (!strcmp (key, "fade-duration")) fade_duration = g_strtod (value, NULL);
     if (!strcmp (key, "fps"))         gedl_set_fps (edl, g_strtod (value, NULL));
     if (!strcmp (key, "output-path")) output_path = g_strdup (value);
     if (!strcmp (key, "video-codec")) video_codec = g_strdup (value);
     if (!strcmp (key, "audio-codec")) audio_codec = g_strdup (value);
     if (!strcmp (key, "audio-sample-rate")) audio_samplerate = g_strtod (value, NULL);
     if (!strcmp (key, "video-bufsize")) video_bufsize = g_strtod (value, NULL);
     if (!strcmp (key, "video-bitrate")) video_bitrate = g_strtod (value, NULL);
     if (!strcmp (key, "audio-bitrate")) video_bitrate = g_strtod (value, NULL);
     if (!strcmp (key, "video-width")) video_width = g_strtod (value, NULL);
     if (!strcmp (key, "video-height")) video_height = g_strtod (value, NULL);
     if (!strcmp (key, "frame-start")) frame_start = g_strtod (value, NULL);
     if (!strcmp (key, "frame-end")) frame_end = g_strtod (value, NULL);

     g_free (key);
     return;
   }
  if (strstr (line, "--"))
    rest = strstr (line, "--") + 3;

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
	 GeglNode *probe = gegl_node_new_child (gegl, "operation", "gegl:ff-load", "path", clip->path, NULL);
	 gegl_node_process (probe);

	 gegl_node_get (probe, "frames", &clip->duration, NULL);
	 gegl_node_get (probe, "frame-rate", &clip->fps, NULL);
	 g_object_unref (gegl);
         frob_fade (clip);
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
  for (const char *p = string; p==string || *(p-1); p++)
  {
    switch (*p)
    {
      case 0:
      case '\n':
       gedl_parse_line (edl, line->str);
       g_string_assign (line, "");
       break;
      default: g_string_append_c (line, *p);
       break;
    }
  }
  g_string_free (line, TRUE);

  if ((video_width == 0 || video_height == 0) && edl->clips)
    {
      Clip *clip = edl->clips->data;
      GeglNode *gegl = gegl_node_new ();
      GeglRectangle rect;
      GeglNode *probe = gegl_node_new_child (gegl, "operation", "gegl:ff-load", "path", clip->path, NULL);
      gegl_node_process (probe);
      rect = gegl_node_get_bounding_box (probe);
      video_width = rect.width;
      video_height = rect.height;
      g_object_unref (gegl);
    }
  gedl_set_size (edl, video_width, video_height);

  return edl;
}

void gedl_load_path (GeglEDL *edl, const char *path)
{
  if (edl->path)
    g_free (edl->path);
  edl->path = g_strdup (path);
  video_width = DEFAULT_video_width;
  video_height = DEFAULT_video_height;

  FILE *file = fopen (path, "r");
  if (file)
    {
       char line [4096];
       while (fgets (line, sizeof (line), file))
         gedl_parse_line (edl, line);
       fclose (file);
    }
}

void gedl_save_path (GeglEDL *edl, const char *path)
{
  GList *l;
  fprintf (stderr, "save to %s\n", path);
  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    fprintf (stdout, "%s %i %i%s\n", clip->path, clip->start, clip->end,
                      clip->fade_out?" [fade]":"");
  }
}

void gedl_update_video_size (GeglEDL *edl)
{
  if (video_width == 0 || video_height == 0)
    {
      Clip *clip = edl->clips->data;
      GeglNode *gegl = gegl_node_new ();
      GeglRectangle rect;
      GeglNode *probe = gegl_node_new_child (gegl, "operation", "gegl:ff-load", "path", clip->path, NULL);
      gegl_node_process (probe);
      rect = gegl_node_get_bounding_box (probe);
      video_width = rect.width;
      video_height = rect.height;
      g_object_unref (gegl);
    }
}

GeglEDL *gedl_new_from_path (const char *path)
{
  GeglEDL *edl = gedl_new ();

  gedl_load_path (edl, path);
  gedl_update_video_size (edl);
  gedl_set_size (edl, video_width, video_height);

  return edl;
}
const char *gedl_get_clip_path (GeglEDL *edl)
{
  return edl->source[0].clip_path;
}
int gedl_get_clip_frame_no    (GeglEDL *edl)
{
  return edl->source[0].clip_frame_no;
}
const char *gedl_get_clip2_path          (GeglEDL *edl)
{
  return edl->source[1].clip_path;
}
int gedl_get_clip2_frame_no      (GeglEDL *edl)
{
  return edl->source[1].clip_frame_no;
}


static void setup (void)
{
  gegl = gegl_node_new ();
  result = gegl_node_new_child (gegl, "operation", "gegl:nop", NULL);
  load_buf = gegl_node_new_child (gegl, "operation", "gegl:buffer-source", NULL);
  load_buf2 = gegl_node_new_child (gegl, "operation", "gegl:buffer-source", NULL);
  crop = gegl_node_new_child (gegl, "operation", "gegl:crop", "x", 0.0, "y", 0.0, "width", 1.0 * edl->width,
                                    "height", 1.0 * edl->height, NULL);
  crop2 = gegl_node_new_child (gegl, "operation", "gegl:crop", "x", 0.0, "y", 0.0, "width", 1.0 * edl->width,
                                    "height", 1.0 * edl->height, NULL);

  over = gegl_node_new_child (gegl, "operation", "gegl:over", NULL);

  nop_raw = gegl_node_new_child (gegl, "operation", "gegl:nop", NULL);
  nop_raw2 = gegl_node_new_child (gegl, "operation", "gegl:nop", NULL);

  nop_transformed = gegl_node_new_child (gegl, "operation", "gegl:nop", NULL);
  nop_transformed2 = gegl_node_new_child (gegl, "operation", "gegl:nop", NULL);

  opacity = gegl_node_new_child (gegl, "operation", "gegl:opacity", NULL);


  scale_size = gegl_node_new_child (gegl, "operation", "gegl:scale-size",
                                    "x", 1.0 * edl->width,
                                    "y", 1.0 * edl->height, NULL);

  scale_size2 = gegl_node_new_child (gegl, "operation", "gegl:scale-size",
                                    "x", 1.0 * edl->width,
                                    "y", 1.0 * edl->height, NULL);

  encode = gegl_node_new_child (gegl, "operation", "gegl:ff-save",
                                      "path",           output_path,
                                      "frame-rate",     gedl_get_fps (edl),
                                      "video-bit-rate", video_bitrate,
                                      "video-bufsize",  video_bufsize,
                                      "audio-bit-rate", audio_bitrate,
                                      "audio-codec",    audio_codec,
                                      "video-codec",    video_codec,
                                      NULL);

  gegl_node_link_many (result, encode, NULL); 
  gegl_node_link_many (load_buf, scale_size, nop_raw, nop_transformed, crop, NULL); 
  gegl_node_link_many (load_buf2, scale_size2, nop_raw2, nop_transformed2, opacity, crop2,  NULL); 

  gegl_node_connect_to (nop_raw, "output", nop_transformed, "input");
  gegl_node_connect_to (nop_raw2, "output", nop_transformed2, "input");

  gegl_node_connect_to (crop2, "output", over, "aux");
  gegl_node_connect_to (crop, "output", over, "input");

  gegl_node_connect_to (over, "output", result, "input");
}

int clip_frame_no = 0;
const char *clip_path = NULL;

void rig_frame (int frame_no);
void rig_frame (int frame_no)
{
  if (edl->frame == frame_no)
    return;
  gedl_set_frame (edl, frame_no);

  clip_path = gedl_get_clip_path (edl);
  clip_frame_no = gedl_get_clip_frame_no (edl);

  gegl_node_set (load_buf, "buffer", gedl_get_buffer (edl), NULL);
  gegl_node_set (encode, "audio", gedl_get_audio (edl), NULL);
}

int skip_encode = 0;

static void process_frame (int frame_no)
{
  rig_frame (frame_no);
  if (!skip_encode)
    gegl_node_process (encode);
}

static void teardown (void)
{
  gedl_free (edl);
  g_object_unref (gegl);
}

static void init (int argc, char **argv)
{
  gegl_init (&argc, &argv);
  g_object_set (gegl_config (),
                "application-license", "GPL3",
                NULL);
}

static void process_frames (void)
{
  int frame_no;
  for (frame_no = frame_start; frame_no <= frame_end; frame_no++)
  {
    process_frame (frame_no);
    fprintf (stderr, "\r%1.2f%% %04d / %04d %s#%04d  [%s][%s]  ",
     100.0 * (frame_no-frame_start) * 1.0 / (frame_end - frame_start),
     frame_no, frame_end, clip_path, clip_frame_no, edl->source[0].filter_graph, edl->source[1].filter_graph); 
  }
}

int gegl_make_thumb_video (const char *path, const char *thumb_path)
{
  int tot_frames;
  GString *str = g_string_new ("");
  g_string_append_printf (str, "video-bitrate=100\n\noutput-path=%s\nvideo-width=320\nvideo-height=240\n\n%s\n", thumb_path, path);
  edl = gedl_new_from_string (str->str);
  setup ();
  tot_frames = gedl_get_frames (edl);
  if (frame_end == 0)
    frame_end = tot_frames-1;
  process_frames ();
  teardown ();
  g_string_free (str, TRUE);
  return 0;
}


int gedl_ui_main (GeglEDL *edl);
int main (int argc, char **argv)
{
  int tot_frames;
  init (argc, argv);
  if (!argv[1])
  {
    fprintf (stderr, "usage: %s <input.edl> [output.mp4]\n", argv[0]);
    return -1;
  }

  if (argv[1] && argv[2] && argv[3] && !strcmp (argv[1], "--make-proxy"))
     return gegl_make_thumb_video (argv[2], argv[3]);

  edl_path = argv[1];
  if (argv[2])
    output_path = argv[2];
  edl = gedl_new_from_path (edl_path);
  setup ();

  for (int i = 1; argv[i]; i++)
    if (!strcmp (argv[i], "-c"))
       skip_encode = 1;
    else
    if (!strcmp (argv[i], "-ui"))
      return gedl_ui_main (edl);

  tot_frames  = gedl_get_frames (edl);
  if (frame_end == 0)
    frame_end = tot_frames-1;
  process_frames ();
  teardown ();
  return 0;
}

char *gedl_serialise (GeglEDL *edl)
{
  GList *l;
  char *ret;
  GString *ser = g_string_new ("");

  if (strcmp(output_path, DEFAULT_output_path))
    g_string_append_printf (ser, "output-path=%s\n", output_path);
  if (strcmp(video_codec, DEFAULT_video_codec))
    g_string_append_printf (ser, "video-codec=%s\n", video_codec);
  if (strcmp(audio_codec, DEFAULT_audio_codec))
    g_string_append_printf (ser, "audio-codec=%s\n", audio_codec);
  if (video_width != DEFAULT_video_width)
    g_string_append_printf (ser, "video-width=%i\n",  video_width);
  if (video_height != DEFAULT_video_height)
    g_string_append_printf (ser, "video-height=%i\n",  video_height);
  if (video_bufsize != DEFAULT_video_bufsize)
    g_string_append_printf (ser, "video-bufsize=%i\n",  video_bufsize);
  if (video_bitrate != DEFAULT_video_bitrate)
    g_string_append_printf (ser, "video-bitrate=%i\n",  video_bitrate);
  if (video_tolerance != DEFAULT_video_tolerance)
    g_string_append_printf (ser, "video-tolerance=%i\n",  video_tolerance);
  if (audio_bitrate != DEFAULT_audio_bitrate)
    g_string_append_printf (ser, "audio-bitrate=%i\n",  audio_bitrate);
  if (audio_samplerate != DEFAULT_audio_samplerate)
    g_string_append_printf (ser, "audio-samplerate=%i\n",  audio_samplerate);
  if (fade_duration != DEFAULT_fade_duration)
    g_string_append_printf (ser, "fade-duration=%i\n",  fade_duration);
  if (frame_start != DEFAULT_frame_start)
    g_string_append_printf (ser, "frame-start=%i\n",  frame_start);
  if (frame_end != DEFAULT_frame_end)
    g_string_append_printf (ser, "frame-end=%i\n",  frame_end);
  
  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    g_string_append_printf (ser, "%s %d %d%s%s%s\n", clip->path, clip->start, clip->end, clip->fade_out?" [fade]":"", clip->filter_graph?" -- ":"",clip->filter_graph?clip->filter_graph:"");
 
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
