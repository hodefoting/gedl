#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <mrg.h>
#include <gegl.h>
#include "gedl.h"
#include "renderer.h"


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

static void clicked_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;

  edl->frame_no = e->x;
  edl->selection_start = edl->frame_no;
  edl->selection_end = edl->frame_no;
  edl->active_clip = clip;
  edl->active_source = NULL;
  edl->playing = 0;
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

static void scroll_to_fit (GeglEDL *edl, Mrg *mrg);


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

static int are_mergable (Clip *clip1, Clip *clip2, int delta)
{
  if (!clip1 || !clip2)
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

  clip_remove (edl->active_clip);
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

#if 0
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
  if (edl->active_source)
    edl->source_frame_no --;
  else
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
  if (edl->active_source)
  {
    edl->source_frame_no ++;
  }
  else
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
  if (edl->active_source)
  {
      edl->active_source->end++;
      edl->active_source->start++;
  }
  else if (edl->active_clip)
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
  if (edl->active_source)
  {
      edl->active_source->end--;
      edl->active_source->start--;
  }
  else if (edl->active_clip)
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
  if (edl->active_source)
  {
      edl->active_source->end++;
  }
  else if (edl->active_clip)
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
  if (edl->active_source)
  {
      edl->active_source->end--;
      gedl_cache_invalid (edl);
  }
  else if (edl->active_clip)
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

void render_clip (Mrg *mrg, GeglEDL *edl, const char *clip_path, int clip_start, int clip_frames, double x, double y)
{
  char *thumb_path = gedl_make_thumb_path (edl, clip_path);

  cairo_t *cr = mrg_cr (mrg);
  cairo_rectangle (cr, x, y, clip_frames, VID_HEIGHT);

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
  if ( (edl->frame_no - edl->t0) / edl->scale > mrg_width (mrg) * 1.0)
    edl->t0 = edl->frame_no - (mrg_width (mrg) * 0.8) * edl->scale;
  else if ( (edl->frame_no - edl->t0) / edl->scale < mrg_width (mrg) * 0.0)
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
    else {
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

static void zoom_fit (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_cache_invalid (edl);
  edl->t0 = 0.0;
  edl->scale = gedl_get_duration (edl) * 1.0 / mrg_width (event->mrg);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
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
  int duration = gedl_get_duration (edl);

  VID_HEIGHT = mrg_height (mrg) * (1.0 - SPLIT_VER) * 0.8;
  int scroll_height = mrg_height (mrg) * (1.0 - SPLIT_VER) * 0.2;
  t = 0;

  if (duration == 0)
    return;

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

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    int frames = clip_get_frames (clip);

    //if (clip == edl->active_clip)
    //  cairo_set_source_rgba (cr, 1, 1, 0.5, 1.0);
    //else
    //  cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
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


  //cairo_show_text (cr, edl->path);
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

     //g_free (bitmap);
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

static void update_clip_title (const char *new_string, void *user_data)
{
  SourceClip *clip = user_data;
  if (clip->title)
          g_free (clip->title);
  clip->title = g_strdup (new_string);
  changed++;
}

#if 0
static void edit_filter_graph (MrgEvent *event, void *data1, void *data2)
{ //XXX
  GeglEDL *edl = data1;

  //edl->active_source = NULL;
  edl->filter_edited = !edl->filter_edited;
  mrg_queue_draw (event->mrg, NULL);
}
#endif

static void update_query (const char *new_string, void *user_data)
{
  GeglEDL *edl = user_data;
  if (edl->clip_query)
    g_free (edl->clip_query);
  changed++;
  edl->clip_query = g_strdup (new_string);
}

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

void trim_ui (Mrg *mrg, GeglEDL *edl)
{
  float h = mrg_height (mrg);
  float w = mrg_width (mrg);
  mrg_start (mrg, "trim-ui", NULL);
  mrg_set_style (mrg, "color: white;background: transparent; text-stroke: 1.5px #000");
  mrg_set_font_size (mrg, h / 14.0);
  mrg_set_edge_right (mrg, w);
  mrg_set_edge_left (mrg, 0);

#define print_str_at(x,y,str) do{\
   mrg_set_xy (mrg, w * x, h * y);\
     mrg_printf (mrg, str); } while(0)

  print_str_at (0.1, 0.3, "(insert)");
  print_str_at (0.3, 0.3, "cut");
  print_str_at (0.6, 0.3, "split");
  print_str_at (0.8, 0.3, "ui");

  print_str_at (0.1, 0.4, "copy");
  print_str_at (0.3, 0.4, "paste");
  print_str_at (0.6, 0.4, "(merge)");
  print_str_at (0.8, 0.4, "(filters)");

  print_str_at (0.1, 0.7, "|<");
  print_str_at (0.3, 0.7, "<");

  if (edl->playing)
    print_str_at (0.5, 0.7, "||");
  else
    print_str_at (0.5, 0.7, "|>");

  print_str_at (0.7, 0.7, ">");
  print_str_at (0.9, 0.7, ">|");

  print_str_at (0.1, 0.6, "]");
  print_str_at (0.3, 0.6, "[");


  print_str_at (0.5, 0.6, "][");
  print_str_at (0.6, 0.6, "slip");
  print_str_at (0.8, 0.6, "slide");

#if 0

 |<-        |>         ->|

 ]     [          ]      [

 ][   slip    slide     ][
#endif
    mrg_end (mrg);

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
     case GEDL_UI_MODE_TIMELINE:
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

  if (edl->active_clip)
    {
      char *basename = g_path_get_basename (edl->active_clip->path);
      mrg_printf (mrg, "| %s %i-%i %i  ", basename,
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

#if 0
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
#endif
    }
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
    mrg_printf (mrg, "rendering ");

  switch (edl->ui_mode)
  {
     case GEDL_UI_MODE_FULL:
       if (!edl->playing && 0)
          trim_ui (mrg, edl);
     case GEDL_UI_MODE_TIMELINE:
     case GEDL_UI_MODE_PART:
  break;
     case GEDL_UI_MODE_NONE:
        break;
     break;
  }


  }

  if (!edl->clip_query_edited &&
      !edl->filter_edited
                  && (
      !edl->active_source   ||
      edl->active_source->editing == 0))
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
      mrg_add_binding (mrg, "e", NULL, "zoom fit", zoom_fit, edl);
      if (edl->use_proxies)
        mrg_add_binding (mrg, "p", NULL, "don't use proxies", toggle_use_proxies, edl);
      else
        mrg_add_binding (mrg, "p", NULL, "use proxies", toggle_use_proxies, edl);

      //mrg_add_binding (mrg, "s", NULL, "save", save, edl);
      mrg_add_binding (mrg, "a", NULL, "select all", select_all, edl);

      mrg_add_binding (mrg, "left/right", NULL, "step frame", step_frame, edl);
      mrg_add_binding (mrg, "right", NULL, NULL, step_frame, edl);
      mrg_add_binding (mrg, "left", NULL, NULL, step_frame_back, edl);

      mrg_add_binding (mrg, "up/down", NULL, "previous/next cut", prev_cut, edl);
      mrg_add_binding (mrg, "up", NULL, NULL, prev_cut, edl);
      mrg_add_binding (mrg, "down", NULL, NULL, next_cut, edl);

      mrg_add_binding (mrg, "shift-left/right", NULL, "extend selection", extend_selection_to_the_right, edl);
      mrg_add_binding (mrg, "shift-right", NULL, NULL, extend_selection_to_the_right, edl);
      mrg_add_binding (mrg, "shift-left", NULL, NULL,  extend_selection_to_the_left, edl);
      mrg_add_binding (mrg, "shift-up", NULL, NULL,    extend_selection_to_previous_cut, edl);
      mrg_add_binding (mrg, "shift-down", NULL, NULL,  extend_selection_to_next_cut, edl);

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

            //mrg_add_binding (mrg, "f", NULL, "toggle fade", toggle_fade, edl);

            if (are_mergable (clip2, edl->active_clip, 0))
              mrg_add_binding (mrg, "v", NULL, "merge clip", merge_clip, edl);
          }
          else
          {
            mrg_add_binding (mrg, "v", NULL, "split clip", split_clip, edl);
          }
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

        if (edl->frame_no == edl->active_clip->abs_start)
        {

          if (empty_selection (edl))
          {
            mrg_add_binding (mrg, "control-left/right", NULL, "adjust in", clip_start_inc, edl);
            mrg_add_binding (mrg, "control-right", NULL, NULL, clip_start_inc, edl);
            mrg_add_binding (mrg, "control-left", NULL, NULL, clip_start_dec, edl);
            mrg_add_binding (mrg, "control-up/down", NULL, "shuffle clip backward/forward", shuffle_back, edl);
            mrg_add_binding (mrg, "control-up", NULL, NULL, shuffle_back, edl);
            mrg_add_binding (mrg, "control-down", NULL, NULL, shuffle_forward, edl);
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
        char *cmd = g_strdup_printf ("gedl %s cache %i %i&", edl->path, i, render_slaves);
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
