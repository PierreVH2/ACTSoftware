#ifndef __g_cclosure_user_marshal_MARSHAL_H__
#define __g_cclosure_user_marshal_MARSHAL_H__

#include        <glib-object.h>

G_BEGIN_DECLS

/* VOID:UCHAR,FLOAT (./acq_marshal.txt:1) */
extern void g_cclosure_user_marshal_VOID__UCHAR_FLOAT (GClosure     *closure,
                                                       GValue       *return_value,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      invocation_hint,
                                                       gpointer      marshal_data);

/* VOID:UINT,UINT (./acq_marshal.txt:2) */
extern void g_cclosure_user_marshal_VOID__UINT_UINT (GClosure     *closure,
                                                     GValue       *return_value,
                                                     guint         n_param_values,
                                                     const GValue *param_values,
                                                     gpointer      invocation_hint,
                                                     gpointer      marshal_data);

/* VOID:FLOAT,FLOAT (./acq_marshal.txt:3) */
extern void g_cclosure_user_marshal_VOID__FLOAT_FLOAT (GClosure     *closure,
                                                       GValue       *return_value,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      invocation_hint,
                                                       gpointer      marshal_data);

G_END_DECLS

#endif /* __g_cclosure_user_marshal_MARSHAL_H__ */
