#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#define USE_CAIRO_SCALING 1

#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <mrg.h>
#include <gegl.h>
#include "gedl.h"
#include "renderer.h"
#include <gegl-paramspecs.h>

static int exited = 0;
long babl_ticks (void);

static unsigned char *copy_buf = NULL;
static int copy_buf_len = 0;

static int changed = 0;

static int empty_selection (GeglEDL *edl)
{
  return edl->selection_start == edl->selection_end;
}
static void clip_split (Clip *oldclip, int shift);
static void clip_remove (Clip *clip);

static void mrg_gegl_blit (Mrg *mrg,
                          float x0, float y0,
                          float width, float height,
                          GeglNode *node,
                          float u, float v,
                          float opacity,
                          GeglEDL *edl)
{
  GeglRectangle bounds;

  cairo_t *cr = mrg_cr (mrg);
  cairo_surface_t *surface = NULL;

  if (!node)
    return;

  bounds = *gegl_buffer_get_extent (edl->buffer_copy_temp);

  if (width == -1 && height == -1)
  {
    width  = bounds.width;
    height = bounds.height;
  }

  if (width == -1)
    width = bounds.width * height / bounds.height;
  if (height == -1)
    height = bounds.height * width / bounds.width;

#ifdef USE_CAIRO_SCALING
  if (copy_buf_len < bounds.width * bounds.height * 4)
  {
    if (copy_buf)
      free (copy_buf);
    copy_buf_len = bounds.width * bounds.height * 4;
    copy_buf = malloc (copy_buf_len);
  }
      float scale = 1.0;
  {
    static int foo = 0;
    unsigned char *buf = copy_buf;
    GeglRectangle roi = {u, v, bounds.width, bounds.height};
    static const Babl *fmt = NULL;

foo++;
    if (!fmt) fmt = babl_format ("cairo-RGB24");

    {
      scale = width / bounds.width;
      if (height / bounds.height < scale)
        scale = height / bounds.height;

      // XXX: the 1.001 instead of 1.00 is to work around a gegl bug
      gegl_buffer_get (edl->buffer_copy_temp, &roi, 1.001, fmt, buf, bounds.width * 4, GEGL_ABYSS_BLACK);
    }

    surface = cairo_image_surface_create_for_data (buf, CAIRO_FORMAT_RGB24, bounds.width, bounds.height, bounds.width * 4);
  }

  cairo_save (cr);
  cairo_surface_set_device_scale (surface, 1.0/scale, 1.0/scale);
#else
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

      gegl_buffer_get (edl->buffer_copy_temp, &roi, scale, fmt, buf, width * 4, GEGL_ABYSS_BLACK);
    }

  surface = cairo_image_surface_create_for_data (buf, CAIRO_FORMAT_RGB24, width, height, width * 4);
  }

  cairo_save (cr);
  cairo_surface_set_device_scale (surface, 1.0, 1.0);
#endif
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

float fpx           = 2;

#if 0
 // with copy/paste we'd want to have this:
 // available: with a path + newline being valid, and perhaps
 // sufficient for the drag + drop case as well

static void insert_string (GeglEDL *edl, const char *string)
{
}
#endif

static void insert_clip (GeglEDL *edl, const char *path,
                         int in, int out)
{
  GList *iter;
  Clip *clip, *cur_clip;
  int end_frame = edl->frame_no;
  if (in < 0)
    in = 0;
  if (out < 0)
  {
    int duration = 0;
    if (!empty_selection (edl))
    {
      out = edl->selection_end - edl->selection_start;
      if (out < 0) out = -out;
    }
    else
    {
      gedl_get_video_info (path, &duration, NULL);
      out = duration;
    }
    if (out < in)
      out = in;
  }
  clip = clip_new_full (edl, path, in, out);
  clip->title = g_strdup (basename (path));
  int clip_frame_no;
  cur_clip = gedl_get_clip (edl, edl->frame_no, &clip_frame_no);

  if (empty_selection (edl))
  {
    gedl_get_duration (edl);
    if (edl->frame_no != cur_clip->abs_start)
    {
      gedl_get_duration (edl);
      clip_split (cur_clip, clip_frame_no);
      cur_clip = edl_get_clip_for_frame (edl, edl->frame_no);
    }
  }
  else
  {
    Clip *last_clip;
    int sin, sout;
    sin = edl->selection_start;
    sout = edl->selection_end + 1;
    if (sin > sout)
    {
      sout = edl->selection_start + 1;
      sin = edl->selection_end;
    }
    int cur_clip_frame_no;
    cur_clip = gedl_get_clip (edl, sin, &cur_clip_frame_no);
    clip_split (cur_clip, cur_clip_frame_no);
    gedl_get_duration (edl);
    int last_clip_frame_no;
    cur_clip = gedl_get_clip (edl, sin, &cur_clip_frame_no);
    last_clip = gedl_get_clip (edl, sout, &last_clip_frame_no);
    if (cur_clip == last_clip)
    {
      clip_split (last_clip, last_clip_frame_no);
    }
    last_clip = edl_get_clip_for_frame (edl, sout);

    cur_clip = edl_get_clip_for_frame (edl, sin);
    while (cur_clip != last_clip)
    {
      clip_remove (cur_clip);
      cur_clip = edl_get_clip_for_frame (edl, sin);
    }
    edl->frame_no = sin;
  }

  cur_clip = edl_get_clip_for_frame (edl, edl->frame_no);
  iter = g_list_find (edl->clips, cur_clip);
  edl->clips = g_list_insert_before (edl->clips, iter, clip);
  end_frame += out - in + 1;
  edl->frame_no = end_frame;

  edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);

  gedl_make_proxies (edl);
}

static void drag_dropped (MrgEvent *ev, void *data1, void *data2)
{
  GeglEDL *edl = data2;

  char *str = g_strdup (ev->string);
  char *s = str;
  char *e;

  e = strchr (s, '\r');
  while (e)
  {
    *e = '\0';
    if (strstr (s, "file://")) s+= strlen ("file://");
    insert_clip (edl, s, -1, -1);
    s = e+1;
    if (*s == '\n') s++;
    e = strchr (s, '\r');
  }

  g_free (str);
}
static void scroll_to_fit (GeglEDL *edl, Mrg *mrg);

static void clicked_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;

  edl->frame_no = e->x;
  edl->selection_start = edl->frame_no;
  edl->selection_end = edl->frame_no;
  edl->active_clip = clip;
  edl->playing = 0;
  scroll_to_fit (edl, e->mrg);
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

#include <math.h>

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
  scroll_to_fit (edl, e->mrg);
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void drag_t0 (MrgEvent *e, void *data1, void *data2)
{
  GeglEDL *edl = data2;
  edl->t0 += e->delta_x;
  if (edl->t0 < 0.0)
    edl->t0 = 0.0;
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

static void released_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;
  edl->frame_no = e->x;
  edl->active_clip = clip;
  if (edl->selection_end < edl->selection_start)
  {
    int temp = edl->selection_end;
    edl->selection_end = edl->selection_start;
    edl->selection_start = temp;
  }
  scroll_to_fit (edl, e->mrg);
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void stop_playing (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  edl->playing = 0;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void select_all (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gint end = gedl_get_duration (edl) - 1;
  if (edl->selection_start == 0 && edl->selection_end == end)
  {
    gedl_set_selection (edl, 0, 0);
  }
  else
  {
    gedl_set_selection (edl, 0, end);
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}



static void prev_cut (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);

    if (iter)
    {
       if (edl->frame_no == edl->active_clip->abs_start)
       {
         iter = iter->prev;
         if (iter) edl->active_clip = iter->data;
       }
    }
    edl->frame_no = edl->active_clip->abs_start;
    edl->selection_start = edl->selection_end = edl->frame_no;
  }
  mrg_event_stop_propagate (event);
  scroll_to_fit (edl, event->mrg);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void next_cut (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter) iter = iter->next;
    if (iter)
    {
      edl->active_clip = iter->data;
      edl->frame_no = edl->active_clip->abs_start;
    }
    else
    {
      edl->frame_no = edl->active_clip->abs_start + clip_get_frames (edl->active_clip);
    }
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  edl->selection_start = edl->selection_end = edl->frame_no;
  scroll_to_fit (edl, event->mrg);
  changed++;
}

static void extend_selection_to_previous_cut (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int sel_start, sel_end;
  edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);

  gedl_get_selection (edl, &sel_start, &sel_end);
  prev_cut (event, data1, data2);
  sel_start = edl->frame_no;
  gedl_set_selection (edl, sel_start, sel_end);

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}


static void extend_selection_to_next_cut (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int sel_start, sel_end;

  gedl_get_selection (edl, &sel_start, &sel_end);
  next_cut (event, data1, data2);
  sel_start = edl->frame_no;
  gedl_set_selection (edl, sel_start, sel_end);

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
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

static int ui_tweaks = 0;
static int are_mergable (Clip *clip1, Clip *clip2, int delta)
{
  if (!clip1 || !clip2)
    return 0;
  if (!clip1->path)
    return 0;
  if (!clip2->path)
    return 0;
  if (strcmp (clip1->path, clip2->path))
    return 0;
  if (clip2->start != (clip1->end + 1 + delta))
    return 0;
  if (clip1->filter_graph==NULL && clip2->filter_graph != NULL)
    return 0;
  if (clip1->filter_graph!=NULL && clip2->filter_graph == NULL)
    return 0;
  if (clip1->filter_graph && strcmp (clip1->filter_graph, clip2->filter_graph))
    return 0;
  return 1;
}

static void clip_remove (Clip *clip)
{
  GeglEDL *edl = clip->edl;
  GList *iter = g_list_find (edl->clips, clip);

  if (iter->next)
    iter = iter->next;
  else if (iter->prev)
    iter = iter->prev;
  else
    return;

  edl->clips = g_list_remove (edl->clips, clip);
  edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
}

static GeglNode *selected_node = NULL;

static void remove_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;

  if (!edl->active_clip)
    return;

  if (selected_node)
  {
    GeglNode *producer = NULL;
    GeglNode *consumer = NULL;
    GeglNode   **nodes = NULL;
    const gchar **pads = NULL;
    char      *prodpad = NULL;

    int count = gegl_node_get_consumers (selected_node, "output", &nodes, &pads);
    if (count)
      {
        consumer= nodes[0];
      }

    producer = gegl_node_get_producer (selected_node, "input", &prodpad);

    if (producer && consumer)
    {
      fprintf (stderr, "%p %s %p %s\n", producer, prodpad, consumer, pads[0]);
      gegl_node_connect_to (producer, prodpad, consumer, pads[0]);
    }

    if (prodpad)
      g_free (prodpad);

    g_object_unref (selected_node);
    selected_node = NULL;
    ui_tweaks++;
  }
  else
  {
    clip_remove (edl->active_clip);
  }

  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static GeglNode *filter_start;

static void insert_filter (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;

  if (!edl->active_clip)
    return;

  if (!selected_node)
  {
    selected_node = filter_start;
    fprintf (stderr, "%p\n", filter_start);
  }

  {
    GeglNode **nodes = NULL;
    GeglNode *new = NULL;
    const gchar **pads = NULL;

    int count = gegl_node_get_consumers (selected_node, "output", &nodes, &pads);
    new = gegl_node_new_child (edl->gegl, "operation", "gegl:unsharp-mask", NULL);
    gegl_node_link_many (selected_node, new, NULL);
    if (count)
    {
      gegl_node_connect_to (new, "output", nodes[0], pads[0]);
    }
    selected_node = new;
  }
  ui_tweaks++;
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);

}

static void merge_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  GList *iter = g_list_find (edl->clips, edl->active_clip);
  Clip *clip2 = NULL;
  if (iter) iter = iter->prev;
  if (iter) clip2 = iter->data;

  if (!are_mergable (clip2, edl->active_clip, 0))
    return;

  clip2->end = edl->active_clip->end;

  remove_clip (event, data1, data2);
  edl->active_clip = clip2;
}

static void toggle_use_proxies (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;

  if (!edl->playing) // disallowing - to avoid some races
  {
    gedl_set_use_proxies (edl, edl->use_proxies?0:1);
    gedl_cache_invalid (edl);

    if (edl->use_proxies)
      gedl_make_proxies (edl);
  }

  if (event)
  {
    mrg_event_stop_propagate (event);
    mrg_queue_draw (event->mrg, NULL);
  }
}

static void clip_split (Clip *oldclip, int shift)
{
  GeglEDL *edl = oldclip->edl;
  GList *iter = g_list_find (edl->clips, oldclip);
  Clip *clip = clip_new_full (edl, oldclip->path, oldclip->start, oldclip->end);
  edl->clips = g_list_insert_before (edl->clips, iter, clip);

  if (oldclip->filter_graph)
    clip->filter_graph = g_strdup (oldclip->filter_graph);

  clip->end      = shift - 1;
  oldclip->start = shift;
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

  clip_split (edl->active_clip, clip_frame_no);
  {
    //edl->active_clip = clip;

  }
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void toggle_fade (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;

  if (!edl->active_clip)
    return;

  if (edl->active_clip->fade)
  {
    edl->active_clip->fade = 0;
  }
  else
  {
    edl->active_clip->fade = (edl->frame - edl->active_clip->abs_start)*2;
  }
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void duplicate_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;

  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    Clip *clip = clip_new_full (edl, edl->active_clip->path, edl->active_clip->start, edl->active_clip->end);
    edl->clips = g_list_insert_before (edl->clips, iter, clip);
    if (edl->active_clip->filter_graph)
      clip->filter_graph = g_strdup (edl->active_clip->filter_graph);
    edl->active_clip = clip;
  }
  gedl_cache_invalid (edl);
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

static void save_edl (GeglEDL *edl)
{
  if (edl->path)
  {
    gedl_save_path (edl, edl->path);
  }
}

#if 1
static void save (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  save_edl (edl);
}
#endif

static gboolean save_idle (Mrg *mrg, gpointer edl)
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

#if 0
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
#endif

#if 0
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
#endif

static void step_frame_back (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  stop_playing (event, data1, data2);
  {
    edl->selection_start = edl->selection_end;
    edl->frame_no --;
    if (edl->frame_no < 0)
      edl->frame_no = 0;
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
  {
    edl->selection_start = edl->selection_end;
    edl->frame_no ++;
    edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void clip_end_start_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  Clip *clip1, *clip2;
  if (edl->selection_start < edl->selection_end)
  {
    clip1 = edl_get_clip_for_frame (edl, edl->selection_start);
    clip2 = edl_get_clip_for_frame (edl, edl->selection_end);
  }
  else
  {
    clip1 = edl_get_clip_for_frame (edl, edl->selection_end);
    clip2 = edl_get_clip_for_frame (edl, edl->selection_start);
  }
  edl->selection_start--;
  edl->selection_end--;
  clip1->end--;
  clip2->start--;
  edl->frame_no--;
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void clip_end_start_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  Clip *clip1, *clip2;
  if (edl->selection_start < edl->selection_end)
  {
    clip1 = edl_get_clip_for_frame (edl, edl->selection_start);
    clip2 = edl_get_clip_for_frame (edl, edl->selection_end);
  }
  else
  {
    clip1 = edl_get_clip_for_frame (edl, edl->selection_end);
    clip2 = edl_get_clip_for_frame (edl, edl->selection_start);
  }
  edl->selection_start++;
  edl->selection_end++;
  clip1->end++;
  clip2->start++;
  edl->frame_no++;

  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void clip_start_end_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
    {
      edl->active_clip->end++;
      edl->active_clip->start++;
    }
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void clip_start_end_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
    {
      edl->active_clip->end--;
      edl->active_clip->start--;
    }
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void clip_end_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
    {
      edl->active_clip->end++;
      edl->frame_no++;
    }
  gedl_cache_invalid (edl);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void clip_end_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
    {
      edl->active_clip->end--;
      edl->frame_no--;
      gedl_cache_invalid (edl);
    }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void clip_start_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
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
  if (edl->active_clip)
    {
      edl->active_clip->start--;
      gedl_cache_invalid (edl);
    }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

#if 0
static void toggle_edit_source (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  edl->active_source->editing = !edl->active_source->editing;
  if (edl->active_source->editing)
    mrg_set_cursor_pos (event->mrg, strlen (edl->active_source->title));
  gedl_cache_invalid (edl);
  mrg_queue_draw (event->mrg, NULL);
}
#endif

static void do_quit (MrgEvent *event, void *data1, void *data2)
{
  exited = 1;
  killpg(0, SIGUSR2);
  mrg_quit (event->mrg);
}

long last_frame = 0;

void gedl_ui (Mrg *mrg, void *data);

static void zoom_timeline (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  switch (event->scroll_direction)
  {
    case MRG_SCROLL_DIRECTION_UP:
      edl->t0 +=    event->x * edl->scale;
      edl->scale *= 1.02;
      edl->t0 -=    event->x * edl->scale;
      break;
    case MRG_SCROLL_DIRECTION_DOWN:
      edl->t0 +=    event->x * edl->scale;
      edl->scale /= 1.02;
      edl->t0 -=    event->x * edl->scale;
      break;
    case MRG_SCROLL_DIRECTION_LEFT:
      edl->t0 += edl->scale * 2;
      break;
    case MRG_SCROLL_DIRECTION_RIGHT:
      edl->t0 -= edl->scale * 2;
      break;
  }

  scroll_to_fit (edl, event->mrg);
  mrg_queue_draw (event->mrg, NULL);
}

#define PAD_DIM     8
int VID_HEIGHT=96; // XXX: ugly global

void render_clip (Mrg *mrg, GeglEDL *edl, const char *clip_path, int clip_start, int clip_frames, double x, double y, int fade, int fade2)
{
  char *thumb_path;
  if (!clip_path)
  {
    return; // XXX: draw string!
  }
  thumb_path = gedl_make_thumb_path (edl, clip_path);

  cairo_t *cr = mrg_cr (mrg);
  if (fade || fade2)
  {
    cairo_move_to (cr, x, y + VID_HEIGHT/2);
    cairo_line_to (cr, x + fade/2, y);
    cairo_line_to (cr, x + clip_frames + fade2/2, y);
    cairo_line_to (cr, x + clip_frames - fade2/2, y + VID_HEIGHT);
    cairo_line_to (cr, x - fade/2, y + VID_HEIGHT);
    cairo_line_to (cr, x, y + VID_HEIGHT/2);
  }
  else
  {
    cairo_rectangle (cr, x, y, clip_frames, VID_HEIGHT);
  }

  int width, height;
  MrgImage *img = mrg_query_image (mrg, thumb_path, &width, &height);
  g_free (thumb_path);
  if (!edl->playing && img && width > 0)
  {
    cairo_surface_t *surface = mrg_image_get_surface (img);
    cairo_matrix_t   matrix;
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface (surface);

    //cairo_matrix_init_rotate (&matrix, M_PI / 2); /* compensate for .. */
    //cairo_matrix_translate (&matrix, 0, -width);  /* vertical format   */

    cairo_matrix_init_scale (&matrix, 1.0, height* 1.0/ VID_HEIGHT);
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

static void scroll_to_fit (GeglEDL *edl, Mrg *mrg)
{
  /* scroll to fit playhead */
  if ( (edl->frame_no - edl->t0) / edl->scale > mrg_width (mrg) * 0.9)
    edl->t0 = edl->frame_no - (mrg_width (mrg) * 0.8) * edl->scale;
  else if ( (edl->frame_no - edl->t0) / edl->scale < mrg_width (mrg) * 0.1)
    edl->t0 = edl->frame_no - (mrg_width (mrg) * 0.2) * edl->scale;
}

static void shuffle_forward (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_cache_invalid (edl);

  GList *prev = NULL,
        *next = NULL,
        *self = g_list_find (edl->clips, edl->active_clip);

  if (self)
  {
    next = self->next;
    prev = self->prev;

    if (self && next)
    {
      GList *nextnext = next->next;
      if (prev)
        prev->next = next;
      next->prev = prev;
      next->next = self;
      self->prev = next;
      self->next = nextnext;
      if (self->next)
        self->next->prev = self;
      edl->frame_no += clip_get_frames (next->data);
    }
  }

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void shuffle_back (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_cache_invalid (edl);

  GList *prev = NULL,
        *prevprev = NULL,
        *next = NULL,
        *self = g_list_find (edl->clips, edl->active_clip);

  if (self)
  {
    next = self->next;
    prev = self->prev;
    if (prev)
      prevprev = prev->prev;

    if (self && prev)
    {
      if (prevprev)
        prevprev->next = self;
      self->prev = prevprev;
      self->next = prev;
      prev->prev = self;
      prev->next = next;
      if (next)
        next->prev = prev;

      edl->frame_no -= clip_get_frames (prev->data);
    }
  }

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void slide_forward (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_cache_invalid (edl);
    edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);

  GList *prev = NULL,
        *next = NULL,
        *self = g_list_find (edl->clips, edl->active_clip);
  /*
        situations to deal with:
          inside mergable clips
          last of inside mergable clips
          inside mergable clips if we padded prev
          last of inside mergable clips if we padded prev
          non mergable clips -> split next and shuffle into
   */

  if (self)
  {
    next = self->next;
    prev = self->prev;

    if (self && next && prev)
    {
      Clip *prev_clip = prev->data;
      Clip *next_clip = next->data;
      Clip *self_clip = self->data;

      if (are_mergable (prev_clip, next_clip, 0))
      {
        if (clip_get_frames (next_clip) == 1)
        {
          prev_clip->end++;
          edl->clips = g_list_remove (edl->clips, next_clip);
          edl->frame_no ++;
        }
        else
        {
          prev_clip->end ++;
          next_clip->start ++;
          edl->frame_no ++;
        }
      } else if (are_mergable (prev_clip, next_clip, clip_get_frames (self_clip)))
      {
        if (clip_get_frames (next_clip) == 1)
        {
          prev_clip->end++;
          edl->clips = g_list_remove (edl->clips, next_clip);
          edl->frame_no ++;
        }
        else
        {
          prev_clip->end ++;
          next_clip->start ++;
          edl->frame_no ++;
        }
      }
      else {
        if (clip_get_frames (next_clip) == 1)
        {
          int frame_no = edl->frame_no + 1;
          shuffle_forward (event, data1, data2);
          edl->frame_no = frame_no;
        } else {
          int frame_no = edl->frame_no + 1;
          clip_split (next_clip, next_clip->start + 1);
          shuffle_forward (event, data1, data2);
          edl->frame_no = frame_no;
        }
      }
    }
  }

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void slide_back (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_cache_invalid (edl);
    edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);

  GList *prev = NULL,
        *next = NULL,
        *self = g_list_find (edl->clips, edl->active_clip);
  /*
        situations to deal with:
          inside mergable clips
          last of inside mergable clips
          inside mergable clips if we padded prev
          last of inside mergable clips if we padded prev
          non mergable clips -> split next and shuffle into
   */

  if (self)
  {
    next = self->next;
    prev = self->prev;

    if (self && next && prev)
    {
      Clip *prev_clip = prev->data;
      Clip *next_clip = next->data;
      Clip *self_clip = self->data;

      if (are_mergable (prev_clip, next_clip, 0))
      {
        if (clip_get_frames (prev_clip) == 1)
        {
          next_clip->start --;
          edl->clips = g_list_remove (edl->clips, prev_clip);
          edl->frame_no --;
        }
        else
        {
          prev_clip->end --;
          next_clip->start --;
          edl->frame_no --;
        }
      } else if (are_mergable (prev_clip, next_clip, clip_get_frames (self_clip)))
      {
        if (clip_get_frames (prev_clip) == 1)
        {
          prev_clip->end--;
          edl->clips = g_list_remove (edl->clips, prev_clip);
          edl->frame_no --;
        }
        else
        {
          prev_clip->end --;
          next_clip->start --;
          edl->frame_no --;
        }
      }
      else
      {
        if (clip_get_frames (prev_clip) == 1)
        {
        int frame_no = edl->frame_no - 1;
        shuffle_back (event, data1, data2);
        edl->frame_no = frame_no;
        } else {
        int frame_no = edl->frame_no - 1;
        clip_split (prev_clip, prev_clip->end );
        shuffle_back (event, data1, data2);
        edl->frame_no = frame_no;
        }
      }
    }
  }

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void zoom_1 (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_cache_invalid (edl);
  edl->scale = 1.0;
  scroll_to_fit (edl, event->mrg);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void zoom_fit (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_cache_invalid (edl);
  edl->t0 = 0.0;
  edl->scale = gedl_get_duration (edl) * 1.0 / mrg_width (event->mrg);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void tweaked_state (Mrg *mrg)
{
  ui_tweaks++;
}

static void toggle_bool (MrgEvent *e, void *data1, void *data2)
{
  GeglNode *node = data1;
  const char *prop = data2;
  gboolean old_value;
  gboolean new_value;
  gegl_node_get (node, prop, &old_value, NULL);
  new_value = !old_value;
  gegl_node_set (node, prop, new_value, NULL);

  changed++;
  mrg_event_stop_propagate (e);
  mrg_queue_draw (e->mrg, NULL);
  tweaked_state (e->mrg);
}

GeglNode *snode = NULL;
const char *sprop = NULL;

static void edit_string (MrgEvent *e, void *data1, void *data2)
{
  GeglNode *node = data1;
  const char *prop = data2;
  snode = node;
  sprop = prop;
  changed++;
  mrg_event_stop_propagate (e);
  mrg_set_cursor_pos (e->mrg, 0); // XXX: could fech strlen and use that
  mrg_queue_draw (e->mrg, NULL);
  tweaked_state (e->mrg);
}

static void drag_double_slider (MrgEvent *e, void *data1, void *data2)
{
  GeglParamSpecDouble *gspec = (void*)data2;
  GParamSpec          *spec  = (void*)data2;
  GeglNode            *node  = (void*)data1;
  char tmpbuf[1024];
  sprintf (tmpbuf, "%s-rel", spec->name);
  GQuark rel_quark = g_quark_from_string (tmpbuf);
  double ui_min = gspec->ui_minimum;
  double ui_max = gspec->ui_maximum;
  if (g_object_get_qdata (G_OBJECT (node), rel_quark) && 1)
    {
      ui_min /= 1000.0;
      ui_max /= 1000.0;
    }

  float new_val = e->x * (ui_max - ui_min) + ui_min;
  gegl_node_set (node, spec->name, new_val, NULL);

  mrg_queue_draw (e->mrg, NULL);
  mrg_event_stop_propagate (e);
  changed++;
  tweaked_state (e->mrg);
}

static void drag_int_slider (MrgEvent *e, void *data1, void *data2)
{
  GeglParamSpecInt *gspec = (void*)data2;
  GParamSpec       *spec  = (void*)data2;
  GeglNode         *node  = (void*)data1;
  char tmpbuf[1024];
  sprintf (tmpbuf, "%s-rel", spec->name);
  GQuark rel_quark = g_quark_from_string (tmpbuf);
  double ui_min = gspec->ui_minimum;
  double ui_max = gspec->ui_maximum;
  if (g_object_get_qdata (G_OBJECT (node), rel_quark) && 1)
    {
      ui_min /= 1000.0;
      ui_max /= 1000.0;
    }

  gint new_val = e->x * (ui_max - ui_min) + ui_min;
  gegl_node_set (node, spec->name, new_val, NULL);

  mrg_queue_draw (e->mrg, NULL);
  mrg_event_stop_propagate (e);
  changed++;
  tweaked_state (e->mrg);
}

static void update_string (const char *new_string, void *user_data)
{
  if (snode && sprop)
    gegl_node_set (snode, sprop, new_string, NULL);
  ui_tweaks++;
}

float print_props (Mrg *mrg, GeglEDL *edl, GeglNode *node, float x, float y)
{
  unsigned int n_props;
  GParamSpec ** props = gegl_operation_list_properties (gegl_node_get_operation (node),
                      &n_props);

  for (int i = 0; i <n_props; i ++)
  {
    char *str = NULL;
    mrg_set_xy (mrg, x, y);
    GType type = props[i]->value_type;

    char tmpbuf[1024];

    sprintf (tmpbuf, "%s-rel", props[i]->name);
    GQuark rel_quark = g_quark_from_string (tmpbuf);
    sprintf (tmpbuf, "%s-anim", props[i]->name);
    GQuark anim_quark = g_quark_from_string (tmpbuf);
    mrg_set_xy (mrg, x, y);

    if (g_type_is_a (type, G_TYPE_DOUBLE))
    {
      GeglParamSpecDouble *gspec = (void*)props[i];
      double val;
      gegl_node_get (node, props[i]->name, &val, NULL);
      double width = mrg_width (mrg) - x - mrg_em(mrg) * 15;
      double ui_min = gspec->ui_minimum;
      double ui_max = gspec->ui_maximum;

      if (g_object_get_qdata (G_OBJECT (node), rel_quark) && 1)
      {
        ui_min /= 1000.0;
        ui_max /= 1000.0;
      }

      cairo_save (mrg_cr (mrg));
      cairo_translate (mrg_cr (mrg), x + mrg_em(mrg) * 10, y - mrg_em(mrg));
      cairo_rectangle (mrg_cr (mrg), 0, 0,
                       width,
                       mrg_em (mrg));
      cairo_save (mrg_cr (mrg));
      cairo_scale (mrg_cr (mrg), width, 1.0);

      mrg_listen (mrg, MRG_DRAG, drag_double_slider, node, gspec);
      cairo_restore (mrg_cr (mrg));
      cairo_set_source_rgba (mrg_cr (mrg), 1,1,1,1.0);
      cairo_stroke (mrg_cr (mrg));

      cairo_rectangle (mrg_cr (mrg), 0,
                       0, (val - ui_min) / (ui_max - ui_min) * width,
                       mrg_em (mrg)  );
      cairo_set_source_rgba (mrg_cr (mrg), 1,1,1,1.0);
      cairo_fill (mrg_cr (mrg));

      cairo_restore (mrg_cr (mrg));

      str = g_strdup_printf ("%s:%f", props[i]->name, val);
      while (str[strlen(str)-1]=='0')
      {
        if (str[strlen(str)-2]=='.')
          break;
        str[strlen(str)-1]='\0';
      }
      mrg_printf (mrg, "%s", str);
    }
    else if (g_type_is_a (type, G_TYPE_INT))
    {
      GeglParamSpecDouble *gspec = (void*)props[i];
      gint val;
      gegl_node_get (node, props[i]->name, &val, NULL);
      double width = mrg_width (mrg) - x - mrg_em(mrg) * 15;
      double ui_min = gspec->ui_minimum;
      double ui_max = gspec->ui_maximum;

      if (g_object_get_qdata (G_OBJECT (node), rel_quark) && 1)
      {
        ui_min /= 1000.0;
        ui_max /= 1000.0;
      }

      cairo_save (mrg_cr (mrg));
      cairo_translate (mrg_cr (mrg), x + mrg_em(mrg) * 10, y - mrg_em(mrg));
      cairo_rectangle (mrg_cr (mrg), 0, 0,
                       width,
                       mrg_em (mrg));
      cairo_save (mrg_cr (mrg));
      cairo_scale (mrg_cr (mrg), width, 1.0);

      mrg_listen (mrg, MRG_DRAG, drag_int_slider, node, gspec);
      cairo_restore (mrg_cr (mrg));
      cairo_set_source_rgba (mrg_cr (mrg), 1,1,1,1.0);
      cairo_stroke (mrg_cr (mrg));

      cairo_rectangle (mrg_cr (mrg), 0,
                       0,
                       (val - ui_min) / (ui_max - ui_min) * width,
                       mrg_em (mrg)  );
      cairo_set_source_rgba (mrg_cr (mrg), 1,1,1,1.0);
      cairo_fill (mrg_cr (mrg));

      cairo_restore (mrg_cr (mrg));

      str = g_strdup_printf ("%s:%d", props[i]->name, val);
      mrg_printf (mrg, "%s", str);
    }
    else if (g_type_is_a (type, G_TYPE_BOOLEAN))
    {
      gboolean val;
      gegl_node_get (node, props[i]->name, &val, NULL);
      str = g_strdup_printf ("%s:%s", props[i]->name, val?"yes":"no");
      mrg_text_listen (mrg, MRG_CLICK, toggle_bool, node, (void*)g_intern_string(props[i]->name));
      mrg_printf (mrg, "%s", str);
      mrg_text_listen_done (mrg);
    }
    else if (g_type_is_a (type, G_TYPE_STRING))
    {
      char *val = NULL;
      gegl_node_get (node, props[i]->name, &val, NULL);
      mrg_printf (mrg, "%s: \"", props[i]->name);
      if (snode && !strcmp (props[i]->name, sprop))
      {
        mrg_edit_start (mrg, update_string, edl);
      }
      else
        mrg_text_listen (mrg, MRG_CLICK, edit_string, node, (void*)g_intern_string(props[i]->name));
      mrg_printf (mrg, "%s", val);

      if (snode && !strcmp (props[i]->name, sprop))
        mrg_edit_end (mrg);
      else
        mrg_text_listen_done (mrg);
      mrg_printf (mrg, "\"");
      g_free (val);
      str= g_strdup ("");
    }
    else
    {
      str = g_strdup_printf ("%s: [todo: handle this property type]", props[i]->name);
      mrg_printf (mrg, "%s", str);
    }

    if (str)
    {
      g_free (str);
      y -= mrg_em (mrg) * 1.2;
    }

    if (g_object_get_qdata (G_OBJECT (node), rel_quark))
       mrg_printf (mrg, "rel");
    if (g_object_get_qdata (G_OBJECT (node), anim_quark))
    {
       GeglPath *path = g_object_get_qdata (G_OBJECT (node), anim_quark);
       mrg_printf (mrg, "{anim}");

       cairo_t *cr = mrg_cr (mrg);

       cairo_save (cr);

       cairo_scale (cr, 1.0/edl->scale, 1);
       cairo_translate (cr,  (edl->active_clip?edl->active_clip->abs_start:0)-edl->t0,
                        mrg_height (mrg) * SPLIT_VER);

       {
         int i;
         gdouble y = 0.0;
         gdouble miny = 100000.0;
         gdouble maxy = -100000.0;

         // todo: draw markers for zero, min and max, with labels
         //       do all curves in one scaled space? - will break for 2 or more magnitudes diffs

         for (i = -10; i < clip_get_frames (edl->active_clip) + 10; i ++)
         {
           gegl_path_calc_y_for_x (path, i, &y);
           if (y < miny) miny = y;
           if (y > maxy) maxy = y;
         }

         cairo_new_path (cr);
         gegl_path_calc_y_for_x (path, 0, &y);
         y = VID_HEIGHT * 0.9 - ((y - miny) / (maxy - miny)) * VID_HEIGHT * 0.8;
         cairo_move_to (cr, 0, y);
         for (i = -10; i < clip_get_frames (edl->active_clip) + 10; i ++)
         {
           gegl_path_calc_y_for_x (path, i, &y);
           y = VID_HEIGHT * 0.9 - ((y - miny) / (maxy - miny)) * VID_HEIGHT * 0.8;
           cairo_line_to (cr, i, y);
         }
       }


       cairo_restore (cr);
       cairo_set_line_width (cr, 2.0);
       cairo_set_source_rgba (cr, 1.0, 0.0, 0.0, 255);
       cairo_stroke (cr);
    }
    if (g_object_get_qdata (G_OBJECT (node), g_quark_from_string (props[i]->name)))
       mrg_printf (mrg, "{???}");
  }


  return y;
}

static Clip *ui_clip = NULL;
static GeglNode *source_start;
static GeglNode *source_end;
static GeglNode *filter_end;


static void select_node (MrgEvent *e, void *data1, void *data2)
{
  if (selected_node == data1)
    selected_node = NULL;
  else
    selected_node = data1;
  snode = NULL;
  sprop = NULL;

  mrg_event_stop_propagate (e);
  mrg_queue_draw (e->mrg, NULL);
}

float print_nodes (Mrg *mrg, GeglEDL *edl, GeglNode *node, float x, float y)
{
    while (node)
    {

      if ((node != source_start) &&
          (node != source_end) &&
          (node != filter_start) &&
          (node != filter_end))
      {
        if (node == selected_node)
          y = print_props (mrg, edl, node, x + mrg_em(mrg) * 0.5, y);

        mrg_set_xy (mrg, x, y);
        mrg_text_listen (mrg, MRG_CLICK, select_node, node, NULL);
        mrg_printf (mrg, "%s", gegl_node_get_operation (node));
        mrg_text_listen_done (mrg);
        y -= mrg_em (mrg) * 1.5;
      }

      GeglNode **nodes = NULL;
      const gchar **pads = NULL;

      int count = gegl_node_get_consumers (node, "output", &nodes, &pads);
      if (count)
      {
        node = nodes[0];
        if (strcmp (pads[0], "input"))
          node = NULL;
      }
      else
        node = NULL;
      g_free (nodes);
      g_free (pads);
      //if (node && node == clip->nop_crop)
       // node = NULL;
      if (node)
      {
        GeglNode *iter = gegl_node_get_producer (node, "aux", NULL);
        if (iter)
        {
          GeglNode *next;
          do {
            next = gegl_node_get_producer (iter, "input", NULL);
            if (next) iter = next;
          } while(next);

          y = print_nodes (mrg, edl, iter, x + mrg_em (mrg) * 2, y);
        }
      }
    }
    return y;
}

void update_ui_clip (Clip *clip, int clip_frame_no)
{
  GError *error = NULL;
  if (ui_clip == NULL ||
      ui_clip != clip)
  {
    selected_node = NULL;
    snode = NULL;
     if (source_start)
      {
        remove_in_betweens (source_start, source_end);
        g_object_unref (source_start);
        source_start = NULL;
        g_object_unref (source_end);
        source_end = NULL;
      }
     if (filter_start)
      {
        remove_in_betweens (filter_start, filter_end);
        g_object_unref (filter_start);
        filter_start = NULL;
        g_object_unref (filter_end);
        filter_end = NULL;
      }

    source_start = gegl_node_new ();
    source_end   = gegl_node_new ();

    gegl_node_set (source_start, "operation", "gegl:nop", NULL);
    gegl_node_set (source_end, "operation", "gegl:nop", NULL);
    gegl_node_link_many (source_start, source_end, NULL);
    gegl_create_chain (clip->path, source_start, source_end,
                       clip->edl->frame_no - clip->abs_start,
                       1.0, NULL, &error);

    filter_start = gegl_node_new ();
    filter_end = gegl_node_new ();

    gegl_node_set (filter_start, "operation", "gegl:nop", NULL);
    gegl_node_set (filter_end,   "operation", "gegl:nop", NULL);

    gegl_node_link_many (filter_start, filter_end, NULL);
    gegl_create_chain (clip->filter_graph, filter_start, filter_end,
                       clip->edl->frame_no - clip->abs_start,
                       1.0, NULL, &error);
    ui_clip = clip;
  }

  if (selected_node)
  {
    unsigned int n_props;

    if (ui_tweaks)
    {
      char *serialized_filter = NULL;
      char *serialized_source = NULL;
      serialized_filter = gegl_serialize (filter_start, filter_end,
                                   NULL,GEGL_SERIALIZE_TRIM_DEFAULTS|GEGL_SERIALIZE_VERSION);
      serialized_source = gegl_serialize (source_start, source_end,
                                   NULL,GEGL_SERIALIZE_TRIM_DEFAULTS|GEGL_SERIALIZE_VERSION);

      if (clip->filter_graph)
      {
        gchar *old = clip->filter_graph;

        if (g_str_has_suffix (serialized_filter, "gegl:nop opi=0:0"))
        { /* XXX: ugly hack - we remove the common bit we do not want */
          serialized_filter[strlen(serialized_filter)-strlen("gegl:nop opi=0:0")]='\0';
        }
        clip->filter_graph = serialized_filter;
        g_free (old);
      }
      else
        g_free (serialized_filter);

      if (clip->is_chain)
      {
        if (g_str_has_suffix (serialized_source, "gegl:nop opi=0:0"))
        { /* XXX: ugly hack - we remove the common bit we do not want */
          serialized_source[strlen(serialized_source)-strlen("gegl:nop opi=0:0")]='\0';
        }
        clip_set_path (clip, serialized_source);
      }
      else
        g_free (serialized_source);
      ui_tweaks = 0;
      changed ++;

      gedl_cache_invalid (clip->edl);
    }

    GParamSpec ** props = gegl_operation_list_properties (gegl_node_get_operation (selected_node), &n_props);

    for (int i = 0; i <n_props; i ++)
    {
      char tmpbuf[1024];
      sprintf (tmpbuf, "%s-anim", props[i]->name);
      GQuark anim_quark = g_quark_from_string (tmpbuf);
      // this only deals with double for now
      if (g_object_get_qdata (G_OBJECT (selected_node), anim_quark))
      {
        GeglPath *path = g_object_get_qdata (G_OBJECT (selected_node), anim_quark);
        gdouble val = 0.0;
        gegl_path_calc_y_for_x (path, clip_frame_no * 1.0, &val);

        gegl_node_set (selected_node, props[i]->name, val, NULL);
      }
    }
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
  int duration = gedl_get_duration (edl); // causes update of abs_start

  VID_HEIGHT = mrg_height (mrg) * (1.0 - SPLIT_VER) * 0.8;
  int scroll_height = mrg_height (mrg) * (1.0 - SPLIT_VER) * 0.2;
  t = 0;
  int clip_frame_no;

  if (duration == 0)
    return;

  float y2 = y - mrg_em (mrg) * 1.5;

  edl->active_clip = gedl_get_clip (edl, edl->frame_no, &clip_frame_no);

  if (edl->active_clip) // && edl->active_clip->filter_graph)
  {
    Clip *clip = edl->active_clip;

    update_ui_clip (clip, clip_frame_no);

    mrg_set_style (mrg, "font-size: 3%; background-color: #0008; color: #fff");

    if (clip->is_chain)
    {
      GeglNode *iter = source_end;
      while (gegl_node_get_producer (iter, "input", NULL))
      {
        iter = gegl_node_get_producer (iter, "input", NULL);
      }
      y2 = print_nodes (mrg, edl, iter, mrg_em (mrg), y2);
    }
    else
    {
      mrg_set_xy (mrg, mrg_em(mrg) * 1, y2);
      mrg_printf (mrg, "%s", clip->path);
      y2 -= mrg_em (mrg) * 1.5;
    }
    y2 = print_nodes (mrg, edl, filter_start, mrg_em (mrg), y2);
  }

  cairo_set_source_rgba (cr, 1, 1,1, 1);

  if (edl->playing)
  {
    scroll_to_fit (edl, mrg);
    t0 = edl->t0;
  }

  cairo_save (cr);
  {
    cairo_scale (cr, 1.0 / duration * mrg_width (mrg), 1.0);
  }

  y += VID_HEIGHT;

  cairo_rectangle (cr, t0, y, mrg_width(mrg)*fpx, scroll_height);
  mrg_listen (mrg, MRG_DRAG, drag_t0, edl, edl);
  cairo_set_source_rgba (cr, 1, 1, 0.5, 0.25);
  if (edl->playing)
  cairo_stroke (cr);
  else
  cairo_fill (cr);

  cairo_rectangle (cr, t0 + mrg_width(mrg)*fpx*0.9, y, mrg_width(mrg)*fpx * 0.1, scroll_height);
  mrg_listen (mrg, MRG_DRAG, drag_fpx, edl, edl);
  cairo_fill (cr);

  /* we could cull drawing already here, we let cairo do it for now, */

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int frames = clip_get_frames (clip);
    cairo_rectangle (cr, t, y, frames, scroll_height);
    cairo_stroke (cr);
    t += frames;
  }

  int start = 0, end = 0;
  gedl_get_range (edl, &start, &end);
  cairo_rectangle (cr, start, y, end - start, scroll_height);
  cairo_set_source_rgba (cr, 0, 0.11, 0.0, 0.5);
  cairo_fill_preserve (cr);
  cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
  cairo_stroke (cr);

  {
    double frame = edl->frame_no;
    if (fpx < 1.0)
      cairo_rectangle (cr, frame, y-5, 1.0, 5 + scroll_height);
    else
      cairo_rectangle (cr, frame, y-5, fpx, 5 + scroll_height);
    cairo_set_source_rgba (cr,1,0,0,0.85);
    cairo_fill (cr);
  }

  cairo_restore (cr);
  y -= VID_HEIGHT;
  t = 0;

  cairo_move_to (cr, x0 + PAD_DIM, y + VID_HEIGHT + PAD_DIM * 3);

  cairo_save (cr);
  cairo_translate (cr,  x0, 0);
  cairo_scale (cr, 1.0/fpx, 1);
  cairo_translate (cr, -t0, 0);

  gedl_get_selection (edl, &start, &end);
  cairo_rectangle (cr, start + 0.5, y - PAD_DIM, end - start, VID_HEIGHT + PAD_DIM * 2);
  cairo_set_source_rgba (cr, 1, 0, 0, 0.75);
  cairo_fill (cr);

  cairo_rectangle (cr, t0, y, mrg_width(mrg)*fpx, VID_HEIGHT);
  mrg_listen (mrg, MRG_DROP, drag_dropped, edl, edl);
  cairo_new_path (cr);

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int frames = clip_get_frames (clip);
    if (clip->is_meta)
    {
      double tx = t, ty = y;
      cairo_save (cr);
      cairo_user_to_device (cr, &tx, &ty);
      cairo_identity_matrix (cr);
      mrg_set_xy (mrg, tx, y + VID_HEIGHT);
      mrg_printf (mrg, "%s", clip->filter_graph); // only used for annotations for now - could script vars
      cairo_restore (cr);
    }
    else
    {
      Clip *next = clip_get_next (clip);
      render_clip (mrg, edl, clip->path, clip->start, frames, t, y, clip->fade, next?next->fade:0);
      /* .. check if we are having anim things going on.. if so - print it here  */
    }

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

  if (!edl->playing){
     static gint bitlen = 0;
     static guchar *bitmap;
     static long bitticks = 0;
     int i;
     int state = -1;
     int length = 0;

     if (bitlen && ( babl_ticks() - bitticks > 1000 * 1000 * 2))
     {
       /* update cache bitmap if it is more than 2s old */
       bitlen = 0;
       g_free (bitmap);
       bitmap = NULL;
     }

     if (bitlen == 0)
     {
       bitmap = gedl_get_cache_bitmap (edl, &bitlen);
       bitticks = babl_ticks ();
     }
     cairo_set_source_rgba (cr, 0.3, 1, 0.3, 1.0);
     for (i = 0; i < bitlen * 8; i++)
     {
        if (bitmap[i / 8] & (1<< (i%8)))
        {
          if (state == 1)
          {
            length++;
          }
          else
          {
            length = 0;
            state = 1;
          }
        }
        else
        {
          if (state == 0)
          {
            length++;
          }
          else
          {
            cairo_rectangle (cr, i-length, y, length + 1, VID_HEIGHT * 0.05);
            length = 0;
            state = 0;
          }
        }
     }
     cairo_fill (cr);
  }

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

#if 0
static void edit_filter_graph (MrgEvent *event, void *data1, void *data2)
{ //XXX
  GeglEDL *edl = data1;

  //edl->active_source = NULL;
  edl->filter_edited = !edl->filter_edited;
  mrg_queue_draw (event->mrg, NULL);
}
#endif

#if 0
static void update_filter (const char *new_string, void *user_data)
{
  GeglEDL *edl = user_data;
  if (edl->active_clip->filter_graph)
    g_free (edl->active_clip->filter_graph);
  edl->active_clip->filter_graph = g_strdup (new_string);
  gedl_cache_invalid (edl);
  mrg_queue_draw (edl->mrg, NULL);
}
#endif

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

void help_ui (Mrg *mrg, GeglEDL *edl)
{
  if (help)
  {
    MrgBinding * bindings = mrg_get_bindings (mrg);
    int i;

    //cairo_set_source_rgba (mrg_cr (mrg), 0, 0, 0, 0.85);
    //cairo_paint (mrg_cr(mrg));
    mrg_set_font_size (mrg, mrg_height (mrg) / 20.0);
  mrg_set_style (mrg, "color: white;background: transparent; text-stroke: 4.5px #000");
    mrg_set_edge_right (mrg, mrg_width (mrg) - mrg_em (mrg) *2);
    mrg_set_edge_left (mrg, mrg_em (mrg));
    mrg_set_xy (mrg, mrg_em (mrg), mrg_em (mrg) * 2);


    for (i = 0; bindings[i].cb; i++)
    {
      if (bindings[i].label)
        mrg_printf_xml (mrg, "<div style='display:inline-block; padding-right: 1em;'><b>%s</b>&nbsp;%s</div>  ", bindings[i].nick, bindings[i].label);
    }
  }
  else
  {
    mrg_set_xy (mrg, mrg_width(mrg) - 10 * mrg_em (mrg), mrg_height(mrg) * SPLIT_VER);
    mrg_printf (mrg, "F1 toggle help");
  }
}

gchar *message = NULL;

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

  if (message)
  {
    mrg_set_style (mrg, "font-size: 0.1yh");
    mrg_printf (mrg, "%s", message);
    return;
  }

  /* XXX: sync ui changes here, rather than deferred  */

  g_mutex_lock (&edl->buffer_copy_mutex);
  if (edl->buffer_copy_temp)
    g_object_unref (edl->buffer_copy_temp);
  edl->buffer_copy_temp = edl->buffer_copy;
  g_object_ref (edl->buffer_copy);
  gegl_node_set (edl->cached_result, "buffer", edl->buffer_copy_temp, NULL);
  g_mutex_unlock (&edl->buffer_copy_mutex);

  switch (edl->ui_mode)
  {
     case GEDL_UI_MODE_FULL:
     case GEDL_UI_MODE_TIMELINE:
     case GEDL_UI_MODE_NONE:
        mrg_gegl_blit (mrg, (int)(mrg_width (mrg) * 0.0), 0,
                      (int)(mrg_width (mrg) * 1.0),
                      mrg_height (mrg),// * SPLIT_VER,
                      o->edl->cached_result,
                      0, 0,
        /* opacity */ 1.0 //edl->frame_no == done_frame?1.0:0.5
                      ,edl);
        break;
     case GEDL_UI_MODE_PART:
        mrg_gegl_blit (mrg, (int)(mrg_width (mrg) * 0.2), 0,
                      (int)(mrg_width (mrg) * 0.8),
                      mrg_height (mrg) * SPLIT_VER,
                      o->edl->cached_result,
                      0, 0,
        /* opacity */ 1.0 //edl->frame_no == done_frame?1.0:0.5
                      ,edl);
        break;
  }


  switch (edl->ui_mode)
  {
     case GEDL_UI_MODE_FULL:
     case GEDL_UI_MODE_TIMELINE:
     case GEDL_UI_MODE_PART:
     gedl_draw (mrg, edl, 0, mrg_height (mrg) * SPLIT_VER, edl->scale, edl->t0);




  break;
     case GEDL_UI_MODE_NONE:
        break;
     break;
  }

  if (edl->ui_mode != GEDL_UI_MODE_NONE)
  {

  mrg_set_xy (mrg, mrg_em (mrg), mrg_height(mrg) * SPLIT_VER);
  mrg_set_style (mrg, "color: white;background: transparent; text-stroke: 1.5px #000");
  mrg_set_edge_right (mrg, mrg_width (mrg));// * 0.25 - 8);
#if 0
  {
    GeglRectangle rect;
    rect = gegl_node_get_bounding_box (o->edl->cached_result);

    mrg_printf (mrg, "%ix%i\n", rect.width, rect.height);
  }
#endif

#if 0
  mrg_printf (mrg, "cache hit: %2.2f%% of %i\n", 100.0 * cache_hits / (cache_hits + cache_misses), cache_hits + cache_misses);
#endif

#if 0
  if (done_frame != edl->frame_no)
    mrg_printf (mrg, "frame %i (%i shown)",edl->frame_no, done_frame);
  else
#endif
  mrg_printf (mrg, " %i  ", edl->frame_no);

#if 0
  if (edl->active_source)
  {
    char *basename = g_path_get_basename (edl->active_source->path);
    mrg_printf (mrg, "%i\n", edl->source_frame_no);
    mrg_printf (mrg, "%s\n", basename);
  }
#endif

  //mrg_printf (mrg, "%i %i %i %i %i\n", edl->frame, edl->frame_no, edl->source_frame_no, rendering_frame, done_frame);

  if (!renderer_done (edl))
    mrg_printf (mrg, "... ");

  }

  if (!edl->clip_query_edited &&
      !edl->filter_edited &&
      !snode)
  {
    mrg_add_binding (mrg, "F1", NULL, "toggle help", toggle_help, edl);
    mrg_add_binding (mrg, "q", NULL, "quit", (void*)do_quit, mrg);

    if (edl->playing)
    {
      mrg_add_binding (mrg, "space", NULL, "pause", renderer_toggle_playing, edl);
      if (edl->active_clip && edl->frame_no != edl->active_clip->abs_start)
        mrg_add_binding (mrg, "v", NULL, "split clip", split_clip, edl);
    }
    else
    {
      mrg_add_binding (mrg, "space", NULL, "play", renderer_toggle_playing, edl);

      mrg_add_binding (mrg, "tab", NULL, "cycle ui amount", toggle_ui_mode, edl);
      mrg_add_binding (mrg, "e", NULL, "zoom timeline to fit", zoom_fit, edl);
      mrg_add_binding (mrg, "1", NULL, "zoom timeline 1px = 1 frame", zoom_1, edl);
      if (edl->use_proxies)
        mrg_add_binding (mrg, "p", NULL, "don't use proxies", toggle_use_proxies, edl);
      else
        mrg_add_binding (mrg, "p", NULL, "use proxies", toggle_use_proxies, edl);

      mrg_add_binding (mrg, "s", NULL, "save", save, edl);
      mrg_add_binding (mrg, "a", NULL, "select all", select_all, edl);

      mrg_add_binding (mrg, "left/right", NULL, "step frame", step_frame, edl);
      mrg_add_binding (mrg, "right", NULL, NULL, step_frame, edl);
      mrg_add_binding (mrg, "left", NULL, NULL, step_frame_back, edl);
      mrg_add_binding (mrg, "l", NULL, NULL, step_frame, edl);
      mrg_add_binding (mrg, "h", NULL, NULL, step_frame_back, edl);

      mrg_add_binding (mrg, "up/down", NULL, "previous/next cut", prev_cut, edl);
      mrg_add_binding (mrg, "up", NULL, NULL, prev_cut, edl);
      mrg_add_binding (mrg, "k", NULL, NULL, prev_cut, edl);
      mrg_add_binding (mrg, "down", NULL, NULL, next_cut, edl);
      mrg_add_binding (mrg, "j", NULL, NULL, next_cut, edl);

      mrg_add_binding (mrg, "shift-left/right", NULL, "extend selection", extend_selection_to_the_right, edl);
      mrg_add_binding (mrg, "shift-right", NULL, NULL, extend_selection_to_the_right, edl);
      mrg_add_binding (mrg, "shift-left", NULL, NULL,  extend_selection_to_the_left, edl);
      mrg_add_binding (mrg, "shift-up", NULL, NULL,    extend_selection_to_previous_cut, edl);
      mrg_add_binding (mrg, "shift-down", NULL, NULL,  extend_selection_to_next_cut, edl);
      mrg_add_binding (mrg, "L", NULL, NULL, extend_selection_to_the_right, edl);
      mrg_add_binding (mrg, "H", NULL, NULL,  extend_selection_to_the_left, edl);
      mrg_add_binding (mrg, "K", NULL, NULL,    extend_selection_to_previous_cut, edl);
      mrg_add_binding (mrg, "J", NULL, NULL,  extend_selection_to_next_cut, edl);

      if (empty_selection (edl))
      {
        mrg_add_binding (mrg, "x", NULL, "remove clip", remove_clip, edl);
        mrg_add_binding (mrg, "d", NULL, "duplicate clip", duplicate_clip, edl);
        //mrg_add_binding (mrg, "i", NULL, "insert clip", insert, edl);

        if (edl->active_clip)
        {
          if (edl->frame_no == edl->active_clip->abs_start)
          {
            GList *iter = g_list_find (edl->clips, edl->active_clip);
            Clip *clip2 = NULL;
            if (iter) iter = iter->prev;
            if (iter) clip2 = iter->data;


            if (are_mergable (clip2, edl->active_clip, 0))
              mrg_add_binding (mrg, "v", NULL, "merge clip", merge_clip, edl);
          }
          else
          {
            mrg_add_binding (mrg, "v", NULL, "split clip", split_clip, edl);
          }
          mrg_add_binding (mrg, "f", NULL, "toggle fade", toggle_fade, edl);
        }

      }
      else
      {
        mrg_add_binding (mrg, "x", NULL, "cut selection", remove_clip, edl);
        mrg_add_binding (mrg, "c", NULL, "copy selection", remove_clip, edl);
        mrg_add_binding (mrg, "r", NULL, "set playback range", set_range, edl);
      }

      if (edl->active_clip)
      {
        mrg_add_binding (mrg, "i", NULL, "insert filter", insert_filter, edl);

        if (edl->frame_no == edl->active_clip->abs_start)
        {

          if (empty_selection (edl))
          {
            mrg_add_binding (mrg, "control-left/right", NULL, "adjust in", clip_start_inc, edl);
            mrg_add_binding (mrg, "control-right", NULL, NULL, clip_start_inc, edl);
            mrg_add_binding (mrg, "control-left", NULL, NULL, clip_start_dec, edl);
            mrg_add_binding (mrg, "control-h", NULL, NULL, clip_start_inc, edl);
            mrg_add_binding (mrg, "control-l", NULL, NULL, clip_start_dec, edl);
            mrg_add_binding (mrg, "control-up/down", NULL, "shuffle clip backward/forward", shuffle_back, edl);
            mrg_add_binding (mrg, "control-up", NULL, NULL, shuffle_back, edl);
            mrg_add_binding (mrg, "control-down", NULL, NULL, shuffle_forward, edl);
            mrg_add_binding (mrg, "control-k", NULL, NULL, shuffle_back, edl);
            mrg_add_binding (mrg, "control-j", NULL, NULL, shuffle_forward, edl);
          }
        }
        else
        {
          if (empty_selection (edl))
          {
            if (edl->frame_no == edl->active_clip->abs_start + clip_get_frames (edl->active_clip)-1)
            {
              mrg_add_binding (mrg, "control-left/right", NULL, "adjust out", clip_end_inc, edl);
              mrg_add_binding (mrg, "control-right", NULL, NULL, clip_end_inc, edl);
              mrg_add_binding (mrg, "control-left", NULL, NULL, clip_end_dec, edl);
            }
            else
            {
              mrg_add_binding (mrg, "control-left/right", NULL, "slide clip backward/forward", slide_back, edl);
              mrg_add_binding (mrg, "control-left", NULL, NULL, slide_back, edl);
              mrg_add_binding (mrg, "control-right", NULL, NULL, slide_forward, edl);


              mrg_add_binding (mrg, "control-up/down", NULL, "slide cut window", clip_start_end_inc, edl);
              mrg_add_binding (mrg, "control-up", NULL, NULL, clip_start_end_inc, edl);
              mrg_add_binding (mrg, "control-down", NULL, NULL, clip_start_end_dec, edl);
            }
          }
          else {
            Clip *start_clip = gedl_get_clip (edl, edl->selection_start, NULL);
            Clip *end_clip = gedl_get_clip (edl, edl->selection_end, NULL);
            GList *start_iter = g_list_find (edl->clips, start_clip);
            GList *end_iter = g_list_find (edl->clips, end_clip);

            if (start_iter &&
                (start_iter->next == end_iter ||
                start_iter->prev == end_iter))
            {
              mrg_add_binding (mrg, "control-left/right", NULL, "move cut", clip_end_start_inc, edl);
              mrg_add_binding (mrg, "control-right", NULL, NULL, clip_end_start_inc, edl);
              mrg_add_binding (mrg, "control-left", NULL, NULL, clip_end_start_dec, edl);
            }
          }
        }
      }
    }
  }

#if 0
  if (edl->active_source)
    mrg_add_binding (mrg, "return", NULL, NULL, toggle_edit_source, edl);
  else //if (edl->filter_edited)
    mrg_add_binding (mrg, "return", NULL, NULL, edit_filter_graph, edl);
#endif

  switch (edl->ui_mode)
  {
     case GEDL_UI_MODE_FULL:
     case GEDL_UI_MODE_TIMELINE:
     case GEDL_UI_MODE_PART:
     default:
        help_ui (mrg, edl);
        break;
     case GEDL_UI_MODE_NONE:
        break;
  }
}

gboolean cache_renderer_iteration (Mrg *mrg, gpointer data)
{
  GeglEDL *edl = data;
  if (!edl->playing)
    {
      int i;
      int render_slaves = g_get_num_processors ();
      killpg(0, SIGUSR2); // this will cause previous set of renderers to quite after current frame
      for (i = 0; i < render_slaves; i ++)
      {
        char *cmd = g_strdup_printf ("%s %s cache %i %i&",
                                     gedl_binary_path,
                                     edl->path, i, render_slaves);
        save_edl (edl);
        system (cmd);
        g_free (cmd);
      }
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

  edl->cache_flags = CACHE_TRY_ALL;// | CACHE_MAKE_ALL;
  mrg_set_ui (mrg, gedl_ui, &o);

  mrg_add_timeout (mrg, 10100, save_idle, edl);

  cache_renderer_iteration (mrg, edl);
  mrg_add_timeout (mrg, 90 /* seconds */  * 1000, cache_renderer_iteration, edl);

  gedl_get_duration (edl);
  mrg_set_target_fps (mrg, -1);
//  gedl_set_use_proxies (edl, 1);
  toggle_use_proxies (NULL, edl, NULL);
  renderer_start (edl);
  mrg_main (mrg);
  gedl_free (edl);
  gegl_exit ();

  return 0;
}
