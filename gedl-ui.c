/*

todo: prime cache frames when navigating clips, shared with raw edits of same
frames appearing in timeline

 */

#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include <string.h>
#include <stdio.h>
#include <mrg.h>
#include <gegl.h>
#include "gedl.h"
#include <SDL.h>
#include <gegl-audio-fragment.h>

#define SPLIT_VER  0.8

static int exited = 0;
static GThread *thread;
long babl_ticks (void);

void frob_fade (void*);
static unsigned char *copy_buf = NULL;
static int copy_buf_len = 0;

static int changed = 0;

GeglNode *preview_loader;

int rendering_frame = -1;
int done_frame     = -1;

static void gedl_cache_invalid (GeglEDL *edl)
{
  edl->frame = -1;
  done_frame=-1;
  rendering_frame=-1;
  changed++;
}

static int audio_len    = 0;
static int audio_pos    = 0;
static int audio_post   = 0;

#define AUDIO_BUF_LEN 819200000

int16_t audio_data[AUDIO_BUF_LEN];

static void sdl_audio_cb(void *udata, Uint8 *stream, int len)
{
  int audio_remaining = audio_len - audio_pos;
  if (audio_remaining < 0)
    return;

  if (audio_remaining < len) len = audio_remaining;

  //SDL_MixAudio(stream, (uint8_t*)&audio_data[audio_pos/2], len, SDL_MIX_MAXVOLUME);
  memcpy (stream, (uint8_t*)&audio_data[audio_pos/2], len);
  audio_pos += len;
  audio_post += len;
  if (audio_pos >= AUDIO_BUF_LEN)
  {
    audio_pos = 0;
  }
}

static void sdl_add_audio_sample (int sample_pos, float left, float right)
{
   audio_data[audio_len/2 + 0] = left * 32767.0 * 0.46;
   audio_data[audio_len/2 + 1] = right * 32767.0 * 0.46;
   audio_len += 4;

   if (audio_len >= AUDIO_BUF_LEN)
   {
     audio_len = 0;
   }
}

static int audio_started = 0;

static void open_audio (int frequency)
{
  SDL_AudioSpec spec = {0};
  SDL_Init(SDL_INIT_AUDIO);
  spec.freq = frequency;
  spec.format = AUDIO_S16SYS;
  spec.channels = 2;
  spec.samples = 1024;
  spec.callback = sdl_audio_cb;
  SDL_OpenAudio(&spec, 0);

  if (spec.format != AUDIO_S16SYS)
   {
      fprintf (stderr, "not getting format we wanted\n");
   }
  if (spec.freq != frequency)
   {
      fprintf (stderr, "not getting desires samplerate(%i) we wanted got %i instead\n", frequency, spec.freq);
   }
}

static void end_audio (void)
{
}


void playing_iteration (Mrg *mrg, GeglEDL *edl);

static gpointer renderer_thread (gpointer data)
{
  /* XXX: the renderer thread should keep more soon to be useful frames available */

  GeglEDL *edl = data;
  for (;;)
  {
    playing_iteration (edl->mrg, edl);
    if (edl->active_source)
    {
      if (edl->source_frame_no != done_frame)
      {
        rendering_frame = edl->source_frame_no;
        gegl_node_set (preview_loader, "path", edl->active_source->path, NULL);
        gegl_node_set (preview_loader, "frame", edl->source_frame_no, NULL);
        GeglRectangle ext = gegl_node_get_bounding_box (preview_loader);
        gegl_buffer_set_extent (edl->buffer, &ext);
        gegl_node_process (edl->source_store_buf);

        done_frame = rendering_frame;
#if 0
        MrgRectangle rect = {mrg_width (edl->mrg)/2, 0,
                             mrg_width (edl->mrg)/2, mrg_height (edl->mrg) * SPLIT_VER};
        mrg_queue_draw (edl->mrg, &rect);
#endif
      }
      else
        g_usleep (50);
    }
    else
    {
      if (edl->frame_no != done_frame)
      {
        GeglRectangle ext = {0, 0, edl->width, edl->height }; //gegl_node_get_bounding_box (edl->result);
        rendering_frame = edl->frame_no;
        gegl_buffer_set_extent (edl->buffer, &ext);

        rig_frame (edl, edl->frame_no); /* this does the frame-set, which causes render  */

        {
          GeglAudioFragment *audio = gedl_get_audio (edl);
if (audio)
{
       int sample_count = gegl_audio_fragment_get_sample_count (audio);
       if (sample_count > 0)
       {
         int i;
         if (!audio_started)
         {
           open_audio (gegl_audio_fragment_get_sample_rate (audio));
           SDL_PauseAudio(0);
           audio_started = 1;
         }
         for (i = 0; i < sample_count; i++)
         {
           sdl_add_audio_sample (0, audio->data[0][i], audio->data[1][i]);
         }
        // while (audio_len > audio_pos + 5000)
        //   g_usleep (50);
       }
}
        }

        /* this set edl->frame */
        //gegl_node_process (edl->store_buf);

        done_frame = rendering_frame;
        MrgRectangle rect = {mrg_width (edl->mrg)/2, 0,
                             mrg_width (edl->mrg)/2, mrg_height (edl->mrg) * SPLIT_VER};
        mrg_queue_draw (edl->mrg, &rect);
      }
      else
      {
        g_usleep (50);
      }
    }
  }
  return NULL;
}

static void mrg_gegl_blit (Mrg *mrg,
                          float x0, float y0,
                          float width, float height,
                          GeglNode *node,
                          float u, float v,
                          float opacity)
{
  GeglRectangle bounds;

  cairo_t *cr = mrg_cr (mrg);
  cairo_surface_t *surface = NULL;

  if (!node)
    return;

  bounds = gegl_node_get_bounding_box (node);

  if (width == -1 && height == -1)
  {
    width  = bounds.width;
    height = bounds.height;
  }

  if (width == -1)
    width = bounds.width * height / bounds.height;
  if (height == -1)
    height = bounds.height * width / bounds.width;

  if (copy_buf_len < width * height * 4)
  {
    if (copy_buf)
      free (copy_buf);
    copy_buf_len = width * height * 4;
    copy_buf = malloc (copy_buf_len);
  }
  {
    static int foo = 0;
    unsigned char *buf = copy_buf;
    GeglRectangle roi = {u, v, width, height};
    static const Babl *fmt = NULL;

foo++;
    if (!fmt) fmt = babl_format ("cairo-RGB24");

    {
      float scale = 1.0;
      scale = width / bounds.width;
      if (height / bounds.height < scale)
        scale = height / bounds.height;

      gegl_node_blit (node, scale, &roi, fmt, buf, width * 4, 
                      GEGL_BLIT_DEFAULT);
    }

  surface = cairo_image_surface_create_for_data (buf, CAIRO_FORMAT_RGB24, width, height, width * 4);
  }

  cairo_save (cr);
  cairo_surface_set_device_scale (surface, 1.0, 1.0);

  cairo_rectangle (cr, x0, y0, width, height);

  cairo_clip (cr);
  cairo_translate (cr, x0, y0);
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_NEAREST);
  cairo_set_source_surface (cr, surface, 0, 0);
   
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);

  if (opacity < 0.9)
  {
    cairo_paint_with_alpha (cr, opacity);
  }
  else
  {
    cairo_paint (cr);
  }
  cairo_surface_destroy (surface);
  cairo_restore (cr);

}


typedef struct _State State;

struct _State {
  void   (*ui) (Mrg *mrg, void *state);
  Mrg     *mrg;
  GeglEDL *edl;
  char    *path;
  char    *save_path;
};

static int playing  = 0;
float fpx           = 2;

static void *prev_sclip = NULL;

void make_active_source (GeglEDL *edl, SourceClip *clip);
void make_active_source (GeglEDL *edl, SourceClip *clip)
{
  if (edl->active_source)
    edl->active_source->editing = 0;
  edl->active_clip = NULL;
  edl->active_source = clip;
  edl->active_source->editing = 0;
  edl->source_frame_no = clip->start;
  edl->selection_start = clip->start;
  edl->selection_end = clip->end;
}

static void clicked_source_clip (MrgEvent *e, void *data1, void *data2)
{
  SourceClip *clip = data1;
  GeglEDL *edl = data2;

  if (prev_sclip != clip)
  {
    make_active_source (edl, clip);
  }
  else
  {
    edl->active_clip = NULL;
    edl->active_source = clip;
    edl->source_frame_no = e->x;
  }
  mrg_queue_draw (e->mrg, NULL);
  prev_sclip = clip;
  changed++;
}

static void clicked_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;
  
  edl->frame_no = e->x;
  edl->selection_start = edl->frame_no;
  edl->selection_end = edl->frame_no;
  edl->active_clip = clip;
  edl->active_source = NULL;
  playing = 0;
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

#include <math.h>

static void drag_source_clip (MrgEvent *e, void *data1, void *data2)
{
  GeglEDL *edl = data2;
  if (fabs(e->delta_x) > 3 ||
      fabs(e->delta_y) > 3)
      {
        fprintf (stderr, "!!!!\n");
        edl->source_frame_no = e->x;
      }
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void drag_clip (MrgEvent *e, void *data1, void *data2)
{
  GeglEDL *edl = data2;
  edl->frame_no = e->x;
  if (e->x >= edl->selection_start)
  {
    edl->selection_end = e->x;
  }
  else
  {
    edl->selection_start = e->x;
  }
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void drag_t0 (MrgEvent *e, void *data1, void *data2)
        {
  GeglEDL *edl = data2;
  edl->t0 += e->delta_x;
  mrg_queue_draw (e->mrg, NULL);
  mrg_event_stop_propagate (e);
  changed++;
}

static void drag_fpx (MrgEvent *e, void *data1, void *data2)
{
  GeglEDL *edl = data2;
  edl->scale = (mrg_width(e->mrg)*edl->scale + e->delta_x) / mrg_width(e->mrg);
  mrg_queue_draw (e->mrg, NULL);
  mrg_event_stop_propagate (e);
  changed++;
}

static void released_source_clip (MrgEvent *e, void *data1, void *data2)
{
}

static void released_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;
  edl->frame_no = e->x;
  edl->active_clip = clip;
  edl->active_source = NULL;
  if (edl->selection_end < edl->selection_start)
  {
    int temp = edl->selection_end;
    edl->selection_end = edl->selection_start;
    edl->selection_start = temp;
  }
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void stop_playing (MrgEvent *event, void *data1, void *data2)
{
  playing = 0;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void select_all (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_set_selection (edl, 0, gedl_get_duration (edl));
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void extend_selection_to_the_right (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int sel_start, sel_end;

  gedl_get_selection (edl, &sel_start, &sel_end);
  if (edl->frame_no == sel_end)
  {
    sel_end ++;
    edl->frame_no ++;
  }
  else if (edl->frame_no == sel_start)
  {
    sel_start ++;
    edl->frame_no ++;
  }
  else
  {
    sel_start = sel_end = edl->frame_no;
    sel_end ++;
    edl->frame_no ++;
  }
  gedl_set_selection (edl, sel_start, sel_end);

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}
static Clip * edl_get_clip_for_frame (GeglEDL *edl, int frame);

static void insert (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;

  if (edl->active_source)
  {
    GList *iter = g_list_find (edl->clip_db, edl->active_source);
    Clip *clip = clip_new_full (edl, edl->active_source->path, edl->active_source->start, edl->active_source->end);
    if (edl->active_source->title)
      clip->title = g_strdup (edl->active_source->title);
    
    iter = g_list_find (edl->clips, edl_get_clip_for_frame (edl, edl->frame_no));
    edl->clips = g_list_insert_before (edl->clips, iter, clip);

    edl->active_clip = clip;
    edl->active_source = NULL;
    gedl_cache_invalid (edl);
    mrg_event_stop_propagate (event);
    mrg_queue_draw (event->mrg, NULL);
    return;
  }
}

static void extend_selection_to_the_left (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int sel_start, sel_end;

  gedl_get_selection (edl, &sel_start, &sel_end);
  if (edl->frame_no == sel_end)
  {
    sel_end --;
    edl->frame_no --;
  }
  else if (edl->frame_no == sel_start)
  {
    sel_start --;
    edl->frame_no --;
  }
  else
  {
    sel_start = sel_end = edl->frame_no;
    sel_end --;
    edl->frame_no --;
  }
  gedl_set_selection (edl, sel_start, sel_end);

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}
static void remove_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
    GList *iter = g_list_find (edl->clip_db, edl->active_source);
    if (iter) iter = iter->next;
    //if (!iter)
    //  return;
    edl->clip_db = g_list_remove (edl->clip_db, edl->active_source);
    if (iter) edl->active_source = iter->data;
    else
        edl->active_source = g_list_last (edl->clip_db)?
                             g_list_last (edl->clip_db)->data:NULL;
    gedl_cache_invalid (edl);
    mrg_event_stop_propagate (event);
    mrg_queue_draw (event->mrg, NULL);
    return;
  }

  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter->next) iter = iter->next;
    else if (iter->prev) iter = iter->prev;
    else 
      return;
    edl->clips = g_list_remove (edl->clips, edl->active_clip);
    if (iter) edl->active_clip = iter->data;
    else
        edl->active_clip = NULL;
    frob_fade (edl->active_clip);
  }
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void toggle_use_proxies (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;

  if (!playing) // disallowing - to avoid some races
  {
    gedl_set_use_proxies (edl, edl->use_proxies?0:1);
    gedl_cache_invalid (edl);
  }

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void split_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int clip_frame_no = 0;
  Clip *clip = gedl_get_clip (edl, edl->frame_no, &clip_frame_no);
  if (!edl->active_clip)
    return;

  if (edl->active_clip !=clip)
  {
    g_warning ("hmmm");
    return;
  }

  {
    int shift;
    Clip *oldclip = edl->active_clip;
    GList *iter = g_list_find (edl->clips, oldclip);
    Clip *clip = clip_new_full (edl, oldclip->path, oldclip->start, oldclip->end);
    edl->clips = g_list_insert_before (edl->clips, iter, clip);
    if (oldclip->filter_graph)
      clip->filter_graph = g_strdup (oldclip->filter_graph);

    shift = clip_frame_no;
    clip->end       =  shift;
    oldclip->start  =  shift + 1;

    edl->active_clip = clip;

  //  frob_fade (clip);
  //  frob_fade (oldclip);
  }
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void duplicate_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
    GList *iter = g_list_find (edl->clip_db, edl->active_source);
    Clip *clip = clip_new_full (edl, edl->active_source->path, edl->active_source->start, edl->active_source->end);
    if (edl->active_source->title)
      clip->title = g_strdup (edl->active_source->title);

    edl->clip_db = g_list_insert_before (edl->clip_db, iter, clip);
    edl->active_source = (void*)clip;
    gedl_cache_invalid (edl);
    mrg_event_stop_propagate (event);
    mrg_queue_draw (event->mrg, NULL);
    changed++;
    return;
  }

  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    Clip *clip = clip_new_full (edl, edl->active_clip->path, edl->active_clip->start, edl->active_clip->end);
    edl->clips = g_list_insert_before (edl->clips, iter, clip);
    frob_fade (edl->active_clip);
    if (edl->active_clip->filter_graph)
      clip->filter_graph = g_strdup (edl->active_clip->filter_graph);
    edl->active_clip = clip;
    frob_fade (edl->active_clip);
  }
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void nav_left (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter) iter = iter->prev;
    if (iter) edl->active_clip = iter->data;
    edl->frame_no = edl->active_clip->abs_start + 1;
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void nav_right (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter) iter = iter->next;
    if (iter) edl->active_clip = iter->data;
    edl->frame_no = edl->active_clip->abs_start + 1;
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static int help = 0;

static void toggle_help (MrgEvent *event, void *data1, void *data2)
{
  //GeglEDL *edl = data1;
  help = help ? 0 : 1;
  mrg_queue_draw (event->mrg, NULL);
}

static void toggle_fade (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!edl->active_clip)
    return;
  edl->active_clip->fade_out = !edl->active_clip->fade_out;
  frob_fade (edl->active_clip);
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter) iter = iter->next;
    if (iter) {
      Clip *clip2 = iter->data;
      clip2->fade_in = edl->active_clip->fade_out;
      frob_fade (clip2);
    }
  }
  if (!edl->active_clip->fade_out)
    edl->active_clip->fade_pad_end = 0;
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void save_edl (GeglEDL *edl)
{
  if (edl->path)
  {
    gedl_save_path (edl, edl->path);
    //fprintf (stderr, "saved to %s\n", edl->path);
  }
}

static void save (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  save_edl (edl);
}

static gboolean save_idle (gpointer edl)
{
  if (changed)
  {
    changed = 0;
    save_edl (edl);
  }
  return TRUE;
}

static void set_range (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int start, end;

  gedl_get_selection (edl, &start, &end);
  gedl_set_range (edl, start, end);
  mrg_queue_draw (event->mrg, NULL);
}



static void up (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip && edl->filter_edited)
    return;
  if (edl->clip_query_edited)
  {
    edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
    edl->clip_query_edited = 0;
  }
  else if (edl->active_source)
  {
    GList *l;
    int found = 0;
    edl->active_source->editing = 0;
    for (l = edl->clip_db; l; l = l->next)
    {
      if (l->next && l->next->data == edl->active_source)
      {
        edl->active_source = l->data;
        make_active_source (edl, edl->active_source);
        found = 1;
        break;
      }
    }
    if (!found)
    {
      edl->active_source = NULL;
      //edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
      edl->clip_query_edited = 1;
      mrg_set_cursor_pos (event->mrg, strlen (edl->clip_query));
    }
  }
  else if (edl->active_clip)
  {
    if(edl->filter_edited)
    {
      edl->active_source = edl->clip_db?g_list_last (edl->clip_db)->data:NULL;
      make_active_source (edl, edl->active_source);
      edl->filter_edited = 0;
      edl->active_clip = NULL;
    }
    else
    {
      edl->active_source = NULL;
      edl->filter_edited = 1;
      mrg_set_cursor_pos (event->mrg, 0);
      fprintf (stderr, "...\n");
    }
  }
  else
  {
          fprintf (stderr, "uh\n");
  }
  mrg_queue_draw (event->mrg, NULL);
  mrg_event_stop_propagate (event);
  changed++;
}

static void down (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip && edl->filter_edited)
    return;
  if (edl->clip_query_edited)
  {
    edl->active_source = edl->clip_db->data;
    make_active_source (edl, (void*)edl->active_source);
    edl->clip_query_edited = 0;
  }
  else if (edl->active_source)
  {
    GList *l;
    int found = 0;
    edl->active_source->editing = 0;
    for (l = edl->clip_db; l; l = l->next)
    {
      if (l->next && l->data == edl->active_source)
      {
        edl->active_source = l->next->data;
        make_active_source (edl, l->next->data);
        found = 1;
        break;
      }
    }
    if (!found)
    {
      edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
      edl->active_source = NULL;
    }
  }
  else
  {
    edl->active_clip = NULL;
    edl->clip_query_edited = 1;
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void step_frame_back (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  stop_playing (event, data1, data2);
  if (edl->active_source)
    edl->source_frame_no --;
  else
  {
    edl->frame_no --;
    edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void step_frame (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  stop_playing (event, data1, data2);
  if (edl->active_source)
  {
    edl->source_frame_no ++;
  }
  else
  {
    edl->frame_no ++;
    edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void clip_end_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
      edl->active_source->end++;
  }
  else if (edl->active_clip)
    {
      edl->active_clip->end++;
    }
      gedl_cache_invalid (edl);
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
}

static void clip_end_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
      edl->active_source->end--;
      gedl_cache_invalid (edl);
  }
  else if (edl->active_clip)
    {
      edl->active_clip->end--;
      gedl_cache_invalid (edl);
    }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void clip_start_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
      edl->active_source->start++;
      gedl_cache_invalid (edl);
  }
  else if (edl->active_clip)
    {
      edl->active_clip->start++;
      gedl_cache_invalid (edl);
    }
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
}

static void clip_start_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
      edl->active_source->start--;
      gedl_cache_invalid (edl);

  }
  else if (edl->active_clip)
    {
      edl->active_clip->start--;
      gedl_cache_invalid (edl);
    }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void toggle_edit_source (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  edl->active_source->editing = !edl->active_source->editing;
  if (edl->active_source->editing)
    mrg_set_cursor_pos (event->mrg, strlen (edl->active_source->title));
  gedl_cache_invalid (edl);
  mrg_queue_draw (event->mrg, NULL);
}


static void do_quit (MrgEvent *event, void *data1, void *data2)
{
  exited = 1;
  mrg_quit (event->mrg);
}

long last_frame = 0;

static Clip * edl_get_clip_for_frame (GeglEDL *edl, int frame)
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

static int max_frame (GeglEDL *edl)
{
  GList *l;
  int t = 0;
  int start, end;

  gedl_get_range (edl, &start, &end);
  if (end)
    return end;

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    t += clip_get_frames (clip);
  }

  return t;
}

void gedl_ui (Mrg *mrg, void *data);

static void zoom_timeline (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  switch (event->scroll_direction)
  {
    case MRG_SCROLL_DIRECTION_UP:
      edl->scale *= 1.02;
      break;
    case MRG_SCROLL_DIRECTION_DOWN:
      edl->scale /= 1.02;
      break;
    case MRG_SCROLL_DIRECTION_LEFT:
      edl->t0 += edl->scale * 2;
      break;
    case MRG_SCROLL_DIRECTION_RIGHT:
      edl->t0 -= edl->scale * 2;
      break;
  }
  mrg_queue_draw (event->mrg, NULL);
}

#define PAD_DIM     8
int VID_HEIGHT=96; // XXX: ugly global

void render_clip (Mrg *mrg, GeglEDL *edl, const char *clip_path, int clip_start, int clip_frames, double x, double y)
{
  char *thumb_path = gedl_make_thumb_path (edl, clip_path);

  cairo_t *cr = mrg_cr (mrg);
  cairo_rectangle (cr, x, y, clip_frames, VID_HEIGHT);

  int width, height;
  MrgImage *img = mrg_query_image (mrg, thumb_path, &width, &height);
  g_free (thumb_path);
  if (!playing && img && width > 0)
  {
    cairo_surface_t *surface = mrg_image_get_surface (img);
    cairo_matrix_t   matrix;
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface (surface);
    cairo_matrix_init_scale (&matrix, 1.0, height * 1.0/ VID_HEIGHT);
    cairo_matrix_translate  (&matrix, -(x - clip_start), -y);
    cairo_pattern_set_matrix (pattern, &matrix);
    cairo_pattern_set_filter (pattern, CAIRO_FILTER_NEAREST);
    cairo_set_source (cr, pattern);

    cairo_save (cr);
    cairo_clip_preserve (cr);
    cairo_paint   (cr);
    cairo_restore (cr);
  }
  else
  {
    //cairo_set_source_rgba (cr, 0.1, 0.1, 0.1, 0.5);
    //cairo_fill_preserve (cr);
  }
}

void gedl_draw (Mrg     *mrg,
                GeglEDL *edl,
                double   x0,
                double    y,
                double  fpx,
                double   t0)
{

  GList *l;
  cairo_t *cr = mrg_cr (mrg);
  double t;
 
  VID_HEIGHT = mrg_height (mrg) * (1.0 - SPLIT_VER) * 0.8;
  t = 0;

  cairo_set_source_rgba (cr, 1, 1,1, 1);
  //y += PAD_DIM * 2;

  if (playing)
  {
    /* scroll to fit playhead */
    if ( (edl->frame_no - t0) / fpx < mrg_width (mrg) * 0.2)
      edl->t0 = edl->frame_no - (mrg_width (mrg) * 0.2) * fpx;
    if ( (edl->frame_no - t0) / fpx > mrg_width (mrg) * 0.8)
      edl->t0 = edl->frame_no - (mrg_width (mrg) * 0.8) * fpx;
    t0 = edl->t0;
  }

  cairo_save (cr);
  {
    int duration = gedl_get_duration (edl);
    cairo_scale (cr, 1.0 / duration * mrg_width (mrg), 1.0);
  }

  cairo_rectangle (cr, t0, y-10, mrg_width(mrg)*fpx, 10);
  mrg_listen (mrg, MRG_DRAG, drag_t0, edl, edl);
  cairo_set_source_rgba (cr, 1, 1, 0.5, 0.25);
  if (playing)
  cairo_stroke (cr);
  else
  cairo_fill (cr);


  cairo_rectangle (cr, t0 + mrg_width(mrg)*fpx*0.9, y-10, mrg_width(mrg)*fpx * 0.1, 10);
  mrg_listen (mrg, MRG_DRAG, drag_fpx, edl, edl);
  cairo_fill (cr);

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int frames = clip_get_frames (clip);

    //if (clip == edl->active_clip)
    //  cairo_set_source_rgba (cr, 1, 1, 0.5, 1.0);
    //else
    //  cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
    cairo_rectangle (cr, t, y-10, frames, 10);

    cairo_stroke (cr);
    t += frames;
  }


  cairo_restore (cr);
  y+=10;
  t = 0;

  cairo_move_to (cr, x0 + PAD_DIM, y + VID_HEIGHT + PAD_DIM * 3);


  //cairo_show_text (cr, edl->path);
  cairo_save (cr);
  cairo_translate (cr,  x0, 0);
  cairo_scale (cr, 1.0/fpx, 1);
  cairo_translate (cr, -t0, 0);

  int start = 0, end = 0;
  gedl_get_selection (edl, &start, &end);
  cairo_rectangle (cr, start + 0.5, y - PAD_DIM, end - start, VID_HEIGHT + PAD_DIM * 2);
  cairo_set_source_rgba (cr, 1, 0, 0, 0.75);
  cairo_fill (cr);


  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int frames = clip_get_frames (clip);
    render_clip (mrg, edl, clip->path, clip->start, frames, t, y);
    if (clip == edl->active_clip)
      cairo_set_source_rgba (cr, 1, 1, 0.5, 1.0);
    else
      cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
    mrg_listen (mrg, MRG_PRESS, clicked_clip, clip, edl);
    mrg_listen (mrg, MRG_DRAG, drag_clip, clip, edl);
    mrg_listen (mrg, MRG_RELEASE, released_clip, clip, edl);
    cairo_stroke (cr);

    t += frames;
  }


  gedl_get_range (edl, &start, &end);

  cairo_rectangle (cr, start, y + VID_HEIGHT + PAD_DIM * 1.5, end - start, PAD_DIM);
  cairo_set_source_rgba (cr, 0, 0.11, 0.0, 0.5);
  cairo_fill_preserve (cr);
  cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
  cairo_stroke (cr);

  double frame = edl->frame_no;
  if (fpx < 1.0)
    cairo_rectangle (cr, frame, y-PAD_DIM, 1.0, VID_HEIGHT + PAD_DIM * 2);
  else
    cairo_rectangle (cr, frame, y-PAD_DIM, fpx, VID_HEIGHT + PAD_DIM * 2);
  cairo_set_source_rgba (cr,1,0,0,1);
  cairo_fill (cr);

  cairo_restore (cr);

  cairo_rectangle (cr, 0, y - PAD_DIM, mrg_width (mrg), VID_HEIGHT + PAD_DIM * 4);
  mrg_listen (mrg, MRG_SCROLL, zoom_timeline, edl, NULL);
  cairo_new_path (cr);
}

static const char *css =
" document { background: black; }"
"";

static void update_clip_title (const char *new_string, void *user_data)
{
  SourceClip *clip = user_data;
  if (clip->title)
          g_free (clip->title);
  clip->title = g_strdup (new_string);
  changed++;
}


static void edit_filter_graph (MrgEvent *event, void *data1, void *data2)
{ //XXX
  GeglEDL *edl = data1;

  //edl->active_source = NULL;
  edl->filter_edited = !edl->filter_edited;
  mrg_queue_draw (event->mrg, NULL);
}

static void update_query (const char *new_string, void *user_data)
{
  GeglEDL *edl = user_data;
  if (edl->clip_query)
    g_free (edl->clip_query);
  changed++;
  edl->clip_query = g_strdup (new_string);
}

static void update_filter (const char *new_string, void *user_data)
{
  GeglEDL *edl = user_data;
  if (edl->active_clip->filter_graph)
    g_free (edl->active_clip->filter_graph);
  edl->active_clip->filter_graph = g_strdup (new_string);
  gedl_cache_invalid (edl);
  mrg_queue_draw (edl->mrg, NULL);
}

void render_clip2 (Mrg *mrg, GeglEDL *edl, SourceClip *clip, float x, float y, float w, float h)
{
    cairo_t *cr = mrg_cr (mrg);
    if (clip->duration == 0)
       {
         GeglNode *gegl = gegl_node_new ();
         GeglNode *probe = gegl_node_new_child (gegl, "operation",
                          "gegl:ff-load", "path", clip->path, NULL);
         gegl_node_process (probe);
         gegl_node_get (probe, "frames", &clip->duration, NULL);
         g_object_unref (gegl);
       }

    cairo_save (cr);
    {
      float scale = 1.0;
      if (clip->duration > w)
        scale = w / clip->duration;
      cairo_scale (cr, scale, 1);

      render_clip (mrg, edl, clip->path, 0, clip->duration, 0, y);

    if (clip == (void*)edl->active_source)
    {
      cairo_set_source_rgba (cr, 1, 1, 0.5, 1.0);
      cairo_stroke_preserve (cr);
    }
    else
    {
      cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
    }

    mrg_listen (mrg, MRG_DRAG_PRESS, clicked_source_clip, clip, edl);
    mrg_listen (mrg, MRG_DRAG, drag_source_clip, clip, edl);
    mrg_listen (mrg, MRG_DRAG_RELEASE, released_source_clip, clip, edl);
    cairo_new_path (cr);

    cairo_set_source_rgba (cr, 0,0,0,0.75);
    cairo_rectangle (cr, 0, y, clip->start, VID_HEIGHT);
    cairo_rectangle (cr, clip->end, y, clip->duration - clip->end, VID_HEIGHT);
    cairo_fill (cr);

    if (clip == (void*)edl->active_source)
      {
        double frame = edl->source_frame_no;
        if (scale > 1.0)
          cairo_rectangle (cr, frame, y-PAD_DIM, 1.0, VID_HEIGHT + PAD_DIM * 2);
        else
          cairo_rectangle (cr, frame, y-PAD_DIM, 1.0/scale, VID_HEIGHT + PAD_DIM * 2);
        cairo_set_source_rgba (cr,1,0,0,1);
        cairo_fill (cr);

#if 0
  int start = 0, end = 0;
  gedl_get_selection (edl, &start, &end);

  cairo_rectangle (cr, start + 0.5, y - PAD_DIM, end - start, VID_HEIGHT + PAD_DIM * 2);
  cairo_set_source_rgba (cr, 1, 0, 0, 0.5);
  cairo_fill (cr);
#endif
      }
      
    cairo_restore (cr);
    if (0) /* draw more aspect right selection (at least for start/end "frame aspect part..?) */
    {
    render_clip (mrg, edl, clip->path, clip->start, (clip->end - clip->start) * scale, clip->start * scale, y);
    cairo_new_path (cr);
    }

#if 0
    cairo_move_to (cr, x, y + 10);
    cairo_set_source_rgba (cr, 0,0,0,0.8);
    cairo_set_font_size (cr, 10.0);
    cairo_show_text (cr, clip->title);
    cairo_set_source_rgba (cr, 1,1,1,0.8);
    cairo_move_to (cr, x - 1, y + 10 - 1);
#endif

    mrg_set_xy (mrg, x, y + 20);
    if (clip->editing)
      mrg_edit_start (mrg, update_clip_title, clip);
    mrg_print (mrg, clip->title);
    if (clip->editing)
      mrg_edit_end (mrg);
    }
}

void draw_clips (Mrg *mrg, GeglEDL *edl, float x, float y, float w, float h)
{
  GList *l;

  cairo_set_source_rgba (mrg_cr (mrg), 1,1,1,1);
  cairo_set_font_size (mrg_cr (mrg), y);

  mrg_set_style (mrg, "background: transparent; color: white");

  mrg_set_xy (mrg, x, y + 10);
  if (edl->clip_query_edited)
    mrg_edit_start (mrg, update_query, edl);
  mrg_print (mrg, edl->clip_query);
  if (edl->clip_query_edited)
    mrg_edit_end (mrg);

  y += 20;

  for (l = edl->clip_db; l; l = l->next)
  {
    SourceClip *clip = l->data;

    if (strlen (edl->clip_query) == 0 ||
        strstr (clip->title, edl->clip_query))
    {
      render_clip2 (mrg, edl, clip, x, y, w, h);
      y += VID_HEIGHT + PAD_DIM * 1;
    }
  }
#if 0
  if (edl->active_clip)
    render_clip2 (mrg, edl, (void*)edl->active_clip, x, y, w, h);
#endif
}

static long prev_ticks = 0;

static inline void skipped_frames (int count)
{
  //fprintf (stderr, "[%i]", count);
}

static inline void wait_for_frame ()
{
  //fprintf (stderr, ".");
}

static void toggle_ui_mode  (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  edl->ui_mode ++;
  if (edl->ui_mode > GEDL_LAST_UI_MODE)
    edl->ui_mode = 0;

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void toggle_playing (MrgEvent *event, void *data1, void *data2)
{
  playing =  !playing;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  prev_ticks = babl_ticks ();
  changed++;
}


void playing_iteration (Mrg *mrg, GeglEDL *edl)
{
  long ticks = 0;
  double delta = 1;
  ticks = babl_ticks ();
  if (prev_ticks == 0) prev_ticks = ticks;

  if (playing)
    {
#if 0
      if (prev_ticks - ticks < 1000000.0 / gedl_get_fps (edl))
        return;
#endif
      delta = (((ticks - prev_ticks) / 1000000.0) * ( edl->fps ));
      //fprintf (stderr, "%f\n", delta);
      if (delta < 1.0)
      {
        wait_for_frame ();
        mrg_queue_draw (mrg, NULL);
        return;
      }
        //delta = 0;
      {
#if 0
        static int frameskip = -1;
        if (frameskip < 0)
        {
          if (getenv ("GEDL_FRAMESKIP"))
            frameskip = 1;
          else
            frameskip = 0;
        }
        if (!frameskip)
          delta = 1;
#else
        if (edl->framedrop)
        {
          if (delta >= 2.0)
            {
              skipped_frames ( (int)(delta)-1 );
            }
        }
        else
        {
          if (delta > 1.0)
            delta = 1;
          else
            delta = 0;
        }
#endif
      }
      if (rendering_frame != done_frame)
        return;
      if (delta >= 1.0)
      {
      if (edl->active_source)
      {
        edl->source_frame_no += delta;
        if (edl->source_frame_no > edl->active_source->end)
        {
           edl->source_frame_no = 0;
           edl->source_frame_no = edl->active_source->start;
        }
        prev_ticks = ticks;
      }

      if (edl->active_clip)
      {
        edl->frame_no += delta;
        int start, end;
        gedl_get_range (edl, &start, &end);
        if (edl->frame_no > max_frame (edl))
        {
           edl->frame_no = 0;
           if (end)
             edl->frame_no = start;
        }
        edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
        prev_ticks = ticks;
      }
      }
    }
}


extern int cache_hits;
extern int cache_misses;

void gedl_ui (Mrg *mrg, void *data)
{
  State *o = data;
  GeglEDL *edl = o->edl;

  mrg_stylesheet_add (mrg, css, NULL, 0, NULL);
  mrg_set_style (mrg, "font-size: 11px");
#if 0
  cairo_set_source_rgb (mrg_cr (mrg), 0,0,0);
  cairo_paint (mrg_cr (mrg));
#endif

  switch (edl->ui_mode)
  {
     case GEDL_UI_MODE_FULL:
     case GEDL_UI_MODE_NONE:

  mrg_gegl_blit (mrg, (int)(mrg_width (mrg) * 0.0), 0,
                      (int)(mrg_width (mrg) * 1.0),
                      mrg_height (mrg),// * SPLIT_VER,

                      o->edl->cached_result,
                      0, 0,
        /* opacity */ 1.0 //edl->frame_no == done_frame?1.0:0.5
                      );
        break;
     case GEDL_UI_MODE_PART:
  mrg_gegl_blit (mrg, (int)(mrg_width (mrg) * 0.2), 0,
                      (int)(mrg_width (mrg) * 0.8),
                      mrg_height (mrg) * SPLIT_VER,

                      o->edl->cached_result,
                      0, 0,
        /* opacity */ 1.0 //edl->frame_no == done_frame?1.0:0.5
                      );
        break;

  }


  switch (edl->ui_mode)
  {
     case GEDL_UI_MODE_FULL:
     case GEDL_UI_MODE_PART:
  gedl_draw (mrg, edl, 0, mrg_height (mrg) * SPLIT_VER, edl->scale, edl->t0);
  break;
     case GEDL_UI_MODE_NONE:
        break;
     break;
  }

  if(0)draw_clips (mrg, edl, 10, mrg_height(mrg) * SPLIT_VER + VID_HEIGHT + PAD_DIM * 5, mrg_width(mrg) - 20, mrg_height(mrg) * SPLIT_VER - VID_HEIGHT + PAD_DIM * 5);


  if (edl->ui_mode != GEDL_UI_MODE_NONE)
  {

  mrg_set_xy (mrg, 0, 40);
  mrg_set_edge_right (mrg, mrg_width (mrg) * 0.25 - 8);
#if 0
  {
    GeglRectangle rect;
    rect = gegl_node_get_bounding_box (o->edl->cached_result);

    mrg_printf (mrg, "%ix%i\n", rect.width, rect.height);
  }
#endif

#if 1
  mrg_printf (mrg, "cache hit: %2.2f%% of %i\n", 100.0 * cache_hits / (cache_hits + cache_misses), cache_hits + cache_misses);
#endif

  if (done_frame != edl->frame_no)
    mrg_printf (mrg, "%i (%i shown)\n",edl->frame_no, done_frame);
  else
    mrg_printf (mrg, "%i\n", edl->frame_no);
  /*
  mrg_printf (mrg, "rendering:%i\n", rendering_frame);
  mrg_printf (mrg, "frame: %i\n", edl->frame);
  mrg_printf (mrg, "done:%i\n", done_frame);
*/
  if (edl->active_clip)
    {
      char *basename = g_path_get_basename (edl->active_clip->path);
      mrg_printf (mrg, "%s:%i %i (%i)\n", basename,
                                     edl->active_clip->start, edl->active_clip->end,
                                     gedl_get_clip_frame_no (edl)
                                     );
      g_free (basename);

      if(0)  mrg_printf (mrg, "%s %s %i %s %s %i %s %ix%i %f",
          "gedl-pre-3", gedl_get_clip_path (edl), gedl_get_clip_frame_no (edl), edl->clip?edl->clip->filter_graph:"-",
                        //gedl_get_clip2_path (edl), gedl_get_clip2_frame_no (edl), clip2->filter_graph,
                        "aaa", 3, "bbb",
                        edl->video_width, edl->video_height, 
                        0.0/*edl->mix*/);


      if (edl->active_clip->filter_graph)
      {
        if (edl->filter_edited)
        {
          mrg_edit_start (mrg, update_filter, edl);
          mrg_printf (mrg, "%s", edl->active_clip->filter_graph);
          mrg_edit_end (mrg);
        }
        else 
        {
          mrg_text_listen (mrg, MRG_PRESS, 
                           edit_filter_graph, edl, NULL);
          if (edl->active_clip->filter_graph && strlen (edl->active_clip->filter_graph) > 2)
            mrg_printf (mrg, " %s", edl->active_clip->filter_graph);
          else
            mrg_printf (mrg, " %s", "[click to add filter]\n");
          mrg_text_listen_done (mrg);
        }
      }
      else
      {
        mrg_printf (mrg, " %s", "[click to add filter]\n");
      }
    }
  if (edl->active_source)
  {
    char *basename = g_path_get_basename (edl->active_source->path);
    mrg_printf (mrg, "%i\n", edl->source_frame_no);
    mrg_printf (mrg, "%s\n", basename);
  }

  //mrg_printf (mrg, "%i %i %i %i %i\n", edl->frame, edl->frame_no, edl->source_frame_no, rendering_frame, done_frame);

  if (done_frame != rendering_frame)
    mrg_printf (mrg, ".. ");
  else if (edl->active_source &&
           edl->source_frame_no != rendering_frame)
    mrg_printf (mrg, "., ");
  else if (edl->active_clip &&
           edl->frame_no != rendering_frame)
    mrg_printf (mrg, ".:");


  if (help)
  {
    mrg_printf (mrg, "\n"
    "F1 toggle_help\n"
    "space toggle_playing\n"
    "tab toggle ui\n"
    "p toggle_use_proxies\n"
    "left/right step_frame\n"
    "shift-left/right extend_selection\n"
    "v split_clip\n"
    "del/x remove_clip\n"
    "d duplicate_clip\n"
    "i insert\n"
    "s save\n"
    "a select_all\n"
    "r set_range\n"
    "control-left nav_left\n"
    "control-right nav_right\n"
    ". clip_end_inc\n"
    ", clip_end_dec\n"
    "alt-left clip_start_inc\n"
    "alt-right clip_start_dec\n"
    "f toggle_fade\n"

    "q quit\n");
  }
  else
  {
    mrg_printf (mrg, "\nF1 toggle help");
  }

  }

  if (!edl->clip_query_edited &&
      !edl->filter_edited 
                  && (
      !edl->active_source   ||
      edl->active_source->editing == 0))
  {
    mrg_add_binding (mrg, "delete", NULL, NULL, remove_clip, edl);
    mrg_add_binding (mrg, "tab", NULL, NULL, toggle_ui_mode, edl);
    mrg_add_binding (mrg, "i", NULL, NULL, insert, edl);
    mrg_add_binding (mrg, "x", NULL, NULL, remove_clip, edl);
    mrg_add_binding (mrg, "d", NULL, NULL, duplicate_clip, edl);
    mrg_add_binding (mrg, "v", NULL, NULL, split_clip, edl);
    mrg_add_binding (mrg, "f", NULL, NULL, toggle_fade, edl);
    mrg_add_binding (mrg, "F1", NULL, NULL, toggle_help, edl);
    mrg_add_binding (mrg, "s", NULL, NULL, save, edl);
    mrg_add_binding (mrg, "a", NULL, NULL, select_all, edl);
    mrg_add_binding (mrg, "r", NULL, NULL, set_range, edl);
    mrg_add_binding (mrg, "q", NULL, NULL, (void*)do_quit, mrg);
    mrg_add_binding (mrg, "space", NULL, NULL, toggle_playing, edl);

    mrg_add_binding (mrg, "p", NULL, NULL, toggle_use_proxies, edl);

    mrg_add_binding (mrg, "control-left", NULL, NULL, nav_left, edl);
    mrg_add_binding (mrg, "control-right", NULL, NULL, nav_right, edl);
    mrg_add_binding (mrg, ".", NULL, NULL, clip_end_inc, edl);
    mrg_add_binding (mrg, ",", NULL, NULL, clip_end_dec, edl);
    mrg_add_binding (mrg, "alt-left", NULL, NULL, clip_start_inc, edl);
    mrg_add_binding (mrg, "alt-right", NULL, NULL, clip_start_dec, edl);
    mrg_add_binding (mrg, "shift-right", NULL, NULL, extend_selection_to_the_right, edl);
    mrg_add_binding (mrg, "shift-left", NULL, NULL, extend_selection_to_the_left, edl);

    mrg_add_binding (mrg, "right", NULL, NULL, step_frame, edl);
    mrg_add_binding (mrg, "left", NULL, NULL, step_frame_back, edl);
  }
  mrg_add_binding (mrg, "up", NULL, NULL, up, edl);
  mrg_add_binding (mrg, "down", NULL, NULL, down, edl);

  if (edl->active_source)
    mrg_add_binding (mrg, "return", NULL, NULL, toggle_edit_source, edl);
  else //if (edl->filter_edited)
    mrg_add_binding (mrg, "return", NULL, NULL, edit_filter_graph, edl);


}


#if 0
int renderer_pos   = 0;
int renderer_start = 0;
int renderer_end   = 0;
static void renderer_set_range (int start, int end)
{
  renderer_start = start;
  renderer_end = end;
  renderer_pos = 0;
}
#endif

gboolean renderer_main (gpointer data)
{
  GeglEDL *edl = data;
  if (!playing)
    {
      char *cmd = g_strdup_printf ("test -f lock || ( touch lock;gedl %s cache;rm lock ) &", edl->path);
      save_edl (edl);
      system (cmd);
    }
  return TRUE;
}

int gedl_ui_main (GeglEDL *edl);
int gedl_ui_main (GeglEDL *edl)
{
  Mrg *mrg = mrg_new (800, 600, NULL);
  //Mrg *mrg = mrg_new (-1, -1, NULL);
  State o = {NULL,};
  o.mrg = mrg;
  o.edl = edl;

  edl->mrg = mrg;
  preview_loader = gegl_node_new_child (edl->gegl, "operation", "gegl:ff-load",
                         "path", "/tmp", NULL);
  gegl_node_connect_to (preview_loader, "output", edl->source_store_buf, "input");

  edl->cache_flags = CACHE_TRY_ALL;// | CACHE_MAKE_ALL;
//  if (!edl->use_proxies)
//   edl->cache_flags |= CACHE_MAKE_ALL;
//renderer_set_range (0, 50);
  mrg_set_ui (mrg, gedl_ui, &o);
  g_timeout_add (3000, save_idle, edl);

  g_timeout_add (1000, renderer_main, edl);

  thread = g_thread_new ("renderer", renderer_thread, edl);
//if(0)  g_thread_new ("cachemaster", renderer_main, edl);

  gedl_get_duration (edl);
  mrg_set_target_fps (mrg, -1);
  mrg_main (mrg);
  gedl_free (edl);
  gegl_exit ();

  end_audio ();

  return 0;
}
