#ifndef __POINT_LIST_H__
#define __POINT_LIST_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define POINT_LIST_TYPE              (point_list_get_type())
#define POINT_LIST(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), POINT_LIST_TYPE, PointList))
#define POINT_LIST_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), POINT_LIST_TYPE, PointListClass))
#define IS_POINT_LIST(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), POINT_LIST_TYPE))
#define IS_POINT_LIST_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), POINT_LIST_TYPE))

typedef struct _Point           Point;

struct _Point
{
  double x,y;
};

typedef struct _PointList       PointList;
typedef struct _PointListClass  PointListClass;

struct _PointList
{
  GObject parent;
  
  guint num_alloc, num_used;
  Point *points;
};

struct _PointListClass
{
  GObjectClass parent_class;
};

GType point_list_get_type(void);
PointList *point_list_new(void);
PointList *point_list_new_with_length(guint length);
void point_list_clear(PointList *list);
gboolean point_list_append(PointList *list, gdouble x, gdouble y);
gboolean point_list_append_point(PointList *list, Point *point);
gboolean point_list_remove(PointList *list, guint index);
guint point_list_get_num_used(PointList *list);
guint point_list_get_num_alloc(PointList *list);
gboolean point_list_get_coord(PointList *list, guint index, gdouble *x, gdouble *y);
gboolean point_list_get_x(PointList *list, guint index, gdouble *x);
gboolean point_list_get_y(PointList *list, guint index, gdouble *y);

G_END_DECLS

#endif  /* __POINT_LIST_H__ */
