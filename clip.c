#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <gegl.h>
#include <gegl-audio-fragment.h>
#include "gedl.h"

Clip *clip_new (GeglEDL *edl)
{
  Clip *clip = g_malloc0 (sizeof (Clip));
  clip->edl  = edl;
  clip->gegl = gegl_node_new ();
  clip->full_loader  = gegl_node_new_child (clip->gegl, "operation", "gegl:ff-load", NULL);
  clip->proxy_loader = gegl_node_new_child (clip->gegl, "operation", "gegl:ff-load", NULL);
  clip->loader       = gegl_node_new_child (clip->gegl, "operation", "gegl:nop", NULL);

  clip->load_buf = gegl_node_new_child (clip->gegl, "operation", "gegl:nop", NULL);
  clip->crop_proxy = gegl_node_new_child (clip->gegl, "operation", "gegl:crop", NULL);
  clip->crop     = gegl_node_new_child (clip->gegl, "operation", "gegl:crop", NULL);
  clip->nop_raw = gegl_node_new_child (clip->gegl, "operation", "gegl:scale-size-keepaspect",
                                       "sampler", GEDL_SAMPLER,
                                       NULL);

  clip->nop_transformed = gegl_node_new_child (clip->gegl, "operation", "gegl:nop", NULL);

  gegl_node_link_many (clip->full_loader, clip->loader, NULL);
  gegl_node_link_many (clip->load_buf, clip->nop_raw, clip->nop_transformed, clip->crop, NULL);

  g_mutex_init (&clip->mutex);

  return clip;
}

void clip_free (Clip *clip)
{
  if (clip->path)
    g_free (clip->path);
  clip->path = NULL;

  if (clip->gegl)
    g_object_unref (clip->gegl);
  clip->gegl = NULL;
  g_mutex_clear (&clip->mutex);
  g_free (clip);
}

void clip_set_path (Clip *clip, const char *in_path)
{
  char *path = NULL;
  if (in_path[0] == '/' || !strcmp (in_path, "black"))
  {
    path = g_strdup (in_path);
  }
  else
  {
    if (clip->edl->parent_path)
      path = g_strdup_printf ("%s%s", clip->edl->parent_path, in_path);
    else
      path = g_strdup_printf ("%s", in_path);
  }

  if (clip->path && !strcmp (clip->path, path))
  {
    g_free (path);
    return;
  }

  if (clip->path)
    g_free (clip->path);
  clip->path = path;


  if (g_str_has_suffix (path, ".png") ||
      g_str_has_suffix (path, ".jpg") ||
      g_str_has_suffix (path, ".exr") ||
      g_str_has_suffix (path, ".EXR") ||
      g_str_has_suffix (path, ".PNG") ||
      g_str_has_suffix (path, ".JPG"))
   {
     g_object_set (clip->full_loader, "operation", "gegl:load", NULL);
     clip->static_source = 1;
   }
  else
   {
     if (strstr (path, "black"))
     {
       GeglColor *color = g_object_new (GEGL_TYPE_COLOR, "string", "black", NULL);
       g_object_set (clip->full_loader, "operation", "gegl:color", NULL);
       g_object_set (clip->proxy_loader, "operation", "gegl:color", NULL);
       gegl_node_set (clip->full_loader, "value", color, NULL);
       gegl_node_set (clip->proxy_loader, "value", color, NULL);
     }
     else
     {
       g_object_set (clip->full_loader, "operation", "gegl:ff-load", NULL);
     }
     clip->static_source = 0;
   }
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

Clip *clip_new_full (GeglEDL *edl, const char *path, int start, int end)
{
  Clip *clip = clip_new (edl);
  clip_set_full (clip, path, start, end);
  return clip;
}

void clip_fade_set (Clip *clip, int do_fade_out)
{
  /*  should cancel any computations due to fade when cancelling it, and add them when fade is set
   */
}

const char *clip_get_path (Clip *clip)
{
  return clip->path;
}

static void clip_set_proxied (Clip *clip)
{
  if (clip->edl->use_proxies)
    {
      char *path = gedl_make_proxy_path (clip->edl, clip->path);
      gchar *old = NULL;
      gegl_node_get (clip->proxy_loader, "path", &old, NULL);

      if (!old || !strcmp (old, "") || !strcmp (path, old))
        gegl_node_set (clip->proxy_loader, "path", path, NULL);
      gegl_node_link_many (clip->proxy_loader, clip->loader, NULL);
      g_free (path);
    }
  else
    {
      gchar *old = NULL;
      gegl_node_get (clip->full_loader, "path", &old, NULL);
      if (!old || !strcmp (old, "") || !strcmp (clip->path, old))
        gegl_node_set (clip->full_loader, "path", clip->path, NULL);
      gegl_node_link_many (clip->full_loader, clip->loader, NULL);
    }
}

void clip_set_frame_no (Clip *clip, int clip_frame_no)
{
  if (clip_frame_no < 0)
    clip_frame_no = 0;

  clip_set_proxied (clip);

  if (!clip_is_static_source (clip))
    {
      if (clip->edl->use_proxies)
        gegl_node_set (clip->proxy_loader, "frame", clip_frame_no, NULL);
      else
        gegl_node_set (clip->full_loader, "frame", clip_frame_no, NULL);
    }
}

int clip_is_static_source (Clip *clip)
{
  return clip->static_source;
}

void clip_fetch_audio (Clip *clip)
{
  int use_proxies = clip->edl->use_proxies;

  if (clip->audio)
    {
      g_object_unref (clip->audio);
      clip->audio = NULL;
    }

  if (clip_is_static_source (clip))
    clip->audio = NULL;
  else
    {
      if (use_proxies)
        gegl_node_get (clip->proxy_loader, "audio", &clip->audio, NULL);
      else
        gegl_node_get (clip->full_loader, "audio", &clip->audio, NULL);
    }
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

void clip_render_frame (Clip *clip, int clip_frame_no, const char *cache_path)
{
  GeglEDL *edl = clip->edl;
  int use_proxies = edl->use_proxies;
  g_mutex_lock (&clip->mutex);

  remove_in_betweens (clip->nop_raw, clip->nop_transformed);
  gegl_node_link_many (clip->nop_raw, clip->nop_transformed, NULL);

  gegl_node_set (clip->nop_raw, "operation", "gegl:scale-size-keepaspect",
                               "y", 0.0, //
                               "x", 1.0 * edl->width,
                               "sampler", use_proxies?GEDL_SAMPLER:GEGL_SAMPLER_CUBIC,
                               NULL);

  gegl_node_set (clip->crop, "width", 1.0 * edl->width,
                 "height", 1.0 * edl->height, NULL);

      if (clip->filter_graph)
        {
           GError *error = NULL;
           gegl_create_chain (clip->filter_graph, clip->nop_raw, clip->nop_transformed, clip_frame_no - clip->start, edl->height, NULL, &error);
           if (error)
             {
               /* should set error string */
               fprintf (stderr, "%s\n", error->message);
               g_error_free (error);
             }
         }
      /**********************************************************************/

      gegl_node_link_many (clip->loader, clip->load_buf, NULL);
      gegl_node_link_many (clip->crop, edl->result, NULL);
      clip_set_frame_no (clip, clip_frame_no);

      gegl_node_process (edl->store_final_buf);

      clip_fetch_audio (clip);

      /* write cached render of this frame for this clip */
      if (!strstr (clip->path, ".gedl/cache") && (!use_proxies))
        {
          const gchar *cache_path_final = cache_path;
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
}

gchar *clip_get_frame_hash (Clip *clip, int clip_frame_no)
{
  GeglEDL *edl = clip->edl;
  gchar *frame_recipe;
  GChecksum *hash;
  int is_static_source = clip_is_static_source (clip);

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
