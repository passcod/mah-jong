/* $Header: /home/jcb/MahJong/newmj/RCS/lazyfixed.h,v 12.0 2009/06/28 20:43:12 jcb Rel $
 * lazyfixed.h
 * A slight variation on the GTK+ fixed widget.
 * It doesn't call queue_resize when children removed;
 * this can reduce flickering in widgets with many children.
 * Of course, it means that you must call gtk_widget_queue_resize manually
 * if you *do* want to remove something and have the size change.
 * Also, of course, if you remove a non-window widget, nothing will
 * happen until the next time something causes redisplay.
 */
/****************** COPYRIGHT STATEMENT **********************
 * This file is Copyright (c) 2001 by J. C. Bradfield.       *
 * This file may be used under the terms of the              *
 * GNU Lesser General Public License (any version).           *
 * The moral rights of the author are asserted.              *
 *                                                           *
 ***************** DISCLAIMER OF WARRANTY ********************
 * This code is not warranted fit for any purpose. See the   *
 * LICENCE file for further information.                     *
 *                                                           *
 *************************************************************/


#ifndef __LAZY_FIXED_H__
#define __LAZY_FIXED_H__


#include <gtk/gtkfixed.h>
#include "sysdep.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define LAZY_FIXED(obj)                  (GTK_CHECK_CAST ((obj), lazy_fixed_get_type(), LazyFixed))
#define LAZY_FIXED_CLASS(klass)          (GTK_CHECK_CLASS_CAST ((klass), lazy_fixed_get_type(), LazyFixedClass))
#define IS_LAZY_FIXED(obj)               (GTK_CHECK_TYPE ((obj), lazy_fixed_get_type()))

typedef struct _LazyFixed        LazyFixed;
typedef struct _LazyFixedClass   LazyFixedClass;

struct _LazyFixed
{
  GtkFixed fixed;
};

struct _LazyFixedClass
{
  GtkFixedClass parent_class;
};


GtkType    lazy_fixed_get_type          (void);
GtkWidget* lazy_fixed_new               (void);
void       lazy_fixed_put               (LazyFixed       *fixed,
                                        GtkWidget      *widget,
                                        gint16         x,
                                        gint16         y);
void       lazy_fixed_move              (LazyFixed       *fixed,
                                        GtkWidget      *widget,
                                        gint16         x,
                                        gint16         y);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __LAZY_FIXED_H__ */
