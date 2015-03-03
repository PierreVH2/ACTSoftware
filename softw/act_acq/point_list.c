// #include <act_log.h>
#include <stdlib.h>
#include "point_list.h"

#define MIN_LENGTH  10

static void point_list_instance_init(GObject *point_list);
static void point_list_class_init(PointListClass *klass);
static void point_list_instance_dispose(GObject *point_list);
static gboolean point_list_expand(PointList *list);

GType point_list_get_type(void)
{
  static GType point_list_type = 0;
  
  if (!point_list_type)
  {
    const GTypeInfo point_list_info =
    {
      sizeof (PointListClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) point_list_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (PointList),
      0,
      (GInstanceInitFunc) point_list_instance_init,
      NULL
    };
    
    point_list_type = g_type_register_static (G_TYPE_OBJECT, "PointList", &point_list_info, 0);
  }
  
  return point_list_type;
}

PointList *point_list_new(void)
{
  PointList *list = g_object_new (point_list_get_type(), NULL);
  list->points = malloc(MIN_LENGTH*sizeof(Point));
  if (list->points == NULL)
  {
    g_object_unref(list);
    return NULL;
  }
  list->num_alloc = MIN_LENGTH;
  return list;
}

PointList *point_list_new_with_length(guint length)
{
  PointList *list = g_object_new (point_list_get_type(), NULL);
  list->points = malloc(length*sizeof(Point));
  if (list->points == NULL)
  {
    g_object_unref(list);
    return NULL;
  }
  list->num_alloc = length;
  return list;
}

void point_list_clear(PointList *list)
{
  free(list->points);
  list->num_alloc = 0;
  list->num_used = 0;
}

gboolean point_list_append(PointList *list, gdouble x, gdouble y)
{
  if (list->num_used == list->num_alloc)
  {
    if (!point_list_expand(list))
      return FALSE;
  }
  list->points[list->num_used].x = x;
  list->points[list->num_used].y = y;
  list->num_used++;
  return TRUE;
}

gboolean point_list_append_point(PointList *list, Point *point)
{
  return point_list_append(list, point->x, point->y);
}

gboolean point_list_remove(PointList *list, guint index)
{
  if (index >= list->num_used)
    return FALSE;
  guint i;
  for (i=index; i<list->num_used-1; i++)
  {
    list->points[i].x = list->points[i+1].x;
    list->points[i].y = list->points[i+1].y;
  }
  list->num_used--;
  return TRUE;
}

guint point_list_get_num_used(PointList *list)
{
  return list->num_used;
}

guint point_list_get_num_alloc(PointList *list)
{
  return list->num_alloc;
}

gboolean point_list_get_coord(PointList *list, guint index, gdouble *x, gdouble *y)
{
  if (index >= list->num_used)
    return FALSE;
  *x = list->points[index].x;
  *y = list->points[index].y;
  return TRUE;
}

gboolean point_list_get_x(PointList *list, guint index, gdouble *x)
{
  if (index >= list->num_used)
    return FALSE;
  *x = list->points[index].x;
  return TRUE;
}

gboolean point_list_get_y(PointList *list, guint index, gdouble *y)
{
  if (index >= list->num_used)
    return FALSE;
  *y = list->points[index].y;
  return TRUE;
}

static void point_list_instance_init(GObject *point_list)
{
  PointList *list = POINT_LIST(point_list);
  list->points = NULL;
  list->num_alloc = 0;
  list->num_used = 0;
}

static void point_list_class_init(PointListClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = point_list_instance_dispose;
}

static void point_list_instance_dispose(GObject *point_list)
{
  PointList *list = POINT_LIST(point_list);
  point_list_clear(list);
}

static gboolean point_list_expand(PointList *list)
{
  guint new_alloc = 3*list->num_alloc;
  Point *new_list = malloc(new_alloc*sizeof(Point));
  if (new_list == NULL)
    return FALSE;
  guint i;
  for (i=0; i<list->num_used; i++)
  {
    new_list[i].x = list->points[i].x;
    new_list[i].y = list->points[i].y;
  }
  free(list->points);
  list->points = new_list;
  list->num_alloc = new_alloc;
  return TRUE;
}
